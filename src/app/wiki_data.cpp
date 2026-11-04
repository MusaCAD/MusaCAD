// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

// `musacad --dump-wiki-data`: the documentation's source of truth, written by the program
// it documents. The Musa CAD Wiki (wiki.musacad.org) builds its command reference, system
// variable reference, ribbon map and shortcut list from this file, so none of them can
// disagree with the application.

#include "musacad/app/wiki_data.hpp"

#include <algorithm>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/command_registry.hpp"
#include "musacad/command/sysvars.hpp"
#include "musacad/core/dimension.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/version.hpp"
#include "musacad/ui/main_window.hpp"

namespace musacad::app {

namespace {

/// What a command says when it starts: the lines it prints and the prompt it waits at.
struct Capture : command::CommandOutput {
    std::vector<std::string> lines;
    std::string prompt;
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompt = p; }
};

/// The drawing area as a command sees it when it starts, with nothing opened. The settings
/// a command reads (the running snaps, the selection settings, the view) come from the real,
/// never-shown window, so the defaults it prints are a fresh install's. A window or dialog it
/// asks for is noted and not opened, and the command is told it is not there.
struct ProbeView : command::ViewControl {
    explicit ProbeView(command::ViewControl* real) : real_(real) {}
    std::string asked; ///< the window or dialog the command asked for ("" = none)

    void zoom_extents() override {}
    void zoom_scale(double /*factor*/) override {}

    // Read: as a fresh window has it.
    bool current_view(core::Vec2& center, double& scale) override {
        return real_ != nullptr && real_->current_view(center, scale);
    }
    bool viewport_size(int& w, int& h) const override { return real_ != nullptr && real_->viewport_size(w, h); }
    std::uint32_t snap_mask() const override { return real_ != nullptr ? real_->snap_mask() : 0; }
    bool grid_mode() const override { return real_ != nullptr && real_->grid_mode(); }
    std::vector<std::string> workspaces() const override { return real_ != nullptr ? real_->workspaces() : std::vector<std::string>{}; }
    std::string current_workspace() const override { return real_ != nullptr ? real_->current_workspace() : std::string{}; }
    std::vector<core::TiledViewport> tiled_viewports() const override {
        return real_ != nullptr ? real_->tiled_viewports() : std::vector<core::TiledViewport>{};
    }
    int active_tile() const override { return real_ != nullptr ? real_->active_tile() : 0; }
    core::MatchPropFilter match_filter() const override { return real_ != nullptr ? real_->match_filter() : core::MatchPropFilter{}; }
    int selection_setting(const std::string& name) const override {
        return real_ != nullptr ? real_->selection_setting(name) : 0;
    }

