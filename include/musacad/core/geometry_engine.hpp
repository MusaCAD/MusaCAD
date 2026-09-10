// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <atomic>
#include <functional>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>

#include <vector>

#include "musacad/core/command.hpp"
#include "musacad/core/image_decoder.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/render_snapshot.hpp"
#include "musacad/core/spatial_grid.hpp"
#include "musacad/core/text/text_codes.hpp"
#include "musacad/core/threading/mpsc_queue.hpp"
#include "musacad/core/threading/triple_buffer.hpp"

namespace musacad::core {

class IFontEngine;

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

    /// Injects the font engine used to render/measure outline (TTF/OTF) text. Must be set
    /// before start() (read on the geometry thread; not changed afterwards). Null = stroke
    /// font only. The engine is owned by the caller (the UI layer). Stored on the
    /// GeometryStore so render/bounds/pick/grips all reach the same metrics (no fork).
    /// The raster decoder (owned above core; the UI's QtImageDecoder). IMAGEATTACH needs
    /// it for the pixel size; every document's store gets it, like the font engine.
    void set_image_decoder(const IImageDecoder* decoder) noexcept {
        image_decoder_ = decoder;
        store_.set_image_decoder(decoder);
    }
    void set_font_engine(const IFontEngine* engine) noexcept {
        font_engine_ = engine;       // remembered so every NEW document's store gets it too
        store_.set_font_engine(engine);
    }

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

