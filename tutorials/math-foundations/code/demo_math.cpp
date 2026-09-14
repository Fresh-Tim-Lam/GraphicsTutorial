// demo_math.cpp —— 教学示例：向量与矩阵的直观演示
//
// 编译（项目内）：cmake --build build --target tutorial_math 后运行
// 产物：build/tutorials/tutorial_math(.exe)
// 内容：用库内变换矩阵（transform2d）验证 2D 齐次变换：先旋转 90°，再平移到 (10,20)
//
// 注：变换矩阵的权威实现在 src/include/gfx/math/transform.hpp（本文件不重复实现）。

#include <cstdio>
#include <gfx/math/math.hpp>

using namespace gfx::math;

int main() {
    // 先旋转后平移：M = T * R（矩阵乘法顺序：先作用右边）
    const float pi = 3.14159265358979f;
    Mat3 transform = transform2d::translate(Vec2(10.0f, 20.0f))
                   * transform2d::rotate(pi / 2.0f);

    Vec3 point(1.0f, 0.0f, 1.0f);   // 单位向量上的点（齐次坐标 w=1）
    Vec3 out = transform * point;

    std::printf("(1,0) 旋转90度并平移(10,20) => (%.3f, %.3f)\n", out.x, out.y);

    // 逆变换应还原
    Mat3 inv;
    if (transform.inverse(inv)) {
        Vec3 back = inv * out;
        std::printf("逆变换还原 => (%.3f, %.3f)\n", back.x, back.y);
    }

    // 翻转演示（左右镜像）
    Vec3 flipped = transform2d::flipX() * Vec3(3.0f, 1.0f, 1.0f);
    std::printf("(3,1) 左右翻转 => (%.3f, %.3f)\n", flipped.x, flipped.y);
    return 0;
}
