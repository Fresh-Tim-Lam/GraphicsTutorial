// pattern.hpp —— gfx 渲染层：程序化图案（颜色 + 高度）⇒ 一对"颜色贴图 + 法线贴图"
//
// @file 在 `normalmap.hpp`（高度场 → 法线贴图）之上补两件事：
//       1) **颜色贴图**：`makeColorTexture` 把一个逐 texel 的颜色函数烘成 `Texture`；
//       2) **若干图案**：大理石 / 瓷砖 / 砖墙 / 编织（碳纤维），每个图案同时给出
//          `color`（基色）与 `height`（起伏），于是 `makePatternMaps` 一次生成配对的两张图。
//
// 为什么需要噪声：纯正弦/方格"太完美"，看起来像机器画的花纹。真实材料（大理石纹、木纹、
//       云、织物）都是**相干噪声**的多倍频叠加。这里用最经典的三个基元：
//         - `valueNoise`：格点哈希 + 五次淡入曲线 + 双线性插值（实现最短、够用）；
//         - `fbm`：多个倍频叠加（`Σ gain^i · noise(lacunarity^i · x)`，默认 lacunarity 2 / gain 0.5）；
//         - `turbulence`：把噪声取绝对值再叠加（`Σ gain^i · |2·noise−1|`）—— 大理石脉络就靠它。
//       三者都支持 `period` 参数：格点索引按 period 环绕 ⇒ 噪声在 u,v 上**可无缝平铺**
//       （`makeNormalMap` 的中心差分是环绕取样的，图案不平铺就会在接缝处留下一条法线突变）。
//
// 与光照解耦：本文件只产出 `Texture`，不认识材质与光源。怎么用（乘到 diffuse 上、还是只取高度做凹凸）
//       由调用方决定；"用哪个图案 + 什么颜色"也不是本层的职责。
//
// 用途：给光栅化/光线跟踪两条渲染路径提供"无需外部图片"的材质外观。
// 目标读者：gfx 渲染层使用者与教学读者（#include <gfx/render/pattern.hpp>）。
// 分层语义：render 层，只依赖 math/ 与 core/（+ 复用 normalmap.hpp 的 makeNormalMap）。
// 关联文档：docs/design/adr-0016-procedural-patterns.md；gfx/render/texture.hpp；gfx/render/normalmap.hpp
//
// 参考做法（噪声/fBm/大理石脉络的经典配方）：
//   The Book of Shaders — Fractal Brownian Motion（fBm 的倍频叠加）
//   MySimulator — Procedural Textures in GLSL（marble = 0.5 + 0.5·sin(x·stripes + turbulence·k)）
//
// 状态：新增（2026-10-10）。

#ifndef GFX_PATTERN_HPP
#define GFX_PATTERN_HPP

#include "gfx/math/vec.hpp"
#include "gfx/render/normalmap.hpp"
#include "gfx/render/texture.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gfx {

namespace detail {

// 圆周率（仅本模块内部使用；不用 M_PI 以避开平台/宏差异）
inline constexpr float PATTERN_PI = 3.14159265358979323846f;

inline float patternLerp(float a, float b, float t) { return a + (b - a) * t; }

} // namespace detail

// ============================ 噪声基元 ============================

// 二维整数格点哈希 → [0, 1)。
// @note 全整数位运算（xorshift 混合），不依赖 sin() 的浮点混沌 —— 大坐标下不会退化，
//       且**跨平台逐位一致**（浮点 sin 在不同实现下可能不同）。
inline float hash2(int x, int y, unsigned seed = 0u) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u
                    + static_cast<std::uint32_t>(y) * 668265263u
                    + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

