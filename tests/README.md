# tests/ — 正式测试

> **用途**：存放 `gfx` API 的单元测试。
> **目标读者**：编写测试的开发者与 AI Agent。
> **关联文档**：[src/](../src/README.md)；[docs/guides/operations.md](../docs/guides/operations.md)（运行命令）；[adr-0001](../docs/design/adr-0001-gfx-math-canvas.md)（测试框架决策 D4）。

> 状态：生效中（核验于 2026-09-09）。测试框架已定：**自写轻量断言**（[gfx_test.hpp](gfx_test.hpp)，零第三方依赖）。

## 测试列表

| 测试程序 | 内容 | 目标 |
|---------|------|------|
| test_math | vec/mat 2D+3D 核心运算（长度/点积/叉积/行列式/逆/齐次变换） | gfx_math |
| test_canvas | Canvas 帧缓冲（putPixel/越界忽略/clear/data 布局） | Canvas（离屏） |
| test_batch | 批量变换正确性 + 后端钩子注册/恢复 | gfx_math（batch） |
| test_transform | 常用变换矩阵：平移/缩放/旋转/翻转（2D/3D）+ 组合顺序 | gfx_math（transform） |
| test_chain | 变换链 Transform&lt;N&gt;：折叠等价、组合语义、批点（连续/步长）、原地 | gfx_math（transform） |
| test_point | 几何层：Point2D/3D、PointBatch2D/3D 的 apply 变换、视图、原地 | gfx（point.hpp） |
| test_raster | 光栅化：画线（Naive/DDA/Bresenham）+ 扫描线多边形填充 | gfx（raster.hpp） |
| test_png | PNG 编码器（CRC32/Adler32/块结构） | gfx（png.hpp） |
| test_view | 相机/视图：世界↔像素映射、fit 语义、3D 观察与投影 | gfx（view.hpp） |
| test_triangle | 三角形光栅化：边界规则、重心坐标、抗锯齿、深度测试 | gfx（triangle.hpp） |
| test_halfedge | 半边结构 + 欧拉操作（MVFS/MEV/MEF/KEMR/KFMRH） | gfx（halfedge.hpp） |
| test_ray | 光线求交：反射/折射 + 球/平面/三角形/圆柱/AABB + BVH 一致性 | gfx（ray.hpp/bvh.hpp） |
| test_raytracer | 光照与递归跟踪：局部光照/阴影/反射/折射/深度/超采样 + 作业三数值对齐 | gfx（lighting.hpp/raytracer.hpp） |
| test_normalmap | 纹理与法线贴图：三种环绕索引、最近邻/双线性采样、decodeNormal、高度场→法线图（含与解析斜率对齐、strength=0 退化）、球/平面切空间正交性与右手系、perturbNormal、跟踪器的着色法向接口 | gfx（texture.hpp/normalmap.hpp/raytracer.hpp） |
| test_shading | 光栅化着色管线：覆盖与 flat 光栅化器逐一一致、正交⇒仿射插值、**透视校正重投影不变式**、深度测试（乱序一致）、近平面裁剪（跨界仍满足不变式 + 整块在后方不绘制）、属性透传 | gfx（shading.hpp/triangle.hpp/depth.hpp/view.hpp） |

## 约定

- 断言宏：`TEST_CHECK(expr)`、`TEST_NEAR(a, b, eps)`；测试程序 `main` 返回 `gfx::test::summary()`。
- 需要 GUI/像素上屏验证的演示放 tutorials/code（不属自动测试）；Canvas 纯内存逻辑才做自动测试。
- 运行：`ctest --test-dir build -C Release --output-on-failure`（或单测 `-R test_math`）。
