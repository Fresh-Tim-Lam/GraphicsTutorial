// color.hpp —— gfx 像素颜色（RGBA8）
//
// 用途：像素/画布使用的 8bit RGBA 颜色类型（内存顺序 R,G,B,A；上屏转换由 Window 内部完成）。
// 目标读者：gfx 像素模块使用者（#include <gfx/core/color.hpp>）。
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

// 带透明度的便捷构造
inline constexpr Color rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    return Color(r, g, b, a);
}

// Alpha 混合（source-over）：把 src 按**自身 a** 叠到 dst 上，返回合成颜色。
// @note 边界情形刻意走整数分支：a == 255 直接返回 src、a == 0 直接返回 dst——
//       既省一次乘除，也避免"完全不透明却因舍入而变色"。
// @note 输出 alpha 固定为 255（当前用途是往不透明画布上叠色；半透明**画布**不在范围内）。
inline Color blend(const Color& src, const Color& dst) {
    const unsigned a = src.a;
    if (a == 255) return src;
    if (a == 0) return dst;
    const unsigned inv = 255u - a;
    const auto mix = [a, inv](std::uint8_t s, std::uint8_t d) -> std::uint8_t {
        return static_cast<std::uint8_t>((static_cast<unsigned>(s) * a + static_cast<unsigned>(d) * inv + 127u) / 255u);
    };
    return Color(mix(src.r, dst.r), mix(src.g, dst.g), mix(src.b, dst.b), 255);
}

} // namespace gfx

#endif // GFX_COLOR_HPP
