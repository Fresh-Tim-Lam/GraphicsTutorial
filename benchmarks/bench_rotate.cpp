// bench_rotate.cpp —— gfx_math 变换构造微基准（正式基准，可复现）
//
// 用途：测量 transform3d 常见构造函数的单次调用开销（ns/call），为"矩阵构造"性能规范
//       （docs/conventions/performance.md）提供可复现依据。
// 构建/运行：
//   g++ -O2 -std=c++17 -I src/include benchmarks/bench_rotate.cpp -o benchmarks/bench_rotate.exe
//   benchmarks\bench_rotate.exe
//
// 基线（GCC 14.2 / MinGW-w64 / -O2 / 5,000,000 次，2026-09-09）：
//   rotate(rad, axis)            42.8 ns   （委托给三参数重载，center = 原点）
//   rotate(rad, axis, center)    42.8 ns   ← 纯展开 + 具名局部（唯一的轴角旋转实现）
//   rotateViaPrimitives          70~75 ns  （组合版：两次 4×4 矩阵乘）
//   rotateViaBasis               67.4 ns   （换基版：两次 4×4 矩阵乘）
// 对照：改造前 rotate(rad, axis, center) 为 49.5 ns（先调用 rotate() 再二次组装矩阵，即多构造一个 Mat4）。
// 注：单轮内比较可靠；跨轮次绝对值有 ~5% 波动（组合版两项尤其明显）。

#include <gfx/math/math.hpp>

#include <chrono>
#include <cstdio>

using namespace gfx::math;

// 防止结果被优化器消除
volatile float g_sink = 0.0f;

template <typename F>
double bench(const char* name, int iters, F f) {
    float acc = 0.0f;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; ++i) {
        const float ang = 0.001f * static_cast<float>(i);   // 逐次变角度，避免循环不变量外提
        const Mat4 m = f(ang);
        acc += m(0, 3) + m(1, 1) + m(2, 2) + m(2, 3);
    }
    const auto t1 = std::chrono::steady_clock::now();
    g_sink = acc;
    const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / iters;
    std::printf("%-30s %8.2f ns/call\n", name, ns);
    return ns;
}

int main() {
    const int iters = 5000000;
    const Vec3 axis(1.0f, 2.0f, 3.0f);
    const Vec3 center(0.5f, -1.0f, 2.0f);

    std::printf("iters = %d, -O2\n\n", iters);
    bench("rotate(rad, axis)", iters, [&](float g) { return transform3d::rotate(g, axis); });
    bench("rotate(rad, axis, center)", iters, [&](float g) { return transform3d::rotate(g, axis, center); });
    bench("rotateViaPrimitives", iters, [&](float g) { return transform3d::rotateViaPrimitives(g, axis, center); });
    bench("rotateViaBasis", iters, [&](float g) { return transform3d::rotateViaBasis(g, axis); });
    return 0;
}
