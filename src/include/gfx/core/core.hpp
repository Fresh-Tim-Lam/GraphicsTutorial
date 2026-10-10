// core.hpp —— gfx::core 汇总头（一次引入全部基础设施）
//
// @brief 方便调用方一次拿全画布/颜色/版本/导出宏等基础设施类型与宏。
//
// 用途：core 层（基础设施）统一入口。像素帧缓冲与颜色在 canvas.hpp / color.hpp；
//       版本事实源在 version.hpp；动态库导出宏在 gfx_export.hpp。
// 分层语义：core = 基础设施（画布 / 颜色 / 版本 / 导出宏），不依赖 geom / render；
//           math 是独立的数学基础层，core 亦不依赖 math。
// 目标读者：调用方与内部模块。
// 关联文档：docs/design/adr-0001-gfx-math-canvas.md；docs/conventions/version.md
//
// 状态：新增（2026-09-30）。头文件按职责分层后的 core 层总入口。

#ifndef GFX_CORE_CORE_HPP
#define GFX_CORE_CORE_HPP

#include "gfx/core/color.hpp"      // Color（RGBA8）+ rgb()/rgba()/blend()
#include "gfx/core/canvas.hpp"     // Canvas 离屏帧缓冲（putPixel/blendPixel/data）
#include "gfx/core/version.hpp"    // 版本唯一事实源（GFX_VERSION_* 宏）
#include "gfx/core/gfx_export.hpp" // GFX_API 动态库导出/导入宏

#endif // GFX_CORE_CORE_HPP
