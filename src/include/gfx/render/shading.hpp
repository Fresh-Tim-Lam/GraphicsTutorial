// shading.hpp —— 光栅化着色管线：顶点属性 + 透视校正插值 + 逐像素着色
//
// 定位：`triangle.hpp` 只会"填纯色"，本文件补上缺的那一层 ——
//       顶点带属性（位置/法向/切线/uv）→ 投影 + 近平面裁剪 → 透视校正插值 → **逐像素算颜色**。
//       于是 `texture.hpp`（贴图采样）与 `normalmap.hpp`（切空间 + 法线扰动）在**光栅化**路径上同样能用。
//
// 与场景/光照解耦：本层不知道"光从哪来、材质是什么" —— 每像素算什么颜色由调用方以
//       **片元着色回调**（FragmentShader）注入，与 raytracer.hpp 的求交回调、lighting.hpp 的阴影回调同一套思路。
//
// 用法：
//     View view; view.setViewport(canvas); view.lookAt(...); view.setPerspective(...);
//     DepthBuffer depth(canvas.width(), canvas.height());
//     fillTriangleShaded(canvas, view, tri, [&](const Fragment& f) -> math::Vec3 {
//         // f.position / f.normal / f.tangent / f.uv 都已完成**透视校正**插值
//         return shadeLocal(f.position, f.normal, ...);
//     }, &depth);
//
// 约定：
//   - 顶点属性一律在**世界空间**给出；投影用 View 的 viewProjectionMatrix。
//   - 采样点 = 像素中心 (x+0.5, y+0.5)；边界沿用 `insideTriangle` 的 top-left 规则（与 triangle.hpp 一致）。
//   - **透视校正**：uv/法向等属性按 `attr/w` 与 `1/w` 在屏幕空间线性插值后再相除；
//     深度（NDC z）在屏幕空间**本就线性**，故用未校正的权重插值（与 depth.hpp 一致）。
//   - 近平面裁剪：在裁剪空间按 `w ≥ 近平面` 裁（Sutherland–Hodgman），最多产生 2 个三角形，
//     属性随裁剪参数线性插值 —— 保证相机后方的几何不会出现"镜像鬼影"。
//
// 关联文档：docs/design/adr-0014-shaded-raster-pipeline.md；gfx/render/triangle.hpp / depth.hpp / texture.hpp / normalmap.hpp
//
// 状态：新增（2026-10-09）。

#ifndef GFX_SHADING_HPP
#define GFX_SHADING_HPP

#include "gfx/core/canvas.hpp"
#include "gfx/core/color.hpp"
#include "gfx/math/math.hpp"
#include "gfx/render/depth.hpp"
#include "gfx/render/triangle.hpp"
#include "gfx/render/view.hpp"

#include <cmath>
#include <cstdint>
#include <functional>

