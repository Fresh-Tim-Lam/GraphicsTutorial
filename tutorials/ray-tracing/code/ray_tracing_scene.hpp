// ray_tracing_scene.hpp —— 光线跟踪示例的**共享场景**（离线渲染 demo 与窗口交互 demo 共用）
//
// 内容：材质与图元（球 / 盒 / 圆柱 / 三角网格）、BVH、两条求交路径（BVH 加速 / 暴力法）、相机模型。
// 用途：`#include "ray_tracing_scene.hpp"` → `Scene scene = makeScene();` → `hitSceneBvh(ray, hit, scene)`。
// 说明：场景里既有"一半透明一半实心"的三角网格（逐面材质），也有前景悬浮透明球；
//       相机用 yaw/pitch 描述朝向，便于交互式调整。
//
// 关联文档：tutorials/ray-tracing/README.md；src/include/gfx/render/raytracer.hpp

#ifndef GFX_TUTORIAL_RAY_TRACING_SCENE_HPP
#define GFX_TUTORIAL_RAY_TRACING_SCENE_HPP

#include <gfx/core/canvas.hpp>
#include <gfx/core/color.hpp>
#include <gfx/geom/bvh.hpp>
#include <gfx/geom/ray.hpp>
#include <gfx/render/lighting.hpp>
#include <gfx/render/raytracer.hpp>
#include <gfx/render/view.hpp>

#include <cmath>
#include <vector>

namespace rt_demo {

using gfx::Aabb;
using gfx::Bvh;
using gfx::HitRecord;
using gfx::Light;
using gfx::Material;
using gfx::Ray;
using gfx::SurfaceHit;
using gfx::View;
using gfx::math::Vec3;

inline constexpr float kPi = 3.14159265358979323846f;

// 三角形（仅"不规则几何体"用）：三点按 a→b→c 右手定则给出**朝外**法向；
// material 为该面自己的材质（nullptr ⇒ 用所属图元的整体材质）——用于做出"一半透明一半实心"。
struct Tri {
    Vec3 a, b, c;
    const Material* material = nullptr;
};

// 场景图元（类型标签 + 参数 + 材质 + 预计算 AABB；BVH 只吃 AABB，精确求交在回调里按索引分派）
// @note 字段按 kind 复用：Sphere/Box/Cylinder/Lens 各自解释 p0/p1/radius/halfHeight，含义见各行注释。
struct Prim {
    enum Kind { Sphere, Box, Cylinder, Mesh, Lens } kind = Sphere;
    Vec3 p0, p1;              // 球：p0=中心；盒：p0=min、p1=max；圆柱：p0=轴心点、p1=轴方向；
                              // 透镜：p0=透镜中心、p1=光轴（单位向量）
    float radius = 0.0f;      // 球 / 圆柱半径；透镜 = 两球的曲率半径 R
    float halfHeight = 0.0f;  // 圆柱半高；透镜 = 球心到透镜中心的距离（< R）
    Material material;
    Aabb box;
    std::vector<Tri> triangles;   // 仅 Mesh 用：不规则几何体的三角面（朝外法向由 a→b→c 定）
};

inline Vec3 compMin(const Vec3& a, const Vec3& b) { return Vec3(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)); }
inline Vec3 compMax(const Vec3& a, const Vec3& b) { return Vec3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)); }

// 轴对齐盒求交：用 geom 的 intersectAabb 拿到 t，再由命中点相对盒中心的最大分量定面法向（朝外）。
inline bool intersectBox(const Ray& ray, const Aabb& box, HitRecord& out) {
    float tNear = 0.0f, tFar = 0.0f;
    if (!gfx::intersectAabb(ray, box, tNear, tFar)) return false;
    const float t = (tNear > 1e-4f) ? tNear : tFar;
    if (t <= 1e-4f) return false;

    const Vec3 p = ray.at(t);
    const Vec3 d = p - box.center();
    const Vec3 e = box.extent() * 0.5f;
    int axis = 0;
    float best = -1.0f;
    for (int i = 0; i < 3; ++i) {
        const float r = (e[i] != 0.0f) ? std::fabs(d[i]) / e[i] : 0.0f;
        if (r > best) { best = r; axis = i; }
    }
    Vec3 n(0.0f, 0.0f, 0.0f);
    n[axis] = (d[axis] >= 0.0f) ? 1.0f : -1.0f;
    out.hit = true; out.t = t; out.point = p; out.normal = n;
    return true;
}

