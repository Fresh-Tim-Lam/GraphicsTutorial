// test_normalmap.cpp —— 纹理采样 / 切空间与法线贴图测试
//
// 覆盖：wrapTexelIndex（三种环绕）、Texture 采样（最近邻 / 双线性）、decodeNormal、
//       makeNormalMap（平整高度 ⇒ 平整法线；已知正弦高度 ⇒ 与解析斜率一致）、
//       sphereTangentBasis / planeTangentBasis（正交性、单位长、右手系）、
//       perturbNormal、以及 RayTracer 的"着色法向"接口（注入 ⇒ 颜色改变；零向量 ⇒ 与基线一致）。
//
// 运行：ctest --test-dir build -C Release

#include <gfx/geom/ray.hpp>
#include <gfx/render/lighting.hpp>
#include <gfx/render/normalmap.hpp>
#include <gfx/render/raytracer.hpp>
#include <gfx/render/texture.hpp>

#include "gfx_test.hpp"

#include <cmath>

using namespace gfx;
using math::Vec3;

namespace {

constexpr float kPi = 3.14159265358979323846f;

// 构造一个 2x2 纹理：四个角颜色互不相同，便于验证采样位置
Texture makeQuad() {
    Texture t(2, 2, Vec3(0.0f, 0.0f, 0.0f));
    t.setTexel(0, 0, Vec3(1.0f, 0.0f, 0.0f));
    t.setTexel(1, 0, Vec3(0.0f, 1.0f, 0.0f));
    t.setTexel(0, 1, Vec3(0.0f, 0.0f, 1.0f));
    t.setTexel(1, 1, Vec3(1.0f, 1.0f, 1.0f));
    return t;
}

void testWrapIndex() {
    // Repeat
    TEST_CHECK(wrapTexelIndex(-1, 4, WrapMode::Repeat) == 3);
    TEST_CHECK(wrapTexelIndex(4, 4, WrapMode::Repeat) == 0);
    TEST_CHECK(wrapTexelIndex(6, 4, WrapMode::Repeat) == 2);
    // Clamp
    TEST_CHECK(wrapTexelIndex(-3, 4, WrapMode::Clamp) == 0);
    TEST_CHECK(wrapTexelIndex(9, 4, WrapMode::Clamp) == 3);
    // Mirror：0 1 2 3 | 3 2 1 0 | 0 1 ...
    TEST_CHECK(wrapTexelIndex(0, 4, WrapMode::Mirror) == 0);
    TEST_CHECK(wrapTexelIndex(3, 4, WrapMode::Mirror) == 3);
    TEST_CHECK(wrapTexelIndex(4, 4, WrapMode::Mirror) == 3);
    TEST_CHECK(wrapTexelIndex(5, 4, WrapMode::Mirror) == 2);
    TEST_CHECK(wrapTexelIndex(8, 4, WrapMode::Mirror) == 0);
    // 负数侧与周期一致：f(-1) = f(7) = 0
    TEST_CHECK(wrapTexelIndex(-1, 4, WrapMode::Mirror) == 0);
    TEST_CHECK(wrapTexelIndex(-2, 4, WrapMode::Mirror) == 1);
}

void testTextureSampling() {
    const Texture quad = makeQuad();

    // 最近邻：四个象限中心分别取到四个角像素（像素中心在 (i+0.5)/2）
    const Vec3 n00 = quad.sample(0.25f, 0.25f, WrapMode::Repeat, TextureFilter::Nearest);
    TEST_NEAR(n00.x, 1.0f, 1e-6f);
    TEST_NEAR(n00.y, 0.0f, 1e-6f);
    const Vec3 n11 = quad.sample(0.75f, 0.75f, WrapMode::Repeat, TextureFilter::Nearest);
    TEST_NEAR(n11.x, 1.0f, 1e-6f);
    TEST_NEAR(n11.y, 1.0f, 1e-6f);

    // 双线性：正中心 = 四角平均
    const Vec3 mid = quad.sample(0.5f, 0.5f, WrapMode::Repeat, TextureFilter::Bilinear);
    TEST_NEAR(mid.x, 0.5f, 1e-5f);
    TEST_NEAR(mid.y, 0.5f, 1e-5f);
    TEST_NEAR(mid.z, 0.5f, 1e-5f);

    // Clamp：越界 uv 取边缘像素
    const Vec3 edge = quad.sample(-5.0f, -5.0f, WrapMode::Clamp, TextureFilter::Nearest);
    TEST_NEAR(edge.x, 1.0f, 1e-6f);

    // 空纹理返回零向量
    const Texture empty;
    const Vec3 zero = empty.sample(0.3f, 0.7f);
    TEST_NEAR(zero.x, 0.0f, 1e-6f);
    TEST_NEAR(zero.y, 0.0f, 1e-6f);
    TEST_NEAR(zero.z, 0.0f, 1e-6f);
}

void testDecodeNormal() {
    const Vec3 flat = decodeNormal(Vec3(0.5f, 0.5f, 1.0f));
    TEST_NEAR(flat.x, 0.0f, 1e-6f);
    TEST_NEAR(flat.y, 0.0f, 1e-6f);
    TEST_NEAR(flat.z, 1.0f, 1e-6f);

    const Vec3 px = decodeNormal(Vec3(1.0f, 0.5f, 0.5f));
    TEST_NEAR(px.x, 1.0f, 1e-6f);
    TEST_NEAR(px.y, 0.0f, 1e-6f);
    TEST_NEAR(px.z, 0.0f, 1e-6f);

    const Vec3 py = decodeNormal(Vec3(0.5f, 1.0f, 0.5f));
    TEST_NEAR(py.y, 1.0f, 1e-6f);
    TEST_NEAR(py.x, 0.0f, 1e-6f);
}

void testMakeNormalMap() {
    // 1) 高度恒定 ⇒ 全平整（(0.5,0.5,1)）
    const Texture flat = makeNormalMap(8, [](float, float) { return 0.37f; }, 1.0f);
    TEST_CHECK(flat.width() == 8 && flat.height() == 8);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const Vec3 c = flat.texel(x, y);
            TEST_NEAR(c.x, 0.5f, 1e-5f);
            TEST_NEAR(c.y, 0.5f, 1e-5f);
            TEST_NEAR(c.z, 1.0f, 1e-5f);
        }
    }

    // 2) 已知正弦高度 h = amp*sin(2πu)：切线空间法向应与解析斜率一致
    //    n ∝ ( -dh/du * strength, 0, 1 )，dh/du = amp*2π*cos(2πu)
    const float amp = 0.2f;
    const float strength = 1.0f;
    const int size = 64;
    const Texture wave = makeNormalMap(size, [amp](float u, float v) {
        (void)v;
        return amp * std::sin(2.0f * kPi * u);
    }, strength);

    const int px = 16, py = 32;
    const float u = (static_cast<float>(px) + 0.5f) / static_cast<float>(size);
    const float dhdu = amp * 2.0f * kPi * std::cos(2.0f * kPi * u);
    Vec3 expected(-dhdu * strength, 0.0f, 1.0f);
    Vec3 expectedUnit;
    TEST_CHECK(math::normalize(expected, expectedUnit));
    const Vec3 actual = decodeNormal(wave.texel(px, py));
    TEST_NEAR(actual.x, expectedUnit.x, 1e-2f);
    TEST_NEAR(actual.y, expectedUnit.y, 1e-2f);
    TEST_NEAR(actual.z, expectedUnit.z, 1e-2f);

    // 3) strength = 0 ⇒ 与"平整"完全一致（不产生任何扰动）
    const Texture zeroStrength = makeNormalMap(size, [amp](float u, float v) {
        (void)v;
        return amp * std::sin(2.0f * kPi * u);
    }, 0.0f);
    for (int y = 0; y < size; y += 7) {
        for (int x = 0; x < size; x += 7) {
            const Vec3 c = zeroStrength.texel(x, y);
            TEST_NEAR(c.x, 0.5f, 1e-5f);
            TEST_NEAR(c.y, 0.5f, 1e-5f);
            TEST_NEAR(c.z, 1.0f, 1e-5f);
        }
    }
}

