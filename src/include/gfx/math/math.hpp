// math.hpp —— gfx::math 汇总头（一次引入全部数学类型与别名）
//
// @brief 方便调用方一次拿全 vec/mat 模板（含 vec2/3/4、mat2/3/4 别名）与批量变换接口。
//
// 用途：统一入口。模板与别名在 vec.hpp / mat.hpp；批量变换见 batch.hpp。
// 目标读者：调用方与内部模块。
// 关联文档：docs/design/adr-0002-template-math.md；docs/design/adr-0003-batch-backend.md
//
// 状态：生效中（2026-09-09）。形状别名已并入 vec.hpp / mat.hpp（整合，去冗余头）。

#ifndef GFX_MATH_MATH_HPP
#define GFX_MATH_MATH_HPP

#include "vec.hpp"       // Vec<N> + vec2/vec3/vec4
#include "mat.hpp"       // Mat<N> + mat2/mat3/mat4
#include "transform.hpp" // transform2d / transform3d（平移/缩放/旋转/翻转）
#include "batch.hpp"     // 批量变换 + 后端钩子

#endif // GFX_MATH_MATH_HPP
