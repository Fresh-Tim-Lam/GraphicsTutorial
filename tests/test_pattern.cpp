// test_pattern.cpp —— 程序化图案与颜色贴图测试
//
// 覆盖：
//   1) 噪声基元：哈希/值噪声/fBm/湍流的**确定性**、取值范围、连续性，以及 period>0 时的**周期性**；
//   2) 四个图案（大理石/瓷砖/砖墙/编织）：颜色与高度在合理范围、**对 u,v 以 1 为周期**（无缝平铺的前提）、
//      以及各自的结构性断言（缝隙比砖面暗且低、砖墙交错、编织上层的丝带更亮更高）；
//   3) makeColorTexture / makePatternMaps：尺寸、texel 中心取样口径、法线为单位向量、高度图非平坦。
//
// 运行：ctest --test-dir build -C Release

#include <gfx/render/pattern.hpp>

#include "gfx_test.hpp"

#include <cmath>

using namespace gfx;
using math::Vec3;

namespace {

// 随机但确定的采样点集合（避免依赖 rand，保证可复现）
constexpr float kUs[] = {0.013f, 0.137f, 0.251f, 0.367f, 0.499f, 0.613f, 0.742f, 0.871f, 0.947f};
constexpr float kVs[] = {0.021f, 0.158f, 0.279f, 0.394f, 0.517f, 0.636f, 0.759f, 0.883f, 0.961f};

bool inUnit01(const Vec3& c) {
    return c.x >= 0.0f && c.x <= 1.0f && c.y >= 0.0f && c.y <= 1.0f && c.z >= 0.0f && c.z <= 1.0f;
}

void testNoisePrimitives() {
    // 哈希：确定 + 落在 [0,1)
    for (int x = -3; x <= 3; ++x) {
        for (int y = -3; y <= 3; ++y) {
            const float h1 = hash2(x, y, 0u);
            const float h2 = hash2(x, y, 0u);
            TEST_NEAR(h1, h2, 0.0f);   // 逐位一致
            TEST_CHECK(h1 >= 0.0f && h1 < 1.0f);
        }
    }
    // 不同格点不应总是相同（抽样检查分布不是常量）
    TEST_CHECK(hash2(0, 0, 0u) != hash2(1, 0, 0u));
    TEST_CHECK(hash2(0, 0, 0u) != hash2(0, 0, 1u));   // 种子参与哈希

    // 值噪声：范围 + 连续（相邻点差异小）+ 确定
    float minN = 1e9f;
    float maxN = -1e9f;
    for (float x = 0.0f; x < 4.0f; x += 0.137f) {
        for (float y = 0.0f; y < 4.0f; y += 0.211f) {
            const float n = valueNoise(x, y, 3u);
            TEST_CHECK(n >= 0.0f && n <= 1.0f);
            minN = std::fmin(minN, n);
            maxN = std::fmax(maxN, n);
            TEST_NEAR(n, valueNoise(x, y, 3u), 0.0f);
        }
    }
    TEST_CHECK(maxN - minN > 0.2f);   // 不是常量
    TEST_CHECK(std::fabs(valueNoise(1.5f, 2.5f, 3u) - valueNoise(1.5f + 1e-4f, 2.5f, 3u)) < 0.01f);

    // 周期性：period = p 时，x 平移 p 得到同一个值（可无缝平铺的关键）
    //   容差取小量而非 0：格点索引会精确回绕，但"格内小数位置" x−floor(x) 在平移后的浮点结果可能差最低位
    for (int p = 2; p <= 5; ++p) {
        for (float x = 0.0f; x < 3.0f; x += 0.31f) {
            for (float y = 0.0f; y < 3.0f; y += 0.43f) {
                TEST_NEAR(valueNoise(x, y, 1u, p), valueNoise(x + static_cast<float>(p), y, 1u, p), 1e-5f);
                TEST_NEAR(valueNoise(x, y, 1u, p), valueNoise(x, y + static_cast<float>(p), 1u, p), 1e-5f);
            }
        }
    }

    // fBm / 湍流：范围 + 周期性（周期按 2 的幂扩张）
    for (float x = 0.0f; x < 2.0f; x += 0.29f) {
        for (float y = 0.0f; y < 2.0f; y += 0.37f) {
            const float f = fbm(x, y, 4, 2.0f, 0.5f, 5u);
            TEST_CHECK(f >= 0.0f && f <= 1.0f);
            TEST_NEAR(f, fbm(x + 4.0f, y, 4, 2.0f, 0.5f, 5u, 4), 1e-5f);

            const float t = turbulence(x, y, 4, 0.5f, 5u);
            TEST_CHECK(t >= 0.0f && t <= 1.0f);
            TEST_NEAR(t, turbulence(x + 4.0f, y, 4, 0.5f, 5u, 4), 1e-5f);
        }
    }
}

// 所有图案都应满足：shape(u+1, v) == shape(u, v) 且 shape(u, v+1) == shape(u, v)
template <class Fn>
void checkPeriodic(Fn&& fn, const char* label) {
    int mismatches = 0;
    for (float u : kUs) {
        for (float v : kVs) {
            const PatternSample a = fn(u, v);
            const PatternSample bu = fn(u + 1.0f, v);
            const PatternSample bv = fn(u, v + 1.0f);
            const float du = std::fmax(std::fmax(std::fabs(a.color.x - bu.color.x), std::fabs(a.color.y - bu.color.y)),
                                       std::fmax(std::fabs(a.color.z - bu.color.z), std::fabs(a.height - bu.height)));
            const float dv = std::fmax(std::fmax(std::fabs(a.color.x - bv.color.x), std::fabs(a.color.y - bv.color.y)),
                                       std::fmax(std::fabs(a.color.z - bv.color.z), std::fabs(a.height - bv.height)));
            if (du > 1e-4f || dv > 1e-4f) ++mismatches;
        }
    }
    if (mismatches != 0) std::fprintf(stderr, "  [%s] 非周期采样点数 = %d\n", label, mismatches);
    TEST_CHECK(mismatches == 0);
}

void testPatterns() {
    // 颜色范围 + 周期（四张图都要能平铺，否则法线图接缝处会有一条突变）
    for (float u : kUs) {
        for (float v : kVs) {
            TEST_CHECK(inUnit01(marblePattern(u, v).color));
            TEST_CHECK(inUnit01(tilePattern(u, v).color));
            TEST_CHECK(inUnit01(brickPattern(u, v).color));
            TEST_CHECK(inUnit01(weavePattern(u, v).color));

            TEST_CHECK(std::fabs(marblePattern(u, v).height) < 1.0f);
            TEST_CHECK(std::fabs(tilePattern(u, v).height) < 1.0f);
            TEST_CHECK(std::fabs(weavePattern(u, v).height) < 1.0f);
        }
    }
    checkPeriodic([](float u, float v) { return marblePattern(u, v); }, "marble");
    checkPeriodic([](float u, float v) { return tilePattern(u, v, 6, 8); }, "tile");
    checkPeriodic([](float u, float v) { return brickPattern(u, v); }, "brick");
    checkPeriodic([](float u, float v) { return weavePattern(u, v); }, "weave");

    // 大理石：底色暗、脉络亮 ⇒ 整张图应有明显的明暗跨度
    float marbleMin = 1e9f;
    float marbleMax = -1e9f;
    for (int i = 0; i < 64; ++i) {
        const float u = static_cast<float>(i) * 0.0137f + 0.003f;
        const float v = static_cast<float>(i) * 0.0211f + 0.007f;
        const Vec3 c = marblePattern(u, v).color;
        marbleMin = std::fmin(marbleMin, c.x);
        marbleMax = std::fmax(marbleMax, c.x);
    }
    TEST_CHECK(marbleMax - marbleMin > 0.35f);

    // 瓷砖：缝隙中心（格子边界）比砖面中心更暗、更低
    //   取 columns = 4 ⇒ 砖面中心在 u = 0.125（格内 0.5 处），缝隙中心在 u = 0（格边界）
    const PatternSample face = tilePattern(0.125f, 0.125f, 4, 4, 0.18f);
    const PatternSample gap = tilePattern(0.0f, 0.0f, 4, 4, 0.18f);
    TEST_CHECK(gap.color.x < face.color.x);
    TEST_CHECK(gap.height < face.height);

    // 砖墙：相邻行交错半块 ⇒ 同一 u 处，第 0 行与第 1 行的格子索引应相差半格
    //   用"缝隙位置"间接验证：u = 0.5/12（第 0 行缝的中心）在第 1 行不是缝
    const PatternSample rowA = brickPattern(0.5f / 12.0f, 0.5f / 8.0f);
    const PatternSample rowB = brickPattern(0.5f / 12.0f, 1.5f / 8.0f);
    TEST_CHECK(std::fabs(rowB.height - rowA.height) > 1e-3f);

    // 编织：整格中心处两根丝带都处于波峰（看不出差别），要在**偏离中心**的位置比较 ——
    // 上层丝带在那里仍然亮（沿中心成脊），下层丝带已压暗。
    const PatternSample c00 = weavePattern(0.5f / 8.0f, 0.15f / 8.0f);    // (0,0) 格：3/1 斜纹下 phase=0 ⇒ 经线在上
    const PatternSample c30 = weavePattern(3.5f / 8.0f, 0.15f / 8.0f);    // (3,0) 格：phase=3 ⇒ 纬线在上
    TEST_CHECK(c00.height > 0.0f);
    TEST_CHECK(c30.height > 0.0f);
    TEST_CHECK(c00.color.x > c30.color.x);

    // 编织的另一半：同一格内，"在上"的丝带沿其中心成脊 ⇒ 横向偏移时高度也不同
    const PatternSample alongRibbon = weavePattern(0.5f / 8.0f, 0.5f / 8.0f);   // 格中心（两脊相交）
    const PatternSample offRibbon = weavePattern(0.9f / 8.0f, 0.5f / 8.0f);     // 靠近格边（fu 大）
    TEST_CHECK(alongRibbon.height > offRibbon.height);
}

void testTextureGenerators() {
    // makeColorTexture：逐 texel 调颜色函数，uv 取 texel 中心
    const Texture t = makeColorTexture(8, [](float u, float v) { return Vec3(u, v, 1.0f - u); });
    TEST_CHECK(t.width() == 8 && t.height() == 8);
    TEST_NEAR(t.texel(0, 0).x, 0.0625f, 1e-6f);        // (0+0.5)/8
    TEST_NEAR(t.texel(7, 7).x, 0.9375f, 1e-6f);        // (7+0.5)/8
    TEST_NEAR(t.texel(3, 5).y, 5.5f / 8.0f, 1e-6f);

    // 空尺寸：不崩，返回空纹理
    TEST_CHECK(makeColorTexture(0, [](float, float) { return Vec3(); }).empty());

    // makePatternMaps：两张图尺寸一致；法线是单位向量且**不是全平坦**（说明高度确实起了作用）
    const PatternMaps maps = makePatternMaps(128, [](float u, float v) { return tilePattern(u, v, 6, 8); }, 1.0f);
    TEST_CHECK(maps.albedo.width() == 128 && maps.normal.width() == 128);

    // 瓷砖的"面"内部本来就是平的，只有缝隙附近的斜边有倾斜 ⇒ 统计**明显倾斜**的 texel 有多少
    int tilted = 0;
    int total = 0;
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            const Vec3 n = decodeNormal(maps.normal.texel(x, y));   // 解码回 [-1,1]
            TEST_NEAR(n.length(), 1.0f, 1e-3f);                     // 单位向量
            ++total;
            if (n.z < 0.9f) ++tilted;
        }
    }
    TEST_CHECK(total == 128 * 128);
    TEST_CHECK(tilted > total / 50);   // 至少 2% 的 texel 明显倾斜 ⇒ 高度场有实质起伏

    // 强度 0 ⇒ 法线全平坦（只有颜色）
    const PatternMaps flatMaps = makePatternMaps(32, [](float u, float v) { return tilePattern(u, v); }, 0.0f);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const Vec3 n = decodeNormal(flatMaps.normal.texel(x, y));
            TEST_NEAR(n.z, 1.0f, 1e-4f);
        }
    }
}

} // namespace

int main() {
    testNoisePrimitives();
    testPatterns();
    testTextureGenerators();
    return gfx::test::summary();
}