// 检查一组基是否正交、单位长、右手系（B = N × T）
void checkBasis(const TangentBasis& b, const char* tag) {
    TEST_NEAR(b.normal.length(), 1.0f, 1e-5f);
    TEST_NEAR(b.tangent.length(), 1.0f, 1e-5f);
    TEST_NEAR(b.bitangent.length(), 1.0f, 1e-5f);
    TEST_NEAR(math::dot(b.tangent, b.normal), 0.0f, 1e-5f);
    TEST_NEAR(math::dot(b.bitangent, b.normal), 0.0f, 1e-5f);
    TEST_NEAR(math::dot(b.tangent, b.bitangent), 0.0f, 1e-5f);
    const Vec3 cross_nt = math::cross(b.normal, b.tangent);
    TEST_NEAR(math::dot(cross_nt, b.bitangent), 1.0f, 1e-4f);   // 右手系
    (void)tag;
}

void testTangentBases() {
    const Vec3 center(0.3f, -0.2f, 0.5f);
    const Vec3 point = center + Vec3(0.6f, 0.8f, 0.1f);
    checkBasis(sphereTangentBasis(point, center), "sphere");

    // 球面切空间：法向应指向球外
    const TangentBasis sb = sphereTangentBasis(point, center);
    Vec3 outward;
    TEST_CHECK(math::normalize(point - center, outward));
    TEST_NEAR(math::dot(sb.normal, outward), 1.0f, 1e-5f);

    // 平面：一般法向与接近 y 的法向都要不退化
    checkBasis(planeTangentBasis(Vec3(0.0f, 1.0f, 0.0f)), "plane+Y");
    checkBasis(planeTangentBasis(Vec3(0.0f, 0.999f, 0.04f)), "plane~Y");
    checkBasis(planeTangentBasis(Vec3(0.0f, 0.0f, -1.0f)), "plane-Z");
}

