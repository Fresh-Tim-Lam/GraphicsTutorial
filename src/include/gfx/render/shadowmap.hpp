// shadowmap.hpp —— 阴影贴图（Shadow Map）：从光源视角做一次"只写深度"的 pass，
//                   主 pass 里把片元的**世界位置**投回光源视角，比较深度判断是否被遮挡。
//
// 定位：光栅化路径的阴影方案（光线跟踪那条路用阴影射线，不需要它）。
//       它复用同一套光栅化管线：光源视角的 View + `fillTriangleShaded`（片元着色器返回黑色、只用它的深度写入）。
//
// 两条 pass：
//   ① `build()`：以 lightView 把场景三角形光栅化进 `DepthBuffer`——只留最近深度，颜色丢弃；
//   ② `visibility()`：主 pass 里对每个片元，把 `worldPos + N*normalOffset` 投进光源视角，取该像素的
//      最近深度与自身深度比较；阴影贴图里的更近 ⇒ 被挡。带**深度容差**与可选 PCF 软化。
//
// 关于自阴影（shadow acne）——本实现的两道防线：
//   1) **法向偏移**：采样点沿法向抬起 normalOffset（世界单位），让它离开自己所在的面；
//   2) **深度容差 = 基本项 + 斜率项**：一个 texel 覆盖的深度范围随"面与光线的夹角"增大而增大
//      （∝ tanθ），故容差按斜率缩放（slopeScale，单位"texel"）。只做法向偏移是不够的：
//      掠射处（N·L→0）法向偏移对**沿光方向的深度**几乎无贡献，棱边/曲面上会留下椒盐状噪点。
//      斜率项把 tanθ 封顶（slopeCap），因为掠射处该光源本来就照不亮，过度加容差只会让真阴影漏光。
//
// 约定与局限（刻意不做的事）：
//   - 光源视角用**正交投影**覆盖场景 ⇒ 等效于"平行光"。用点光源 + 正交贴图是常见近似（离得越远越准）；
//     真要点光源应改用立方体阴影贴图或透视贴图（本库暂不做）。斜率项依赖"方向处处相同"，也只对正交成立。
//   - **一张贴图只能服务一个光源**：多个光源要各建一张。演示里用"光源方向匹配"来决定该问哪张图。
//   - 不做背面剔除：光源视角下正反两面都写深度。好处是单面几何（如地面）也能进图；
//     代价是斜面上的自阴影需要上面两道防线压掉。
//
// 关联文档：docs/design/adr-0015-shadow-map.md；gfx/render/shading.hpp / depth.hpp / view.hpp
//
// 状态：新增（2026-10-09）。

#ifndef GFX_SHADOWMAP_HPP
#define GFX_SHADOWMAP_HPP

#include "gfx/core/canvas.hpp"
#include "gfx/math/math.hpp"
#include "gfx/render/depth.hpp"
#include "gfx/render/shading.hpp"
#include "gfx/render/view.hpp"

#include <algorithm>
#include <cmath>

namespace gfx {

class ShadowMap {
private:
    View lightView_;
    DepthBuffer depth_;
    math::Vec3 dirToLight_{0.0f, 1.0f, 0.0f};   // 场景 → 光源（正交视角下处处相同；configure 时取得）
    float texelWorld_ = 1.0f;                   // 光源视角下 1 texel 的世界尺寸（斜率项的尺度）
    float ndcPerWorld_ = 1.0f;                  // 正交投影：1 世界单位的深度 = 多少 NDC z
    float normalOffset_ = 0.02f;                // 沿法向抬起的距离（世界单位）
    float depthBias_ = 0.01f;                   // 深度比较的基本容差（世界单位）
    float slopeScale_ = 2.5f;                   // 斜率缩放系数（单位：texel）
    float slopeCap_ = 8.0f;                     // tanθ 封顶（防掠射处无限放大）
    int pcfRadius_ = 1;                         // 0 = 单点采样；1 = 3×3 平均（软化锯齿边）

public:
    ShadowMap() = default;

    // 设定光源视角与分辨率（分辨率 ≤ 0 时忽略本次设置），并派生出方向/尺度等派生量
    // @note 期望 lightView 为**正交**投影（lookAt + setOrthographic）；透视投影下"方向处处相同"不成立，
    //       斜率项会失真（但 basic 项仍按正交口径换算，故请勿这样用）。
    ShadowMap& configure(const View& lightView, int size) {
        if (size <= 0) return *this;
        lightView_ = lightView;
        depth_.resize(size, size);

        dirToLight_ = -lightView_.forward();   // 光源看向场景，反过来就是"场景指向光源"
        const ViewFrustum& frustum = lightView_.frustum();
        texelWorld_ = frustum.width() / static_cast<float>(size);
        const float depthRange = frustum.farZ - frustum.nearZ;
        ndcPerWorld_ = (depthRange > 1e-6f) ? (2.0f / depthRange) : 1.0f;
        return *this;
    }

