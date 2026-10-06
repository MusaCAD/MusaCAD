// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/crash_report.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define MUSACAD_HAVE_BACKTRACE 1
#endif
#endif
#if __has_include(<cxxabi.h>)
#include <cxxabi.h>
#define MUSACAD_HAVE_DEMANGLE 1
#endif

// Under AddressSanitizer / ThreadSanitizer the sanitizer's own report of a bad memory access
// says more than ours: the fault signals are left to it.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define MUSACAD_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define MUSACAD_SANITIZED 1
#endif
#endif

namespace musacad::core::crash {
namespace {

// Everything the handler reads is fixed storage written ahead of time: a handler running
// after a crash must not allocate or take a lock.
template <std::size_t N>
struct FixedText {
    std::array<char, N + 1> buf{};
    std::atomic<std::size_t> len{0};
    void set(std::string_view s) noexcept {
        const std::size_t n = std::min(s.size(), N);
        len.store(0, std::memory_order_release);
        if (n > 0) {
            std::memcpy(buf.data(), s.data(), n);
        }
        buf[n] = '\0';
        len.store(n, std::memory_order_release);
    }
    [[nodiscard]] std::string_view view() const noexcept {
        return {buf.data(), std::min(len.load(std::memory_order_acquire), N)};
    }
};

constexpr std::size_t kSlots = 96;
constexpr std::size_t kSlotLen = 200;
struct Slot {
    std::atomic<std::size_t> len{0};
    std::array<char, kSlotLen> text{};
};

std::array<Slot, kSlots> g_ring;
std::atomic<std::uint64_t> g_next{0};
std::mutex g_ring_mutex; // writers take turns; the handler only reads
FixedText<4096> g_about;
FixedText<1024> g_document;
FixedText<512> g_graphics;
FixedText<1024> g_note; // what std::terminate knew about the exception
FixedText<2048> g_path; // where the report goes
std::atomic<bool> g_installed{false};
std::atomic<bool> g_writing{false};

constexpr const char* kTimeLabel = "Time: ";
constexpr const char* kTimeUnit = " seconds since 1970-01-01 UTC";

/// The report written to a file descriptor without allocating.
class FdSink {
public:
    explicit FdSink(int fd) noexcept : fd_(fd) {}
    void put(std::string_view s) noexcept {
        while (!s.empty() && fd_ >= 0) {
#if defined(_WIN32)
            const int n = ::_write(fd_, s.data(), static_cast<unsigned>(std::min<std::size_t>(s.size(), 1U << 20U)));
#else
            const ssize_t n = ::write(fd_, s.data(), s.size());
#endif
            if (n < 0 && errno == EINTR) {
                continue;
            }
            if (n <= 0) {
                return;
            }
            s.remove_prefix(static_cast<std::size_t>(n));
        }
    }
    [[nodiscard]] int fd() const noexcept { return fd_; }

private:
    int fd_;
};

/// The same report into a string (a report written on request).
class StringSink {
public:
    void put(std::string_view s) { out_.append(s); }
    [[nodiscard]] std::string take() { return std::move(out_); }

private:
    std::string out_;
};

template <class Sink>
void put_dec(Sink& out, std::uint64_t v) {
    std::array<char, 24> b{};
    std::size_t i = b.size();
    do {
        b[--i] = static_cast<char>('0' + static_cast<int>(v % 10U));
        v /= 10U;
    } while (v != 0U && i > 0);
    out.put(std::string_view(b.data() + i, b.size() - i));
}

template <class Sink>
void put_sdec(Sink& out, std::int64_t v) {
    if (v < 0) {
        out.put("-");
        put_dec(out, static_cast<std::uint64_t>(-(v + 1)) + 1U);
        return;
    }
    put_dec(out, static_cast<std::uint64_t>(v));
}

template <class Sink>
void put_hex(Sink& out, std::uint64_t v) {
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::array<char, 18> b{};
    std::size_t i = b.size();
    do {
        b[--i] = kDigits[static_cast<std::size_t>(v & 0xFU)];
        v >>= 4U;
    } while (v != 0U && i > 2);
    b[--i] = 'x';
    b[--i] = '0';
    out.put(std::string_view(b.data() + i, b.size() - i));
}

const char* signal_text(int sig) noexcept {
    switch (sig) {
    case SIGSEGV:
        return "SIGSEGV (invalid memory access)";
    case SIGABRT:
        return "SIGABRT (the program stopped itself: a failed check or an unhandled exception)";
    case SIGFPE:
        return "SIGFPE (arithmetic error)";
    case SIGILL:
        return "SIGILL (illegal instruction)";
#ifdef SIGBUS
    case SIGBUS:
        return "SIGBUS (memory that cannot be reached)";
#endif
    default:
        return nullptr;
    }
}

/// What the handler knows about the crash.
struct Event {
    int sig = 0;
    const char* what = nullptr; // set instead of `sig` (Windows exceptions)
    bool has_code = false;
    std::int64_t code = 0;
    bool has_addr = false;
    std::uint64_t addr = 0;
    bool on_request = false; // nothing crashed: "Save Bug Report…"
};

template <class Sink>
void put_ring(Sink& out) {
    const std::uint64_t n = g_next.load(std::memory_order_acquire);
    const std::uint64_t first = n > kSlots ? n - kSlots : 0;
    bool any = false;
    for (std::uint64_t i = first; i < n; ++i) {
        const Slot& s = g_ring[static_cast<std::size_t>(i % kSlots)];
        const std::size_t len = std::min(s.len.load(std::memory_order_acquire), kSlotLen);
        if (len == 0) {
            continue;
        }
        out.put("  ");
        out.put(std::string_view(s.text.data(), len));
        out.put("\n");
        any = true;
    }
    if (!any) {
        out.put("  (none)\n");
    }
}

/// The report up to the call stack, the same on every path.
template <class Sink>
void put_report(Sink& out, const Event& ev, std::string_view about) {
    out.put(ev.on_request ? "Musa CAD bug report\n===================\n\n" : "Musa CAD crash report\n=====================\n\n");
    out.put(about);
    if (!about.empty() && about.back() != '\n') {
        out.put("\n");
    }
    if (const std::string_view gl = g_graphics.view(); !gl.empty()) {
        out.put("OpenGL: ");
        out.put(gl);
        out.put("\n");
    }
    out.put("\nWhat happened: ");
    if (ev.on_request) {
        out.put("nothing crashed; this report was saved from the application menu.\n");
    } else {
        const char* what = ev.what != nullptr ? ev.what : signal_text(ev.sig);
        if (what != nullptr) {
            out.put(what);
        } else {
            out.put("signal ");
            put_dec(out, static_cast<std::uint64_t>(ev.sig < 0 ? 0 : ev.sig));
        }
        if (ev.has_code) {
            out.put(", code ");
            put_sdec(out, ev.code);
        }
        if (ev.has_addr) {
            out.put(", at address ");
            put_hex(out, ev.addr);
        }
        out.put("\n");
    }
    out.put(kTimeLabel);
    put_sdec(out, static_cast<std::int64_t>(std::time(nullptr)));
    out.put(kTimeUnit);
    out.put("\n");
    const std::string_view doc = g_document.view();
    out.put("Drawing: ");
    out.put(doc.empty() ? std::string_view("(none, or not saved yet)") : doc);
    out.put("\n");
    if (const std::string_view note = g_note.view(); !note.empty()) {
        out.put("Note: ");
        out.put(note);
        out.put("\n");
    }
    out.put("\nRecent activity, oldest first:\n");
    put_ring(out);
}

#if defined(__linux__)
/// The executable's and Musa CAD's own mappings from /proc/self/maps: with them a call
/// stack's addresses resolve against the matching binary.
void put_maps(FdSink& out) noexcept {
    const int fd = ::open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return;
    }
    out.put("\nMusa CAD in memory:\n");
    std::array<char, 4096> chunk{};
    std::array<char, 512> line{};
    std::size_t used = 0;
    const auto flush = [&] {
        const std::string_view l(line.data(), used);
        if (l.find("musacad") != std::string_view::npos && l.find(" r-xp ") != std::string_view::npos) {
            out.put("  ");
            out.put(l);
            out.put("\n");
        }
        used = 0;
    };
    for (;;) {
        const ssize_t n = ::read(fd, chunk.data(), chunk.size());
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        for (ssize_t k = 0; k < n; ++k) {
            const char c = chunk[static_cast<std::size_t>(k)];
            if (c == '\n') {
                flush();
            } else if (used < line.size()) {
                line[used++] = c;
            }
        }
    }
    if (used > 0) {
        flush();
    }
    ::close(fd);
}
#endif

#if defined(_WIN32)
/// A code address as module+offset (musacad_app.exe+0x1a2b3c), which a matching .pdb
/// turns into a function and a line.
void put_module_offset(FdSink& out, const void* addr) noexcept {
    HMODULE mod = nullptr;
    if (::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             static_cast<LPCWSTR>(addr), &mod) != 0 &&
        mod != nullptr) {
        std::array<wchar_t, MAX_PATH> wname{};
        const DWORD wn = ::GetModuleFileNameW(mod, wname.data(), static_cast<DWORD>(wname.size()));
        std::array<char, MAX_PATH * 3> name{};
        const int n = ::WideCharToMultiByte(CP_UTF8, 0, wname.data(), static_cast<int>(wn), name.data(),
                                            static_cast<int>(name.size()), nullptr, nullptr);
        std::string_view base(name.data(), n > 0 ? static_cast<std::size_t>(n) : 0U);
        if (const std::size_t slash = base.find_last_of("\\/"); slash != std::string_view::npos) {
            base.remove_prefix(slash + 1);
        }
        out.put(base);
        out.put("+");
        put_hex(out, reinterpret_cast<std::uintptr_t>(addr) - reinterpret_cast<std::uintptr_t>(mod));
        return;
    }
    put_hex(out, reinterpret_cast<std::uintptr_t>(addr));
}
#endif