namespace gfx {

// 顶点属性（世界空间）
struct VertexAttributes {
    math::Vec3 position;
    math::Vec3 normal;
    math::Vec3 tangent;   // 可选：零向量表示"不提供"（法线贴图才需要）
    math::Vec2 uv;
};

// 一个片元：**透视校正插值之后**的逐像素属性
struct Fragment {
    math::Vec3 position;   // 世界空间（插值得到，可直接用于光照/阴影）
    math::Vec3 normal;
    math::Vec3 tangent;
    math::Vec2 uv;
    float depth = 0.0f;    // NDC z（越小越近）
    float invW = 1.0f;     // 1/w（视图深度倒数；需要自定义校正时可用）
};

// 片元着色回调：给定片元返回颜色（0..1 线性）
using FragmentShader = std::function<math::Vec3(const Fragment&)>;

// 一个带属性的三角形（顶点顺序决定绕序，本层不做背面剔除）
struct ShadingTriangle {
    VertexAttributes a;
    VertexAttributes b;
    VertexAttributes c;
};

namespace detail {

// 线性色 [0,1] → 8bit（四舍五入 + 钳制）
inline std::uint8_t shadingChannelToByte(float linear) {
    const float scaled = linear * 255.0f + 0.5f;
    if (scaled <= 0.0f) return 0;
    if (scaled >= 255.0f) return 255;
    return static_cast<std::uint8_t>(scaled);
}

inline Color shadingToColor(const math::Vec3& c) {
    return Color(shadingChannelToByte(c.x), shadingChannelToByte(c.y), shadingChannelToByte(c.z));
}

// 裁剪空间 → 像素坐标（公式与 View::clipToPixel 一致：透视除法 + 视口映射 + y 翻转）
inline math::Vec2 clipToPixel(const View& view, const math::Vec4& clip) {
    const float invW = 1.0f / clip.w;
    const float ox = static_cast<float>(view.viewportX());
    const float oy = static_cast<float>(view.viewportY());
    return math::Vec2(ox + (clip.x * invW * 0.5f + 0.5f) * static_cast<float>(view.viewportWidth()),
                      oy + (1.0f - (clip.y * invW * 0.5f + 0.5f)) * static_cast<float>(view.viewportHeight()));
}

// 裁剪空间顶点（clip 是齐次坐标，其 w 就是视图深度）
struct ClipVertex {
    math::Vec4 clip;
    VertexAttributes attr;
};

inline ClipVertex makeClipVertex(const math::Mat4& viewProjection, const VertexAttributes& attr) {
    ClipVertex out;
    out.clip = viewProjection * math::Vec4(attr.position.x, attr.position.y, attr.position.z, 1.0f);
    out.attr = attr;
    return out;
}

// 沿 v0 → v1 按参数 t 插值出裁剪顶点（属性随裁剪参数线性插值）
inline ClipVertex lerpClipVertex(const ClipVertex& v0, const ClipVertex& v1, float t) {
    ClipVertex out;
    out.clip = v0.clip * (1.0f - t) + v1.clip * t;
    out.attr.position = v0.attr.position * (1.0f - t) + v1.attr.position * t;
    out.attr.normal = v0.attr.normal * (1.0f - t) + v1.attr.normal * t;
    out.attr.tangent = v0.attr.tangent * (1.0f - t) + v1.attr.tangent * t;
    out.attr.uv = v0.attr.uv * (1.0f - t) + v1.attr.uv * t;
    return out;
}

// 按 `w ≥ wMin`（近平面）裁剪三角形：输出 0..4 个顶点的凸多边形（Sutherland–Hodgman 单平面版）
inline int clipTriangleToNearPlane(const ClipVertex in[3], float wMin, ClipVertex out[4]) {
    int count = 0;
    for (int i = 0; i < 3; ++i) {
        const ClipVertex& current = in[i];
        const ClipVertex& next = in[(i + 1) % 3];
        const bool currentInside = (current.clip.w >= wMin);
        const bool nextInside = (next.clip.w >= wMin);
        if (currentInside) out[count++] = current;
        if (currentInside != nextInside) {
            const float t = (wMin - current.clip.w) / (next.clip.w - current.clip.w);
            out[count++] = lerpClipVertex(current, next, t);
        }
    }
    return count;
}

// 光栅化一个"已在近平面前方"的裁剪三角形：透视校正插值 + 逐像素着色 + 可选深度测试
inline bool rasterizeClipTriangle(Canvas& canvas, const View& view,
                                  const ClipVertex& c0, const ClipVertex& c1, const ClipVertex& c2,
                                  const FragmentShader& shader, DepthBuffer* depth) {
    const math::Vec2 p0 = clipToPixel(view, c0.clip);
    const math::Vec2 p1 = clipToPixel(view, c1.clip);
    const math::Vec2 p2 = clipToPixel(view, c2.clip);
    if (edgeFunction(p0, p1, p2) == 0.0f) return false;   // 退化（零面积）

    // 屏幕空间包围盒（与 triangle.hpp 一致：floor(min) .. ceil(max)，末端开区间）
    const int xBegin = static_cast<int>(std::floor(std::min({p0.x, p1.x, p2.x})));
    const int xEnd = static_cast<int>(std::ceil(std::max({p0.x, p1.x, p2.x})));
    const int yBegin = static_cast<int>(std::floor(std::min({p0.y, p1.y, p2.y})));
    const int yEnd = static_cast<int>(std::ceil(std::max({p0.y, p1.y, p2.y})));

    // 三个顶点的 NDC 深度与 1/w（透视校正用）
    const float invW0 = 1.0f / c0.clip.w;
    const float invW1 = 1.0f / c1.clip.w;
    const float invW2 = 1.0f / c2.clip.w;
    const float z0 = c0.clip.z * invW0;
    const float z1 = c1.clip.z * invW1;
    const float z2 = c2.clip.z * invW2;

    bool drew = false;
    for (int y = yBegin; y < yEnd; ++y) {
        const float py = static_cast<float>(y) + 0.5f;
        if (py < 0.0f || py >= static_cast<float>(canvas.height())) continue;
        for (int x = xBegin; x < xEnd; ++x) {
            const float px = static_cast<float>(x) + 0.5f;
            if (px < 0.0f || px >= static_cast<float>(canvas.width())) continue;

            const math::Vec2 p(px, py);
            if (!insideTriangle(p, p0, p1, p2)) continue;      // 含 top-left 边界规则

            math::Vec3 w;
            if (!barycentric(p, p0, p1, p2, w)) continue;      // 屏幕空间重心坐标（未校正）

            // 深度：NDC z 在屏幕空间本就线性 ⇒ 直接用未校正权重插值
            const float z = w.x * z0 + w.y * z1 + w.z * z2;
            if (depth != nullptr && !depth->testAndSet(x, y, z)) continue;

            // 透视校正：先插值 attr/w 与 1/w，再相除
            const float invW = w.x * invW0 + w.y * invW1 + w.z * invW2;
            const float k0 = w.x * invW0 / invW;
            const float k1 = w.y * invW1 / invW;
            const float k2 = w.z * invW2 / invW;

            Fragment fragment;
            fragment.position = c0.attr.position * k0 + c1.attr.position * k1 + c2.attr.position * k2;
            fragment.normal = c0.attr.normal * k0 + c1.attr.normal * k1 + c2.attr.normal * k2;
            fragment.tangent = c0.attr.tangent * k0 + c1.attr.tangent * k1 + c2.attr.tangent * k2;
            fragment.uv = c0.attr.uv * k0 + c1.attr.uv * k1 + c2.attr.uv * k2;
            fragment.depth = z;
            fragment.invW = invW;

            canvas.putPixel(x, y, shadingToColor(shader(fragment)));
            drew = true;
        }
    }
    return drew;
}

// 取当前投影下的近平面阈值：透视用视体的 nearZ（= 裁剪空间 w 的门槛），正交下 w ≡ 1 ⇒ 用小量即可
inline float nearPlaneW(const View& view) {
    return (view.fovYRadians() > 0.0f) ? view.frustum().nearZ : 1e-4f;
}

} // namespace detail

// 画一个带属性的三角形：投影 → 近平面裁剪 → 透视校正插值 → 逐像素着色（可选深度测试）。
// @param canvas 目标画布
// @param view   相机/投影（顶点属性按世界空间解释）
// @param tri    三个带属性顶点
// @param shader 片元着色回调（nullptr ⇒ 直接返回 false）
// @param depth  可选深度缓冲；非空时做"越小越近"的深度测试并写入
// @return 至少写入了一个像素返回 true
inline bool fillTriangleShaded(Canvas& canvas, const View& view, const ShadingTriangle& tri,
                               const FragmentShader& shader, DepthBuffer* depth = nullptr) {
    if (!shader) return false;

    const math::Mat4 viewProjection = view.viewProjectionMatrix();
    const detail::ClipVertex in[3] = {
        detail::makeClipVertex(viewProjection, tri.a),
        detail::makeClipVertex(viewProjection, tri.b),
        detail::makeClipVertex(viewProjection, tri.c),
    };

    detail::ClipVertex polygon[4];
    const int count = detail::clipTriangleToNearPlane(in, detail::nearPlaneW(view), polygon);
    if (count < 3) return false;   // 整个三角形在近平面之后

    // 扇形三角化（count 只会是 3 或 4）
    bool drew = false;
    for (int i = 1; i + 1 < count; ++i) {
        drew |= detail::rasterizeClipTriangle(canvas, view, polygon[0], polygon[i], polygon[i + 1], shader, depth);
    }
    return drew;
}

} // namespace gfx

#endif // GFX_SHADING_HPP