// 值噪声：格点随机值 + 五次淡入曲线（f*f*f*(f*(f*6-15)+10)，一阶与二阶导在格点处均为 0）+ 双线性插值。
// @param period > 0 时格点索引按 period 环绕 ⇒ 噪声以 period 为周期，可用于**无缝平铺**（0 = 不环绕）
inline float valueNoise(float x, float y, unsigned seed = 0u, int period = 0) {
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    int ix = static_cast<int>(fx);
    int iy = static_cast<int>(fy);
    const float tx = x - fx;
    const float ty = y - fy;
    if (period > 0) {
        ix = ((ix % period) + period) % period;
        iy = ((iy % period) + period) % period;
    }
    const int ix1 = (period > 0) ? ((ix + 1) % period) : (ix + 1);
    const int iy1 = (period > 0) ? ((iy + 1) % period) : (iy + 1);

    const float ux = tx * tx * tx * (tx * (tx * 6.0f - 15.0f) + 10.0f);
    const float uy = ty * ty * ty * (ty * (ty * 6.0f - 15.0f) + 10.0f);

    const float a = hash2(ix, iy, seed);
    const float b = hash2(ix1, iy, seed);
    const float c = hash2(ix, iy1, seed);
    const float d = hash2(ix1, iy1, seed);
    const float top = detail::patternLerp(a, b, ux);
    const float bottom = detail::patternLerp(c, d, ux);
    return detail::patternLerp(top, bottom, uy);
}

// 分形布朗运动（fBm）：倍频叠加，频率 ×lacunarity、幅度 ×gain。
// @return 大致落在 [0,1]（各倍频幅度归一化后）
// @param period > 0 时可无缝平铺：第 i 个倍频的周期取 period·lacunarity^i（故 lacunarity 应取整数）
inline float fbm(float x, float y, int octaves = 4, float lacunarity = 2.0f,
                 float gain = 0.5f, unsigned seed = 0u, int period = 0) {
    if (octaves < 1) octaves = 1;
    float sum = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float norm = 0.0f;
    int cellPeriod = period;
    for (int i = 0; i < octaves; ++i) {
        const int p = (period > 0) ? cellPeriod : 0;
        sum += amplitude * valueNoise(x * frequency, y * frequency, seed + static_cast<unsigned>(i), p);
        norm += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
        if (period > 0) cellPeriod *= static_cast<int>(lacunarity + 0.5f);
    }
    return (norm > 0.0f) ? (sum / norm) : 0.0f;
}

// 湍流：|2·noise−1| 的倍频叠加 —— 噪声的"脊线"被强调，适合做脉络/褶皱/大理石纹。
inline float turbulence(float x, float y, int octaves = 4, float gain = 0.5f,
                        unsigned seed = 0u, int period = 0) {
    if (octaves < 1) octaves = 1;
    float sum = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float norm = 0.0f;
    int cellPeriod = period;
    for (int i = 0; i < octaves; ++i) {
        const int p = (period > 0) ? cellPeriod : 0;
        const float n = valueNoise(x * frequency, y * frequency, seed + static_cast<unsigned>(i), p);
        sum += amplitude * std::fabs(2.0f * n - 1.0f);
        norm += amplitude;
        amplitude *= gain;
        frequency *= 2.0f;
        if (period > 0) cellPeriod *= 2;
    }
    return (norm > 0.0f) ? (sum / norm) : 0.0f;
}

// ============================ 图案 ============================

// 一个图案样本：颜色（0..1 线性基色）+ 高度（交给 makeNormalMap 求斜率）
struct PatternSample {
    math::Vec3 color;
    float height = 0.0f;
};

// 大理石：`s = sin(2π·(u·stripes) + turbulence·k)` 把正弦条带扭成脉络，再取幂压出"宽底 + 细亮脉"。
// @param cells              湍流的噪声格数（**取整数**才能无缝平铺；纹理在 u 方向铺 cells 个噪声格）
// @param stripes            脉络条带数（整数）
// @param turbulenceStrength 扭曲强度（越大脉络越乱、越像天然石）
inline PatternSample marblePattern(float u, float v, int cells = 3, int stripes = 4,
                                   float turbulenceStrength = 1.6f, int octaves = 4,
                                   unsigned seed = 11u) {
    const float t = turbulence(u * static_cast<float>(cells), v * static_cast<float>(cells),
                               octaves, 0.5f, seed, cells);
    const float phase = 2.0f * detail::PATTERN_PI *
                        (u * static_cast<float>(stripes) + t * turbulenceStrength * static_cast<float>(stripes));
    const float s = 0.5f + 0.5f * std::sin(phase);

    PatternSample out;
    // 细亮脉：s 抬到高次幂 → 只有接近波峰的一小段被点亮
    const float vein = std::pow(s, 6.0f);
    // 底色再叠一点 fBm 斑驳，避免"纯渐变"的塑料感
    const float mottle = fbm(u * static_cast<float>(cells), v * static_cast<float>(cells),
                             octaves, 2.0f, 0.5f, seed + 7u, cells);
    const math::Vec3 base(0.26f, 0.27f, 0.31f);
    const math::Vec3 veinColor(0.95f, 0.94f, 0.90f);
    out.color = base + (veinColor - base) * vein + math::Vec3(1.0f, 1.0f, 1.0f) * (mottle - 0.5f) * 0.06f;
    // 高度只取很小的幅度：makeNormalMap 用的是**斜率**，s^6 这类陡峭项要压住，否则脉络会变成刀锋
    out.height = 0.12f * vein + 0.05f * (mottle - 0.5f);
    return out;
}