void put_stack(FdSink& out) noexcept {
    out.put("\nCall stack:\n");
#if defined(MUSACAD_HAVE_BACKTRACE)
    std::array<void*, 64> frames{};
    const int n = ::backtrace(frames.data(), static_cast<int>(frames.size()));
    ::backtrace_symbols_fd(frames.data(), n, out.fd());
#elif defined(_WIN32)
    std::array<void*, 62> frames{};
    const USHORT n = ::RtlCaptureStackBackTrace(0, static_cast<DWORD>(frames.size()), frames.data(), nullptr);
    for (USHORT i = 0; i < n; ++i) {
        out.put("  ");
        put_module_offset(out, frames[i]);
        out.put("\n");
    }
#else
    out.put("  (not available on this platform)\n");
#endif
#if defined(__linux__)
    put_maps(out);
#endif
}

int open_report(const char* path) noexcept {
#if defined(_WIN32)
    return ::_open(path, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    return ::open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
#endif
}

void close_report(int fd) noexcept {
#if defined(_WIN32)
    ::_close(fd);
#else
    ::close(fd);
#endif
}

bool write_event(const char* path, const Event& ev) noexcept {
    const int fd = open_report(path);
    if (fd < 0) {
        return false;
    }
    FdSink out(fd);
    put_report(out, ev, g_about.view());
    put_stack(out);
    close_report(fd);
    return true;
}

[[noreturn]] void on_terminate() noexcept {
    std::string what = "std::terminate was called";
    if (const std::exception_ptr e = std::current_exception()) {
        try {
            std::rethrow_exception(e);
        } catch (const std::exception& ex) {
            what = std::string("Unhandled exception: ") + ex.what();
        } catch (...) {
            what = "Unhandled exception (not a std::exception)";
        }
    }
    g_note.set(what);
    std::abort(); // SIGABRT writes the report
}

#if defined(_WIN32)
const char* exception_text(DWORD code) noexcept {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
        return "Access violation (invalid memory access)";
    case EXCEPTION_STACK_OVERFLOW:
        return "Stack overflow";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "Integer division by zero";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "Illegal instruction";
    case EXCEPTION_IN_PAGE_ERROR:
        return "In-page error (memory that cannot be read)";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return "Array bounds exceeded";
    default:
        return "Unhandled exception";
    }
}