    // Windows and dialogs: noted, never opened.
    bool layer_dialog() override { return ask("Layer Properties Manager"); }
    void osnap_settings_dialog() override { ask("Drafting Settings"); }
    bool find_dialog() override { return ask("Find and Replace"); }
    bool layer_walk_dialog() override { return ask("LayerWalk"); }
    bool text_style_dialog() override { return ask("Text Style"); }
    bool plot_preview() override { return ask("Plot Preview"); }
    bool export_pdf() override { return ask("Export to PDF"); }
    bool publish() override { return ask("Publish (batch plot)"); }
    bool page_setup_manager() override { return ask("Page Setup Manager"); }
    bool window_action(const std::string& action) override { return ask("window:" + action); }
    int field_dialog(std::string& /*code*/) override { return ask_int("Field"); }
    int tolerance_dialog(std::vector<std::string>& /*cells*/) override { return ask_int("Geometric Tolerance"); }
    int choose_dialog(const std::string& title, const std::vector<std::string>& /*items*/, std::string& /*chosen*/) override {
        return ask_int(title);
    }
    int select_color_dialog(bool& /*by_layer*/, core::Rgb& /*color*/) override { return ask_int("Select Color"); }
    int lineweight_dialog(bool& /*by_layer*/, std::uint8_t& /*lineweight*/) override { return ask_int("Lineweight"); }
    bool linetype_manager() override { return ask("Linetype Manager"); }
    int qleader_settings_dialog(command::QLeaderSettings& /*io*/) override { return ask_int("Leader Settings"); }
    int insert_table_dialog(command::InsertTableSettings& /*io*/) override { return ask_int("Insert Table"); }
    bool view_manager() override { return ask("View Manager"); }
    bool layer_states_manager() override { return ask("Layer States Manager"); }
    bool table_style_dialog() override { return ask("Table Style"); }
    int dim_inspect_dialog(core::DimInspectCommand& /*io*/) override { return ask_int("Inspection Dimension"); }
    bool open_help() override { return ask("Help"); }
    void block_attribute_manager() override { ask("Block Attribute Manager"); }
    std::string image_file_dialog() override { ask("Select Image File"); return {}; }
    std::string open_file_dialog(const std::string& /*filter*/) override { ask("Open file"); return {}; }
    std::string save_file_dialog(const std::string& /*filter*/, const std::string& /*suggested*/) override {
        ask("Save file");
        return {};
    }
    void open_properties() override { ask("Properties palette"); }
    void import_dwg() override { ask("Import DWG"); }
    void export_dwg() override { ask("Export DWG"); }
    void plot_dialog() override { ask("Plot"); }
    void options_dialog() override { ask("Options"); }
    bool dimstyle_dialog() override { return ask("Dimension Style Manager"); }
    void match_settings_dialog() override { ask("Property Settings"); }
    bool quick_select_dialog(bool filter) override { return ask(filter ? "Filter" : "Quick Select"); }
    bool purge_dialog() override { return ask("Purge"); }
    bool units_dialog() override { return ask("Drawing Units"); }
    bool quickcalc_dialog() override { return ask("QuickCalc"); }
    bool dwgprops_dialog() override { return ask("Drawing Properties"); }

private:
    bool ask(const std::string& what) {
        if (asked.empty()) {
            asked = what;
        }
        return false;
    }
    int ask_int(const std::string& what) {
        ask(what);
        return -1;
    }
    command::ViewControl* real_;
};

QString q(const std::string& s) { return QString::fromStdString(s); }

bool in(std::span<const char* const> names, const std::string& name) {
    return std::any_of(names.begin(), names.end(), [&](const char* n) { return name == n; });
}

/// The keys the drawing area reads itself, outside any action (ViewportWindow's key
/// handling and its temporary overrides). Kept by hand, here beside the code that reads
/// them; everything else in the shortcut list comes from the window's actions.
QJsonArray viewport_keys() {
    struct Key {
        const char* keys;
        const char* action;
    };
    static constexpr Key kKeys[] = {
        {"Ctrl+A", "Select every object (when no command is running)"},
        {"Ctrl+W", "Selection cycling on or off (SELECTIONCYCLING)"},
        {"Ctrl+H", "Group selection on or off (PICKSTYLE)"},
        {"Ctrl+Shift+A", "Group selection on or off (PICKSTYLE)"},
        {"Delete", "Erase the selected objects"},
        {"Backspace", "Erase the selected objects"},
        {"Esc", "Cancel the command, or clear the selection"},
        {"Enter", "Accept the input; at an empty prompt, repeat the last command"},
        {"Space", "The same as Enter; while drawing a lasso, cycle window, crossing and fence"},
        {"Shift (held at a point prompt)", "Ortho the other way for as long as it is held"},
        {"Shift+A (held)", "Object snap on or off for the moment"},
        {"Shift+X (held)", "Polar tracking on or off for the moment"},
        {"Shift+Q (held)", "Object snap tracking on or off for the moment"},
        {"Shift+D or Shift+L (held)", "No snapping or tracking for the moment"},
        {"Shift+E or Shift+P (held)", "Endpoint snap alone for the moment"},
        {"Shift+V or Shift+M (held)", "Midpoint snap alone for the moment"},
        {"Shift+C (held)", "Center snap alone for the moment"},
        {"Shift+right-click", "The object snap menu for one pick"},
    };
    QJsonArray out;
    for (const Key& k : kKeys) {
        out.append(QJsonObject{{QStringLiteral("keys"), QString::fromUtf8(k.keys)},
                               {QStringLiteral("action"), QString::fromUtf8(k.action)}});
    }
    return out;
}

} // namespace

