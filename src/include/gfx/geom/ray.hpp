// ray.hpp —— gfx 几何层：光线求交（Ray / Aabb / 各类图元）
//
// @file 提供光线跟踪的**纯几何地基**：光线（起点 + 单位方向）、轴对齐包围盒（AABB）、
//       以及命中结果 HitRecord，与球/平面/三角形/圆柱的求交、光线与 AABB 的 slab 测试。
//       本层只做"命中什么、命中在哪、法向朝哪"，**不含光照与递归跟踪**（后续阶段）。
//
// 用途：光线跟踪的求交内核；BVH（bvh.hpp）在此之上做加速。
// 目标读者：使用光线求交的调用方与教学读者（#include <gfx/geom/ray.hpp>）。
// 分层语义：geom 层，**只依赖 math/**（Vec3 与 dot/cross/normalize 等），不依赖 core/ 或 render/。
// 关联文档：docs/design/adr-0011-ray-geometry.md；gfx/math/vec.hpp（reflect/refract）；gfx/geom/bvh.hpp
//
// 状态：新增（2026-10-08）。光线求交地基（作业三沉淀为库能力）。
//
// 约定（全部求交函数一致）：
//   * Ray::direction 构造时归一化 ⇒ 参数 t 即"沿光线的距离"。
//   * 只接受 t > tMin（默认 1e-4f）的解：忽略原点背后与自交的平凡解（避免后续阴影出现黑斑）。
//   * HitRecord::normal 一律为**朝外**的单位法向（球/圆柱为几何朝外，平面/三角形为给定/几何法向）。
//   * 命中则写入 out 并返回 true；未命中返回 false（out 保持不变）。

#ifndef GFX_GEOM_RAY_HPP
#define GFX_GEOM_RAY_HPP

#include "gfx/math/math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gfx {

// 光线：起点 + 方向。方向在构造时**归一化**（零方向保持零向量）。
struct Ray {
    math::Vec3 origin;
    math::Vec3 direction;

    // @param o 起点
    // @param d 方向（内部归一化；零向量则保持零向量）
    Ray(const math::Vec3& o, const math::Vec3& d) : origin(o) {
        math::normalize(d, direction);
    }

    // 光线上的点：origin + t·direction
    math::Vec3 at(float t) const { return origin + direction * t; }
};

// 轴对齐包围盒（AABB）：由最小角 min 与最大角 max 定义。
struct Aabb {
    math::Vec3 min;
    math::Vec3 max;

    math::Vec3 center() const { return (min + max) * 0.5f; }
    math::Vec3 extent() const { return max - min; }

    // 表面积 2(xy + yz + zx)：BVH 的 SAH 代价度量常用量。
    float surfaceArea() const {
        const math::Vec3 e = extent();
        return 2.0f * (e.x * e.y + e.y * e.z + e.z * e.x);
    }
};

// 两个 AABB 的并（各自 min 取小、max 取大）。
inline Aabb aabbUnion(const Aabb& a, const Aabb& b) {
    return Aabb{math::Vec3(std::min(a.min.x, b.min.x), std::min(a.min.y, b.min.y), std::min(a.min.z, b.min.z)),
                math::Vec3(std::max(a.max.x, b.max.x), std::max(a.max.y, b.max.y), std::max(a.max.z, b.max.z))};
}

// 求交结果：只表达"命中了什么"，最近性由调用方在多物体之间比较。
struct HitRecord {
    bool      hit = false;
    float     t = 0.0f;        // 沿光线的参数（= 距离，因方向单位）
    math::Vec3 point;          // 交点
    math::Vec3 normal;         // **朝外**的单位法向
};

// ---- 球求交：解 |origin + t·d − center|² = r²，取最近且 t > tMin 的根 ----
// @return 命中则写入 out 并返回 true；起点在球内时命中**远面**（近根 t ≤ 0 被忽略）。
inline bool intersectSphere(const Ray& ray, const math::Vec3& center, float radius,
                            HitRecord& out, float tMin = 1e-4f) {
    const math::Vec3 oc = ray.origin - center;
    const float a = math::dot(ray.direction, ray.direction);   // 单位方向时为 1
    const float b = 2.0f * math::dot(oc, ray.direction);
    const float c = math::dot(oc, oc) - radius * radius;
    const float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) return false;                             // 完全错过

    const float sq = std::sqrt(disc);
    const float t0 = (-b - sq) / (2.0f * a);                   // 近根
    const float t1 = (-b + sq) / (2.0f * a);                   // 远根
    const float t = (t0 > tMin) ? t0 : t1;                     // 近根在起点之前则取远根
    if (t <= tMin) return false;                               // 两解都不在 t > tMin 一侧

    const math::Vec3 p = ray.at(t);
    math::Vec3 nrm;
    if (!math::normalize(p - center, nrm)) return false;       // 退化球（半径 0）
    out.hit = true; out.t = t; out.point = p; out.normal = nrm;
    return true;
}

