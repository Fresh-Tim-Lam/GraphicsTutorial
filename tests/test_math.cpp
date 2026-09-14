// test_math.cpp —— gfx_math 测试（vec2/3/4 + mat2/3/4 核心运算）
//
// 运行：ctest --test-dir build -C Release -R test_math

#include "gfx_test.hpp"
#include <gfx/math/math.hpp>

using namespace gfx::math;

int main() {
    // ---- vec2 ----
    {
        Vec2 a(3.0f, 4.0f);
        TEST_CHECK(a.length() == 5.0f);
        TEST_NEAR(dot(a, a), 25.0f, 1e-6f);
        Vec2 n;
        TEST_CHECK(normalize(a, n));
        TEST_NEAR(n.length(), 1.0f, 1e-6f);
        Vec2 zero;
        TEST_CHECK(!normalize(zero, n));   // 零向量归一失败
    }

    // ---- vec3 ----
    {
        Vec3 a(1.0f, 0.0f, 0.0f);
        Vec3 b(0.0f, 1.0f, 0.0f);
        Vec3 c = cross(a, b);
        TEST_NEAR(c.x, 0.0f, 1e-6f);
        TEST_NEAR(c.y, 0.0f, 1e-6f);
        TEST_NEAR(c.z, 1.0f, 1e-6f);
        Vec3 v(1.0f, 2.0f, 2.0f);
        TEST_NEAR(v.length(), 3.0f, 1e-6f);
        TEST_CHECK((a + b).z == 0.0f && (a + b).x == 1.0f && (a + b).y == 1.0f);
    }

    // ---- vec4 齐次 ----
    {
        Vec4 p(1.0f, 2.0f, 3.0f, 1.0f);
        Vec4 scaled = p * 2.0f;
        TEST_NEAR(scaled.w, 2.0f, 1e-6f);
        Vec4 q;
        TEST_CHECK(normalize(Vec4(0,0,2,0), q));   // w=0 表示方向向量，归一后仍 w=0
        TEST_NEAR(q.z, 1.0f, 1e-6f);
    }

    // ---- mat2：单位元 + 逆 ----
    {
        Mat2 id = Mat2::identity();
        Vec2 v(3.0f, -2.0f);
        Vec2 r = id * v;
        TEST_NEAR(r.x, 3.0f, 1e-6f);
        TEST_NEAR(r.y, -2.0f, 1e-6f);

        Mat2 m;               // 缩放 (2, -1) 的对角阵
        m(0,0)=2.0f; m(1,1)=-1.0f;
        Mat2 inv;
        TEST_CHECK(m.inverse(inv));
        Mat2 prod = m * inv;
        for (int r=0;r<2;++r) for (int c=0;c<2;++c)
            TEST_NEAR(prod(r,c), (r==c)?1.0f:0.0f, 1e-5f);
        Mat2 singular;        // 奇异阵求逆失败
        TEST_CHECK(!singular.inverse(inv));
    }

    // ---- mat3：行列式 + 逆 ----
    {
        Mat3 m;
        m(0,0)=1; m(0,1)=2; m(0,2)=3;
        m(1,0)=0; m(1,1)=1; m(1,2)=4;
        m(2,0)=5; m(2,1)=6; m(2,2)=0;
        TEST_NEAR(m.determinant(), 1.0f, 1e-4f);   // 手算 det=1
        Mat3 inv;
        TEST_CHECK(m.inverse(inv));
        Mat3 prod = m * inv;
        for (int r=0;r<3;++r) for (int c=0;c<3;++c)
            TEST_NEAR(prod(r,c), (r==c)?1.0f:0.0f, 1e-4f);
    }

    // ---- mat4：逆（伴随矩阵法）+ 齐次点变换 ----
    {
        // 平移矩阵：x' = x+tx ... 齐次坐标
        Mat4 t = Mat4::identity();
        t(0,3)=5.0f; t(1,3)=-3.0f; t(2,3)=2.0f;
        Mat4 inv;
        TEST_CHECK(t.inverse(inv));
        Mat4 prod = t * inv;
        for (int r=0;r<4;++r) for (int c=0;c<4;++c)
            TEST_NEAR(prod(r,c), (r==c)?1.0f:0.0f, 1e-4f);

        Vec4 p(1.0f, 2.0f, 3.0f, 1.0f);
        Vec4 moved = t * p;
        TEST_NEAR(moved.x, 6.0f, 1e-5f);
        TEST_NEAR(moved.y, -1.0f, 1e-5f);
        TEST_NEAR(moved.z, 5.0f, 1e-5f);

        Mat4 singular = Mat4::zero();
        TEST_CHECK(!singular.inverse(inv));
    }

    // ---- 行字面量构造（[[...],[...],[...]] 风格）----
    {
        Mat3 lit{{1.0f, 2.0f, 3.0f},
                 {4.0f, 5.0f, 6.0f},
                 {7.0f, 8.0f, 9.0f}};
        TEST_NEAR(lit(0, 0), 1.0f, 1e-6f);
        TEST_NEAR(lit(0, 2), 3.0f, 1e-6f);
        TEST_NEAR(lit(1, 1), 5.0f, 1e-6f);
        TEST_NEAR(lit(2, 1), 8.0f, 1e-6f);

        // 缺省的行/列补 0
        Mat4 partial{{1.0f, 2.0f, 3.0f, 4.0f}};
        TEST_NEAR(partial(0, 3), 4.0f, 1e-6f);
        TEST_NEAR(partial(1, 0), 0.0f, 1e-6f);
        TEST_NEAR(partial(3, 3), 0.0f, 1e-6f);

        // 与单位阵语义一致：字面量写法可直接当矩阵用
        const Mat3 id{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
        const Vec3 r = id * Vec3(2.0f, 3.0f, 1.0f);
        TEST_NEAR(r.x, 2.0f, 1e-6f);
        TEST_NEAR(r.y, 3.0f, 1e-6f);
    }

    return gfx::test::summary();
}