void testPerturbNormal() {
    const Vec3 center(0.0f, 0.0f, 0.0f);
    const Vec3 point(0.0f, 1.0f, 0.0f);
    const TangentBasis b = sphereTangentBasis(point, center);

    // 平整法向 ⇒ 还原几何法向
    const Vec3 same = perturbNormal(b, Vec3(0.0f, 0.0f, 1.0f));
    TEST_NEAR(math::dot(same, b.normal), 1.0f, 1e-5f);

    // 倾斜法向 ⇒ 与几何法向夹角应为 45°（n = (1,0,1)/√2）
    const float s = 1.0f / std::sqrt(2.0f);
    const Vec3 tilted = perturbNormal(b, Vec3(s, 0.0f, s));
    TEST_NEAR(math::dot(tilted, b.normal), s, 1e-4f);
    TEST_NEAR(math::dot(tilted, b.tangent), s, 1e-4f);
}

// 端到端：RayTracer 的着色法向接口
void testTracerShadingNormal() {
    const Vec3 center(0.0f, 0.0f, 0.0f);
    const float radius = 1.0f;
    Material mat;
    mat.diffuse = Vec3(0.6f, 0.6f, 0.6f);
    mat.specular = Vec3(0.0f, 0.0f, 0.0f);   // 关掉高光，只留漫反射，便于观察 N·L

    std::vector<Light> lights;
    lights.push_back(Light{Vec3(3.0f, 3.0f, 3.0f), Vec3(1.0f, 1.0f, 1.0f), 1.0f});

    const Ray ray(Vec3(0.0f, 0.0f, 5.0f), Vec3(0.0f, 0.0f, -1.0f));   // 打向球的正前方

    auto makeHit = [&](const Vec3& shadingNormal) {
        return [&, shadingNormal](const Ray& r, SurfaceHit& out) {
            HitRecord h;
            if (!gfx::intersectSphere(r, center, radius, h)) return false;
            out.geometry = h;
            out.material = &mat;
            out.shadingNormal = shadingNormal;
            return true;
        };
    };

    RayTracer baseline(makeHit(Vec3(0.0f, 0.0f, 0.0f)), lights);
    const Vec3 baseColor = baseline.trace(ray, 4);

    // 几何法向本身（球正前方命中点法向 = (0,0,1)）⇒ 与零向量等价
    RayTracer geometric(makeHit(Vec3(0.0f, 0.0f, 1.0f)), lights);
    const Vec3 geoColor = geometric.trace(ray, 4);
    TEST_NEAR(geoColor.x, baseColor.x, 1e-6f);
    TEST_NEAR(geoColor.y, baseColor.y, 1e-6f);
    TEST_NEAR(geoColor.z, baseColor.z, 1e-6f);

    // 把着色法向歪向光源方向 ⇒ 变亮
    RayTracer tilted(makeHit(Vec3(1.0f, 0.0f, 1.0f)), lights);
    const Vec3 tiltColor = tilted.trace(ray, 4);
    TEST_CHECK(tiltColor.x > baseColor.x + 1e-3f);
}

} // namespace

int main() {
    testWrapIndex();
    testTextureSampling();
    testDecodeNormal();
    testMakeNormalMap();
    testTangentBases();
    testPerturbNormal();
    testTracerShadingNormal();
    return gfx::test::summary();
}