int run_dump_wiki_data(const CliOptions& opts, std::string& error) {
    QJsonObject root;
    root.insert(QStringLiteral("schema"), 1);
    root.insert(QStringLiteral("generator"), QStringLiteral("musacad --dump-wiki-data"));
    root.insert(QStringLiteral("version"), q(std::string(core::version_string())));
    root.insert(QStringLiteral("formatVersion"), static_cast<int>(core::io::kFormatVersion));

    // Every command once, with what it opens with. Each is started in a processor of its own
    // (an empty drawing, the probe for a window) and cancelled straight away; nothing is
    // drawn, nothing is opened and nothing is saved.
    const command::CommandRegistry registry = command::CommandRegistry::make_default();
    ui::MainWindow window; // built, never shown: the ribbon, the shortcuts, a fresh drawing area
    Capture capture;
    ProbeView view(window.view_control());
    command::CommandProcessor proc([](core::Command) {}, &view, capture);

    const std::span<const char* const> setvar_names = command::system_variable_names();
    QJsonArray commands;
    for (const command::CommandSuggestion& c : registry.commands()) {
        QJsonObject o;
        o.insert(QStringLiteral("name"), q(c.name));
        o.insert(QStringLiteral("description"), q(c.description));
        QJsonArray aliases;
        for (const std::string& a : registry.aliases_of(c.name)) {
            aliases.append(q(a));
        }
        o.insert(QStringLiteral("aliases"), aliases);
        if (const command::CommandInfo* info = registry.find(c.name); info != nullptr) {
            o.insert(QStringLiteral("primaryAlias"), q(info->primary_alias));
            if (!info->icon.empty()) {
                o.insert(QStringLiteral("icon"), q(info->icon));
            }
        }
        o.insert(QStringLiteral("setvar"), in(setvar_names, c.name));
        o.insert(QStringLiteral("dimvar"), in(core::kDimVars, c.name));

        capture.lines.clear();
        capture.prompt.clear();
        view.asked.clear();
        proc.start_command(c.name);
        QJsonArray echo;
        for (const std::string& l : capture.lines) {
            echo.append(q(l));
        }
        QJsonObject opening{{QStringLiteral("echo"), echo}};
        if (!view.asked.empty()) {
            opening.insert(QStringLiteral("window"), q(view.asked));
        }
        if (proc.has_active_command()) {
            opening.insert(QStringLiteral("prompt"), q(proc.current_prompt()));
            proc.cancel();
        } else {
            opening.insert(QStringLiteral("finished"), true);
        }
        o.insert(QStringLiteral("opening"), opening);
        commands.append(o);
    }
    root.insert(QStringLiteral("commands"), commands);

    QJsonArray setvars;
    for (const char* n : setvar_names) {
        setvars.append(QString::fromUtf8(n));
    }
    root.insert(QStringLiteral("setvarNames"), setvars);
    QJsonArray dimvars;
    for (const char* n : core::kDimVars) {
        dimvars.append(QString::fromUtf8(n));
    }
    root.insert(QStringLiteral("dimVars"), dimvars);
    QJsonArray dimoverride;
    for (const char* n : core::kDimOverrideVars) {
        dimoverride.append(QString::fromUtf8(n));
    }
    root.insert(QStringLiteral("dimOverrideVars"), dimoverride);

    root.insert(QStringLiteral("cli"), QJsonObject{{QStringLiteral("help"), q(help_text())},
                                                   {QStringLiteral("version"), q(version_text())}});

    // The interface, read off the same never-shown window.
    QJsonObject ui = window.wiki_ui_data();
    ui.insert(QStringLiteral("viewportKeys"), viewport_keys());
    root.insert(QStringLiteral("ui"), ui);

    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (opts.dump_path == "-") {
        std::fwrite(json.constData(), 1, static_cast<std::size_t>(json.size()), stdout);
        return kExitOk;
    }
    QFile f(QString::fromStdString(opts.dump_path));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(json) != json.size()) {
        error = "could not write " + opts.dump_path;
        return kExitOutput;
    }
    std::printf("%s: %d commands, %s\n", opts.dump_path.c_str(), static_cast<int>(commands.size()),
                version_text().c_str());
    return kExitOk;
}

} // namespace musacad::app