// ---- 无限平面求交：平面由一点与非零法向定义 ----
// @note normal 为**给定法向**（归一化后原样返回）；朝向由调用方决定。
inline bool intersectPlane(const Ray& ray, const math::Vec3& planePoint, const math::Vec3& planeNormal,
                           HitRecord& out, float tMin = 1e-4f) {
    const float denom = math::dot(planeNormal, ray.direction);
    if (std::fabs(denom) < 1e-8f) return false;                // 射线平行于平面
    const float t = math::dot(planePoint - ray.origin, planeNormal) / denom;
    if (t <= tMin) return false;                               // 平面在起点背后

    math::Vec3 nrm;
    if (!math::normalize(planeNormal, nrm)) return false;      // 零法向
    out.hit = true; out.t = t; out.point = ray.at(t); out.normal = nrm;
    return true;
}

// ---- 三角形求交：Möller–Trumbore 算法（T. Möller & B. Trumbore, 1997）----
// 出处：T. Möller, B. Trumbore, "Fast, Minimum Storage Ray/Triangle Intersection",
//       Journal of Graphics Tools, 2(1):21-28, 1997。
// 选择理由：无需预计算平面方程、无需显式算交点，一次行列式判据同时给出重心坐标与 t；数值稳定、代码短。
// @note **不剔除背面**（双面）：det 取绝对值判定，正反面均可命中，保持几何通用性。
// @note 边界口径：u、v 判定取**闭区间**（u ≥ 0、v ≥ 0、u + v ≤ 1），故命中边/顶点也算命中。
// @note 退化三角形（面积 0，三顶点共线）⇒ det ≈ 0，返回 false。
inline bool intersectTriangle(const Ray& ray, const math::Vec3& a, const math::Vec3& b, const math::Vec3& c,
                              HitRecord& out, float tMin = 1e-4f) {
    const math::Vec3 e1 = b - a;
    const math::Vec3 e2 = c - a;
    const math::Vec3 pvec = math::cross(ray.direction, e2);
    const float det = math::dot(e1, pvec);
    if (std::fabs(det) < 1e-8f) return false;                  // 平行于平面或三角形退化

    const float invDet = 1.0f / det;
    const math::Vec3 tvec = ray.origin - a;
    const float u = math::dot(tvec, pvec) * invDet;
    if (u < 0.0f || u > 1.0f) return false;

    const math::Vec3 qvec = math::cross(tvec, e1);
    const float v = math::dot(ray.direction, qvec) * invDet;
    if (v < 0.0f || u + v > 1.0f) return false;

    const float t = math::dot(e2, qvec) * invDet;
    if (t <= tMin) return false;

    math::Vec3 nrm;
    if (!math::normalize(math::cross(e1, e2), nrm)) return false;   // 退化保护
    out.hit = true; out.t = t; out.point = ray.at(t); out.normal = nrm;
    return true;
}

