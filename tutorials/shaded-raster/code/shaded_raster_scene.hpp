// shaded_raster_scene.hpp —— 光栅化着色管线演示的**共享场景**（离线出图版与窗口交互版共用）
//
// 用途：把"部件/材质/程序化法线贴图 + 光源 + 阴影贴图 + 渲染一遍 + 相机操作"集中在这里，
//       让 shaded_raster.cpp（渲一张 PNG）与 shaded_raster_interactive.cpp（窗口内实时调相机）
//       跑的是**同一个场景、同一份着色代码** —— 与 ray-tracing 目录里 ray_tracing_scene.hpp 的分工一致。
//
// 关键性质（交互版靠它做到零成本实时）：
//   阴影贴图只依赖"光源 + 场景"，**与相机无关** ⇒ 相机移动时不必重建，启动时建一次即可。
//
// @note 贴图归属：`Scene` 自己拥有 `Maps`，部件只用 `MapKind` **枚举**指认用哪张，
//       不存 `const Texture*` —— 避免"部位持有指向另一个对象内部数据的指针"这类悬垂指针陷阱
//       （一旦 Scene 被移动/拷贝，指针就会失效；用枚举则怎么搬都安全）。
//
// 关联文档：tutorials/shaded-raster/README.md；src/include/gfx/render/shading.hpp、shadowmap.hpp
//
// 状态：新增（2026-10-10）。

#ifndef GFX_TUTORIAL_SHADED_RASTER_SCENE_HPP
#define GFX_TUTORIAL_SHADED_RASTER_SCENE_HPP

#include <gfx/core/canvas.hpp>
#include <gfx/core/color.hpp>
#include <gfx/math/vec.hpp>
#include <gfx/render/depth.hpp>
#include <gfx/render/lighting.hpp>
#include <gfx/render/normalmap.hpp>
#include <gfx/render/shading.hpp>
#include <gfx/render/shadowmap.hpp>
#include <gfx/render/texture.hpp>
#include <gfx/render/view.hpp>

#include <cmath>
#include <cstddef>
#include <vector>

