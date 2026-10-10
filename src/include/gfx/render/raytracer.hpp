// raytracer.hpp —— gfx 渲染层：递归光线跟踪器（反射 / 折射 / 阴影 / 超采样）
//
// @file 提供**与具体物体类型解耦**的递归光线跟踪器（作业三沉淀为库能力）：
//       场景由调用方以"求交函数"注入（给定光线返回最近命中的 SurfaceHit），跟踪器只负责
//       着色、法向定向、自交偏移、阴影、镜面反射、规则透射与递归深度控制。
//
// @note 解耦的两条缝（都靠回调）：
//       1) 主求交：RayTracer::HitFunction = std::function<bool(const Ray&, SurfaceHit&)>；
//       2) 阴影 / 二次光线：直接复用同一个 HitFunction（光照层不硬编码物体类型）。
// @note 递归口径（与作业三一致，便于对照）：trace(ray, depth) 进入本层即消耗一层预算；
//       depth ≤ 0 ⇒ 黑；反射 / 折射仅在**消耗后仍有 ≥ 2 层**（即传入 depth ≥ 3）时才发射二次光线。
//       故 depth = 1 / 2 只算局部光照，depth ≥ 3 才开始出现反射 / 折射。
// @note 颜色为 0..1 线性 float；trace 结果已钳制到 [0,1]。写像素时可选择"线性→近似 sRGB"伽马。
//
// 用途：用 gfx 自己渲染光线跟踪图像（脱离外部框架）；配合 gfx/render/view.hpp 生成主光线。
// 目标读者：使用 / 学习光线跟踪的调用方（#include <gfx/render/raytracer.hpp>）。
// 分层语义：render 层，单向依赖 geom/（命中记录）与 math/（向量运算）。
// 关联文档：docs/design/adr-0012-raytracing-lighting.md；gfx/render/lighting.hpp；gfx/geom/ray.hpp；gfx/geom/bvh.hpp
//
// 状态：新增（2026-10-09）。递归跟踪 + 超采样渲染（作业三沉淀为库能力）。

#ifndef GFX_RENDER_RAYTRACER_HPP
#define GFX_RENDER_RAYTRACER_HPP

#include "gfx/core/canvas.hpp"
#include "gfx/core/color.hpp"
#include "gfx/geom/ray.hpp"
#include "gfx/math/vec.hpp"
#include "gfx/render/lighting.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace gfx {

// 阴影测试的容忍量：交点已沿法向偏移（offsetAlong），这里再留一点裕量，避免掠射角下自遮挡。
inline constexpr float kShadowTolerance = 1e-4f;

namespace detail {

// 逐分量钳制到 [0,1]（反射 / 透射的线性叠加可能过亮或为负）。
inline math::Vec3 clamp01(const math::Vec3& c) {
    return math::Vec3(std::min(1.0f, std::max(0.0f, c.x)),
                      std::min(1.0f, std::max(0.0f, c.y)),
                      std::min(1.0f, std::max(0.0f, c.z)));
}

// 默认材质（SurfaceHit 未携带材质时用）：0.8 灰、其余取默认值。
inline Material defaultMaterial() {
    Material m;
    m.diffuse = math::Vec3(0.8f, 0.8f, 0.8f);
    return m;
}

} // namespace detail

// 一次命中的完整信息：几何（交点 / t / 朝外法向）+ 材质指针（nullptr = 用默认材质）。
struct SurfaceHit {
    HitRecord geometry;
    const Material* material = nullptr;
    // 可选的**着色法向**（世界空间、朝外的单位向量）：法线贴图在这里注入。
    // 零向量（默认）⇒ 用几何法向着色。
    // @note 分工：几何法向负责全部"光线数学"（内外判定 / 自交偏移 / 反射 / 折射），
    //       着色法向只负责光照 —— 这样凹凸不会把反射/折射光线算歪，也不会破坏透明体的进出判定。
    math::Vec3 shadingNormal;
};