    /// The dedicated fine-tolerance snapshot built by BuildPlotSnapshotCommand (for
    /// PLOT/print). Stable after `plot_snapshot_version()` bumps until the next request;
    /// the geometry thread only writes it inside that command handler.
    [[nodiscard]] const RenderSnapshot& plot_snapshot() const noexcept { return plot_snapshot_; }
    [[nodiscard]] std::uint64_t plot_snapshot_version() const noexcept {
        return plot_version_.load(std::memory_order_acquire);
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

    // --- multi-document ----------------------------------------------------
    // The HEAVY per-document state. The ACTIVE document's heavy state lives in the
    // engine's live members (store_, grid_, undo_, ... below) so all 30+ phases of
    // single-document code is untouched; INACTIVE documents park their heavy state here
    // (moved in/out on switch -- GeometryStore/SpatialGrid are movable). Display
    // metadata (id/name/path) lives in DocMeta, kept for every document at all times.
    /// An in-place block edit (REFEDIT .. REFCLOSE): which definition, the reference
    /// as it was (re-created on close), the model-space working set, and which of
    /// those were added by REFSET (kept as ordinary objects on Discard).
    struct RefEditSession {
        bool active = false;
        std::uint16_t block = 0;
        AddInsertCommand insert;
        std::vector<EntityHandle> working_set;
        std::vector<EntityHandle> added;
    };
    struct DocState {
        GeometryStore store;
        SpatialGrid grid;
        std::vector<Group> undo;
        std::vector<Group> redo;
        std::vector<EntityHandle> selection;
        RenderSnapshot geom_cache;
        bool geom_dirty = true;
        std::uint64_t geom_version = 0;
        bool dirty = false;
        std::uint64_t document_version = 0;
        bool has_pending_dim = false;
        DimData pending_dim{};
        std::uint64_t pending_dim_version = 0;
        bool grip_active = false;
        EntityHandle grip_handle{};
        RefEditSession refedit;
        std::uint32_t grip_index = 0;
        Vec2 grip_pos{};
        GeometryStore grip_preview_store;
        std::vector<std::pair<Vec2, Vec2>> stretch_windows;
        std::vector<EntityHandle> stretch_windows_sel;
        bool stretch_preview_active = false;
        Vec2 stretch_preview_delta{};
    };
    struct DocMeta {
        std::uint64_t id = 0;
        std::string name;      ///< tab display name ("DrawingN" or a filename)
        std::string path;      ///< native file path ("" = untitled)
        bool is_dxf_path = false;
    };
    // Park the active document's live heavy state into `d` (move); restore it from `d`.
    void park_active(DocState& d);
    void load_active(DocState& d);
    // Reset the live members to a fresh empty document (used by NewDocument + Create).
    void reset_active_state();
    void create_document(const std::string& name); // new tab, activated
    void switch_document(std::uint64_t id);
    void close_document(std::uint64_t id);
    void open_into_new_tab(const Command& command); // OpenDocument with new_tab
    [[nodiscard]] std::size_t doc_index(std::uint64_t id) const; // SIZE_MAX if absent

    std::vector<DocMeta> doc_metas_;                       // all open docs, in tab order
    std::unordered_map<std::uint64_t, DocState> parked_;   // heavy state of INACTIVE docs
    std::size_t active_idx_ = 0;                           // index into doc_metas_
    std::uint64_t next_doc_id_ = 1;                        // monotonic document id
    std::uint64_t doc_name_counter_ = 0;                   // monotonic "DrawingN" sequence
    const IFontEngine* font_engine_ = nullptr;            // applied to every new doc's store
    const IImageDecoder* image_decoder_ = nullptr;        // likewise
    /// Embedded image payloads shared with the renderer by pointer (RenderSnapshot::
    /// image_defs), re-made only when a definition's version changes.
    struct ImageBytesSlot {
        std::shared_ptr<const std::vector<std::uint8_t>> ptr;
        std::uint32_t version = 0;
    };
    std::vector<ImageBytesSlot> image_bytes_cache_;

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
    void push_erase_item(std::uint64_t group, EntityHandle erased, Command data);
    void do_undo_group();
    void remap_history_handle(EntityHandle from, EntityHandle to, Group& in_flight);
    void do_redo_group();
    void do_undo_op();

    // --- selection ---
    [[nodiscard]] bool sel_contains(EntityHandle h) const;
    void sel_add(EntityHandle h);
    void prune_selection();
    void select_window(Vec2 min, Vec2 max, bool crossing, bool additive, bool announce);
    /// Does `h` satisfy a box selection? `crossing` = any part inside or touching the
    /// box; otherwise every point must be inside. THE box test, shared by window
    /// selection and by STRETCH's "was this object crossed by the window" decision, so
    /// the two can never disagree about what a crossing window caught.
    [[nodiscard]] bool entity_hits_rect(EntityHandle h, Vec2 mn, Vec2 mx, bool crossing) const;
    /// The crossing windows on record are only meaningful for the selection they built.
    void note_selection_for_windows() { stretch_windows_sel_ = selection_; }
    void forget_stretch_windows() {
        stretch_windows_.clear();
        stretch_windows_sel_.clear();
    }

    // --- cross-document clipboard (Phase B) ---
    // In-process, app-global clipboard (NOT per-document, so a copy survives switching /
    // closing the source). Self-contained: it snapshots the source document's named
    // tables so paste can remap layer/dimstyle/block references by NAME into the target.
    struct Clipboard {
        std::vector<Command> items;        // captured entities (Add* commands)
        std::vector<Layer> src_layers;     // source layer table (index -> Layer)
        std::vector<DimStyle> src_dimstyles;
        std::vector<BlockDef> src_blocks;
        std::vector<std::string> src_fonts; // source font table (index -> name)
        Vec2 base{};                       // reference point (selection AABB min)
        bool has = false;
    };
    Clipboard clipboard_;
    [[nodiscard]] EntityHandle most_recent_dimension() const;
    void apply_chain_dimension(Vec2 at, bool baseline, std::uint64_t group);
    void apply_area_query(Vec2 at, double radius);
    void apply_list_query(Vec2 at, double radius);
    void apply_stretch(Vec2 delta, std::uint64_t group);
    /// One stretched entity: the handle it replaces and the edit that replaces it.
    struct StretchEdit {
        EntityHandle handle;
        Command edited;
    };
    /// THE stretch computation: every selected entity that would change under `delta`,
    /// with its edited form. Used by the commit AND by the live preview, so the rubber
    /// band cannot show something different from what the click will produce.
    [[nodiscard]] std::vector<StretchEdit> stretched_commands(Vec2 delta) const;
    void apply_copy_clipboard();
    void apply_cut_clipboard(std::uint64_t group);
    void apply_paste_clipboard(Vec2 at, std::uint64_t group, bool at_cursor);

    // --- modify (operate on the selection / a pick) ---
    void apply_move(Vec2 delta, bool copy, std::uint64_t group);
    void apply_mirror(Vec2 a, Vec2 b, bool erase_source, std::uint64_t group);
    void apply_offset(Vec2 pick, double radius, double distance, Vec2 side, std::uint64_t group);
    void apply_trim(Vec2 pick, double radius, std::uint64_t group);
    void apply_join(const std::vector<Vec2>& picks, double radius, std::uint64_t group);
    void apply_join_selection(double radius, std::uint64_t group);
    /// HATCH "Select objects" mode: build a hatch from the selected closed polylines.
    void apply_hatch_from_selection(const std::string& pattern, double scale, double angle,
                                    std::uint64_t group, Rgb color2 = {});
    /// HATCH "Pick internal point" mode: trace the boundary enclosing `p` (+ islands) from
    /// the surrounding geometry and create the hatch. Shared boundary builder below.
    void apply_hatch_pick_point(Vec2 p, const std::string& pattern, double scale, double angle,
                                std::uint64_t group, Rgb color2 = {});
    /// Shared JOIN core: merge every connected sub-chain among `ents` (lines/arcs/open
    /// polylines sharing endpoints within `radius`) into polyline(s), one undo group.
    void join_entities(const std::vector<EntityHandle>& ents, double radius, std::uint64_t group);
    void apply_rotate(Vec2 base, double angle, std::uint64_t group, bool copy = false);
    void apply_scale(Vec2 base, double factor, std::uint64_t group, bool copy = false);
    void apply_array_rect(int rows, int cols, double dx, double dy, double angle,
                          std::uint64_t group);
    void apply_array_path(const ArrayPathCommand& c);
    void apply_divide_measure(const DividePathCommand& c);
    void apply_break(const BreakCommand& c);
    void apply_align(const AlignSelectionCommand& c);
    void apply_lengthen(const LengthenCommand& c);
    bool nearest_boundary_ahead(EntityHandle self, Vec2 fix, Vec2 mov, Vec2& target) const;
    void apply_fillet_curves(EntityHandle h1, EntityHandle h2, Vec2 pick1, Vec2 pick2,
                             double radius, std::uint64_t group);
    void apply_extend_arc(EntityHandle h, Vec2 pick, std::uint64_t group);
    void apply_purge(std::uint8_t what);
    void apply_audit(bool fix);
    void apply_define_block(const DefineBlockCommand& c);
    void apply_write_block(const WriteBlockCommand& c);
    void apply_pedit(const PeditCommand& c);
    [[nodiscard]] text::FieldContext field_context_now() const;
    /// The block-content form of `handles` (kinds a block can hold); `taken` lists the
    /// ones that went in, `skipped` counts the rest.
    void collect_block_content(const std::vector<EntityHandle>& handles, BlockContent& content,
                               std::vector<EntityHandle>& taken, int& skipped) const;
    void apply_refedit(const RefEditCommand& c);
    void apply_refset(const RefSetCommand& c);
    void apply_refclose(const RefCloseCommand& c);
    void apply_polyline_vertex(const PolylineVertexCommand& c);
    void apply_attach_image(const AttachImageCommand& c);
    void apply_image_clip(const SetImageClipCommand& c);
    void apply_layout(const LayoutCommand& c);
    void apply_create_viewport(const CreateViewportCommand& c);
    void apply_viewport_view(const SetViewportViewCommand& c);
    [[nodiscard]] std::string fmt_len(double v) const;
    [[nodiscard]] std::string fmt_ang(double radians) const;
    void apply_revcloud_object(const RevcloudObjectCommand& c);
    void apply_revcloud_reverse(std::uint64_t group);
    void apply_explode(std::uint64_t group);
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
    void apply_object_dimension(std::uint8_t type, Vec2 pick1, Vec2 pick2, Vec2 pick3, Vec2 pick4, double radius,
                                std::uint16_t style, std::uint64_t group);
    // Shared resolution: the entity(ies) under the pick(s) -> a dimension's def
    // points (a, b, line_pt). Used by both apply_object_dimension (create) and the
    // ResolveDimObjectCommand preview query, so there is one resolution path.
    [[nodiscard]] bool resolve_dim_defs(std::uint8_t type, Vec2 pick1, Vec2 pick2, double radius,
                          DimData& out, Vec2 pick3 = {}, Vec2 pick4 = {}) const;
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
    RefEditSession refedit_;

    // MATCHPROP source: the captured source entity (a snapshot of its property values as
    // an Add*Command) plus its handle, set by MatchPropPickSourceCommand and reused by
    // each MatchPropApplyCommand. Engine-side only; persists across a command's target
    // picks and is replaced on the next source pick. Does not affect the selection.
    std::optional<Command> match_source_;
    EntityHandle match_source_handle_;

    RenderSnapshot geom_cache_; // payload rebuilt only when geometry changes
    bool geom_dirty_ = true;
    bool pickstyle_group_ = true; ///< PICKSTYLE: pick a member -> select its group
    RenderSnapshot plot_snapshot_;          // fine-tolerance buffer for PLOT (geom-thread written)
    std::atomic<std::uint64_t> plot_version_{0};
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
    // Edit a text entity's content in place (TEXT/MTEXT/QLEADER label), preserving
    // all other fields, as one undo group. Used by double-click + TEXTEDIT.
    void apply_text_edit(Vec2 at, double pick_radius, const std::string& content,
                         std::uint64_t group);
    // PR palette: set one property on every selected entity as one undo group.
    void apply_set_property(PropertyId id, const PropertyValue& value, std::uint64_t group);
    // MATCHPROP: capture the entity nearest `point` as the source; apply the captured
    // source's filtered properties to the entity nearest `point` (one undo group each).
    void apply_match_pick_source(Vec2 point, double radius);
    void apply_match_source_from_selection(); // noun-verb: source = first selected entity
    void apply_match_apply(Vec2 point, double radius, const MatchPropFilter& filter,
                           std::uint64_t group);
    bool grip_active_ = false;
    EntityHandle grip_handle_{};
    std::uint32_t grip_index_ = 0;
    Vec2 grip_pos_{};
    GeometryStore grip_preview_store_;

    // STRETCH state. The crossing windows that built the current selection, kept so
    // STRETCH knows which vertices the user "caught" -- AutoCAD's rule is that a crossed
    // object stretches only those, while a picked or enclosed one moves whole. Valid only
    // while `stretch_windows_sel_` still equals `selection_`: any edit that replaces the
    // selection invalidates them without every such site having to know.
    std::vector<std::pair<Vec2, Vec2>> stretch_windows_;
    std::vector<EntityHandle> stretch_windows_sel_;
    // The live rubber-band (StretchPreviewCommand): previewed on grip_preview_store_.
    bool stretch_preview_active_ = false;
    Vec2 stretch_preview_delta_{};

    // Publish-cost bookkeeping. A publish runs on every cursor move, so anything in it
    // that scales with the scene or the selection is paid per mouse move -- which is
    // what makes a large drawing feel laggy. These make the per-move work O(1) in scene
    // size and O(selection) only when the selection or the drawing actually changed.
    std::uint64_t geom_cache_id_ = 0; ///< bumps on every scene rebuild; never resets
    std::uint64_t edit_serial_ = 0;   ///< bumps on every mutating command and rebuild
    // Selection highlight + property summary, rebuilt only when (selection, edit_serial)
    // changes; `sel_cache_build_` stamps the slots that already hold it.
    bool sel_cache_valid_ = false;
    std::uint64_t sel_cache_serial_ = 0;
    std::uint64_t sel_cache_build_ = 0;
    std::vector<EntityHandle> sel_cache_handles_;
    std::vector<Vec2> sel_cache_lines_;
    std::vector<Vec2> sel_cache_fills_;
    SelectionSummary sel_cache_summary_;
    // STRETCH's "was this object crossed by a recorded window" classification does not
    // depend on the drag delta, so it is computed once per (selection, windows, edit)
    // rather than on every preview frame. Mutable: stretched_commands() is const.
    mutable bool stretch_class_valid_ = false;
    mutable std::uint64_t stretch_class_serial_ = 0;
    mutable std::vector<EntityHandle> stretch_class_handles_;
    mutable std::vector<std::pair<Vec2, Vec2>> stretch_class_windows_;
    mutable std::vector<bool> stretch_class_crossed_;

    std::atomic<std::uint64_t> version_{0};
    std::jthread worker_;
};

} // namespace musacad::core
