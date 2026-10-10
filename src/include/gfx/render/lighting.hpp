// lighting.hpp —— gfx 渲染层：光照模型（材质 / 点光源 / 局部光照 + 阴影）
//
// @file 提供光线跟踪的**局部光照**模型（作业三沉淀为库能力）：
//       - Material：材质（漫反射 / 高光 / 自发光 + 高光指数 + 反射率 / 透射率 / 折射率）；
//       - Light：点光源（位置 / 颜色 / 强度）；
//       - shadeLocal(...)：环境光 + 自发光 + 逐光源 Lambert 漫反射 + Blinn-Phong 高光 + 阴影。
//
// @note **与场景解耦**：本层不知道场景里有哪些图元，故"该点是否被光源遮挡"由调用方以
//       **阴影测试回调**注入（签名见 shadeLocal）。这既是分层纪律（render 不硬编码物体类型），
//       也让同一份光照代码能配任意求交实现（BVH / 暴力 / 别的图元）。
// @note 颜色一律为 0..1 的**线性** float 三元组；转 8bit 与是否伽马由调用方决定
//       （见 gfx/render/raytracer.hpp 的 RayTracer::render）。
// @note 约定与课程（作业三）一致，便于数值对齐：环境光 = 0.05·diffuse；漫反射 = diffuse ⊙ 光色·强度·max(0,N·L)；
//       高光 = specular ⊙ 光色·强度·pow(max(0,N·H), shininess)（Blinn-Phong）。
//
// 用途：光线跟踪的着色内核；配合 gfx/render/raytracer.hpp 组成递归跟踪器。
// 目标读者：使用光照模型的调用方与教学读者（#include <gfx/render/lighting.hpp>）。
// 分层语义：render 层，**只依赖 math/**；命中几何（geom::HitRecord）由调用方提供。
// 关联文档：docs/design/adr-0012-raytracing-lighting.md；gfx/render/raytracer.hpp；gfx/geom/ray.hpp；gfx/math/vec.hpp
//
// 状态：新增（2026-10-09）。光照模型（作业三沉淀为库能力）。

#ifndef GFX_RENDER_LIGHTING_HPP
#define GFX_RENDER_LIGHTING_HPP

#include "gfx/math/vec.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gfx {

// 自交偏移量：二级光线（阴影 / 反射 / 折射）起点沿法向错开这一距离，
// 避免因浮点误差在交点处立刻"命中自己"（表现为黑斑 / 噪点）。
// 与 geom/ray.hpp 的默认 tMin = 1e-4f 同量级。
inline constexpr float kSelfIntersectionEpsilon = 1e-4f;

// 沿法向把交点偏移一点：point + normal·eps。（供光照与跟踪器共用。）
inline math::Vec3 offsetAlong(const math::Vec3& point, const math::Vec3& normal) {
    return point + normal * kSelfIntersectionEpsilon;
}

// 材质：光线跟踪常用约定。颜色为 0..1 的线性 float 三元组。
struct Material {
    math::Vec3 diffuse;              // 漫反射系数（也是"基色"）
    math::Vec3 specular;             // 高光系数
    math::Vec3 emission;             // 自发光
    float shininess = 32.0f;         // 高光指数（越大高光越锐）
    float reflectivity = 0.0f;       // 镜面反射率 ks
    float transparency = 0.0f;       // 透射率 kt
    float refractiveIndex = 1.0f;    // 折射率 n（空气 1.0，玻璃约 1.5）
};

// 点光源：位置 + 颜色（0..1）+ 强度（可 > 1 用于叠加）。
struct Light {
    math::Vec3 position;
    math::Vec3 color;                // 0..1
    float intensity = 1.0f;
};

namespace detail {

// 逐分量相乘（Hadamard 积）：a ⊙ b。用于"系数 ⊙ 颜色"。
inline math::Vec3 modulate(const math::Vec3& a, const math::Vec3& b) {
    return math::Vec3(a.x * b.x, a.y * b.y, a.z * b.z);
}

} // namespace detail

// 局部光照（环境光 + 自发光 + 逐光源 Lambert 漫反射 + Blinn-Phong 高光 + 阴影）。
// @param point/normal 交点与**朝外**的单位法向
// @param viewDir      从交点指向视点的单位矢量
// @param material     材质
// @param lights       光源列表
// @param shadowTest   阴影测试回调：返回 true 表示"该点到光源被遮挡"。
//                     签名 bool(const math::Vec3& point, const math::Vec3& dirToLight, float distanceToLight)
//                     传入时点**已做自交偏移**（offsetAlong），调用方只需判断"沿该方向、在距离内是否命中物体"。
// @param ambient      环境光系数（默认 0.05，作用为 ambient · material.diffuse）
// @return material.emission + ambient·material.diffuse + Σ(漫反射 + 高光)
// @note 单光源无阴影时的贡献：
//       diffuse ⊙ light.color · light.intensity · max(0, N·L)，L = normalize(light.position − point)；
//       高光 H = normalize(L + viewDir)，spec = pow(max(0, N·H), shininess)，
//       贡献 specular ⊙ light.color · light.intensity · spec。
template <class ShadowTest>
math::Vec3 shadeLocal(const math::Vec3& point, const math::Vec3& normal, const math::Vec3& viewDir,
                      const Material& material, const std::vector<Light>& lights,
                      ShadowTest&& shadowTest, float ambient = 0.05f) {
    math::Vec3 result = material.emission + material.diffuse * ambient;

    // 自交偏移后的可见点：阴影光线从这里出发（由调用方内部完成，回调收到的已是偏移点）。
    const math::Vec3 pointOffset = offsetAlong(point, normal);

    for (const Light& light : lights) {
        math::Vec3 dirToLight;
        const math::Vec3 toLight = light.position - point;
        const float distanceToLight = toLight.length();
        if (!math::normalize(toLight, dirToLight)) continue;   // 光源与交点重合：跳过

        // 遮挡：跳过该光源的漫反射与高光（只剩环境光 / 自发光）
        if (shadowTest(pointOffset, dirToLight, distanceToLight)) continue;

        // 漫反射（Lambert）
        const float nDotL = math::dot(normal, dirToLight);
        result += detail::modulate(material.diffuse, light.color) *
                  (light.intensity * std::max(0.0f, nDotL));

        // 高光（Blinn-Phong）
        math::Vec3 halfDir;
        if (math::normalize(dirToLight + viewDir, halfDir)) {
            const float nDotH = std::max(0.0f, math::dot(normal, halfDir));
            const float spec = std::pow(nDotH, material.shininess);
            result += detail::modulate(material.specular, light.color) * (light.intensity * spec);
        }
    }
    return result;
}

} // namespace gfx

#endif // GFX_RENDER_LIGHTING_HPP