// 光线跟踪器。场景由调用方通过"求交函数"给出：给定光线，返回是否命中最近表面（并写 SurfaceHit）。
class RayTracer {
public:
    // 求交函数：命中最近表面 ⇒ 写 surface 并返回 true；未命中返回 false。
    // （声明为公开 API：私有成员 hitFunction_ 的类型依赖它，见 docs/conventions/code-style.md 例外 4。）
    using HitFunction = std::function<bool(const Ray&, SurfaceHit&)>;

private:
    HitFunction        hitFunction_;                        // 场景求交（注入，与物体类型解耦）
    std::vector<Light> lights_;                             // 点光源列表
    math::Vec3         background_{0.0f, 0.0f, 0.0f};       // 未命中时返回的背景色（0..1）
    int                samplesPerAxis_ = 1;                 // 每像素 n×n 子采样（抗锯齿）；1 = 关闭
    bool               gammaCorrect_ = false;               // 输出是否做"线性→近似 sRGB"伽马（默认关）

    // 单通道 [0,1] → 8bit；gamma 为真时先做 pow(c, 1/2.2)（线性 → 近似 sRGB）。
    static std::uint8_t channelToByte(float v, bool gamma) {
        float c = std::min(1.0f, std::max(0.0f, v));
        if (gamma) c = std::pow(c, 1.0f / 2.2f);
        return static_cast<std::uint8_t>(std::lround(c * 255.0f));
    }

    Color toColor(const math::Vec3& c) const {
        return Color(channelToByte(c.x, gammaCorrect_),
                     channelToByte(c.y, gammaCorrect_),
                     channelToByte(c.z, gammaCorrect_));
    }

public:
    // @param hitFunction 场景求交函数（见 HitFunction）
    // @param lights      点光源列表
    RayTracer(HitFunction hitFunction, std::vector<Light> lights)
        : hitFunction_(std::move(hitFunction)), lights_(std::move(lights)) {}

    // 背景色（未命中时返回；约定 0..1）。
    void setBackground(const math::Vec3& color) { background_ = color; }

    // 每像素 n×n 子采样（抗锯齿）；n < 1 按 1 处理（= 关闭）。
    void setSamplesPerAxis(int n) { samplesPerAxis_ = (n < 1) ? 1 : n; }

    int samplesPerAxis() const { return samplesPerAxis_; }

    // 写像素前是否做伽马（pow(c, 1/2.2)："线性 → 近似 sRGB"）。默认关闭。
    void setGammaCorrect(bool enabled) { gammaCorrect_ = enabled; }