// 光线与"实心球"的交区间 [t0, t1]；区间完全落在射线后方时返回 false。
inline bool rayBallInterval(const Ray& ray, const Vec3& center, float radius, float& t0, float& t1) {
    const Vec3 oc = ray.origin - center;
    const float a = gfx::math::dot(ray.direction, ray.direction);
    if (a <= 0.0f) return false;
    const float b = 2.0f * gfx::math::dot(oc, ray.direction);
    const float c = gfx::math::dot(oc, oc) - radius * radius;
    const float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) return false;
    const float sq = std::sqrt(disc);
    t0 = (-b - sq) / (2.0f * a);
    t1 = (-b + sq) / (2.0f * a);
    return t1 > 0.0f;
}

// 双凸透镜求交：透镜 = 两颗半径同为 R 的实心球的**交集**（球心在 center ∓ halfSep*axis）。
// 做法：分别求射线与两球的实心区间，取交集即"在透镜内部"的区间。
// 命中点是"较晚进入 / 较早离开"的那颗球的球面，其朝外法向就是透镜表面法向。
// 进入与射出都返回**最近的正向交点**且法向始终朝外 —— 交给跟踪器判"从内侧命中"从而翻转，正好用于两次折射。
inline bool intersectLens(const Ray& ray, const Vec3& center, const Vec3& axis,
                          float R, float halfSep, HitRecord& out) {
    const Vec3 cA = center - axis * halfSep;
    const Vec3 cB = center + axis * halfSep;
    float a0 = 0.0f, a1 = 0.0f, b0 = 0.0f, b1 = 0.0f;
    if (!rayBallInterval(ray, cA, R, a0, a1)) return false;
    if (!rayBallInterval(ray, cB, R, b0, b1)) return false;

    const float tEnter = std::max(a0, b0);
    const float tExit  = std::min(a1, b1);
    if (tExit <= tEnter) return false;   // 交集为空 ⇒ 光线只擦到单颗球，没穿过透镜

    const float tMin = 1e-4f;
    float t = 0.0f;
    Vec3 sphereCenter;
    if (tEnter > tMin) {                 // 从外部进入：入口在"较晚进入"的那颗球面上
        t = tEnter;
        sphereCenter = (a0 > b0) ? cA : cB;
    } else if (tExit > tMin) {           // 从透镜内部射出：出口在"较早离开"的那颗球面上
        t = tExit;
        sphereCenter = (a1 < b1) ? cA : cB;
    } else {
        return false;
    }

    out.hit = true;
    out.t = t;
    out.point = ray.at(t);
    if (!gfx::math::normalize(out.point - sphereCenter, out.normal)) return false;   // 朝外法向
    return true;
}

// 双凸透镜的构造与焦距计算见 computeBox 之后（需要先有 computeBox）。

// 按图元类型分派到 geom 的精确求交（与 BVH 解耦的核心：回调自己知道 index 对应谁）。
// outFaceMaterial（可选）：Mesh 命中那个三角面自带的材质；非 Mesh 或该面未指定时为 nullptr。
inline bool intersectPrim(const Ray& ray, const Prim& prim, HitRecord& out, const Material** outFaceMaterial = nullptr) {
    if (outFaceMaterial) *outFaceMaterial = nullptr;
    switch (prim.kind) {
        case Prim::Sphere:   return gfx::intersectSphere(ray, prim.p0, prim.radius, out);
        case Prim::Box:      return intersectBox(ray, prim.box, out);
        case Prim::Cylinder: return gfx::intersectCylinder(ray, prim.p0, prim.p1, prim.radius, prim.halfHeight, out);
        case Prim::Lens:     return intersectLens(ray, prim.p0, prim.p1, prim.radius, prim.halfHeight, out);
        case Prim::Mesh: {
            // 逐三角面求交取最近；面法向由顶点顺序（a→b→c）给出 ⇒ 对闭合实体即朝外法向。
            HitRecord best;
            const Tri* bestTri = nullptr;
            for (const Tri& tri : prim.triangles) {
                HitRecord h;
                if (gfx::intersectTriangle(ray, tri.a, tri.b, tri.c, h) && (!bestTri || h.t < best.t)) {
                    best = h;
                    bestTri = &tri;
                }
            }
            if (!bestTri) return false;
            out = best;
            if (outFaceMaterial) *outFaceMaterial = bestTri->material;
            return true;
        }
    }
    return false;
}

