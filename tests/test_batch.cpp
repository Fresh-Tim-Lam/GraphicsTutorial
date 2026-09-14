// test_batch.cpp —— 批量变换 + 后端钩子测试
//
// 验证：CPU 默认批量变换与逐点 mat*vec 一致；注册自定义后端后生效；恢复后回 CPU。
// 运行：ctest --test-dir build -C Release -R test_batch

#include "gfx_test.hpp"
#include <gfx/math/math.hpp>

using namespace gfx::math;

namespace {

// 自定义"后端"（模拟未来 CUDA/自研 GPU 分派点被注入）：每点 x += 1（占位逻辑）
int g_customCalls = 0;
void customBackend(const float* m, const float* in, float* out, std::size_t count) {
    (void)m;
    ++g_customCalls;
    for (std::size_t i = 0; i < count; ++i) {
        out[i * 4 + 0] = in[i * 4 + 0] + 1.0f;
        out[i * 4 + 1] = in[i * 4 + 1];
        out[i * 4 + 2] = in[i * 4 + 2];
        out[i * 4 + 3] = in[i * 4 + 3];
    }
}

} // namespace

int main() {
    // ---- CPU 默认批量变换：平移矩阵 Mat4 ----
    Mat4 t = Mat4::identity();
    t(0, 3) = 5.0f;
    t(1, 3) = -3.0f;

    Vec4 pts[3] = {
        Vec4(0.0f, 0.0f, 0.0f, 1.0f),
        Vec4(1.0f, 2.0f, 3.0f, 1.0f),
        Vec4(-4.0f, 7.0f, 0.5f, 1.0f),
    };
    Vec4 out[3];
    batchTransform<4>(t, pts, out, 3);

    for (int i = 0; i < 3; ++i) {
        Vec4 expected = t * pts[i];   // 与逐点 mat*vec 对照
        TEST_NEAR(out[i].x, expected.x, 1e-5f);
        TEST_NEAR(out[i].y, expected.y, 1e-5f);
        TEST_NEAR(out[i].z, expected.z, 1e-5f);
        TEST_NEAR(out[i].w, expected.w, 1e-5f);
    }

    // 默认后端存在（非空即 CPU 或已注册实现）
    TEST_CHECK(batchTransformBackend<4>() != nullptr);

    // ---- 注册自定义后端（模拟注入 GPU 分派点）----
    setBatchTransformBackend<4>(&customBackend);
    TEST_CHECK(g_customCalls == 0);
    batchTransform<4>(t, pts, out, 3);
    TEST_CHECK(g_customCalls == 1);
    TEST_NEAR(out[1].x, pts[1].x + 1.0f, 1e-6f);   // 自定义逻辑生效

    // ---- 恢复 CPU：再次批量变换应回到平移语义 ----
    resetBatchTransformBackend<4>();
    batchTransform<4>(t, pts, out, 3);
    Vec4 expected0 = t * pts[1];
    TEST_NEAR(out[1].x, expected0.x, 1e-5f);

    // 空指针注册 == 恢复 CPU（同 reset）
    setBatchTransformBackend<4>(nullptr);
    batchTransform<4>(t, pts, out, 1);
    Vec4 e = t * pts[0];
    TEST_NEAR(out[0].x, e.x, 1e-5f);

    return gfx::test::summary();
}
