// bench_point_apply.cpp —— 点变换的两条消费路径对比（正式基准，可复现）
//
// 用途：回答"流式链 `p.rotate().translate()`" vs "先建变换再 `t.apply(p)`"谁更快，
//       为点/点批 API 设计提供依据（见 docs/conventions/performance.md）。
// 构建/运行：
//   g++ -O2 -std=c++17 -I src/include benchmarks/bench_point_apply.cpp -o benchmarks/bench_point_apply.exe
//   benchmarks\bench_point_apply.exe
//
// 基线（GCC 14.2 / MinGW-w64 / -O2 / N = 100,000 点 / 变换 3 步，2026-09-09）：
//   A  逐点 t.apply(p)               5.20 ns/点   （1.00x；折叠惰性缓存后 = 1 次 mat-vec）
//   B  折叠一次 + 逐点 mat-vec        5.00 ns/点   （0.96x，与 A 等价）
//   C  批量 t.apply(in,out,count)     3.60 ns/点   （0.69x，最快）
//   D  逐点流式链（每点新建链）     110.07 ns/点   （21.2x，反模式：每点构造链 = 堆分配）
//   E  逐点 p.apply(t)                8.97 ns/点   （1.72x；已消除链复制，原为 46.3）
//   F  逐点立即求值 T*R*S*p          38.86 ns/点   （7.47x，每次重建矩阵）
// 历史对照（同一机器，改造前）：
//   A 27.18（链逐次 mat-vec）→ 5.20（惰性折叠缓存）；E 46.30（复制链）→ 8.97（不复制）。
// 结论：
//   ① 链只记录、不计算；**折叠结果惰性缓存** ⇒ 逐点 apply 从 k 次 mat-vec 降为 1 次（快 5x）；
//   ② 消费点决定策略：批量接口（C）最快；单点/复用（A/B）等价；每次重建矩阵（F）最慢；
//   ③ "每点新建一条链"（D）是反模式——链应挂在变换对象上被多点复用。

#include <gfx/math/math.hpp>
#include <gfx/geom/point.hpp>

#include <chrono>
#include <cstdio>
#include <vector>

using namespace gfx;
using namespace gfx::math;

volatile float g_sink = 0.0f;

template <typename F>
double timePerPoint(int n, F f) {
    const auto t0 = std::chrono::steady_clock::now();
    const float acc = f();
    const auto t1 = std::chrono::steady_clock::now();
    g_sink = acc;
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / n;
}

int main() {
    const int n = 100000;
    std::vector<Vec3> pts(n);
    std::vector<Vec3> out(n);
    for (int i = 0; i < n; ++i) pts[i] = Vec3(static_cast<float>(i % 97), 1.0f, 2.0f);

    // 一个可复用的变换（3 步：平移 → 绕 Y 转 → 缩放）
    Transform<4> t;
    t.then(transform3d::translate(Vec3(5.0f, -3.0f, 1.0f)))
     .then(transform3d::rotate(0.7f, Axis::Y))
     .then(transform3d::scale(2.0f));

    std::printf("N = %d 点，变换 3 步（-O2）\n\n", n);

    // A：先建变换，再逐点 t.apply(p)（用户主张的写法）
    const double a = timePerPoint(n, [&] {
        float acc = 0.0f;
        for (int i = 0; i < n; ++i) {
            const Vec4 h = t.apply(Vec4(pts[i].x, pts[i].y, pts[i].z, 1.0f));
            out[i] = Vec3(h.x, h.y, h.z);
            acc += out[i].x;
        }
        return acc;
    });
    std::printf("A  逐点 t.apply(p)            %8.2f ns/点\n", a);

    // B：折叠一次 + 逐点 mat-vec（复用变换的最优路径）
    const double b = timePerPoint(n, [&] {
        const Mat4 m = t.matrix();
        float acc = 0.0f;
        for (int i = 0; i < n; ++i) {
            const Vec4 h = m * Vec4(pts[i].x, pts[i].y, pts[i].z, 1.0f);
            out[i] = Vec3(h.x, h.y, h.z);
            acc += out[i].x;
        }
        return acc;
    });
    std::printf("B  折叠一次 + 逐点 mat-vec    %8.2f ns/点\n", b);

    // C：批量接口（内部折叠 + batchTransform 后端钩子）
    const double c = timePerPoint(n, [&] {
        float acc = 0.0f;
        for (int i = 0; i < n; ++i) pts[i].w = 1.0f;   // 齐次注入（模拟真实使用）
        t.apply(reinterpret_cast<const float*>(pts.data()), reinterpret_cast<float*>(out.data()),
                n, sizeof(Vec3));
        for (int i = 0; i < n; ++i) acc += out[i].x;
        return acc;
    });
    std::printf("C  批量 t.apply(in,out,count) %8.2f ns/点\n", c);

    // D：对照——每个点各自"流式链"（Point3D 自带 Transform 状态：逐点构造/累积/求值）
    const double d = timePerPoint(n, [&] {
        float acc = 0.0f;
        for (int i = 0; i < n; ++i) {
            gfx::Point3D p(pts[i].x, pts[i].y, pts[i].z);
            const Vec3 v = p.translate(5.0f, -3.0f, 1.0f).rotate(0.7f, Axis::Y).scale(2.0f).value();
            acc += v.x;
        }
        return acc;
    });
    std::printf("D  逐点流式链（每点自带链）  %8.2f ns/点\n", d);

    // E：当前 Point3D::apply(t) —— 实现里 eff = t_; eff.then(t) 会复制链（含堆分配）
    const double e = timePerPoint(n, [&] {
        float acc = 0.0f;
        for (int i = 0; i < n; ++i) {
            gfx::Point3D p(pts[i].x, pts[i].y, pts[i].z);
            const Vec3 v = p.apply(t);
            acc += v.x;
        }
        return acc;
    });
    std::printf("E  逐点 p.apply(t)（含链复制） %8.2f ns/点\n", e);

    // F：立即求值（无链）：每点重算 T*R*S 再乘点（"链"要替代的写法）
    const double f = timePerPoint(n, [&] {
        float acc = 0.0f;
        for (int i = 0; i < n; ++i) {
            const Mat4 m = transform3d::translate(Vec3(5.0f, -3.0f, 1.0f)) *
                           transform3d::rotate(0.7f, Axis::Y) * transform3d::scale(2.0f);
            const Vec4 h = m * Vec4(pts[i].x, pts[i].y, pts[i].z, 1.0f);
            acc += h.x;
        }
        return acc;
    });
    std::printf("F  逐点立即求值 T*R*S*p     %8.2f ns/点\n", f);

    std::printf("\n相对 A：B %.2fx  C %.2fx  D %.2fx  E %.2fx  F %.2fx\n",
                b / a, c / a, d / a, e / a, f / a);
    return 0;
}
