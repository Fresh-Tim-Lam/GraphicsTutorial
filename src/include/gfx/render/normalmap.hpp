// normalmap.hpp —— 法线贴图：切空间基（TBN）+ 由高度场生成法线图 + 扰动法向
//
// 定位：这是**与渲染路径无关**的公共件（既给光线跟踪用，也能给将来的光栅化着色管线用）。
//       纹理本身与采样见 gfx/render/texture.hpp。
//
// 三件事：
//   1) TangentBasis：切空间基（切线 T / 副切线 B / 法向 N），并提供球面、平面的**解析**切空间；
//   2) decodeNormal / perturbNormal：把贴图里的 [0,1] 颜色解码成 [-1,1] 的切线空间法向，
//      再变换到世界空间；
//   3) makeNormalMap：给一个高度函数 h(u,v) 就能生成法线贴图（内置棋格 / 砖缝 / 波纹三个高度场）。
//
// 用法（配合光线跟踪）：
//     TangentBasis tb = sphereTangentBasis(hit.point, sphereCenter);
//     math::Vec3 nTangent = decodeNormal(normalMap.sample(u, v));
//     hit.shadingNormal   = perturbNormal(tb, nTangent);   // 只参与光照，不参与光线数学
//
// 约定：切线空间为 OpenGL 风格 —— +X = 切线 T（沿 +u 增大方向）、+Y = 副切线 B、
//       +Z = 几何法向 N；贴图里以 RGB 存 (n*0.5 + 0.5)，故解码时 n = rgb*2 - 1。
//       切空间必须**右手系**（B = N × T），否则凹凸会左右/上下镜像。
//
// 关联文档：docs/design/adr-0013-normal-mapping.md；gfx/render/texture.hpp；gfx/render/lighting.hpp
//
// 状态：新增（2026-10-09）。法线贴图沉淀为库能力。

#ifndef GFX_NORMALMAP_HPP
#define GFX_NORMALMAP_HPP

#include "gfx/math/math.hpp"
#include "gfx/render/texture.hpp"

#include <cmath>

