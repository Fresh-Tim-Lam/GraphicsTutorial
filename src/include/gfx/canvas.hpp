// canvas.hpp —— gfx 像素帧缓冲（Canvas）
//
// 用途：内存像素画布：把像素写到指定位置（putPixel），供窗口整帧上屏或离屏计算。
// 本类型为"画布（离屏帧缓冲）"，纯内存、无平台依赖；上屏展示由 Window 负责（见 window.hpp）。
// 目标读者：gfx 像素模块使用者与实现者。
// 关联文档：docs/design/adr-0001-gfx-math-canvas.md；window.hpp；color.hpp
//
// 状态：生效中（2026-09-09）

#ifndef GFX_CANVAS_HPP
#define GFX_CANVAS_HPP

#include "color.hpp"

#include <cstddef>
#include <vector>

namespace gfx {

class Canvas {
public:
    Canvas() = default;
    Canvas(int width, int height)
        : width_(width), height_(height), pixels_(static_cast<std::size_t>(width) * height, Color(0, 0, 0, 255)) {}
    // `cast(width * height)` `int` （32 位） 先溢出成负数，再转 size_t → 错 ❌
    int width() const { return width_; }
    int height() const { return height_; }

    // 把像素写到 (x, y)。越界像素被忽略（教学初期的裁剪策略）。
    void putPixel(int x, int y, const Color& c) {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
        pixels_[indexOf(x, y)] = c;
    }

    Color pixelAt(int x, int y) const {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) return Color(0, 0, 0, 0);
        return pixels_[indexOf(x, y)];
    }

    // 全幅清屏为同一颜色
    void clear(const Color& c) {
        for (auto& p : pixels_) p = c;
    }

    // 原始像素缓冲（行主序：第 y 行第 x 列 = data[y*width + x]），供 Window 上屏或测试读取
    const Color* data() const { return pixels_.data(); }
    Color* data() { return pixels_.data(); }

    std::size_t indexOf(int x, int y) const { return static_cast<std::size_t>(y) * width_ + x; }

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<Color> pixels_;
};

} // namespace gfx

#endif // GFX_CANVAS_HPP
