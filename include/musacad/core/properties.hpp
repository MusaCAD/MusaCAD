#pragma once

#include <cstdint>
#include <string>

namespace musacad::core {

/// An RGB colour (0-255 per channel). Used for both layer colours (always
/// explicit) and entity colour overrides.
struct Rgb {
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    friend bool operator==(const Rgb&, const Rgb&) = default;
};

/// The standard linetypes Musa CAD models. Stored as the property; visual
/// stippling is applied render-side.
enum class Linetype : std::uint8_t {
    Continuous = 0,
    Dashed = 1,
    Center = 2,
    Hidden = 3,
};

/// A drawing layer: name + default properties + display state. Lineweight is in
/// hundredths of a millimetre (the DXF unit, code 370; 0..211).
struct Layer {
    std::string name;
    Rgb color{255, 255, 255};
    Linetype linetype = Linetype::Continuous;
    std::uint8_t lineweight = 25; ///< 0.25 mm
    bool on = true;               ///< off layers don't render
    bool frozen = false;          ///< frozen layers don't render
    bool locked = false;          ///< locked layers render but can't be selected/modified
    friend bool operator==(const Layer&, const Layer&) = default;
};

/// Per-entity property attributes. Each property is either inherited from the
/// entity's layer (its ByLayer flag set) or an explicit override. Kept compact
/// (8 bytes) because every entity carries one: a packed flags byte instead of
/// three bools, and an 8-bit lineweight instead of a double. Remains an aggregate
/// (brace-init {layer, color, linetype, lineweight, flags}).
struct EntityProps {
    static constexpr std::uint8_t kColorByLayer = 1u << 0;
    static constexpr std::uint8_t kLinetypeByLayer = 1u << 1;
    static constexpr std::uint8_t kLineweightByLayer = 1u << 2;
    static constexpr std::uint8_t kAllByLayer =
        kColorByLayer | kLinetypeByLayer | kLineweightByLayer;

    std::uint16_t layer = 0; ///< index into the store's layer table
    Rgb color{};             ///< override colour when !color_by_layer()
    Linetype linetype = Linetype::Continuous;
    std::uint8_t lineweight = 25;       ///< override, hundredths of a mm
    std::uint8_t flags = kAllByLayer;   ///< which properties are ByLayer

    [[nodiscard]] bool color_by_layer() const noexcept { return (flags & kColorByLayer) != 0; }
    [[nodiscard]] bool linetype_by_layer() const noexcept {
        return (flags & kLinetypeByLayer) != 0;
    }
    [[nodiscard]] bool lineweight_by_layer() const noexcept {
        return (flags & kLineweightByLayer) != 0;
    }
    void set_color_by_layer(bool v) noexcept { set_flag(kColorByLayer, v); }
    void set_linetype_by_layer(bool v) noexcept { set_flag(kLinetypeByLayer, v); }
    void set_lineweight_by_layer(bool v) noexcept { set_flag(kLineweightByLayer, v); }

    friend bool operator==(const EntityProps&, const EntityProps&) = default;

private:
    void set_flag(std::uint8_t bit, bool v) noexcept {
        if (v) {
            flags = static_cast<std::uint8_t>(flags | bit);
        } else {
            flags = static_cast<std::uint8_t>(flags & ~bit);
        }
    }
};

/// The effective properties used to draw an entity, after ByLayer resolution.
struct ResolvedProps {
    Rgb color;
    Linetype linetype = Linetype::Continuous;
    std::uint8_t lineweight = 25; ///< hundredths of a mm
};

/// Resolves an entity's effective properties: an explicit override wins,
/// otherwise the value is inherited from its layer (ByLayer). This is the
/// conceptual core of the property model.
[[nodiscard]] inline ResolvedProps resolve(const EntityProps& e, const Layer& layer) noexcept {
    ResolvedProps out;
    out.color = e.color_by_layer() ? layer.color : e.color;
    out.linetype = e.linetype_by_layer() ? layer.linetype : e.linetype;
    out.lineweight = e.lineweight_by_layer() ? layer.lineweight : e.lineweight;
    return out;
}

} // namespace musacad::core