namespace detail {

// 方格类图案的公共部分：返回"到最近缝隙中心的归一化深度"与格子索引。
// @param offsetRows 奇数行错开半格（砖墙）；false ⇒ 对齐网格（瓷砖）
// @param edge       输出：0 = 缝隙中心，1 = 格子内部（用于把缝隙做成一条有宽度的斜边）
inline void gridCell(float u, float v, int columns, int rows, float gapWidth, bool offsetRows,
                     int& outIx, int& outIy, float& edge) {
    const float uu = u * static_cast<float>(columns);
    const float vv = v * static_cast<float>(rows);
    const float rowIndex = std::floor(vv);
    const float shift = offsetRows ? (fractPart(rowIndex * 0.5f) * 2.0f * 0.5f) : 0.0f;
    const float fu = fractPart(uu + shift);
    const float fv = fractPart(vv);
    const float distance = std::min(std::min(fu, 1.0f - fu), std::min(fv, 1.0f - fv));
    edge = (gapWidth > 0.0f) ? std::min(distance / (gapWidth * 0.5f), 1.0f) : 1.0f;
    outIx = static_cast<int>(std::floor(uu + shift));
    outIy = static_cast<int>(rowIndex);
}

} // namespace detail

// 瓷砖 / 砖墙：格子内是"面"，缝隙处下沉成一条有宽度的斜边（法线因此有明确朝向）。
// @param columns/rows 两个方向上的格子数（**整数**才能无缝平铺；offsetRows 时 rows 需为**偶数**）
// @param gapWidth     缝隙宽度（格子内的归一化比例）
// @param offsetRows   砖墙（交错）还是瓷砖（对齐）
// @param faceColor/gapColor 面子与缝隙的颜色；面子会按格子哈希做轻微深浅差异（真实砖不会块块同色）
inline PatternSample tilePattern(float u, float v, int columns = 4, int rows = 4, float gapWidth = 0.18f,
                                 bool offsetRows = false,
                                 const math::Vec3& faceColor = math::Vec3(0.82f, 0.80f, 0.76f),
                                 const math::Vec3& gapColor = math::Vec3(0.15f, 0.15f, 0.17f),
                                 float faceHeight = 0.50f, float gapDepth = 0.42f) {
    int ix = 0;
    int iy = 0;
    float edge = 1.0f;
    detail::gridCell(u, v, columns, rows, gapWidth, offsetRows, ix, iy, edge);

    PatternSample out;
    // 每块面子的深浅差异必须用**环绕后**的格子索引：否则图案沿 u 平移 1（= columns 块）后颜色会变，
    // 纹理就不能无缝平铺了（法线图会在接缝处留一条突变）。
    const int hx = ((ix % columns) + columns) % columns;
    const int hy = ((iy % rows) + rows) % rows;
    const float variation = 0.88f + 0.12f * hash2(hx, hy, 23u);
    const math::Vec3 face = faceColor * variation;
    out.color = gapColor + (face - gapColor) * edge;
    out.height = gapDepth + (faceHeight - gapDepth) * edge;
    return out;
}