namespace sr_demo {

using gfx::Canvas;
using gfx::Light;
using gfx::Material;
using gfx::ShadingTriangle;
using gfx::ShadowMap;
using gfx::TangentBasis;
using gfx::Texture;
using gfx::VertexAttributes;
using gfx::View;
using gfx::math::Vec2;
using gfx::math::Vec3;

inline constexpr float kPi = 3.14159265358979323846f;

// 程序化法线贴图（与 tutorials/normal-mapping 同一套生成器）
struct Maps {
    Texture brick;     // 砖缝
    Texture wave;      // 波纹
    Texture checker;   // 棋格
};

inline Maps makeMaps(int size, float strength) {
    Maps maps;
    maps.brick = gfx::makeNormalMap(size, [](float u, float v) {
        return gfx::brickHeight(u, v, /*rows=*/5, /*columns=*/4, /*mortarWidth=*/0.05f, /*amplitude=*/0.24f);
    }, strength);
    maps.wave = gfx::makeNormalMap(size, [](float u, float v) {
        return gfx::waveHeight(u, v, /*frequency=*/6.0f, /*amplitude=*/0.22f);
    }, strength);
    maps.checker = gfx::makeNormalMap(size, [](float u, float v) {
        return gfx::checkerHeight(u, v, /*cells=*/6, /*amplitude=*/0.22f);
    }, strength);
    return maps;
}

// 部件用哪张法线贴图（用枚举指认，不存指针 —— 见文件头的 @note）
enum class MapKind {
    None,
    Brick,
    Wave,
    Checker,
};

inline const Texture* mapTexture(const Maps& maps, MapKind kind) {
    switch (kind) {
        case MapKind::Brick:   return &maps.brick;
        case MapKind::Wave:    return &maps.wave;
        case MapKind::Checker: return &maps.checker;
        case MapKind::None:    break;
    }
    return nullptr;
}

// ============================ 部件与几何 ============================

// 每个"部件"= 一批三角形 + 材质 + 用哪张法线贴图
struct Part {
    std::vector<ShadingTriangle> triangles;
    Material material;
    MapKind normalMap = MapKind::None;
};

inline VertexAttributes attribute(const Vec3& p, const Vec3& n, const Vec3& t, const Vec2& uv) {
    VertexAttributes a;
    a.position = p;
    a.normal = n;
    a.tangent = t;
    a.uv = uv;
    return a;
}

// 一块四边形（两个三角形）：origin 为左下角，edgeU/edgeV 是两条边向量，uv 按 uvScale 平铺
inline void addQuad(Part& part, const Vec3& origin, const Vec3& edgeU, const Vec3& edgeV,
                    const Vec3& normal, float uvScale) {
    Vec3 tangent;
    if (!gfx::math::normalize(edgeU, tangent)) return;

    const Vec3 p00 = origin;
    const Vec3 p10 = origin + edgeU;
    const Vec3 p11 = origin + edgeU + edgeV;
    const Vec3 p01 = origin + edgeV;
    const Vec2 uv00(0.0f, 0.0f);
    const Vec2 uv10(uvScale, 0.0f);
    const Vec2 uv11(uvScale, uvScale);
    const Vec2 uv01(0.0f, uvScale);

    part.triangles.push_back(ShadingTriangle{attribute(p00, normal, tangent, uv00),
                                             attribute(p10, normal, tangent, uv10),
                                             attribute(p11, normal, tangent, uv11)});
    part.triangles.push_back(ShadingTriangle{attribute(p00, normal, tangent, uv00),
                                             attribute(p11, normal, tangent, uv11),
                                             attribute(p01, normal, tangent, uv01)});
}

// 立方体：六个面各铺一张贴图，每面自带法向与切线
inline Part makeCube(const Vec3& center, float half, const Material& material, MapKind map,
                     float uvScale) {
    Part part;
    part.material = material;
    part.normalMap = map;
    // 面法向 + 面内两条边（沿 +u / +v）
    const Vec3 normals[6] = {Vec3(0, 0, 1), Vec3(0, 0, -1), Vec3(1, 0, 0),
                             Vec3(-1, 0, 0), Vec3(0, 1, 0), Vec3(0, -1, 0)};
    for (int i = 0; i < 6; ++i) {
        const Vec3 n = normals[i];
        Vec3 edgeU;
        if (!gfx::math::normalize(gfx::math::cross(Vec3(0.0f, 1.0f, 0.0f), n), edgeU)) {
            edgeU = Vec3(1.0f, 0.0f, 0.0f);   // 上/下面退化时换参考轴
        }
        const Vec3 edgeV = gfx::math::cross(n, edgeU);
        const Vec3 origin = center + n * half - edgeU * half - edgeV * half;
        addQuad(part, origin, edgeU * (2.0f * half), edgeV * (2.0f * half), n, uvScale);
    }
    return part;
}

// 球：经纬网格；法向 = 径向，切线 = 经度方向（与 normalmap.hpp 的球面切空间口径一致）
inline Part makeSphere(const Vec3& center, float radius, int rings, int segments,
                       const Material& material, MapKind map, float uvScale) {
    Part part;
    part.material = material;
    part.normalMap = map;
    for (int r = 0; r < rings; ++r) {
        const float v0 = static_cast<float>(r) / static_cast<float>(rings);
        const float v1 = static_cast<float>(r + 1) / static_cast<float>(rings);
        for (int s = 0; s < segments; ++s) {
            const float u0 = static_cast<float>(s) / static_cast<float>(segments);
            const float u1 = static_cast<float>(s + 1) / static_cast<float>(segments);
            const auto pointAt = [&](float u, float v) {
                const float phi = 2.0f * kPi * u;
                const float theta = kPi * v;
                return Vec3(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
            };
            const Vec3 d00 = pointAt(u0, v0), d10 = pointAt(u1, v0);
            const Vec3 d11 = pointAt(u1, v1), d01 = pointAt(u0, v1);
            const auto tangentAt = [](float u) {
                const float phi = 2.0f * kPi * u;
                return Vec3(-std::sin(phi), 0.0f, std::cos(phi));
            };
            const Vec2 uv00(u0 * uvScale, v0 * uvScale), uv10(u1 * uvScale, v0 * uvScale);
            const Vec2 uv11(u1 * uvScale, v1 * uvScale), uv01(u0 * uvScale, v1 * uvScale);
            const Vec3 t0 = tangentAt(u0), t1 = tangentAt(u1);
            part.triangles.push_back(ShadingTriangle{
                attribute(center + d00 * radius, d00, t0, uv00),
                attribute(center + d10 * radius, d10, t1, uv10),
                attribute(center + d11 * radius, d11, t1, uv11)});
            part.triangles.push_back(ShadingTriangle{
                attribute(center + d00 * radius, d00, t0, uv00),
                attribute(center + d11 * radius, d11, t1, uv11),
                attribute(center + d01 * radius, d01, t0, uv01)});
        }
    }
    return part;
}

// 地面：一整块平面（法向 +y，切线 +x）
inline Part makeFloor(float halfSize, float y, const Material& material, MapKind map, float uvScale) {
    Part part;
    part.material = material;
    part.normalMap = map;
    addQuad(part, Vec3(-halfSize, y, -halfSize), Vec3(2.0f * halfSize, 0, 0), Vec3(0, 0, 2.0f * halfSize),
            Vec3(0, 1, 0), uvScale);
    return part;
}

inline Material makeMaterial(Vec3 diffuse, Vec3 specular, float shininess) {
    Material m;
    m.diffuse = diffuse;
    m.specular = specular;
    m.shininess = shininess;
    return m;
}

// ============================ 场景 ============================

struct Scene {
    Maps maps;                                // 自己拥有贴图（部件只用 MapKind 指认）
    std::vector<Part> parts;
    std::vector<Light> lights;
    std::vector<ShadingTriangle> triangles;   // 全部三角形（深度 pass 用；预先拼好，免得每帧重拼）
};

inline Scene makeScene(int mapSize, float strength) {
    Scene scene;
    scene.maps = makeMaps(mapSize, strength);

    scene.parts.push_back(makeFloor(6.0f, -1.0f,
                                    makeMaterial(Vec3(0.34f, 0.36f, 0.40f), Vec3(0.35f, 0.35f, 0.35f), 64.0f),
                                    MapKind::Checker, 6.0f));
    scene.parts.push_back(makeCube(Vec3(-1.5f, -0.35f, 0.0f), 0.65f,
                                   makeMaterial(Vec3(0.70f, 0.42f, 0.22f), Vec3(0.55f, 0.55f, 0.55f), 64.0f),
                                   MapKind::Brick, 1.5f));
    scene.parts.push_back(makeSphere(Vec3(1.6f, -0.35f, 0.1f), 0.7f, 28, 44,
                                     makeMaterial(Vec3(0.30f, 0.52f, 0.78f), Vec3(0.80f, 0.80f, 0.80f), 128.0f),
                                     MapKind::Wave, 1.5f));

    scene.lights.push_back(Light{Vec3(3.5f, 4.5f, 4.0f), Vec3(1.00f, 0.96f, 0.90f), 1.10f});
    scene.lights.push_back(Light{Vec3(-4.0f, 2.5f, 1.5f), Vec3(1.00f, 0.72f, 0.50f), 0.50f});

    for (const Part& part : scene.parts) {
        scene.triangles.insert(scene.triangles.end(), part.triangles.begin(), part.triangles.end());
    }
    return scene;
}

// ============================ 阴影贴图 ============================

// 光源视角与分辨率：一个光源一张，全部取**正交投影**覆盖整个场景（等效平行光近似）。
struct ShadowConfig {
    int size = 1024;                  // 贴图分辨率（正方形）
    float extent = 10.0f;             // 光源视角的正交半宽
    float nearZ = 0.1f;
    float farZ = 24.0f;
    float normalOffsetTexels = 2.0f;  // 沿法向抬起几个 texel（治自阴影；曲面上尤其关键）
};

// @note 只依赖"光源 + 场景"，与相机无关 ⇒ 相机移动时**不必重建**
inline std::vector<ShadowMap> buildShadowMaps(const Scene& scene, const ShadowConfig& cfg) {
    const float texelWorld = 2.0f * cfg.extent / static_cast<float>(cfg.size > 0 ? cfg.size : 1);

    std::vector<ShadowMap> maps;
    maps.reserve(scene.lights.size());
    for (const Light& light : scene.lights) {
        View lightView;
        lightView.setViewport(cfg.size, cfg.size);
        lightView.setOrthographic(-cfg.extent, cfg.extent, -cfg.extent, cfg.extent, cfg.nearZ, cfg.farZ);
        lightView.lookAt(light.position, Vec3(0.0f, -0.5f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));

        ShadowMap map;
        map.configure(lightView, cfg.size);
        map.setNormalOffset(cfg.normalOffsetTexels * texelWorld);
        map.build(scene.triangles);   // 深度 pass：只写深度，颜色丢弃
        maps.push_back(std::move(map));
    }
    return maps;
}

// ============================ 相机（窗口交互用）============================

// 与 tutorials/ray-tracing 的交互版**同一套操作手感**：yaw/pitch + 位置
struct CameraRig {
    Vec3 position{0.0f, 1.30f, 4.60f};
    float yaw = 0.0f;        // 绕 Y：0 ⇒ 看向 −z；+ ⇒ 向右转
    float pitch = -0.3186f;  // 俯仰（≈ −18.3°）：与离线 demo 的 lookAt 取景一致
    float fovDeg = 42.0f;    // 与离线 demo 的垂直视场角一致

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

    View makeView(int width, int height, float nearZ = 0.1f, float farZ = 100.0f) const {
        View view;
        view.setViewport(width, height);
        view.lookAt(position, position + forward(), Vec3(0.0f, 1.0f, 0.0f));
        view.setPerspective(fovDeg * kPi / 180.0f,
                            static_cast<float>(width) / static_cast<float>(height), nearZ, farZ);
        return view;
    }
};

// ============================ 渲染一遍 ============================

struct RenderOptions {
    bool normalMaps = true;   // 关掉 ⇒ 不做贴图采样与 TBN 扰动，直接用插值法向（A/B 对照）
    bool shadow = true;       // 关掉 ⇒ 阴影回调查表恒为"不遮挡"（A/B 对照）
};

// 把场景光栅化一遍（**不清屏**，背景由调用方决定）。
// @param eye 相机位置（世界空间，供逐像素算视线方向）
// @param shadowMaps 与 scene.lights 一一对应的阴影贴图；空 vector ⇒ 无条件受光
// @return 本遍的着色片元数
inline long long renderScene(Canvas& canvas, const View& view, const Vec3& eye, const Scene& scene,
                            const std::vector<ShadowMap>& shadowMaps, const RenderOptions& options) {
    gfx::DepthBuffer depth(canvas.width(), canvas.height());

    long long fragments = 0;
    for (const Part& part : scene.parts) {
        const Texture* map = mapTexture(scene.maps, part.normalMap);
        const Material& material = part.material;
        for (const ShadingTriangle& tri : part.triangles) {
            gfx::fillTriangleShaded(canvas, view, tri, [&](const gfx::Fragment& f) -> Vec3 {
                ++fragments;
                Vec3 normal = f.normal;
                if (map != nullptr && options.normalMaps && gfx::math::dot(f.tangent, f.tangent) > 0.0f) {
                    // 用插值后的法向/切线重建正交基（插值会让它们不再严格正交）
                    TangentBasis basis;
                    if (gfx::math::normalize(normal, basis.normal)) {
                        basis.tangent = gfx::orthogonalize(f.tangent, basis.normal, Vec3(1.0f, 0.0f, 0.0f));
                        basis.bitangent = gfx::math::cross(basis.normal, basis.tangent);
                        normal = gfx::perturbNormal(basis, gfx::decodeNormal(map->sample(f.uv.x, f.uv.y)));
                    }
                } else {
                    gfx::math::normalize(normal, normal);
                }

                Vec3 viewDir;
                if (!gfx::math::normalize(eye - f.position, viewDir)) viewDir = Vec3(0.0f, 0.0f, 1.0f);

                const Vec3 linear = gfx::shadeLocal(
                    f.position, normal, viewDir, material, scene.lights,
                    [&](const Vec3& p, const Vec3& dirToLight, float) {
                        if (!options.shadow) return false;
                        // 一张贴图只服务一个光源：按"到光源的方向"判断当前循环到哪个光源，
                        // 取它那张贴图做深度比较（用**着色法向**偏移，凹凸引起的自阴影也一并压掉）。
                        int best = -1;
                        float bestDot = -2.0f;
                        for (std::size_t i = 0; i < shadowMaps.size(); ++i) {
                            Vec3 dir;
                            if (!gfx::math::normalize(scene.lights[i].position - p, dir)) continue;
                            const float dp = gfx::math::dot(dir, dirToLight);
                            if (dp > bestDot) { bestDot = dp; best = static_cast<int>(i); }
                        }
                        return best >= 0 && shadowMaps[static_cast<std::size_t>(best)].isShadowed(p, normal);
                    });

                // 输出前做伽马（shading.hpp 负责 0..1 → 8bit 量化，不做色彩空间变换）
                return Vec3(std::pow(std::max(0.0f, linear.x), 1.0f / 2.2f),
                            std::pow(std::max(0.0f, linear.y), 1.0f / 2.2f),
                            std::pow(std::max(0.0f, linear.z), 1.0f / 2.2f));
            }, &depth);
        }
    }
    return fragments;
}

} // namespace sr_demo

#endif // GFX_TUTORIAL_SHADED_RASTER_SCENE_HPP
