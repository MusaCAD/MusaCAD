#pragma once

#include <atomic>
#include <cstdint>
#include <stop_token>
#include <thread>

#include <vector>

#include "musacad/core/command.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/render_snapshot.hpp"
#include "musacad/core/spatial_grid.hpp"
#include "musacad/core/threading/mpsc_queue.hpp"
#include "musacad/core/threading/triple_buffer.hpp"

namespace musacad::core {

/// Owns the geometry thread and everything it owns: the SoA GeometryStore, the
/// kernel, the inbound command queue, and the outbound snapshot triple buffer.
/// Producers (UI/command threads) call submit(); the render thread calls
/// consume_snapshot()/snapshot(). The worker runs on a std::jthread and is
/// stopped cleanly via its stop_token; no detached threads, no manual join
/// races (the destructor stops and joins).
class GeometryEngine {
public:
    GeometryEngine() = default;
    ~GeometryEngine() { stop(); }

    GeometryEngine(const GeometryEngine&) = delete;
    GeometryEngine& operator=(const GeometryEngine&) = delete;
    GeometryEngine(GeometryEngine&&) = delete;
    GeometryEngine& operator=(GeometryEngine&&) = delete;

    /// Launches the geometry worker thread (idempotent).
    void start();

    /// Requests stop and joins the worker (idempotent; also called by dtor).
    void stop() noexcept;

    /// Enqueues a command for the geometry thread (any producer thread).
    void submit(Command command) { queue_.push(std::move(command)); }

    /// Render-thread side: tries to swap in the latest published snapshot.
    /// Returns true if a newer snapshot became available.
    bool consume_snapshot() noexcept { return snapshots_.acquire(); }

    /// The most recently consumed snapshot (valid until the next consume).
    [[nodiscard]] const RenderSnapshot& snapshot() const noexcept {
        return snapshots_.read_buffer();
    }

    /// Total number of snapshots published so far (monotonic). For tests/metrics.
    [[nodiscard]] std::uint64_t published_version() const noexcept {
        return version_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool running() const noexcept { return worker_.joinable(); }

private:
    // An undo/redo group is one command invocation's worth of entity changes.
    // Each item is either a creation (undo removes it, redo recreates it) or an
    // erasure (undo recreates it, redo removes it); a group may mix both, which
    // is how MOVE/MIRROR/TRIM (erase originals + create results) are one
    // undoable step. `data` is the Add* command that recreates the entity;
    // `handle` is its current live handle (null when removed).
    struct Item {
        Command data;
        EntityHandle handle;
        bool is_create = true;
    };
    struct Group {
        std::uint64_t id = 0;
        std::vector<Item> items;
    };

    void run(std::stop_token token);
    void apply(const Command& command);
    void rebuild_and_publish();

    // --- store + index maintenance (kept in lockstep) ---
    EntityHandle create_entity(const Command& add_command);  // store add only
    EntityHandle create_indexed(const Command& add_command); // store add + grid insert
    void remove_indexed(EntityHandle handle);                // grid remove + store remove
    [[nodiscard]] Command capture_entity(EntityHandle handle) const;
    [[nodiscard]] EntityHandle most_recent_live() const;
    [[nodiscard]] std::vector<EntityHandle> all_live() const;
    [[nodiscard]] EntityHandle pick_nearest(Vec2 world, double radius) const;

    // --- undo/redo ---
    void push_create_item(std::uint64_t group, EntityHandle handle, Command data);
    void push_erase_item(std::uint64_t group, Command data);
    void do_undo_group();
    void do_redo_group();
    void do_undo_op();

    // --- selection ---
    [[nodiscard]] bool sel_contains(EntityHandle h) const;
    void sel_add(EntityHandle h);
    void prune_selection();
    void select_window(Vec2 min, Vec2 max, bool crossing, bool additive);

    // --- modify (operate on the selection / a pick) ---
    void apply_move(Vec2 delta, bool copy, std::uint64_t group);
    void apply_mirror(Vec2 a, Vec2 b, bool erase_source, std::uint64_t group);
    void apply_offset(Vec2 pick, double radius, double distance, Vec2 side, std::uint64_t group);
    void apply_trim(Vec2 pick, double radius, std::uint64_t group);

    GeometryStore store_;
    NativeKernel2D kernel_;
    SpatialGrid grid_;
    MpscQueue<Command> queue_;
    TripleBuffer<RenderSnapshot> snapshots_;

    std::vector<Group> undo_;
    std::vector<Group> redo_;
    std::vector<EntityHandle> selection_;

    RenderSnapshot geom_cache_; // payload rebuilt only when geometry changes
    bool geom_dirty_ = true;
    std::uint64_t geom_version_ = 0; // bumps only when geometry changes

    Vec2 cursor_{};
    double pick_radius_ = 0.0;
    bool osnap_enabled_ = false;
    std::uint32_t snap_mask_ = kAllSnaps;
    Vec2 from_{};
    bool has_from_ = false;

    std::atomic<std::uint64_t> version_{0};
    std::jthread worker_;
};

} // namespace musacad::core