// ---- 有限高圆柱求交：任意轴朝向，含两端圆盖 ----
// 几何：轴线段 = axisCenter ± halfHeight·â（â 为 axis 归一化）；半径 radius。
// 做法：侧面 = 无限圆柱二次方程 + |沿轴偏移| ≤ halfHeight 裁剪；两端圆盖 = 平面求交 + 径向 ≤ radius 判定；
//       在所有有效解中取最近者。法向一律朝外（侧面为径向向外，圆盖为 ±â）。
inline bool intersectCylinder(const Ray& ray, const math::Vec3& axisCenter, const math::Vec3& axis,
                              float radius, float halfHeight, HitRecord& out, float tMin = 1e-4f) {
    math::Vec3 a;
    if (!math::normalize(axis, a)) return false;               // 零轴向量：无效

    const math::Vec3 w = ray.origin - axisCenter;
    const float dA = math::dot(ray.direction, a);              // 方向沿轴分量
    const float wA = math::dot(w, a);                          // 原点沿轴偏移
    const math::Vec3 dPerp = ray.direction - a * dA;           // 方向在垂直于轴平面内的分量
    const math::Vec3 wPerp = w - a * wA;

    float bestT = 0.0f;
    math::Vec3 bestN;
    bool hit = false;

    // ---- 侧面：|wPerp + t·dPerp|² = r² ----
    const float A = math::dot(dPerp, dPerp);
    if (A > 1e-12f) {                                          // 方向不与轴平行时才有侧面解
        const float B = 2.0f * math::dot(dPerp, wPerp);
        const float C = math::dot(wPerp, wPerp) - radius * radius;
        const float disc = B * B - 4.0f * A * C;
        if (disc >= 0.0f) {
            const float sq = std::sqrt(disc);
            const float ts[2] = {(-B - sq) / (2.0f * A), (-B + sq) / (2.0f * A)};
            for (float t : ts) {
                if (t <= tMin) continue;
                const float h = wA + t * dA;                   // 相对轴心的沿轴偏移
                if (h < -halfHeight || h > halfHeight) continue;   // 超出有限高：裁掉
                math::Vec3 n;
                if (!math::normalize((w + ray.direction * t) - a * h, n)) continue;
                if (!hit || t < bestT) { hit = true; bestT = t; bestN = n; }
            }
        }
    }

    // ---- 两端圆盖：平面求交 + 径向裁剪 ----
    const float capOffsets[2] = {halfHeight, -halfHeight};
    for (float hh : capOffsets) {
        const math::Vec3 capCenter = axisCenter + a * hh;
        const math::Vec3 capNormal = a * (hh >= 0.0f ? 1.0f : -1.0f);   // 朝外的圆盖法向
        const float denom = math::dot(ray.direction, capNormal);
        if (std::fabs(denom) < 1e-8f) continue;                // 平行于圆盖平面
        const float t = math::dot(capCenter - ray.origin, capNormal) / denom;
        if (t <= tMin) continue;
        const math::Vec3 radial = ray.at(t) - capCenter;
        if (radial.lengthSquared() > radius * radius) continue;   // 落在圆盘之外
        if (!hit || t < bestT) { hit = true; bestT = t; bestN = capNormal; }
    }

    if (!hit) return false;
    out.hit = true; out.t = bestT; out.point = ray.at(bestT); out.normal = bestN;
    return true;
}

// ---- 光线与 AABB 的 slab 测试 ----
// 各轴分别求射线与两平行平面的参数区间 [t1, t2]，取所有轴的交集 [tNear, tFar]。
// @param tNear 交集下界（可为负：起点在盒内时 tNear < 0）
// @param tFar  交集上界
// @return tFar ≥ tMin 且 tNear ≤ tFar 时返回 true（tMin 用于忽略"整段都在起点之前"的盒）；
//         方向分量为 0 时**不除零**，改用"起点是否落在该轴 slab 内"的分支判断。
inline bool intersectAabb(const Ray& ray, const Aabb& box, float& tNear, float& tFar, float tMin = 1e-4f) {
    float nearT = -std::numeric_limits<float>::infinity();
    float farT  =  std::numeric_limits<float>::infinity();
    for (int i = 0; i < 3; ++i) {
        const float o = ray.origin[i];
        const float d = ray.direction[i];
        if (std::fabs(d) < 1e-8f) {                            // 方向分量 ≈ 0：不除零
            if (o < box.min[i] || o > box.max[i]) return false;   // 起点在 slab 之外 ⇒ 永不相交
            continue;
        }
        const float inv = 1.0f / d;
        float t1 = (box.min[i] - o) * inv;
        float t2 = (box.max[i] - o) * inv;
        if (t1 > t2) std::swap(t1, t2);
        if (t1 > nearT) nearT = t1;
        if (t2 < farT)  farT = t2;
        if (nearT > farT) return false;                        // 区间为空：无交
    }
    if (farT < tMin) return false;                             // 整个区间都在起点之前
    tNear = nearT;
    tFar = farT;
    return true;
}

} // namespace gfx

#endif // GFX_GEOM_RAY_HPP
