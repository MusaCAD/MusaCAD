// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// The update checker's decisions: version parsing and ordering, the release file each
// package updates from, and which installs check automatically.

#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "musacad/ui/update_channel.hpp"

using namespace musacad::ui::update;

TEST_CASE("Update check: versions parse from tags and plain numbers") {
    CHECK(parse_version("v0.5.0") == std::array<int, 3>{0, 5, 0});
    CHECK(parse_version("0.5.0") == std::array<int, 3>{0, 5, 0});
    CHECK(parse_version("1.2") == std::array<int, 3>{1, 2, 0});
    CHECK(parse_version("2") == std::array<int, 3>{2, 0, 0});
    CHECK(parse_version("0.6.0-rc1") == std::array<int, 3>{0, 6, 0});
    CHECK(parse_version("V10.20.30") == std::array<int, 3>{10, 20, 30});
    CHECK_FALSE(parse_version("").has_value());
    CHECK_FALSE(parse_version("latest").has_value());
    CHECK_FALSE(parse_version("v").has_value());
}

TEST_CASE("Update check: only a strictly newer version is an update") {
    CHECK(is_newer("0.6.0", "0.5.0"));
    CHECK(is_newer("v0.5.1", "0.5.0"));
    CHECK(is_newer("1.0.0", "0.99.99"));
    CHECK(is_newer("0.10.0", "0.9.0")); // numeric, not lexical
    CHECK_FALSE(is_newer("0.5.0", "0.5.0"));
    CHECK_FALSE(is_newer("0.4.0", "0.5.0"));
    // An unreadable answer never nags.
    CHECK_FALSE(is_newer("", "0.5.0"));
    CHECK_FALSE(is_newer("garbage", "0.5.0"));
    CHECK_FALSE(is_newer("0.6.0", "unknown"));
}

TEST_CASE("Update check: each package picks its own release file") {
    const std::vector<std::pair<std::string, std::string>> assets = {
        {"MusaCAD-0.6.0-arm64.dmg", "https://x/dmg"},
        {"MusaCAD-0.6.0-x86_64-setup.exe", "https://x/exe"},
        {"MusaCAD-0.6.0-x86_64.AppImage", "https://x/appimage"},
        {"MusaCAD-0.6.0.flatpak", "https://x/flatpak"},
    };
    CHECK(pick_asset(assets, Channel::AppImage, "x86_64") == "https://x/appimage");
    CHECK(pick_asset(assets, Channel::WindowsInstaller, "x86_64") == "https://x/exe");
    CHECK(pick_asset(assets, Channel::MacDmg, "arm64") == "https://x/dmg");
    // No file for this architecture, or a channel that is not updated by a download.
    CHECK(pick_asset(assets, Channel::AppImage, "arm64").empty());
    CHECK(pick_asset(assets, Channel::Flatpak, "x86_64").empty());
    CHECK(pick_asset(assets, Channel::SourceBuild, "x86_64").empty());
    CHECK(pick_asset({}, Channel::AppImage, "x86_64").empty());
}

TEST_CASE("Update check: packaged installs check automatically, source builds do not") {
    CHECK(auto_check_default(Channel::Flatpak));
    CHECK(auto_check_default(Channel::AppImage));
    CHECK(auto_check_default(Channel::WindowsInstaller));
    CHECK(auto_check_default(Channel::MacDmg));
    CHECK_FALSE(auto_check_default(Channel::SourceBuild));
}