inline Aabb computeBox(const Prim& prim) {
    switch (prim.kind) {
        case Prim::Sphere:
            return Aabb{prim.p0 - Vec3(prim.radius, prim.radius, prim.radius),
                        prim.p0 + Vec3(prim.radius, prim.radius, prim.radius)};
        case Prim::Box:
            return Aabb{compMin(prim.p0, prim.p1), compMax(prim.p0, prim.p1)};
        case Prim::Cylinder: {
            Vec3 a;
            gfx::math::normalize(prim.p1, a);
            const Vec3 e0 = prim.p0 - a * prim.halfHeight;
            const Vec3 e1 = prim.p0 + a * prim.halfHeight;
            const Vec3 r(prim.radius, prim.radius, prim.radius);
            return Aabb{compMin(e0, e1) - r, compMax(e0, e1) + r};
        }
        case Prim::Lens: {
            // 透镜包围盒：以光轴为轴，轴向半长 = 中心厚/2，径向半长 = 通光半径
            const float halfThick = prim.radius - prim.halfHeight;   // R - halfSep
            const float aperture = std::sqrt(std::max(0.0f, prim.radius * prim.radius - prim.halfHeight * prim.halfHeight));
            Vec3 e;
            for (int i = 0; i < 3; ++i) {
                const float a = std::fabs(prim.p1[i]);
                e[i] = halfThick * a + aperture * std::sqrt(std::max(0.0f, 1.0f - a * a));
            }
            return Aabb{prim.p0 - e, prim.p0 + e};
        }
        case Prim::Mesh: {
            if (prim.triangles.empty()) return Aabb{};
            Aabb b{prim.triangles.front().a, prim.triangles.front().a};
            for (const Tri& tri : prim.triangles) {
                b.min = compMin(b.min, compMin(tri.a, compMin(tri.b, tri.c)));
                b.max = compMax(b.max, compMax(tri.a, compMax(tri.b, tri.c)));
            }
            return b;
        }
    }
    return Aabb{};
}

// 双凸透镜：由"通光半径 aperture + 中心厚 thickness"反解两球参数。
//   曲率半径 R = (aperture² + (thickness/2)²) / thickness；球心到中心距离 = R - thickness/2
//   （这样在镜边两球恰好相切，边缘厚度为 0）
inline Prim makeLens(const Material& material, const Vec3& center, const Vec3& axis,
                     float aperture, float thickness) {
    Vec3 ax;
    if (!gfx::math::normalize(axis, ax)) ax = Vec3(0.0f, 0.0f, 1.0f);
    Prim prim;
    prim.kind = Prim::Lens;
    prim.p0 = center;
    prim.p1 = ax;
    prim.radius = (aperture * aperture + thickness * thickness * 0.25f) / thickness;
    prim.halfHeight = prim.radius - thickness * 0.5f;
    prim.material = material;
    prim.box = computeBox(prim);
    return prim;
}

// 厚透镜焦距（双凸、两面曲率半径相同 R、中心厚 d、折射率 n）：
//   1/f = (n-1) * [ 2/R - (n-1)*d / (n*R²) ]
// 仅供摆放时估算物距用（真实成像是射线跟踪算出来的，不依赖此式）。
inline float lensFocalLength(float aperture, float thickness, float refractiveIndex) {
    const float R = (aperture * aperture + thickness * thickness * 0.25f) / thickness;
    const float n = refractiveIndex;
    return 1.0f / ((n - 1.0f) * (2.0f / R - (n - 1.0f) * thickness / (n * R * R)));
}

