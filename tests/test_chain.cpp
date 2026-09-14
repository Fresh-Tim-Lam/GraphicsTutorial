// test_chain.cpp —— 变换链 math::Transform<N> 测试
//
// 覆盖：链折叠等价性、单点/批点求值一致、组合语义、步长（AoS）视图、原地变换。
// 运行：ctest --test-dir build -C Release -R test_chain

#include "gfx_test.hpp"
#include <gfx/math/math.hpp>

#include <cstddef>

using namespace gfx::math;

namespace {
constexpr float kPi = 3.14159265358979f;

// AoS 顶点结构体：位置后面跟别的字段，用于验证"带步长"视图。
// @note 数学层的批量 apply 要求点是**已齐次**的（N=4 时 w=1），故位置用 Vec4。
struct Vertex4 {
    Vec4 pos;
    Vec2 uv;      // 干扰字段：使 stride != sizeof(Vec4)
};
} // namespace

int main() {
    // ---- 链折叠 == 逐步作用（单点）----
    {
        Transform<3> t;
        t.then(transform2d::translate(Vec2(10.0f, 20.0f)))
         .then(transform2d::rotate(kPi / 2.0f))
         .then(transform2d::scale(2.0f));

        TEST_CHECK(t.stepCount() == 3);
        TEST_CHECK(!t.isIdentity());

        Vec3 p(1.0f, 0.0f, 1.0f);
        // 单点路径：按记录顺序逐次作用
        const Vec3 byChain = t.apply(p);
        // 折叠路径：等价
        const Vec3 byMatrix = t.matrix() * p;
        TEST_NEAR(byChain.x, byMatrix.x, 1e-4f);
        TEST_NEAR(byChain.y, byMatrix.y, 1e-4f);

        // 手算校验（书写顺序 = 作用顺序：先平移、再旋转、后缩放）：
        // (1,0) --T(10,20)--> (11,20) --R(90°)--> (-20,11) --S(2)--> (-40,22)
        TEST_NEAR(byChain.x, -40.0f, 1e-4f);
        TEST_NEAR(byChain.y, 22.0f, 1e-4f);
    }

    // ---- 组合语义：a * b 表示先作用 a、再作用 b（与 then 同口径 = 左乘口径）----
    {
        Transform<3> a(transform2d::translate(Vec2(10.0f, 0.0f)));
        Transform<3> b(transform2d::rotate(kPi / 2.0f));
        const Transform<3> ab = a * b;      // 先平移，再旋转

        const Vec3 p(1.0f, 0.0f, 1.0f);
        const Vec3 r = ab.apply(p);
        TEST_NEAR(r.x, 0.0f, 1e-4f);        // (1,0) --T(10,0)--> (11,0) --R(90°)--> (0,11)
        TEST_NEAR(r.y, 11.0f, 1e-4f);

        // 与 then() 等价：书写顺序 = 作用顺序
        Transform<3> viaThen = a;
        viaThen.then(b);
        const Vec3 r2 = viaThen.apply(p);
        TEST_NEAR(r2.x, r.x, 1e-4f);
        TEST_NEAR(r2.y, r.y, 1e-4f);

        // 矩阵口径：a*b ≡ 把 b 的矩阵**左乘**到 a 上 ⇒ matrix(ab) == matrix(b) * matrix(a)
        const Mat3 m = ab.matrix();
        const Mat3 expect = b.matrix() * a.matrix();
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                TEST_NEAR(m(i, j), expect(i, j), 1e-4f);
    }

    // ---- 恒等链 / then 追加整条链 ----
    {
        Transform<3> empty;
        TEST_CHECK(empty.isIdentity());
        const Vec3 p(3.0f, 4.0f, 1.0f);
        const Vec3 r = empty.apply(p);
        TEST_NEAR(r.x, 3.0f, 1e-6f);
        TEST_NEAR(r.y, 4.0f, 1e-6f);

        Transform<3> t(transform2d::translate(Vec2(1.0f, 2.0f)));
        t.then(Transform<3>(transform2d::translate(Vec2(10.0f, 20.0f))));
        const Vec3 q = t.apply(p);
        TEST_NEAR(q.x, 14.0f, 1e-5f);
        TEST_NEAR(q.y, 26.0f, 1e-5f);
    }

    // ---- 批点：连续布局（stride = sizeof(Vec4)）+ 原地 ----
    {
        Transform<4> t;
        t.then(transform3d::translate(Vec3(5.0f, -3.0f, 1.0f)))
         .then(transform3d::rotate(kPi / 2.0f, Axis::Z));

        Vec4 pts[3] = {
            Vec4(0.0f, 0.0f, 0.0f, 1.0f),
            Vec4(1.0f, 0.0f, 0.0f, 1.0f),
            Vec4(0.0f, 2.0f, 0.0f, 1.0f),
        };
        Vec4 out[3];
        t.apply(reinterpret_cast<const float*>(pts), reinterpret_cast<float*>(out),
                3, sizeof(Vec4));

        for (int i = 0; i < 3; ++i) {
            const Vec4 expect = t.matrix() * pts[i];
            TEST_NEAR(out[i].x, expect.x, 1e-4f);
            TEST_NEAR(out[i].y, expect.y, 1e-4f);
            TEST_NEAR(out[i].z, expect.z, 1e-4f);
        }

        // 原地变换：in == out 也应正确
        t.apply(reinterpret_cast<const float*>(pts), reinterpret_cast<float*>(pts),
                3, sizeof(Vec4));
        const Vec4 expect0 = t.matrix() * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
        TEST_NEAR(pts[0].x, expect0.x, 1e-4f);
        TEST_NEAR(pts[0].y, expect0.y, 1e-4f);
    }

    // ---- 批点：带步长（AoS 交错布局），零拷贝读取 ----
    {
        Vertex4 verts[3] = {
            { Vec4(0.0f, 0.0f, 0.0f, 1.0f), Vec2(9.0f, 9.0f) },
            { Vec4(1.0f, 0.0f, 0.0f, 1.0f), Vec2(9.0f, 9.0f) },
            { Vec4(0.0f, 2.0f, 0.0f, 1.0f), Vec2(9.0f, 9.0f) },
        };
        Transform<4> t(transform3d::translate(Vec3(1.0f, 1.0f, 0.0f)));

        // 输入为 AoS（stride = sizeof(Vertex4)），输出同 stride 的等距内存
        Vertex4 out[3];
        t.apply(reinterpret_cast<const float*>(&verts[0].pos),
                reinterpret_cast<float*>(&out[0].pos), 3, sizeof(Vertex4));

        TEST_NEAR(out[0].pos.x, 1.0f, 1e-5f);   // 点0: (0,0) -> (1,1)
        TEST_NEAR(out[0].pos.y, 1.0f, 1e-5f);
        TEST_NEAR(out[1].pos.x, 2.0f, 1e-5f);   // 点1: (1,0) -> (2,1)
        TEST_NEAR(out[2].pos.y, 3.0f, 1e-5f);   // 点2: (0,2) -> (1,3)

        // 干扰字段未被改动（视图确实是零拷贝读取）
        TEST_NEAR(verts[1].uv.x, 9.0f, 1e-6f);
    }

    return gfx::test::summary();
}
