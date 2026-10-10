// shaded_raster_scene.hpp —— 光栅化着色管线演示的**共享场景**（离线出图版与窗口交互版共用）
//
// 用途：把"部件/材质/程序化贴图 + 光源 + 阴影贴图 + 渲染一遍 + 相机操作"集中在这里，
//       让 shaded_raster.cpp（渲一张 PNG）与 shaded_raster_interactive.cpp（窗口内实时调相机）
//       跑的是**同一个场景、同一份着色代码** —— 与 ray-tracing 目录里 ray_tracing_scene.hpp 的分工一致。
//
// 关键性质（交互版靠它做到零成本实时）：
//   阴影贴图只依赖"光源 + 场景"，**与相机无关** ⇒ 相机移动时不必重建，启动时建一次即可。
//
// 场景：一间"开着顶的展厅" —— 瓷砖地面 + 四面**不同颜色**的砖墙（交错砖缝）+ 碳纤维立方体
//       + 大理石球。四套外观全部由 `gfx/render/pattern.hpp` 程序化生成（颜色贴图 + 法线贴图成对）。
//
// @note 贴图归属：`Scene` 自己拥有 `Surfaces`（四套 PatternMaps），部件只用 `SurfaceKind` **枚举**指认，
//       不存 `const Texture*` —— 避免"部位持有指向另一个对象内部数据的指针"这类悬垂指针陷阱
//       （一旦 Scene 被移动/拷贝，指针就会失效；用枚举则怎么搬都安全）。
// @note 颜色怎么落地：颜色贴图**乘**到材质的 diffuse 上（`Material::diffuse ⊙ albedo`），
//       于是"图案给出明暗结构、材质给出色调" —— 同一张砖纹换个 tint 就是另一种颜色的墙。
//
// 关联文档：tutorials/shaded-raster/README.md；gfx/render/pattern.hpp、shading.hpp、shadowmap.hpp
//
// 状态：新增（2026-10-10；2026-10-10 改为"展厅"场景并接入颜色贴图）。

#ifndef GFX_TUTORIAL_SHADED_RASTER_SCENE_HPP
#define GFX_TUTORIAL_SHADED_RASTER_SCENE_HPP

#include <gfx/core/canvas.hpp>
#include <gfx/core/color.hpp>
#include <gfx/math/vec.hpp>
#include <gfx/render/depth.hpp>
#include <gfx/render/lighting.hpp>
#include <gfx/render/pattern.hpp>
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
using gfx::PatternMaps;
using gfx::ShadingTriangle;
using gfx::ShadowMap;
using gfx::TangentBasis;
using gfx::Texture;
using gfx::VertexAttributes;
using gfx::View;
using gfx::math::Vec2;
using gfx::math::Vec3;

inline constexpr float kPi = 3.14159265358979323846f;

// 部件用哪套外观（用枚举指认，不存指针 —— 见文件头的 @note）
enum class SurfaceKind {
    None,
    Marble,   // 大理石（噪声脉络）
    Tile,     // 瓷砖（对齐方格 + 宽缝隙）
    Brick,    // 砖墙（交错砖缝）
    Weave,    // 编织 / 碳纤维（3/1 斜纹）
};

// 四套外观贴图（颜色 + 法线），由 Scene 拥有
struct Surfaces {
    PatternMaps marble;
    PatternMaps tile;
    PatternMaps brick;
    PatternMaps weave;
};

inline const PatternMaps* surfaceMaps(const Surfaces& surfaces, SurfaceKind kind) {
    switch (kind) {
        case SurfaceKind::Marble: return &surfaces.marble;
        case SurfaceKind::Tile:   return &surfaces.tile;
        case SurfaceKind::Brick:  return &surfaces.brick;
        case SurfaceKind::Weave:  return &surfaces.weave;
        case SurfaceKind::None:   break;
    }
    return nullptr;
}