// 造一个"不规则半透明晶体"：以正八面体为骨架，6 个顶点各自扰动 + 非等比缩放 + 绕 Y 轴旋转，
// 得到外形不规则的多面体。8 个三角面按**一致朝外**的顶点顺序给出（法向由 a→b→c 右手定则确定，
// 故射线跟踪器只在"从内侧命中"时把它翻过来，正好用于透明体的进出判定）。
// 面材质按"法向的 x 分量"分两半：+x 侧（画面右侧）透明、-x 侧实心 ⇒ 一半透明一半实心。
// （每个面恰好只含 +x 或 -x 两个顶点之一，故直接用顶点索引分组即可。）
inline Prim makeCrystal(const Material& clearHalf, const Material& solidHalf,
                        const Vec3& center, float scale, float yaw) {
    // 局部骨架顶点：+x, -x, +y, -y, +z, -z（各带手工扰动 ⇒ 不规则）
    const Vec3 local[6] = {
        Vec3( 1.15f,  0.10f, -0.15f),
        Vec3(-0.95f, -0.20f,  0.22f),
        Vec3( 0.12f,  1.05f,  0.28f),
        Vec3(-0.18f, -1.28f, -0.10f),
        Vec3( 0.20f, -0.12f,  1.22f),
        Vec3(-0.25f,  0.16f, -1.02f),
    };
    // 正八面体的 8 个面（顶点索引；顺序保证法向朝外）。前 4 个面含 +x 顶点(0)，后 4 个含 -x 顶点(1)。
    const int faces[8][3] = {
        {0, 2, 4}, {0, 4, 3}, {0, 3, 5}, {0, 5, 2},
        {1, 2, 5}, {1, 4, 2}, {1, 3, 4}, {1, 5, 3},
    };

    const float c = std::cos(yaw), s = std::sin(yaw);
    Vec3 v[6];
    for (int i = 0; i < 6; ++i) {
        const Vec3 p = local[i] * scale;
        v[i] = center + Vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);   // 绕 Y 轴旋转
    }

    Prim prim;
    prim.kind = Prim::Mesh;
    prim.material = solidHalf;          // 兜底（本网格每个面都会显式指定材质）
    prim.triangles.reserve(8);
    for (int f = 0; f < 8; ++f) {
        const Material& faceMat = (f < 4) ? clearHalf : solidHalf;
        prim.triangles.push_back(Tri{v[faces[f][0]], v[faces[f][1]], v[faces[f][2]], &faceMat});
    }
    prim.box = computeBox(prim);
    return prim;
}

// 材质小工厂：字段多且大多默认 0，用具名参数比逐字段赋值清爽。
inline Material makeMaterial(Vec3 diffuse, Vec3 specular, float shininess,
                             float reflectivity = 0.0f, float transparency = 0.0f, float refractiveIndex = 1.0f) {
    Material m;
    m.diffuse = diffuse;
    m.specular = specular;
    m.shininess = shininess;
    m.reflectivity = reflectivity;
    m.transparency = transparency;
    m.refractiveIndex = refractiveIndex;
    return m;
}

// 完整场景：图元 + BVH + 无限地面 + 光源 + 背景色。
// @note 材质统一放进 materials 池：三角面会**长期持有指向材质的指针**（逐面材质），
//       故材质元素地址必须稳定 —— 池用 vector 且构造期预留容量（不重分配）；
//       移动 Scene 只是抢走缓冲（元素地址不变）；禁用拷贝，避免意外复制后指针失效。
struct Scene {
    std::vector<Material> materials;           // 材质池（元素地址稳定）
    const Material* planeMaterial = nullptr;   // 无限地面材质（指向 materials 池内元素）
    std::vector<Prim> prims;                   // 入 BVH 的图元
    Bvh bvh;                                   // 只吃 AABB
    Vec3 planePoint{0.0f, -1.0f, 0.0f};
    Vec3 planeNormal{0.0f, 1.0f, 0.0f};
    std::vector<Light> lights;
    Vec3 background{0.06f, 0.08f, 0.12f};

    Scene() { materials.reserve(16); }
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = default;
    Scene& operator=(Scene&&) = default;
};