    // 递归跟踪：返回该光线对应的颜色（已钳制在 [0,1]）。
    // @param depth 剩余递归预算；进入本层即消耗 1，预算耗尽（传入 ≤ 0）返回零向量（黑）。
    //              反射 / 折射需要消耗后仍 ≥ 2 层（传入 depth ≥ 3）才发射二次光线。
    math::Vec3 trace(const Ray& ray, int depth) const {
        --depth;                                          // 进入本层消耗一层预算
        if (depth < 0) return math::Vec3();               // 预算耗尽（传入 depth ≤ 0）：黑

        // 1) 求交：未命中 ⇒ 背景色
        SurfaceHit surface;
        if (!hitFunction_(ray, surface)) return background_;

        // 2) 取几何与材质（材质 nullptr ⇒ 一份默认材质）
        const HitRecord& hit = surface.geometry;
        const Material fallback = detail::defaultMaterial();
        const Material& material = surface.material ? *surface.material : fallback;

        // 3) 法向定向：朝外法向与入射方向同侧 ⇒ 从内侧命中，翻转法向
        math::Vec3 normal = hit.normal;
        bool inside = false;
        if (math::dot(normal, ray.direction) > 0.0f) {
            normal = -normal;
            inside = true;
        }

        // 3b) 着色法向：回调若提供了（如被法线贴图扰动过），**只用于光照**；零向量 ⇒ 同几何法向。
        //     内测命中时要跟着几何法向一起翻转，否则透明体内部的光照会反。
        math::Vec3 shadingNormal = normal;
        if (surface.shadingNormal.length() > 0.0f) {
            math::Vec3 unit;
            shadingNormal = inside ? -surface.shadingNormal : surface.shadingNormal;
            if (!math::normalize(shadingNormal, unit)) shadingNormal = normal;
            else shadingNormal = unit;
        }

        // 4) 自交偏移：反射 / 阴影起点沿 +N，透射起点沿 −N
        const math::Vec3 pointOffset = offsetAlong(hit.point, normal);
        const math::Vec3 viewDir = -ray.direction;        // 交点 → 视点

        // 5) 局部光照：阴影测试复用同一个求交函数（命中且更近 ⇒ 遮挡）
        const math::Vec3 local = shadeLocal(
            hit.point, shadingNormal, viewDir, material, lights_,
            [this](const math::Vec3& p, const math::Vec3& dirToLight, float distanceToLight) {
                SurfaceHit blocker;
                if (!hitFunction_(Ray(p, dirToLight), blocker)) return false;
                return blocker.geometry.t < distanceToLight - kShadowTolerance;
            });

        // 6) 早退：既不透射也不反射 ⇒ 直接返回局部光照（避免无意义递归）
        if (material.reflectivity + material.transparency < 1e-3f) {
            return detail::clamp01(local);
        }

        math::Vec3 color = local;

        // 7) 镜面反射：R = reflect(d, N)
        if (material.reflectivity > 0.0f && depth > 1) {
            const math::Vec3 reflected = math::reflect(ray.direction, normal);
            color += trace(Ray(pointOffset, reflected), depth) * material.reflectivity;
        }

        // 8) 规则透射：eta = inside ? n : 1/n；全反射（refract 返回 false）⇒ 不加折射光
        if (material.transparency > 0.0f && depth > 1) {
            const float eta = inside ? material.refractiveIndex : 1.0f / material.refractiveIndex;
            math::Vec3 refracted;
            if (math::refract(ray.direction, normal, eta, refracted)) {
                const math::Vec3 transmitOrigin = hit.point - normal * kSelfIntersectionEpsilon;
                color += trace(Ray(transmitOrigin, refracted), depth) * material.transparency;
            }
        }

        return detail::clamp01(color);
    }

    // 渲染进度回调：逐行渲染时上报（已完成行数 done / 总行数 total，done 从 1 起）。传空则不上报。
    using ProgressCallback = std::function<void(int done, int total)>;

    // 渲染整张图像到 Canvas（颜色 [0,1] → 8bit，可选伽马）。
    // @param canvas       目标画布（逐像素覆盖写）
    // @param rayForPixel  生成器：给定**像素坐标**（浮点，像素中心 / 子采样位置）返回世界射线；
    //                     与现有 View 配合时即 `View::pixelToRay` 的封装。
    // @param maxDepth     初始递归预算（见 trace）
    // @param onProgress   可选进度回调（每渲染完一行调用一次）；默认不回调。
    // @note 超采样：每像素 n×n 条射线，子采样位置取**子像素中心**；结果取平均。n=1 即逐像素中心采样。
    template <class RayGenerator>
    void render(Canvas& canvas, RayGenerator&& rayForPixel, int maxDepth,
                const ProgressCallback& onProgress = ProgressCallback()) const {
        const int width = canvas.width();
        const int height = canvas.height();
        const int n = samplesPerAxis_;
        const float invN = 1.0f / static_cast<float>(n);
        const float invSamples = 1.0f / static_cast<float>(n * n);

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                math::Vec3 accum;
                for (int sy = 0; sy < n; ++sy) {
                    for (int sx = 0; sx < n; ++sx) {
                        // 子像素中心：n=1 时即 (x+0.5, y+0.5)
                        const float px = static_cast<float>(x) + (static_cast<float>(sx) + 0.5f) * invN;
                        const float py = static_cast<float>(y) + (static_cast<float>(sy) + 0.5f) * invN;
                        accum += trace(rayForPixel(px, py), maxDepth);
                    }
                }
                canvas.putPixel(x, y, toColor(accum * invSamples));
            }
            if (onProgress) onProgress(y + 1, height);
        }
    }
};

} // namespace gfx

#endif // GFX_RENDER_RAYTRACER_HPP
