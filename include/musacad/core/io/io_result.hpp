#pragma once

#include <string>
#include <utility>

namespace musacad::core::io {

/// The outcome of a file operation. `message` is a human-readable summary on
/// success (e.g. "Saved 173 entities.") or the reason on failure -- surfaced
/// verbatim through the engine's honest status channel.
struct IoResult {
    bool ok = false;
    std::string message;

    static IoResult success(std::string m) { return {true, std::move(m)}; }
    static IoResult failure(std::string m) { return {false, std::move(m)}; }
};

} // namespace musacad::core::io