// 把材质登记进池并返回其**稳定引用**（供图元 / 三角面长期持有）。
inline const Material& addMaterial(Scene& scene, const Material& material) {
    scene.materials.push_back(material);
    return scene.materials.back();
}

// 构建默认示例场景（地面 + 4 球 + 立方体 + 圆柱 + 半透明晶体 + 前景悬浮球 + 5 光源）。
inline Scene makeScene() {
    Scene scene;

    // ---- 材质（登记进池取得稳定引用；三角面的逐面材质会长期持有这些指针）----
    const Material& groundMat = addMaterial(scene, makeMaterial(
        Vec3(0.35f, 0.36f, 0.42f), Vec3(0.12f, 0.12f, 0.12f), 64.0f, /*ks=*/0.12f));
    const Material& cubeMat = addMaterial(scene, makeMaterial(
        Vec3(0.72f, 0.36f, 0.14f), Vec3(0.25f, 0.25f, 0.25f), 48.0f));
    const Material& cylMat = addMaterial(scene, makeMaterial(
        Vec3(0.18f, 0.52f, 0.78f), Vec3(0.4f, 0.4f, 0.4f), 64.0f));
    const Material& mirrorMat = addMaterial(scene, makeMaterial(
        Vec3(0.05f, 0.05f, 0.05f), Vec3(0.9f, 0.9f, 0.9f), 256.0f, /*ks=*/0.85f));
    const Material& glassMat = addMaterial(scene, makeMaterial(
        Vec3(0.02f, 0.02f, 0.02f), Vec3(0.9f, 0.9f, 0.9f), 256.0f, /*ks=*/0.0f, /*kt=*/0.9f, /*n=*/1.5f));
    const Material& redMat = addMaterial(scene, makeMaterial(
        Vec3(0.85f, 0.15f, 0.15f), Vec3(0.6f, 0.6f, 0.6f), 64.0f));
    // 不规则晶体：一半透明的玻璃、一半不透明的深青绿实心
    const Material& crystalClearMat = addMaterial(scene, makeMaterial(
        Vec3(0.04f, 0.10f, 0.12f), Vec3(0.95f, 0.95f, 0.95f), 256.0f, /*ks=*/0.0f, /*kt=*/0.90f, /*n=*/1.55f));
    const Material& crystalSolidMat = addMaterial(scene, makeMaterial(
        Vec3(0.10f, 0.34f, 0.32f), Vec3(0.45f, 0.50f, 0.50f), 96.0f));
    // 悬浮透明球：淡青玻璃泡（靠近摄像头的前景物体）
    const Material& bubbleMat = addMaterial(scene, makeMaterial(
        Vec3(0.03f, 0.07f, 0.08f), Vec3(0.95f, 0.95f, 0.95f), 512.0f, /*ks=*/0.0f, /*kt=*/0.92f, /*n=*/1.45f));
    // 凸透镜材质：强折射玻璃（留一点表面反射）
    const Material& lensMat = addMaterial(scene, makeMaterial(
        Vec3(0.02f, 0.02f, 0.02f), Vec3(0.95f, 0.95f, 0.95f), 256.0f, /*ks=*/0.0f, /*kt=*/0.97f, /*n=*/1.5f));

    scene.planeMaterial = &groundMat;

    // ---- 场景：立方体 + 圆柱 + 镜面球 + 玻璃球 + 红球 + 不规则晶体 + 悬浮透明球 ----
    scene.prims.push_back(Prim{Prim::Box, Vec3(-2.10f, -1.00f, -1.00f), Vec3(-1.10f, 0.00f, 0.00f), 0.0f, 0.0f, cubeMat, {}});
    scene.prims.push_back(Prim{Prim::Cylinder, Vec3(1.05f, -0.50f, -0.70f), Vec3(0.0f, 1.0f, 0.0f), 0.40f, 0.50f, cylMat, {}});
    scene.prims.push_back(Prim{Prim::Sphere, Vec3(0.15f, -0.40f, -1.70f), {}, 0.60f, 0.0f, mirrorMat, {}});
    scene.prims.push_back(Prim{Prim::Sphere, Vec3(1.55f, -0.55f, -2.30f), {}, 0.45f, 0.0f, glassMat, {}});
    scene.prims.push_back(Prim{Prim::Sphere, Vec3(-0.90f, -0.60f, -2.40f), {}, 0.40f, 0.0f, redMat, {}});
    // 不规则半透明晶体：右前方、悬于地面之上（+x 侧透明、-x 侧实心）
    scene.prims.push_back(makeCrystal(crystalClearMat, crystalSolidMat, Vec3(2.30f, -0.30f, -1.10f), 0.55f, 0.45f));
    // 悬浮透明球：位于相机（z = +4.6）与场景之间，只遮住右侧三个物体的一部分
    scene.prims.push_back(Prim{Prim::Sphere, Vec3(0.70f, 0.40f, 2.40f), {}, 0.36f, 0.0f, bubbleMat, {}});

    // ---- 3 块凸透镜（真实折射实体，双凸 = 两球交集）----
    // 三块**同一规格**的透镜，只是物距 u 不同，用于对照三个成像区间：
    //   u ∈ (0, f)   ⇒ 正立放大的**虚像**（放大镜）
    //   u ∈ (f, 2f)  ⇒ 倒立放大的**实像**
    //   u > 2f       ⇒ 倒立缩小的**实像**
    // 每块透镜各有自己的目标物体（连线互不重叠），摆放方向由"目标 → 透镜"的方向决定。
    const float lensAperture  = 0.28f;
    const float lensThickness = 0.078f;
    const float lensIor       = 1.5f;
    const float lensF         = lensFocalLength(lensAperture, lensThickness, lensIor);   // 厚透镜焦距 ≈ 1.04
    auto placeLens = [&](const Vec3& target, float objectDistance, const Vec3& dirFromTarget) {
        Vec3 d;
        if (!gfx::math::normalize(dirFromTarget, d)) d = Vec3(0.0f, 0.0f, 1.0f);
        return makeLens(lensMat, target + d * objectDistance, d * -1.0f, lensAperture, lensThickness);
    };
    scene.prims.push_back(placeLens(Vec3(-0.90f, -0.60f, -2.40f), 0.70f * lensF, Vec3(-0.30f, 0.62f, 0.72f)));  // 红球：u = 0.7f
    scene.prims.push_back(placeLens(Vec3( 0.15f, -0.40f, -1.70f), 1.50f * lensF, Vec3( 0.05f, 0.45f, 0.89f)));  // 镜面球：u = 1.5f
    scene.prims.push_back(placeLens(Vec3( 1.05f, -0.50f, -0.70f), 3.00f * lensF, Vec3(-0.45f, 0.32f, 0.83f)));  // 圆柱：u = 3f

    for (Prim& p : scene.prims) p.box = computeBox(p);

    // ---- BVH：只吃 AABB ----
    std::vector<Aabb> boxes;
    boxes.reserve(scene.prims.size());
    for (const Prim& p : scene.prims) boxes.push_back(p.box);
    scene.bvh.build(boxes);

    // ---- 5 个彩色点光源 ----
    scene.lights.push_back(Light{Vec3(5.0f, 7.0f, 4.0f), Vec3(1.00f, 0.96f, 0.90f), 1.15f});   // 主光
    scene.lights.push_back(Light{Vec3(-6.0f, 3.0f, 3.0f), Vec3(1.00f, 0.55f, 0.35f), 0.65f});  // 暖色补光
    scene.lights.push_back(Light{Vec3(0.5f, 4.0f, -6.0f), Vec3(0.35f, 0.55f, 1.00f), 0.85f});  // 冷色轮廓光
    scene.lights.push_back(Light{Vec3(-4.5f, 6.0f, 6.0f), Vec3(1.00f, 0.82f, 0.62f), 0.55f});  // 前排暖补光
    scene.lights.push_back(Light{Vec3(2.0f, 1.2f, 6.5f), Vec3(0.55f, 0.72f, 1.00f), 0.45f});   // 近景冷补光

    return scene;
}