// 砖墙 = 交错行 + 偏暖的砖面（颜色仍会被材质 diffuse 再染一次，故这里保持"中性偏暖"）
inline PatternSample brickPattern(float u, float v, int columns = 12, int rows = 8, float mortarWidth = 0.14f,
                                 unsigned seed = 0u) {
    (void)seed;
    return tilePattern(u, v, columns, rows, mortarWidth, /*offsetRows=*/true,
                       math::Vec3(0.84f, 0.82f, 0.79f), math::Vec3(0.34f, 0.33f, 0.32f),
                       /*faceHeight=*/0.50f, /*gapDepth=*/0.47f);
}

// 编织（平纹 2/2 或 3/1 斜纹）：两条丝带互相压过 —— "在上"的那根亮、"在下"的那根暗。
// 碳纤维之所以一眼认出来，靠的就是这种明暗交替 + 沿丝带中心的各向异性高光。
// @param threads 每个 uv 单位的丝带格数（整数 ⇒ 无缝平铺）
// @param twill   false = 平纹（2/2 交替），true = 3/1 斜纹（更有高级感）
// @note 高度在格边处恒为 0（sin(π·f) 的两端都是 0），所以"哪根在上"切换时**不会跳变** ⇒ 高度场连续。
inline PatternSample weavePattern(float u, float v, float threads = 8.0f, bool twill = true,
                                 unsigned seed = 0u) {
    (void)seed;
    const float uu = u * threads;
    const float vv = v * threads;
    const int ix = static_cast<int>(std::floor(uu));
    const int iy = static_cast<int>(std::floor(vv));
    const float fu = fractPart(uu);
    const float fv = fractPart(vv);

    const float ribbonU = std::sin(detail::PATTERN_PI * fu);   // 沿 v 走向的丝带（中心最高）
    const float ribbonV = std::sin(detail::PATTERN_PI * fv);   // 沿 u 走向的丝带

    const int phase = twill ? ((((ix - iy) % 4) + 4) % 4) : ((ix + iy) & 1);
    const bool warpOnTop = twill ? (phase < 3) : (phase == 0);

    PatternSample out;
    const float top = warpOnTop ? ribbonU : ribbonV;
    const float bottom = warpOnTop ? ribbonV : ribbonU;
    // 上层丝带亮（沿中心最亮），下层丝带压暗 —— 模拟编织的互相遮挡与树脂高光
    out.color = math::Vec3(0.30f, 0.32f, 0.36f) * (0.40f + 0.60f * top)
              + math::Vec3(0.10f, 0.10f, 0.12f) * (0.30f + 0.30f * bottom);
    out.height = 0.25f * top + 0.08f * bottom;
    return out;
}

// ============================ 贴图生成 ============================

// 把一个逐 texel 的颜色函数烘成颜色贴图（texel 中心取样，与 makeNormalMap 的口径一致 ⇒ 两张图对齐）。
// @param color color(u, v) → 0..1 线性颜色；uv 为归一化坐标
template <class ColorFn>
inline Texture makeColorTexture(int size, ColorFn&& color,
                               const math::Vec3& fill = math::Vec3(0.5f, 0.5f, 0.5f)) {
    Texture texture(size, size, fill);
    if (size <= 0) return texture;

    const float inv = 1.0f / static_cast<float>(size);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) * inv;
            const float v = (static_cast<float>(y) + 0.5f) * inv;
            texture.setTexel(x, y, color(u, v));
        }
    }
    return texture;
}

// 一套表面贴图：颜色（albedo）+ 法线（由同一图案的高度场求斜率）
struct PatternMaps {
    Texture albedo;
    Texture normal;
};

// 一个图案 → 两张配对的贴图（颜色 + 法线）。
// @param pattern         pattern(u, v) → PatternSample
// @param normalStrength  "高度 → 斜率"的放大倍数（0 ⇒ 法线全平整，只剩颜色）
// @note 高度场会调用 pattern 两次（中心差分），图案函数本身应保持无副作用、纯函数。
template <class PatternFn>
inline PatternMaps makePatternMaps(int size, PatternFn&& pattern, float normalStrength = 1.0f) {
    PatternMaps maps;
    maps.albedo = makeColorTexture(size, [&](float u, float v) { return pattern(u, v).color; });
    maps.normal = makeNormalMap(size, [&](float u, float v) { return pattern(u, v).height; }, normalStrength);
    return maps;
}

} // namespace gfx

#endif // GFX_PATTERN_HPP
