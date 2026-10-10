// render.hpp —— gfx::render 汇总头（一次引入全部渲染设施）
//
// @brief 方便调用方一次拿全光栅化/深度/相机/图像输出/窗口等渲染类型。
//
// 用途：render 层（渲染）统一入口。光栅化在 raster.hpp / triangle.hpp；深度缓冲在 depth.hpp；
//       相机/视图在 view.hpp；PNG 输出在 png.hpp；窗口上屏在 window.hpp；
//       光照模型在 lighting.hpp；递归光线跟踪在 raytracer.hpp。
// 分层语义：render = 渲染（光栅化、深度、相机、图像输出、窗口、光照、光线跟踪）；
//           本层依赖 core/（画布/颜色）与 math/，并**单向**依赖 geom/（光线跟踪复用 geom 的
//           命中记录与求交，见 raytracer.hpp）—— geom 仍不得反向依赖 render。
// 目标读者：调用方与内部模块。
// 关联文档：docs/design/adr-0005-raster-polygon-fill.md；docs/design/adr-0008-triangle-raster.md；docs/design/adr-0012-raytracing-lighting.md
//
// 状态：新增（2026-09-30）。头文件按职责分层后的 render 层总入口；2026-10-09 增补光照/光线跟踪。

#ifndef GFX_RENDER_RENDER_HPP
#define GFX_RENDER_RENDER_HPP

#include "gfx/render/raster.hpp"   // 画线（Bresenham/DDA/Naive）+ 扫描线多边形填充
#include "gfx/render/triangle.hpp" // 三角形光栅化（采样/重心/抗锯齿）
#include "gfx/render/depth.hpp"    // 深度缓冲（Z-Buffer）
#include "gfx/render/view.hpp"     // 相机/视图（世界 ↔ 像素映射）
#include "gfx/render/png.hpp"      // PNG 图像输出
#include "gfx/render/window.hpp"   // 原生窗口（Canvas 上屏）
#include "gfx/render/lighting.hpp" // 光照模型（材质 / 点光源 / 局部光照 + 阴影回调）
#include "gfx/render/raytracer.hpp"// 递归光线跟踪器（反射 / 折射 / 阴影 / 超采样）

#endif // GFX_RENDER_RENDER_HPP