// 统计：场景求交的"精确求交次数"与 BVH"节点访问次数"
struct Stats {
    long long intersectCalls = 0;
    long long nodeVisits = 0;
};

// 用 BVH 求最近命中（含无限地面平面：不入 BVH，单独求交后取最近）
inline bool hitSceneBvh(const Ray& ray, SurfaceHit& out, const Scene& scene, Stats& stats) {
    HitRecord best;
    const Material* bestMat = nullptr;
    bool found = false;

    HitRecord gp;
    if (gfx::intersectPlane(ray, scene.planePoint, scene.planeNormal, gp)) {
        best = gp; bestMat = scene.planeMaterial; found = true;
    }

    HitRecord bp;
    int bi = -1;
    const Material* bMat = nullptr;   // Mesh 命中面上的材质（nullptr ⇒ 用图元整体材质）
    float bt = 0.0f;
    scene.bvh.closestHit(ray, [&](const Ray& r, int i, float& ot) {
        ++stats.intersectCalls;
        const Material* faceMat = nullptr;
        HitRecord h;
        if (!intersectPrim(r, scene.prims[static_cast<std::size_t>(i)], h, &faceMat)) return false;
        ot = h.t;
        if (bi < 0 || h.t < bp.t) {
            bp = h;
            bi = i;
            bMat = faceMat ? faceMat : &scene.prims[static_cast<std::size_t>(i)].material;
        }
        return true;
    }, bt);
    stats.nodeVisits += static_cast<long long>(scene.bvh.lastVisitCount());

    if (bi >= 0 && (!found || bp.t < best.t)) {
        best = bp; bestMat = bMat; found = true;
    }
    if (!found) return false;
    out.geometry = best; out.material = bestMat;
    return true;
}

