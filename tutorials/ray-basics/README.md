# ray-basics —— 光线求交地基：法向可视化（无光照）

> **用途**：演示 `gfx` 的**纯几何**光线求交能力（[`gfx/geom/ray.hpp`](../../../src/include/gfx/geom/ray.hpp) + [`gfx/geom/bvh.hpp`](../../../src/include/gfx/geom/bvh.hpp)）：为每个像素射出一条主光线，在"球 / 三角形 / 圆柱 / 无限平面"场景里求**最近命中**，把命中点法向直接映射成 RGB 并保存 PNG。
> **目标读者**：学习"从 0 造轮子做光线跟踪"的读者。
> **关联文档**：[adr-0011](../../../docs/design/adr-0011-ray-geometry.md)（光线求交与 BVH 的决策）；权威实现 [ray.hpp](../../../src/include/gfx/geom/ray.hpp)、[bvh.hpp](../../../src/include/gfx/geom/bvh.hpp)。

> 状态：新增（2026-10-08）。

## 这一节在讲什么

- **只做地基**：本示例**只用求交**——命中 → 取交点/法向 → 上色；**不含光照模型、不含递归跟踪**（反射/折射/阴影留待后续阶段）。这正是"光线跟踪"最底层的骨架。
- **法向即颜色**：把单位法向 `n` 的 `(x,y,z)` 从 `[-1,1]` 线性映射到 `[0,255]` 当作 RGB。球面呈现平滑三色渐变，平面/三角形为纯色，圆柱侧面与顶盖颜色清晰可分——一眼看出"法向朝外且正确"。
- **BVH 解耦**：场景里每个图元的 AABB 交给 `gfx::Bvh`；BVH 只做"哪些叶子可能与光线相交"，**精确求交由回调按图元索引自行完成**——这就是 `Bvh` 与具体图元类型解耦的用法。

## 怎么运行

```bat
:: 1) 构建（产物 build/tutorials/RayBasics.exe）
cmake --build build --target tutorial_ray_basics

:: 2) 渲染并保存 PNG（父目录会自动创建）
build\tutorials\RayBasics.exe --output tutorials\ray-basics\output\ray-basics.png
```

参数：

- `--output <png>`：结果 PNG 路径（默认 `output/ray-basics.png`）。
- `--size WxH`：画布尺寸（默认 `800x600`）。
- `--help`：显示帮助。

运行后在控制台打印**真实统计**（分辨率、命中像素占比、每像素平均 BVH 回调次数 / 节点访问次数，可与"暴力法 5 次/像素"对比），产物为 `output/ray-basics.png`。

## 结果

`output/ray-basics.png`：800×600 的法向可视化图——地面（法向 `(0,1,0)` → 绿色）、三颗球（各向渐变的彩色球）、背景三角形背板与一根圆柱；背景为深色。全程无光照，颜色只承载**法向信息**。

## 与后续阶段的关系

本阶段交付"命中什么、命中在哪、法向朝哪"；下一阶段（光照与递归）会直接复用：
`gfx::math::reflect` / `gfx::math::refract`（[vec.hpp](../../../src/include/gfx/math/vec.hpp)）已就位，命中的 `HitRecord` 已带交点与朝外法向，可直接用于漫反射/镜面/折射着色与二次光线。
