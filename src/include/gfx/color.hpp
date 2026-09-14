// color.hpp —— gfx 像素颜色（RGBA8）
//
// 用途：像素/画布使用的 8bit RGBA 颜色类型（内存顺序 R,G,B,A；上屏转换由 Window 内部完成）。
// 目标读者：gfx 像素模块使用者（#include <gfx/color.hpp>）。
// 关联文档：docs/design/adr-0001-gfx-math-canvas.md；docs/conventions/naming.md
//
// 状态：生效中（2026-09-09）

#ifndef GFX_COLOR_HPP
#define GFX_COLOR_HPP

#include <cstdint>

namespace gfx {

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;   // 默认不透明

    constexpr Color() = default;
    constexpr Color(std::uint8_t r_, std::uint8_t g_, std::uint8_t b_, std::uint8_t a_ = 255)
        : r(r_), g(g_), b(b_), a(a_) {}

    constexpr bool operator==(const Color& o) const {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
    constexpr bool operator!=(const Color& o) const { return !(*this == o); }
};

// 常用便捷构造（语义化命名）可能以后还有其他的协议（如 HSL）也可以考虑添加
inline constexpr Color rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return Color(r, g, b, 255);
}

} // namespace gfx

#endif // GFX_COLOR_HPP
