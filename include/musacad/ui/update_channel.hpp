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

} // namespace musacad::ui::update