// 暴力法（关掉 BVH）：对全部图元线性遍历取最近。用于与 BVH 版做逐像素一致性校验。
inline bool hitSceneBrute(const Ray& ray, SurfaceHit& out, const Scene& scene, Stats& stats) {
    HitRecord best;
    const Material* bestMat = nullptr;
    bool found = false;

    HitRecord gp;
    if (gfx::intersectPlane(ray, scene.planePoint, scene.planeNormal, gp)) {
        best = gp; bestMat = scene.planeMaterial; found = true;
    }

    for (const Prim& prim : scene.prims) {
        ++stats.intersectCalls;
        const Material* faceMat = nullptr;
        HitRecord h;
        if (!intersectPrim(ray, prim, h, &faceMat)) continue;
        if (!found || h.t < best.t) { best = h; bestMat = faceMat ? faceMat : &prim.material; found = true; }
    }
    if (!found) return false;
    out.geometry = best; out.material = bestMat;
    return true;
}

// 相机：位置 + 朝向（yaw 绕 Y、pitch 俯仰）+ 视场角。朝向用 yaw/pitch 描述便于交互式调整。
struct CameraRig {
    Vec3 position{0.0f, 1.35f, 4.60f};
    float yaw = 0.0f;         // 绕 Y：0 ⇒ 看向 -z；+ ⇒ 向右转
    float pitch = -0.2618f;   // 俯仰：+ ⇒ 抬头（初始 ≈ -15°，与离线 demo 的 lookAt 一致）
    float fovDeg = 42.0f;

    Vec3 forward() const {
        const float cp = std::cos(pitch);
        return Vec3(std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp);
    }
    Vec3 right() const {
        // 与 (0,1,0) 叉乘：pitch 被夹在 ±85° 内，不会退化
        Vec3 r;
        gfx::math::normalize(gfx::math::cross(forward(), Vec3(0.0f, 1.0f, 0.0f)), r);
        return r;
    }
    Vec3 up() const { return gfx::math::cross(right(), forward()); }

    View makeView(int width, int height) const {
        View view;
        view.setViewport(width, height);
        view.lookAt(position, position + forward(), Vec3(0.0f, 1.0f, 0.0f));
        view.setPerspective(fovDeg * kPi / 180.0f, static_cast<float>(width) / static_cast<float>(height),
                            0.05f, 200.0f);
        return view;
    }
};

} // namespace rt_demo

#endif // GFX_TUTORIAL_RAY_TRACING_SCENE_HPP