// 四套外观的图案定义（集中在此，改一处就两个 demo 都变）
inline Surfaces makeSurfaces(int mapSize, float normalStrength) {
    Surfaces surfaces;
    surfaces.marble = gfx::makePatternMaps(mapSize, [](float u, float v) {
        return gfx::marblePattern(u, v, /*cells=*/4, /*stripes=*/5, /*turbulenceStrength=*/1.7f, /*octaves=*/4);
    }, normalStrength);
    surfaces.tile = gfx::makePatternMaps(mapSize, [](float u, float v) {
        // 6×8 格 = 1.2×1.2 世界单位（地面 7.2×9.6）⇒ 近似正方形的大瓷砖
        return gfx::tilePattern(u, v, /*columns=*/6, /*rows=*/8, /*gapWidth=*/0.20f, /*offsetRows=*/false,
                                Vec3(0.84f, 0.83f, 0.80f), Vec3(0.13f, 0.13f, 0.15f));
    }, normalStrength);
    surfaces.brick = gfx::makePatternMaps(mapSize, [](float u, float v) {
        return gfx::brickPattern(u, v, /*columns=*/12, /*rows=*/8, /*mortarWidth=*/0.16f);
    }, normalStrength);
    surfaces.weave = gfx::makePatternMaps(mapSize, [](float u, float v) {
        return gfx::weavePattern(u, v, /*threads=*/8.0f, /*twill=*/true);
    }, normalStrength);
    return surfaces;
}

// ============================ 部件与几何 ============================

// 每个"部件"= 一批三角形 + 材质 + 用哪套外观
struct Part {
    std::vector<ShadingTriangle> triangles;
    Material material;
    SurfaceKind surface = SurfaceKind::None;
};

inline VertexAttributes attribute(const Vec3& p, const Vec3& n, const Vec3& t, const Vec2& uv) {
    VertexAttributes a;
    a.position = p;
    a.normal = n;
    a.tangent = t;
    a.uv = uv;
    return a;
}

// 一块四边形（两个三角形）：origin 为左下角，edgeU/edgeV 是两条边向量，uv 按 uvScaleU/uvScaleV 平铺
// @note 两个方向分开给缩放：墙面（宽 9.6 / 窄 7.2）用同一套砖纹时，只有分开给才能让砖块尺寸一致
inline void addQuad(Part& part, const Vec3& origin, const Vec3& edgeU, const Vec3& edgeV,
                    const Vec3& normal, float uvScaleU, float uvScaleV) {
    Vec3 tangent;
    if (!gfx::math::normalize(edgeU, tangent)) return;

    const Vec3 p00 = origin;
    const Vec3 p10 = origin + edgeU;
    const Vec3 p11 = origin + edgeU + edgeV;
    const Vec3 p01 = origin + edgeV;
    const Vec2 uv00(0.0f, 0.0f);
    const Vec2 uv10(uvScaleU, 0.0f);
    const Vec2 uv11(uvScaleU, uvScaleV);
    const Vec2 uv01(0.0f, uvScaleV);

    part.triangles.push_back(ShadingTriangle{attribute(p00, normal, tangent, uv00),
                                             attribute(p10, normal, tangent, uv10),
                                             attribute(p11, normal, tangent, uv11)});
    part.triangles.push_back(ShadingTriangle{attribute(p00, normal, tangent, uv00),
                                             attribute(p11, normal, tangent, uv11),
                                             attribute(p01, normal, tangent, uv01)});
}

// 立方体：六个面各铺一套外观，每面自带法向与切线
inline Part makeCube(const Vec3& center, float half, const Material& material, SurfaceKind surface,
                     float uvScale) {
    Part part;
    part.material = material;
    part.surface = surface;
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
        addQuad(part, origin, edgeU * (2.0f * half), edgeV * (2.0f * half), n, uvScale, uvScale);
    }
    return part;
}

