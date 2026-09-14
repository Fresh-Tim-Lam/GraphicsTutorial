// batch.hpp —— gfx::math 批量运算与后端钩子（GPU 切换预留点）
//
// @file 定义批量变换入口与"后端分派点"：当前内置 CPU 实现；未来接入 CUDA/自研
//       GPU 后端时，通过注册一个后端函数指针即可切换，不改调用方代码。
//
// 用途：对一批点/向量做同一矩阵变换（矩阵-点批量），并预留 GPU 后端注入接口。
// 目标读者：需要批量变换（大量顶点/点）或接入 GPU 后端的开发者与 AI Agent。
// 关联文档：docs/design/adr-0003-batch-backend.md；docs/conventions/api-docs.md；
//           mat.hpp（矩阵乘法为何不单点上 GPU）
//
// 状态：生效中（2026-09-09）。ADR-0003（✅ 采纳）：仅 CPU + 钩子，零第三方依赖。
//
// 布局说明：Vec<N> 恒为 4 个 float 连续存储（未用分量恒 0，见 vec.hpp / ADR-0002），
// 故批量数据流按"每点 4 个 float"对齐（GPU/SIMD 友好）；仅前 N 分量参与变换。

#ifndef GFX_MATH_BATCH_HPP
#define GFX_MATH_BATCH_HPP

#include "mat.hpp"

#include <cstddef>

namespace gfx::math {

// 批量变换后端函数签名：
// 对 count 个点执行 v' = m * v（逐点 4 个 float，仅前 N 分量有意义，其余应置 0）。
// @param m     N×N 行主序矩阵数据首地址（长度 N*N）
// @param in    输入点数组（长度 count，每点连续 4 个 float）
// @param out   输出点数组（长度 count，每点连续 4 个 float；可与 in 重叠做原地变换）
// @param count 点的数量
// @note 这是"后端分派点"：任何实现（CPU 内置 / 未来 CUDA / 自研）都以此签名接入。
template <int N>
using BatchTransformFn = void (*)(const float* m, const float* in, float* out, std::size_t count);

namespace detail {

// 内置 CPU 后端：逐点朴素矩阵乘向量。
// @note 教学库 CPU 实现即可；真正的性能点在"大批量/SIMD/GPU"，不在此微调（见 ADR-0002/0003）。
// @note 先把源点读到局部变量再写出，保证 in == out（原地）时结果正确。
template <int N>
void cpuBatchTransform(const float* m, const float* in, float* out, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        float src[4];
        for (int c = 0; c < 4; ++c) src[c] = in[i * 4 + c];
        float* q = out + i * 4;
        for (int c = 0; c < 4; ++c) q[c] = 0.0f;      // 对齐填充分量清零
        for (int r = 0; r < N; ++r) {
            float acc = 0.0f;
            for (int k = 0; k < N; ++k) acc += m[r * N + k] * src[k];
            q[r] = acc;
        }
    }
}

// 当前后端（默认 = CPU）。注册即替换（置空则恢复 CPU 默认）。
// 通过模板维度 N 各自维护，互不影响。
template <int N>
inline BatchTransformFn<N>& currentBatchFn() {
    static BatchTransformFn<N> fn = &cpuBatchTransform<N>;
    return fn;
}

} // namespace detail

// 注册批量变换后端：fn 为空指针时恢复内置 CPU 默认。
template <int N>
inline void setBatchTransformBackend(BatchTransformFn<N> fn) {
    detail::currentBatchFn<N>() = fn ? fn : &detail::cpuBatchTransform<N>;
}

// 查询当前后端（用于调试/教学展示；返回空表示 CPU 默认）。
template <int N>
inline BatchTransformFn<N> batchTransformBackend() {
    return detail::currentBatchFn<N>();
}

// 恢复 CPU 默认后端
template <int N>
inline void resetBatchTransformBackend() {
    setBatchTransformBackend<N>(nullptr);
}

// 批量变换（面向 Vec<N> 数组的便捷重载）：
// 对 points[0..count) 执行 v' = m * v，写入 out。
// @note 调用方视角固定，切换 GPU 不影响此接口。
template <int N>
inline void batchTransform(const Mat<N>& m, const Vec<N>* in, Vec<N>* out, std::size_t count) {
    detail::currentBatchFn<N>()(m.data,
                                reinterpret_cast<const float*>(in),
                                reinterpret_cast<float*>(out),
                                count);
}

} // namespace gfx::math

#endif // GFX_MATH_BATCH_HPP
