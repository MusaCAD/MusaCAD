// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <array>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace musacad::ui::update {

// The update checker's decisions, kept Qt-free so they are unit-tested without a GUI:
// how this copy of Musa CAD was installed, which release file (if any) updates it, and
// whether a published version is newer than the running one.

/// How this copy was installed -- which decides where updates come from and how the user
/// applies them.
enum class Channel {
    Flatpak,          ///< Flathub: the software center or `flatpak update` applies it
    AppImage,         ///< a downloaded AppImage: the user replaces the file
    WindowsInstaller, ///< the setup .exe: run the new installer over this one
    MacDmg,           ///< the .dmg: drag the new app over this one
    SourceBuild,      ///< built from source or a distro package: not ours to update
};

/// `major.minor.patch` from "v0.5.0", "0.5.0", "0.5" or "0.5.0-rc1" (a suffix after the
/// numbers is ignored). nullopt when there is no leading number.
[[nodiscard]] inline std::optional<std::array<int, 3>> parse_version(std::string_view s) {
    if (!s.empty() && (s.front() == 'v' || s.front() == 'V')) {
        s.remove_prefix(1);
    }
    std::array<int, 3> v{0, 0, 0};
    for (std::size_t i = 0; i < v.size(); ++i) {
        int n = 0;
        const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), n);
        if (ec != std::errc{} || n < 0) {
            if (i == 0) {
                return std::nullopt;
            }
            break;
        }
        v[i] = n;
        s.remove_prefix(static_cast<std::size_t>(end - s.data()));
        if (s.empty() || s.front() != '.') {
            break;
        }
        s.remove_prefix(1);
    }
    return v;
}

/// True when `candidate` names a strictly newer release than `current`. An unparsable
/// version on either side is never "newer" -- the checker stays quiet rather than guess.
[[nodiscard]] inline bool is_newer(std::string_view candidate, std::string_view current) {
    const auto c = parse_version(candidate);
    const auto r = parse_version(current);
    return c && r && *c > *r;
}

/// The name ending of the release file that updates `channel` on `arch` (Qt's
/// QSysInfo::currentCpuArchitecture spelling: "x86_64", "arm64"): e.g.
/// "-x86_64.AppImage". Empty for the channels that are not updated by a download.
[[nodiscard]] inline std::string asset_suffix(Channel channel, std::string_view arch) {
    const std::string a(arch);
    switch (channel) {
    case Channel::AppImage:
        return "-" + a + ".AppImage";
    case Channel::WindowsInstaller:
        return "-" + a + "-setup.exe";
    case Channel::MacDmg:
        return "-" + a + ".dmg";
    case Channel::Flatpak:
    case Channel::SourceBuild:
        break;
    }
    return {};
}

/// The download URL among a release's (name, url) assets for this channel and arch, or
/// "" when the release carries no such file.
[[nodiscard]] inline std::string pick_asset(const std::vector<std::pair<std::string, std::string>>& assets,
                                            Channel channel, std::string_view arch) {
    const std::string suffix = asset_suffix(channel, arch);
    if (suffix.empty()) {
        return {};
    }
    for (const auto& [name, url] : assets) {
        if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return url;
        }
    }
    return {};
}

/// Whether the automatic check is on by default: yes for every packaged install, no for
/// a source build, whose updates are its builder's (or its distribution's) business.
[[nodiscard]] inline bool auto_check_default(Channel channel) {
    return channel != Channel::SourceBuild;
}

/// The release file that carries a download's SHA-256: "<file>.sha256" beside it.
[[nodiscard]] inline std::string checksum_asset_name(std::string_view download_name) {
    return std::string(download_name) + ".sha256";
}

/// The hex SHA-256 for `filename` in a checksum file, lower-cased, or "" when the file
/// names no such entry. Accepts the sha256sum forms ("<hex>  <name>", "<hex> *<name>")
/// and a bare "<hex>" when the file holds one hash and no name.
[[nodiscard]] inline std::string parse_sha256_sums(std::string_view text, std::string_view filename) {
    const auto is_hex = [](std::string_view s) {
        if (s.size() != 64) {
            return false;
        }
        for (const char c : s) {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                return false;
            }
        }
        return true;
    };
    const auto lower = [](std::string_view s) {
        std::string out(s);
        for (char& c : out) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        return out;
    };
    std::string bare;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.remove_suffix(1);
        }
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
            line.remove_prefix(1);
        }
        if (line.empty()) {
            continue;
        }
        const std::size_t sp = line.find_first_of(" \t");
        const std::string_view hash = line.substr(0, sp);
        if (!is_hex(hash)) {
            continue;
        }
        if (sp == std::string_view::npos) {
            bare = bare.empty() ? lower(hash) : std::string("?"); // two bare hashes: ambiguous
            continue;
        }
        std::string_view name = line.substr(sp);
        while (!name.empty() && (name.front() == ' ' || name.front() == '\t' || name.front() == '*')) {
            name.remove_prefix(1);
        }
        // A path before the name ("dist/MusaCAD-…") still names the file.
        const std::size_t slash = name.find_last_of("/\\");
        if (slash != std::string_view::npos) {
            name.remove_prefix(slash + 1);
        }
        if (name == filename) {
            return lower(hash);
        }
    }
    return bare == "?" ? std::string() : bare;
}

/// The version an installer file name carries: "MusaCAD-0.6.0-x86_64-setup.exe" -> "0.6.0";
/// "" when the name is not of that shape. Used to tell a stale download from a fresh one.
[[nodiscard]] inline std::string version_in_asset_name(std::string_view name) {
    constexpr std::string_view prefix = "MusaCAD-";
    if (name.size() <= prefix.size() || name.substr(0, prefix.size()) != prefix) {
        return {};
    }
    name.remove_prefix(prefix.size());
    std::size_t n = 0;
    while (n < name.size() && ((name[n] >= '0' && name[n] <= '9') || name[n] == '.')) {
        ++n;
    }
    if (n == 0 || n == name.size() || name[n] != '-') {
        return {};
    }
    return std::string(name.substr(0, n));
}

} // namespace musacad::ui::update