LONG WINAPI on_unhandled(EXCEPTION_POINTERS* ep) {
    if (!g_writing.exchange(true)) {
        Event ev;
        if (ep != nullptr && ep->ExceptionRecord != nullptr) {
            ev.what = exception_text(ep->ExceptionRecord->ExceptionCode);
            ev.has_code = true;
            ev.code = static_cast<std::int64_t>(ep->ExceptionRecord->ExceptionCode);
            ev.has_addr = true;
            ev.addr = reinterpret_cast<std::uintptr_t>(ep->ExceptionRecord->ExceptionAddress);
        } else {
            ev.what = "Unhandled exception";
        }
        const int fd = open_report(g_path.buf.data());
        if (fd >= 0) {
            FdSink out(fd);
            put_report(out, ev, g_about.view());
            if (ev.has_addr) {
                out.put("\nFaulting address: ");
                put_module_offset(out, ep->ExceptionRecord->ExceptionAddress);
                out.put("\n");
            }
            put_stack(out);
            close_report(fd);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH; // and on to Windows Error Reporting
}

void on_abort(int sig) {
    if (!g_writing.exchange(true)) {
        Event ev;
        ev.sig = sig;
        (void)write_event(g_path.buf.data(), ev);
    }
    std::signal(sig, SIG_DFL);
}
#else
void on_signal(int sig, siginfo_t* info, void* /*context*/) {
    if (g_writing.exchange(true)) {
        for (;;) {
            ::pause(); // another thread is writing the report; it ends the process
        }
    }
    Event ev;
    ev.sig = sig;
    if (info != nullptr) {
        ev.has_code = true;
        ev.code = info->si_code;
        if (sig != SIGABRT && info->si_code > 0) { // raised by the fault itself, not sent
            ev.has_addr = true;
            ev.addr = reinterpret_cast<std::uintptr_t>(info->si_addr);
        }
    }
    (void)write_event(g_path.buf.data(), ev);
    ::signal(sig, SIG_DFL);
    ::raise(sig); // delivered when the handler returns: the usual end, a core dump included
}

std::array<char, 64 * 1024> g_altstack{};
#endif

void install_handlers() {
    std::set_terminate(&on_terminate);
#if defined(_WIN32)
    ::SetUnhandledExceptionFilter(&on_unhandled);
    std::signal(SIGABRT, &on_abort);
#else
#if defined(MUSACAD_HAVE_BACKTRACE)
    std::array<void*, 2> warm{};
    (void)::backtrace(warm.data(), 2); // loads the unwinder now rather than in the handler
#endif
    // A stack of its own, so a stack overflow in the main thread is still reported.
    stack_t ss{};
    ss.ss_sp = g_altstack.data();
    ss.ss_size = g_altstack.size();
    ss.ss_flags = 0;
    (void)::sigaltstack(&ss, nullptr);
    struct sigaction sa {};
    sa.sa_sigaction = &on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
#if defined(MUSACAD_SANITIZED)
    for (const int sig : {SIGABRT}) {
#else
    for (const int sig : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL}) {
#endif
        (void)::sigaction(sig, &sa, nullptr);
    }
#endif
}

int process_id() {
#if defined(_WIN32)
    return ::_getpid();
#else
    return static_cast<int>(::getpid());
#endif
}

/// The local time of `t`: "2026-10-06 22:14:03" for a reader, "2026-10-06-221403" for a
/// file name.
std::string local_time_text(std::int64_t t, bool for_file) {
    const auto tt = static_cast<std::time_t>(t);
    std::tm tm{};
#if defined(_WIN32)
    if (::localtime_s(&tm, &tt) != 0) {
        return {};
    }
#else
    if (::localtime_r(&tt, &tm) == nullptr) {
        return {};
    }
#endif
    std::array<char, 64> b{};
    const std::size_t n = for_file ? std::strftime(b.data(), b.size(), "%Y-%m-%d-%H%M%S", &tm)
                                   : std::strftime(b.data(), b.size(), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(b.data(), n);
}

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

void install(const std::string& dir, const std::string& about) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(dir), ec);
    const std::string path =
        (std::filesystem::path(dir) / ("crash-pending-" + std::to_string(process_id()) + ".txt")).string();
    g_path.set(path);
    g_about.set(about);
    if (!g_installed.exchange(true)) {
        install_handlers();
    }
}

void set_about(std::string_view about) { g_about.set(about); }

void set_graphics(std::string_view renderer) { g_graphics.set(renderer); }

void set_document(std::string_view path) { g_document.set(path); }

void note(std::string_view line) {
    std::scoped_lock lock(g_ring_mutex);
    while (!line.empty()) {
        const std::size_t eol = line.find_first_of("\r\n");
        std::string_view piece = line.substr(0, eol);
        line = eol == std::string_view::npos ? std::string_view() : line.substr(eol + 1);
        while (!piece.empty() && (piece.back() == ' ' || piece.back() == '\t')) {
            piece.remove_suffix(1);
        }
        if (piece.empty()) {
            continue;
        }
        const std::uint64_t i = g_next.load(std::memory_order_relaxed);
        Slot& s = g_ring[static_cast<std::size_t>(i % kSlots)];
        s.len.store(0, std::memory_order_release);
        const std::size_t n = std::min(piece.size(), kSlotLen);
        std::memcpy(s.text.data(), piece.data(), n);
        s.len.store(n, std::memory_order_release);
        g_next.store(i + 1, std::memory_order_release);
    }
}

std::vector<std::string> recent() {
    std::scoped_lock lock(g_ring_mutex);
    std::vector<std::string> out;
    const std::uint64_t n = g_next.load(std::memory_order_acquire);
    const std::uint64_t first = n > kSlots ? n - kSlots : 0;
    for (std::uint64_t i = first; i < n; ++i) {
        const Slot& s = g_ring[static_cast<std::size_t>(i % kSlots)];
        const std::size_t len = std::min(s.len.load(std::memory_order_acquire), kSlotLen);
        if (len > 0) {
            out.emplace_back(s.text.data(), len);
        }
    }
    return out;
}

std::string live_report(std::string_view about) {
    std::scoped_lock lock(g_ring_mutex);
    StringSink out;
    Event ev;
    ev.on_request = true;
    put_report(out, ev, about);
    return out.take();
}

bool write_report(const std::string& path, int sig) {
    Event ev;
    ev.sig = sig;
    return write_event(path.c_str(), ev);
}

std::optional<std::int64_t> report_time(std::string_view text) {
    // "Time: <seconds> seconds since ...", or after the crash was taken
    // "Time: <local time>, <seconds> seconds since ...": the number before the unit.
    const std::string_view label = kTimeLabel;
    const std::string_view unit = kTimeUnit;
    std::size_t at = 0;
    while ((at = text.find(label, at)) != std::string_view::npos) {
        if (at == 0 || text[at - 1] == '\n') {
            const std::size_t eol = std::min(text.find('\n', at), text.size());
            const std::string_view line = text.substr(at + label.size(), eol - at - label.size());
            const std::size_t u = line.find(unit);
            if (u != std::string_view::npos) {
                std::size_t k = u;
                while (k > 0 && line[k - 1] >= '0' && line[k - 1] <= '9') {
                    --k;
                }
                if (k < u && u - k <= 18) {
                    std::int64_t v = 0;
                    for (std::size_t i = k; i < u; ++i) {
                        v = v * 10 + (line[i] - '0');
                    }
                    return (k > 0 && line[k - 1] == '-') ? -v : v;
                }
            }
        }
        at += label.size();
    }
    return std::nullopt;
}

std::string signal_name(int sig) {
    const char* t = signal_text(sig);
    return t != nullptr ? std::string(t) : "signal " + std::to_string(sig);
}

std::string demangle_stack(std::string_view text) {
#if defined(MUSACAD_HAVE_DEMANGLE)
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    const auto is_sym = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
               c == '$';
    };
    while (i < text.size()) {
        const std::size_t z = text.find("_Z", i);
        if (z == std::string_view::npos) {
            out.append(text.substr(i));
            break;
        }
        // A mangled name starts a token: after "(", a space or the start of a line.
        const bool starts = z == 0 || text[z - 1] == '(' || text[z - 1] == ' ' || text[z - 1] == '\n';
        std::size_t end = z;
        while (end < text.size() && is_sym(text[end])) {
            ++end;
        }
        out.append(text.substr(i, z - i));
        if (!starts) {
            out.append(text.substr(z, end - z));
            i = end;
            continue;
        }
        const std::string mangled(text.substr(z, end - z));
        int status = 0;
        char* plain = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);
        if (status == 0 && plain != nullptr) {
            out.append(plain);
        } else {
            out.append(mangled);
        }
        std::free(plain);
        i = end;
    }
    return out;
