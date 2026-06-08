#pragma once

#include <atomic>
#include <functional>
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
    /// True if the entity may be picked/selected/modified: its layer is on, not
    /// frozen, and not locked. Off/frozen aren't drawn; locked is drawn but inert.
    [[nodiscard]] bool selectable(EntityHandle h) const;

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
    void apply_rotate(Vec2 base, double angle, std::uint64_t group);
    void apply_scale(Vec2 base, double factor, std::uint64_t group);
    void apply_array_rect(int rows, int cols, double dx, double dy, std::uint64_t group);
    void apply_array_polar(Vec2 center, int count, double total_angle, bool rotate_items,
                           std::uint64_t group);
    void apply_extend(Vec2 pick, double radius, std::uint64_t group);
    void apply_fillet(Vec2 pick1, Vec2 pick2, double radius, double pick_radius,
                      std::uint64_t group);
    void apply_chamfer(Vec2 pick1, Vec2 pick2, double dist1, double dist2, double pick_radius,
                       std::uint64_t group);
    // Object-aware dimensioning: resolve the entity(ies) under the pick(s) via the
    // spatial index + selectable() gate and build the matching dimension from their
    // intrinsic geometry. The dimension captures DEF POINTS only (no entity ref), so
    // later deleting the source entity leaves it intact (no dangling reference).
    void apply_object_dimension(std::uint8_t type, Vec2 pick1, Vec2 pick2, double radius,
                                std::uint16_t style, std::uint64_t group);
    // Shared resolution: the entity(ies) under the pick(s) -> a dimension's def
    // points (a, b, line_pt). Used by both apply_object_dimension (create) and the
    // ResolveDimObjectCommand preview query, so there is one resolution path.
    [[nodiscard]] bool resolve_dim_defs(std::uint8_t type, Vec2 pick1, Vec2 pick2, double radius,
                                        DimData& out) const;
    // Property changes on the selection (erase+recreate so they're undoable).
    void apply_props_change(const std::function<void(EntityProps&)>& modify, std::uint64_t group);
    void apply_entity_layer(std::uint16_t layer, std::uint64_t group);
    void apply_entity_color(bool by_layer, Rgb color, std::uint64_t group);

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

    // Honest command-result feedback, published in every snapshot. `report()`
    // records what an op actually did so the command line can echo the truth.
    void report(std::string message);
    std::string status_;
    std::uint64_t status_version_ = 0;

    // Persistence: unsaved-changes flag + a version that bumps on save/open/new.
    void load_document_replace(const Command& command); // Open / DXF import
    void new_document();
    bool dirty_ = false;
    std::uint64_t document_version_ = 0;
    bool lineweight_display_ = true; // LWDISPLAY (default on so default 0.25mm shows)

    Vec2 cursor_{};
    double pick_radius_ = 0.0;
    bool osnap_enabled_ = false;
    std::uint32_t snap_mask_ = kAllSnaps;
    Vec2 from_{};
    bool has_from_ = false;

    // Zoom-adaptive tessellation: the view scale (world units / pixel) and the
    // half-octave bucket derived from it. Curves re-tessellate only when the bucket
    // changes (i.e. a meaningful zoom step), never on pan. `tess_tolerance_` is the
    // world-space chord tolerance fed to the kernel for the current bucket.
    double view_world_per_px_ = 1.0;
    int tess_bucket_ = 0;
    double tess_tolerance_ = kDefaultTessTolerance;

    // Pending object-dimension def points, published for the UI placement preview
    // (set by ResolveDimObjectCommand; never mutates the store or op-log).
    bool has_pending_dim_ = false;
    DimData pending_dim_{};
    std::uint64_t pending_dim_version_ = 0;

    // Active grip drag (direct manipulation). While armed, the dragged entity is
    // previewed on `grip_preview_store_` and published; the real store is untouched
    // until commit. Commit applies the edit as one undo group; cancel drops it.
    void apply_grip_commit(std::uint64_t group);
    bool grip_active_ = false;
    EntityHandle grip_handle_{};
    std::uint32_t grip_index_ = 0;
    Vec2 grip_pos_{};
    GeometryStore grip_preview_store_;

    std::atomic<std::uint64_t> version_{0};
    std::jthread worker_;
};

} // namespace musacad::core