namespace gfx {

// 切空间基（三个单位向量，应两两正交且右手：B = N × T）
struct TangentBasis {
    math::Vec3 tangent;
    math::Vec3 bitangent;
    math::Vec3 normal;
};

// 把 [0,1] 的贴图颜色解码成单位切线空间法向：n = 2*c - 1
inline math::Vec3 decodeNormal(const math::Vec3& texel) {
    math::Vec3 n = texel * 2.0f - math::Vec3(1.0f, 1.0f, 1.0f);
    math::Vec3 unit;
    if (!math::normalize(n, unit)) return math::Vec3(0.0f, 0.0f, 1.0f);   // 退化 ⇒ 平整
    return unit;
}

// 切线空间法向 → 世界空间：n_world = T*n.x + B*n.y + N*n.z（TBN 为正交单位基）
inline math::Vec3 perturbNormal(const TangentBasis& basis, const math::Vec3& nTangent) {
    math::Vec3 n = basis.tangent * nTangent.x + basis.bitangent * nTangent.y + basis.normal * nTangent.z;
    math::Vec3 unit;
    if (!math::normalize(n, unit)) return basis.normal;
    return unit;
}

// 内部：把 t 正交化到与 n 垂直并归一；退化时返回 fallback
inline math::Vec3 orthogonalize(const math::Vec3& t, const math::Vec3& n, const math::Vec3& fallback) {
    math::Vec3 value = t - n * math::dot(t, n);
    if (!math::normalize(value, value)) return fallback;
    return value;
}

// 球面的解析切空间：按经纬度参数化（u 随经度增长、v 随纬度增长）。
// @param point  球面上的点
// @param center 球心
// @note 两极处切线退化，退回任意一组稳定基（只影响极区一小片）。
inline TangentBasis sphereTangentBasis(const math::Vec3& point, const math::Vec3& center) {
    const math::Vec3 d = point - center;
    math::Vec3 n;
    if (!math::normalize(d, n)) n = math::Vec3(0.0f, 1.0f, 0.0f);

    // 经度 φ = atan2(z, x)：沿 +u 的切向为 d/dφ (-sinφ, 0, cosφ)
    const float phi = std::atan2(d.z, d.x);
    const math::Vec3 t(-std::sin(phi), 0.0f, std::cos(phi));

    TangentBasis basis;
    basis.normal = n;
    basis.tangent = orthogonalize(t, n, orthogonalize(math::Vec3(1.0f, 0.0f, 0.0f), n,
                                                      math::Vec3(0.0f, 0.0f, 1.0f)));
    basis.bitangent = math::cross(n, basis.tangent);   // 右手系
    return basis;
}

// 平面的解析切空间：给定法向，任取一组与它正交的切向量。
// @note 参考轴选 y 轴（法向接近 y 时改选 x 轴），保证不退化。
inline TangentBasis planeTangentBasis(const math::Vec3& normal) {
    math::Vec3 n;
    if (!math::normalize(normal, n)) n = math::Vec3(0.0f, 1.0f, 0.0f);

    const math::Vec3 reference = (std::fabs(n.y) < 0.9f) ? math::Vec3(0.0f, 1.0f, 0.0f)
                                                        : math::Vec3(1.0f, 0.0f, 0.0f);
    math::Vec3 t;
    if (!math::normalize(math::cross(reference, n), t)) t = math::Vec3(1.0f, 0.0f, 0.0f);

    TangentBasis basis;
    basis.normal = n;
    basis.tangent = t;
    basis.bitangent = math::cross(n, t);
    return basis;
}

// ---- 程序化法线贴图 ----

// 内部：uv 的小数部分（保证图案在 [0,1] 边界上无缝平铺）
inline float fractPart(float x) { return x - std::floor(x); }

// 棋格 / 凹凸格：两方向正弦相乘 ⇒ 光滑的"蛋托"起伏（周期 = 1/cells）
inline float checkerHeight(float u, float v, int cells = 6, float amplitude = 0.03f) {
    const float a = 2.0f * 3.14159265358979323846f * static_cast<float>(cells);
    return amplitude * std::sin(a * u) * std::sin(a * v);
}

// 波纹：沿 u 方向的单向正弦（周期 = 1/frequency）
inline float waveHeight(float u, float v, float frequency = 6.0f, float amplitude = 0.03f) {
    (void)v;
    return amplitude * std::sin(2.0f * 3.14159265358979323846f * frequency * u);
}

// 砖缝：交错排列的砖块，砖缝处下沉（带一条有限宽度的斜边 ⇒ 法线有明确朝向）
// @param rows        纵向砖块数（必须是整数才能无缝平铺）
// @param columns     横向砖块数
// @param mortarWidth 砖缝宽度（uv 单位）
// @param amplitude   砖缝下沉深度
inline float brickHeight(float u, float v, int rows = 6, int columns = 3,
                         float mortarWidth = 0.06f, float amplitude = 0.05f) {
    const float fv = fractPart(v * static_cast<float>(rows));
    const float rowIndex = std::floor(v * static_cast<float>(rows));
    const float odd = fractPart(rowIndex * 0.5f) * 2.0f;          // 奇数行错开半块
    const float fu = fractPart(u * static_cast<float>(columns) + odd * 0.5f);

    const float distance = std::min(std::min(fu, 1.0f - fu), std::min(fv, 1.0f - fv));
    const float edge = std::min(distance / mortarWidth, 1.0f);     // 0 = 缝底，1 = 砖面
    return amplitude * edge;
}

// 由高度场生成法线贴图。
//   切线空间法向 ∝ ( -∂h/∂u * strength, -∂h/∂v * strength, 1 )
//   中心差分求梯度（uv 环绕取样 ⇒ 生成结果**可无缝平铺**），再编码成 (n*0.5 + 0.5) 存入 RGB。
// @param size    贴图边长（正方形）
// @param height  高度函数 h(u, v)，参数为归一化 uv。**应是 uv 的周期函数** —— 中心差分在 uv 边界处
//                也是环绕取样，非周期函数会在贴图接缝上留下一条法线突变。
// @param strength "高度 → 斜率"的放大倍数：越大凹凸越明显，0 ⇒ 全平整（等价于不用贴图）
template <class HeightFn>
inline Texture makeNormalMap(int size, HeightFn&& height, float strength = 1.0f) {
    Texture texture(size, size, math::Vec3(0.5f, 0.5f, 1.0f));   // 默认 = 平整法向 (0,0,1)
    if (size <= 0) return texture;

    const float inv = 1.0f / static_cast<float>(size);
    const float step = inv;   // 差分步长取一个像素

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) * inv;
            const float v = (static_cast<float>(y) + 0.5f) * inv;

            const float dhdu = (height(fractPart(u + step) , v) - height(fractPart(u - step + 1.0f), v)) / (2.0f * step);
            const float dhdv = (height(u, fractPart(v + step)) - height(u, fractPart(v - step + 1.0f))) / (2.0f * step);

            math::Vec3 n(-dhdu * strength, -dhdv * strength, 1.0f);
            math::Vec3 unit;
            if (!math::normalize(n, unit)) unit = math::Vec3(0.0f, 0.0f, 1.0f);
            texture.setTexel(x, y, unit * 0.5f + math::Vec3(0.5f, 0.5f, 0.5f));
        }
    }
    return texture;
}

} // namespace gfx

#endif // GFX_NORMALMAP_HPP