    // ---- 可调参数（世界单位，故与"把场景整体缩放"无关；默认值针对物体尺度 ~1 的教学场景）----
    ShadowMap& setNormalOffset(float v) { normalOffset_ = (v < 0.0f) ? 0.0f : v; return *this; }
    ShadowMap& setDepthBias(float v) { depthBias_ = (v < 0.0f) ? 0.0f : v; return *this; }
    ShadowMap& setSlopeScale(float v) { slopeScale_ = (v < 0.0f) ? 0.0f : v; return *this; }
    ShadowMap& setPcfRadius(int r) { pcfRadius_ = (r < 0) ? 0 : (r > 3 ? 3 : r); return *this; }
    float normalOffset() const { return normalOffset_; }
    float depthBias() const { return depthBias_; }
    float slopeScale() const { return slopeScale_; }
    int pcfRadius() const { return pcfRadius_; }

    int size() const { return depth_.width(); }
    bool ready() const { return depth_.width() > 0 && depth_.height() > 0; }
    const View& lightView() const { return lightView_; }
    const DepthBuffer& depthBuffer() const { return depth_; }

    // 深度 pass：把三角形按光源视角光栅化，**只写深度**（颜色丢弃）
    // @return 实际写入过像素的三角形数量
    // @note 片元着色器返回零色 —— 本 pass 不关心颜色，只用管线里的深度测试与写入。
    template <class TriangleRange>
    int build(const TriangleRange& triangles) {
        if (!ready()) return 0;
        depth_.clear();
        Canvas scratch(depth_.width(), depth_.height());   // 只为复用管线；内容不参与后续计算
        int drawn = 0;
        for (const ShadingTriangle& triangle : triangles) {
            const FragmentShader discardColor = [](const Fragment&) { return math::Vec3(); };
            if (fillTriangleShaded(scratch, lightView_, triangle, discardColor, &depth_)) ++drawn;
        }
        return drawn;
    }

    // 可见性：1 = 完全受光，0 = 完全在阴影里（PCF 下取平均，得到中间值）
    // @param worldPos 片元的世界位置
    // @param normal   片元的**着色法向**（法线贴图扰动后的更好：能同时压掉凹凸引起的自阴影）
    float visibility(const math::Vec3& worldPos, const math::Vec3& normal) const {
        if (!ready()) return 1.0f;

        const math::Vec3 samplePoint = worldPos + normal * normalOffset_;
        const math::Vec4 clip = lightView_.viewProjectionMatrix()
                              * math::Vec4(samplePoint.x, samplePoint.y, samplePoint.z, 1.0f);
        if (clip.w <= 1e-6f) return 1.0f;   // 落在光源后方 ⇒ 不投影阴影（正交下 w ≡ 1，实际只在透视时触发）

        const math::Vec2 pixel = lightView_.clipToPixel(clip);
        const int centerX = static_cast<int>(std::floor(pixel.x));
        const int centerY = static_cast<int>(std::floor(pixel.y));
        const float ndcZ = clip.z / clip.w;

        // 斜率缩放容差：tanθ = √(1−N·L²)/N·L，封顶后与"1 texel 的世界尺寸"相乘
        const float nDotL = std::max(1e-3f, math::dot(normal, dirToLight_));
        float slope = std::sqrt(std::max(0.0f, 1.0f - nDotL * nDotL)) / nDotL;
        if (slope > slopeCap_) slope = slopeCap_;
        const float biasNdc = (depthBias_ + slopeScale_ * texelWorld_ * slope) * ndcPerWorld_;

        int blocked = 0;
        int total = 0;
        for (int dy = -pcfRadius_; dy <= pcfRadius_; ++dy) {
            for (int dx = -pcfRadius_; dx <= pcfRadius_; ++dx) {
                // 越界时 depthAt 返回远平面 ⇒ 视为"没有遮挡物"，贴图边缘不会误判成阴影
                const float stored = depth_.depthAt(centerX + dx, centerY + dy);
                ++total;
                if (stored < ndcZ - biasNdc) ++blocked;
            }
        }
        return 1.0f - static_cast<float>(blocked) / static_cast<float>(total);
    }

    // 是否被判定为"在阴影里"（visibility < 0.5 的便捷封装，可直接当 shadeLocal 的阴影回调用）
    bool isShadowed(const math::Vec3& worldPos, const math::Vec3& normal) const {
        return visibility(worldPos, normal) < 0.5f;
    }
};

} // namespace gfx

#endif // GFX_SHADOWMAP_HPP
