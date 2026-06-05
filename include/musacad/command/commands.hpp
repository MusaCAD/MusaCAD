#pragma once

#include <vector>

#include "musacad/command/command.hpp"
#include "musacad/core/math/math.hpp"

namespace musacad::command {

// Each command is a small state machine. They share no control flow with the
// alias table or the processor.

class LineCommand final : public ICommand {
public:
    std::string name() const override { return "LINE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    std::vector<core::Vec2> points_;
    bool done_ = false;
};

class CircleCommand final : public ICommand {
public:
    std::string name() const override { return "CIRCLE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { Center, Radius } state_ = State::Center;
    core::Vec2 center_{};
    bool done_ = false;
};

class PolylineCommand final : public ICommand {
public:
    std::string name() const override { return "PLINE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    void prompt_next(CommandContext& ctx);
    std::vector<core::Vec2> points_;
    bool done_ = false;
};

class ArcCommand final : public ICommand {
public:
    std::string name() const override { return "ARC"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    std::vector<core::Vec2> points_;
    bool done_ = false;
};

class RectangleCommand final : public ICommand {
public:
    std::string name() const override { return "RECTANGLE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    enum class State { First, Second } state_ = State::First;
    core::Vec2 first_{};
    bool done_ = false;
};

class EraseCommand final : public ICommand {
public:
    std::string name() const override { return "ERASE"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }
    bool wants_selection() const override { return true; }

private:
    bool done_ = false;
};

class UndoCommand final : public ICommand {
public:
    std::string name() const override { return "U"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

class ZoomCommand final : public ICommand {
public:
    std::string name() const override { return "ZOOM"; }
    void start(CommandContext& ctx) override;
    void input(CommandContext& ctx, const std::string& text) override;
    void cancel(CommandContext& ctx) override;
    bool done() const override { return done_; }

private:
    bool done_ = false;
};

} // namespace musacad::command
