#include "musacad/command/command_processor.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <utility>

#include "musacad/command/command.hpp"

namespace musacad::command {

namespace {

std::string trimmed(const std::string& s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) {
        ++a;
    }
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
        --b;
    }
    return s.substr(a, b - a);
}

std::string first_token(const std::string& s) {
    const std::string t = trimmed(s);
    const auto sp = t.find_first_of(" \t");
    return sp == std::string::npos ? t : t.substr(0, sp);
}

} // namespace

CommandProcessor::CommandProcessor(CommandSink sink, ViewControl* view, CommandOutput& output)
    : sink_(std::move(sink)), view_(view), output_(output), registry_(CommandRegistry::make_default()) {
    show_ready();
}

void CommandProcessor::submit_line(const std::string& text) {
    if (active_) {
        active_->input(*this, text);
        finalize_if_done();
        return;
    }

    const std::string token = first_token(text);
    if (token.empty()) {
        if (!last_command_alias_.empty()) {
            start_command(last_command_alias_); // ENTER repeats last command
        }
        return;
    }
    start_command(token);
}

void CommandProcessor::cancel() {
    if (active_) {
        active_->cancel(*this);
        active_.reset();
        preview_ = PreviewSpec{};
        show_ready();
        return;
    }
    // ESC while idle clears the current selection.
    submit(core::ClearSelectionCommand{});
}

core::Vec2 CommandProcessor::resolve_constraints(core::Vec2 world) const {
    core::Vec2 p = world;
    if (grid_snap_ && grid_spacing_ > 0.0) {
        p.x = std::round(p.x / grid_spacing_) * grid_spacing_;
        p.y = std::round(p.y / grid_spacing_) * grid_spacing_;
    }
    if (last_point_) {
        const core::Vec2 d = p - *last_point_;
        if (ortho_) {
            p = (std::abs(d.x) >= std::abs(d.y)) ? core::Vec2{p.x, last_point_->y}
                                                 : core::Vec2{last_point_->x, p.y};
        } else if (polar_) {
            const double dist = core::length(d);
            const double step = core::kPi / 4.0; // 45-degree increments
            const double ang = std::round(std::atan2(d.y, d.x) / step) * step;
            p = *last_point_ + core::Vec2{dist * std::cos(ang), dist * std::sin(ang)};
        }
    }
    return p;
}

core::Vec2 CommandProcessor::resolve_pick(core::Vec2 world, std::optional<core::Vec2> snap) const {
    // OSNAP wins; otherwise apply ortho/polar/grid-snap to the free cursor point.
    return snap ? *snap : resolve_constraints(world);
}

void CommandProcessor::pick_point(core::Vec2 world, std::optional<core::Vec2> snap) {
    if (!active_) {
        return; // nothing to receive a point
    }
    if (active_->wants_selection()) {
        submit(core::ErasePickCommand{world, pick_radius_, current_group_});
        output_.append_line("Selected object near picked point.");
        finalize_if_done();
        return;
    }
    const core::Vec2 p = resolve_pick(world, snap);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g,%.10g", p.x, p.y);
    submit_line(buf); // feed as an absolute coordinate to the active command
}

void CommandProcessor::undo() {
    submit(core::UndoLastGroupCommand{});
    output_.append_line("Undo");
}

void CommandProcessor::redo() {
    submit(core::RedoLastGroupCommand{});
    output_.append_line("Redo");
}

void CommandProcessor::start_command(const std::string& alias) {
    std::unique_ptr<ICommand> cmd = registry_.create(alias);
    if (!cmd) {
        output_.append_line("Unknown command \"" + alias + "\".");
        show_ready();
        return;
    }
    current_group_ = ++group_counter_;
    last_command_alias_ = alias;
    active_ = std::move(cmd);
    output_.append_line("Command: " + active_->name());
    active_->start(*this);
    finalize_if_done();
}

void CommandProcessor::finalize_if_done() {
    if (active_ && active_->done()) {
        active_.reset();
        preview_ = PreviewSpec{}; // drop any preview when the command ends
        show_ready();
    }
}

void CommandProcessor::show_ready() { output_.set_prompt("Command: "); }

void CommandProcessor::echo(const std::string& line) { output_.append_line(line); }

void CommandProcessor::set_prompt(const std::string& prompt) { output_.set_prompt(prompt); }

void CommandProcessor::submit(core::Command command) {
    if (sink_) {
        sink_(std::move(command));
    }
}

} // namespace musacad::command