#else
    return std::string(text);
#endif
}

std::vector<PendingReport> take_pending(const std::string& dir) {
    namespace fs = std::filesystem;
    std::vector<std::pair<std::int64_t, fs::path>> found;
    std::error_code ec;
    for (fs::directory_iterator it(fs::path(dir), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind("crash-pending-", 0) == 0 && name.size() > 4 && name.substr(name.size() - 4) == ".txt") {
            const std::string text = read_file(it->path());
            found.emplace_back(report_time(text).value_or(static_cast<std::int64_t>(std::time(nullptr))), it->path());
        }
    }
    std::sort(found.begin(), found.end());
    std::vector<PendingReport> out;
    for (const auto& [t, from] : found) {
        std::string text = demangle_stack(read_file(from));
        // The time as the reader wants it, the seconds kept for a program.
        if (const std::size_t at = text.find(std::string("\n") + kTimeLabel); at != std::string::npos) {
            const std::size_t eol = text.find('\n', at + 1);
            text.replace(at + 1, (eol == std::string::npos ? text.size() : eol) - (at + 1),
                         kTimeLabel + local_time_text(t, false) + " local time" + ", " +
                             std::to_string(t) + kTimeUnit);
        }
        const std::string stem = "musacad-crash-" + local_time_text(t, true);
        fs::path to = fs::path(dir) / (stem + ".txt");
        for (int k = 2; fs::exists(to, ec) && k < 100; ++k) {
            to = fs::path(dir) / (stem + "-" + std::to_string(k) + ".txt");
        }
        bool written = false;
        {
            std::ofstream o(to, std::ios::binary | std::ios::trunc);
            if (o) {
                o << text;
                written = static_cast<bool>(o);
            }
        }
        if (written) {
            fs::remove(from, ec);
            out.push_back({to.string(), std::move(text)});
        } else {
            out.push_back({from.string(), std::move(text)}); // left where it is; offered again
        }
    }
    return out;
}

} // namespace musacad::core::crash
