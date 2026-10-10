// depth.hpp —— gfx 深度缓冲（Z-Buffer）：逐像素深度测试，解决遮挡
//
// @file 提供与 Canvas 同尺寸的深度缓冲，以及"带深度测试的三角形填充"（讲义第五讲"可见性/遮挡、Z 缓冲"）：
//       - 深度用 **NDC 深度**（z_ndc ∈ [−1, 1]，**越小越近**；由 View::projectToNdc 产出）；
//       - 三角形内部的深度由**重心坐标插值**三个顶点的深度（讲义 p29 的"重心插值"）；
//       - 逐像素 test-and-set：只有更近的片元才写入颜色与深度。
//
// @note 为什么不能靠"画家算法"（按远近排序整个物体再依次画）：它处理不了**相互穿插**的三角形
//       （一个三角形的一部分在另一个前面、另一部分在后面）。深度缓冲是**逐像素**的，
//       因此天然处理任意遮挡，而且**与绘制顺序无关**（两种顺序结果相同，测试锁定）。
//
// 用途：3D 面片绘制的可见性；为后续 3D 光栅管线打基础。
// 目标读者：gfx 光栅化/3D 管线使用者（#include <gfx/render/depth.hpp>）。
// 关联文档：docs/design/adr-0008-triangle-raster.md；gfx/render/triangle.hpp；gfx/render/view.hpp（projectToNdc 产出深度）
//
// 状态：新增（2026-09-16）。对应讲义第五讲的 Z 缓冲。

#ifndef GFX_DEPTH_HPP
#define GFX_DEPTH_HPP

#include "gfx/core/canvas.hpp"
#include "gfx/core/color.hpp"
#include "gfx/math/vec.hpp"
#include "gfx/render/triangle.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace gfx {

// 深度缓冲：与画布同尺寸，逐像素保存"最近"的深度值。
// @note 深度用 NDC z（越小越近）；初始化为 kFar（远平面），故任何真实片元都会通过第一次测试。
class DepthBuffer {
private:
    int width_ = 0;
    int height_ = 0;
    std::vector<float> depths_;

public:
    static constexpr float kFar = 1.0f;   // 远平面（NDC 最大深度）

    DepthBuffer() = default;

    DepthBuffer(int width, int height) { resize(width, height); }

    void resize(int width, int height) {
        width_ = width;
        height_ = height;
        depths_.assign(static_cast<std::size_t>(width) * height, kFar);
    }

    int width() const { return width_; }
    int height() const { return height_; }

    // 全部重置为远平面
    void clear() {
        std::fill(depths_.begin(), depths_.end(), kFar);
    }

    // 读取 (x, y) 的深度；越界返回 kFar
    float depthAt(int x, int y) const {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) return kFar;
        return depths_[static_cast<std::size_t>(y) * width_ + x];
    }

    // 深度测试并写入：新片元**更近**（depth < 已存值）时写入并返回 true，否则返回 false。
    // @note 用严格小于（等价 GL_LESS）：同深度时先画的保留，避免同深度反复覆盖造成闪烁。
    bool testAndSet(int x, int y, float depth) {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) return false;
        const std::size_t i = static_cast<std::size_t>(y) * width_ + x;
        if (depth < depths_[i]) {
            depths_[i] = depth;
            return true;
        }
        return false;
    }
};

// 带**深度测试**的三角形填充：像素内部深度由重心坐标插值，逐像素与深度缓冲比较。
// @param depth3 三个顶点各自的深度（NDC z，越小越近），顺序与 a/b/c 对应
// @return 三角形未退化返回 true（退化时不绘制、返回 false）
// @note 内部判定沿用 insideTriangle（含 top-left 边界规则），保证共享边不重不漏。
inline bool fillTriangleDepth(Canvas& canvas, DepthBuffer& depth,
                              const math::Vec2& a, const math::Vec2& b, const math::Vec2& c,
                              const math::Vec3& depth3, const Color& color) {
    if (edgeFunction(a, b, c) == 0.0f) return false;

    const int xBegin = static_cast<int>(std::floor(std::min({a.x, b.x, c.x})));
    const int xEnd = static_cast<int>(std::ceil(std::max({a.x, b.x, c.x})));
    const int yBegin = static_cast<int>(std::floor(std::min({a.y, b.y, c.y})));
    const int yEnd = static_cast<int>(std::ceil(std::max({a.y, b.y, c.y})));

    for (int y = yBegin; y < yEnd; ++y) {
        for (int x = xBegin; x < xEnd; ++x) {
            const math::Vec2 p(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
            if (!insideTriangle(p, a, b, c)) continue;

            math::Vec3 w;
            if (!barycentric(p, a, b, c, w)) continue;
            const float z = w.x * depth3.x + w.y * depth3.y + w.z * depth3.z;

            if (depth.testAndSet(x, y, z)) canvas.putPixel(x, y, color);
        }
    }
    return true;
}

} // namespace gfx

#endif // GFX_DEPTH_HPP