// 球：经纬网格；法向 = 径向，切线 = 经度方向（与 normalmap.hpp 的球面切空间口径一致）
inline Part makeSphere(const Vec3& center, float radius, int rings, int segments,
                       const Material& material, SurfaceKind surface, float uvScale) {
    Part part;
    part.material = material;
    part.surface = surface;
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
inline Part makeFloor(float halfX, float halfZ, float y, const Material& material, SurfaceKind surface,
                      float uvScaleU, float uvScaleV) {
    Part part;
    part.material = material;
    part.surface = surface;
    addQuad(part, Vec3(-halfX, y, -halfZ), Vec3(2.0f * halfX, 0, 0), Vec3(0, 0, 2.0f * halfZ),
            Vec3(0, 1, 0), uvScaleU, uvScaleV);
    return part;
}

// 一面墙：bottomLeft 为左下角，along 为水平方向（长度 = 墙宽），height 为墙高，normal 朝**房间内**。
// @note v 轴固定取世界竖直向上 ⇒ 砖块自然层层向上叠
inline Part makeWall(const Vec3& bottomLeft, const Vec3& along, float height, const Vec3& normal,
                     const Material& material, SurfaceKind surface, float uvScaleU, float uvScaleV) {
    Part part;
    part.material = material;
    part.surface = surface;
    addQuad(part, bottomLeft, along, Vec3(0.0f, height, 0.0f), normal, uvScaleU, uvScaleV);
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

// 展厅尺寸（正方体房间，开顶）
constexpr float kRoomHalfX = 3.6f;    // 左右墙在 x = ±3.6
constexpr float kRoomHalfZ = 4.8f;    // 前后墙在 z = ±4.8
constexpr float kFloorY = -1.0f;      // 地面高度
constexpr float kWallHeight = 3.4f;   // 墙高（墙顶在 y = kFloorY + kWallHeight）

struct Scene {
    Surfaces surfaces;                        // 自己拥有贴图（部件只用 SurfaceKind 指认）
    std::vector<Part> parts;
    std::vector<Light> lights;
    std::vector<ShadingTriangle> triangles;   // 全部三角形（深度 pass 用；预先拼好，免得每帧重拼）
};

inline Scene makeScene(int mapSize, float normalStrength) {
    Scene scene;
    scene.surfaces = makeSurfaces(mapSize, normalStrength);

    // ---- 地面：偏冷的瓷砖（颜色贴图乘上去 ⇒ 明亮的砖面 + 深色缝，对比清楚）----
    scene.parts.push_back(makeFloor(kRoomHalfX, kRoomHalfZ, kFloorY,
                                    makeMaterial(Vec3(0.78f, 0.82f, 0.90f), Vec3(0.45f, 0.45f, 0.45f), 96.0f),
                                    SurfaceKind::Tile, 1.0f, 1.0f));

    // ---- 四面墙：同一套砖纹，靠材质 tint 变成四种颜色（每面一眼可区分）----
    const float widthX = 2.0f * kRoomHalfX;   // 7.2（前后墙的宽）
    const float widthZ = 2.0f * kRoomHalfZ;   // 9.6（左右墙的宽）
    // 砖纹每 uv 单位 12×8 块 ⇒ 左右墙 uvScaleU = 1 得 0.8 宽的砖；前后墙窄 25%，取 0.75 保持同尺寸
    const float wallTileU = 1.0f;
    const float wallTileNarrowU = widthX / widthZ;
    const float wallTileV = 1.0f;

    struct WallSpec {
        Vec3 corner;      // 左下角
        Vec3 along;       // 水平方向
        Vec3 normal;      // 朝房间内
        Vec3 tint;        // 材质色调
        float uvScaleU;
    };
    const WallSpec walls[4] = {
        // 远墙（z = −4.8，法向 +z）：暖陶土色
        {Vec3(kRoomHalfX, kFloorY, -kRoomHalfZ), Vec3(-widthX, 0.0f, 0.0f), Vec3(0, 0, 1),
         Vec3(1.00f, 0.62f, 0.46f), wallTileNarrowU},
        // 左墙（x = −3.6，法向 +x）：青绿
        {Vec3(-kRoomHalfX, kFloorY, -kRoomHalfZ), Vec3(0.0f, 0.0f, widthZ), Vec3(1, 0, 0),
         Vec3(0.46f, 0.86f, 0.80f), wallTileU},
        // 右墙（x = +3.6，法向 −x）：紫罗兰
        {Vec3(kRoomHalfX, kFloorY, kRoomHalfZ), Vec3(0.0f, 0.0f, -widthZ), Vec3(-1, 0, 0),
         Vec3(0.72f, 0.58f, 1.00f), wallTileU},
        // 近墙（z = +4.8，法向 −z，在相机身后）：琥珀
        {Vec3(-kRoomHalfX, kFloorY, kRoomHalfZ), Vec3(widthX, 0.0f, 0.0f), Vec3(0, 0, -1),
         Vec3(0.98f, 0.84f, 0.48f), wallTileNarrowU},
    };
    for (const WallSpec& wall : walls) {
        scene.parts.push_back(makeWall(wall.corner, wall.along, kWallHeight, wall.normal,
                                       makeMaterial(wall.tint, Vec3(0.22f, 0.22f, 0.22f), 24.0f),
                                       SurfaceKind::Brick, wall.uvScaleU, wallTileV));
    }

    // ---- 立方体：碳纤维斜纹（uvScale 1.5 ⇒ 每面约 18 根丝带）----
    scene.parts.push_back(makeCube(Vec3(-1.5f, -0.35f, 0.0f), 0.65f,
                                   makeMaterial(Vec3(0.86f, 0.90f, 1.00f), Vec3(0.85f, 0.85f, 0.85f), 96.0f),
                                   SurfaceKind::Weave, 1.5f));

    // ---- 球：大理石（uvScale 1 ⇒ uv 整个 [0,1]，配合周期图案 ⇒ 球面无接缝）----
    scene.parts.push_back(makeSphere(Vec3(1.6f, -0.35f, 0.1f), 0.7f, 28, 44,
                                     makeMaterial(Vec3(0.80f, 0.78f, 0.74f), Vec3(0.90f, 0.90f, 0.90f), 160.0f),
                                     SurfaceKind::Marble, 1.0f));

    // ---- 光源：两盏都放在**开口上方**、不同水平方向（屋顶天窗式的照明）----
    // @note 阴影贴图用正交投影 = 把点光源当平行光近似，**离得越远越准**。若把灯放进屋里、
    //       贴到墙角，这个近似会让"墙挡光"的范围被放大得离谱（实测能吃掉大半个地面）。
    //       抬高到开口外 + 两根不同方向的灯互相补光 ⇒ 每面墙的影子都被另一盏灯填掉。
    scene.lights.push_back(Light{Vec3(2.6f, 8.0f, 3.0f), Vec3(1.00f, 0.96f, 0.90f), 1.15f});
    scene.lights.push_back(Light{Vec3(-3.0f, 6.0f, -2.6f), Vec3(1.00f, 0.82f, 0.62f), 0.55f});

    for (const Part& part : scene.parts) {
        scene.triangles.insert(scene.triangles.end(), part.triangles.begin(), part.triangles.end());
    }
    return scene;
}

// ============================ 阴影贴图 ============================

// 光源视角与分辨率：一个光源一张，全部取**正交投影**覆盖整个场景（等效平行光近似）。
struct ShadowConfig {
    int size = 1024;                  // 贴图分辨率（正方形）
    float extent = 8.0f;              // 光源视角的正交半宽
    float nearZ = 0.1f;
    float farZ = 18.0f;
    // 沿法向抬起几个 texel。@note 这个场景里**抬得越多反而越糟**：抬起会让采样点偏离自己的面，
    //       在"墙的轮廓 / 开口边缘"处与邻域存值产生更大的错配 ⇒ 假暗点。实测 1/2/3 texel 的椒盐像素
    //       为 149 / 622 / 1964，故取 1（曲面的自阴影由 ShadowMap 的斜率缩放容差兜住）。
    float normalOffsetTexels = 1.0f;
};

// @note 只依赖"光源 + 场景"，与相机无关 ⇒ 相机移动时**不必重建**
// @note extent = 8 的来由：实测两面墙的最高角在光源视角下 |x| ≤ 5.9、|y| ≤ 6.4、深度 ∈ [1.2, 11.6]，
//       取 8 / [0.1, 18] 留余量 —— 比早先的 ±10 更紧，texel 更小 ⇒ 阴影更锐。
inline std::vector<ShadowMap> buildShadowMaps(const Scene& scene, const ShadowConfig& cfg) {
    const float texelWorld = 2.0f * cfg.extent / static_cast<float>(cfg.size > 0 ? cfg.size : 1);

    std::vector<ShadowMap> maps;
    maps.reserve(scene.lights.size());
    for (const Light& light : scene.lights) {
        View lightView;
        lightView.setViewport(cfg.size, cfg.size);
        lightView.setOrthographic(-cfg.extent, cfg.extent, -cfg.extent, cfg.extent, cfg.nearZ, cfg.farZ);
        lightView.lookAt(light.position, Vec3(0.0f, -0.4f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));

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
    Vec3 position{0.0f, 1.45f, 4.05f};   // 站在屋里、靠近后墙
    float yaw = 0.0f;                    // 绕 Y：0 ⇒ 看向 −z；+ ⇒ 向右转
    float pitch = -0.3410f;              // 俯仰（≈ −19.5°）
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
    bool albedo = true;       // 关掉 ⇒ 不采样颜色贴图，diffuse 用材质的常量色（A/B 对照）
    bool shadow = true;       // 关掉 ⇒ 阴影回调查表恒为"不遮挡"（A/B 对照）
};

// 环境光系数（shadeLocal 的 ambient）。
// @note 比 shadeLocal 的默认值（0.05）高：这是一间有墙的房间，四面墙对两盏顶光都是**掠射**
//       （N·L 很小），只靠直接光墙面会一片死黑 —— 抬一点环境光，墙的砖纹与颜色才看得清。
inline constexpr float kAmbient = 0.14f;

// 把场景光栅化一遍（**不清屏**，背景由调用方决定）。
// @param eye 相机位置（世界空间，供逐像素算视线方向）
// @param shadowMaps 与 scene.lights 一一对应的阴影贴图；空 vector ⇒ 无条件受光
// @return 本遍的着色片元数
inline long long renderScene(Canvas& canvas, const View& view, const Vec3& eye, const Scene& scene,
                            const std::vector<ShadowMap>& shadowMaps, const RenderOptions& options) {
    gfx::DepthBuffer depth(canvas.width(), canvas.height());

    long long fragments = 0;
    for (const Part& part : scene.parts) {
        const PatternMaps* maps = surfaceMaps(scene.surfaces, part.surface);
        const Material& baseMaterial = part.material;
        for (const ShadingTriangle& tri : part.triangles) {
            gfx::fillTriangleShaded(canvas, view, tri, [&](const gfx::Fragment& f) -> Vec3 {
                ++fragments;

                // 颜色贴图：乘到材质的 diffuse 上（"图案给结构、材质给色调"）
                Material material = baseMaterial;
                if (maps != nullptr && options.albedo) {
                    const Vec3 c = maps->albedo.sample(f.uv.x, f.uv.y);
                    material.diffuse = Vec3(material.diffuse.x * c.x,
                                            material.diffuse.y * c.y,
                                            material.diffuse.z * c.z);
                }

                Vec3 normal = f.normal;
                if (maps != nullptr && options.normalMaps && gfx::math::dot(f.tangent, f.tangent) > 0.0f) {
                    // 用插值后的法向/切线重建正交基（插值会让它们不再严格正交）
                    TangentBasis basis;
                    if (gfx::math::normalize(normal, basis.normal)) {
                        basis.tangent = gfx::orthogonalize(f.tangent, basis.normal, Vec3(1.0f, 0.0f, 0.0f));
                        basis.bitangent = gfx::math::cross(basis.normal, basis.tangent);
                        normal = gfx::perturbNormal(basis, gfx::decodeNormal(maps->normal.sample(f.uv.x, f.uv.y)));
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
                    },
                    kAmbient);

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
