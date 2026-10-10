// geom.hpp —— gfx::geom 汇总头（一次引入全部几何与建模设施）
//
// @brief 方便调用方一次拿全点/点批变换、半边 B-rep 拓扑与光线求交/加速结构。
//
// 用途：geom 层（几何与建模）统一入口。点在 point.hpp；半边结构在 halfedge.hpp；
//       光线求交在 ray.hpp；BVH 在 bvh.hpp。
// 分层语义：geom = 几何与建模（点、半边 B-rep 拓扑、光线求交/BVH）。本层**只依赖 math/**，
//           与 render/ **零耦合** —— 这是刻意保留的界线，将来可把本层单独拆库。
// 目标读者：调用方与内部模块。
// 关联文档：docs/design/adr-0004-transform-chain.md；adr-0009-halfedge-euler-ops.md；adr-0011-ray-geometry.md
//
// 状态：新增（2026-09-30）。头文件按职责分层后的 geom 层总入口。

#ifndef GFX_GEOM_GEOM_HPP
#define GFX_GEOM_GEOM_HPP

#include "gfx/geom/point.hpp"    // Point2D/3D + PointBatch2D/3D（apply 变换）
#include "gfx/geom/halfedge.hpp" // HalfEdgeMesh（半边结构 + 欧拉操作）
#include "gfx/geom/ray.hpp"      // Ray / Aabb / HitRecord + 球/平面/三角形/圆柱/AABB 求交
#include "gfx/geom/bvh.hpp"      // Bvh（层次包围盒 + 最近命中剪枝）

#endif // GFX_GEOM_GEOM_HPP
