# ray-tracing-framework-scene —— 用 gfx 复刻课程框架的原始场景，与框架基准图做等价性比对

> **用途**：把老师《作业三》课程框架里**原始 `main.cpp` 构造的场景**（房间 6 面 + 玻璃球 + 绿球 + 蓝立方 + 3 点光源）用 `gfx` 的类型与函数逐项复刻，且**照抄框架 `render_scene` 的相机与像素→射线公式**，渲染出 `hw3_gfx_result.png`，再与"课程框架自己渲染的同一场景"（`hw3_framework_baseline.png`）做**逐像素比对**。
> **本教程的目的就是这次等价性验证**：证明 gfx 的光照 / 递归跟踪 / 求交 / BVH 与课程框架**数值口径一致**（不是"看起来像"，而是逐像素对得上）。
> **目标读者**：想确认"我们自己的库能得到与课程框架一致的结果"的读者。
> **关联文档**：[adr-0011](../../../docs/design/adr-0011-ray-geometry.md)（求交与 BVH）、[adr-0012](../../../docs/design/adr-0012-raytracing-lighting.md)（光照与递归跟踪，"与课程框架的等价性验证"一节）；姊妹教程 [ray-tracing](../ray-tracing/README.md)（用 gfx 自造场景出图）。

> 状态：新增（2026-10-09）。

## 这一节在讲什么

- **同场景**：物体、材质、光源、背景色、分辨率、递归深度**逐项复刻**框架原始 `main.cpp`（清单见下）。
- **同相机**：相机**硬编码在框架 `render_scene` 里**（`camera_pos(0,6,-10)`、`look_at(0,0,10)`、`up(0,1,0)`、`fov=60°`）；本教程**逐字照抄**框架的像素→射线公式生成主光线，而**不**改用 `View::pixelToRay`（虽然示例启动时会顺便自检两者等价，实测最大方向偏差 ≈ 2.5e-7）。
- **同量化**：每像素 **1 采样**（关超采样）、**关伽马**，与框架"原始语义"一致。
- **加速**：把各物体的 AABB 建成 `gfx::Bvh`（无限平面用极大盒，等效于"总是访问"，与框架一致）。

## 老师原始场景的完整参数清单

> 来源：`artifacts/homework3/_verify/backup/main.cpp.bak`（原始 `main.cpp` 的备份）。

**物体（按加入顺序）**（材质未列出的字段取框架 `Material()` 默认：specular/emission=(0,0,0)、shininess=0、reflectivity/transparency=0、refractive_index=1）：

| # | 类型 | 几何 | diffuse | specular | shininess | reflectivity | transparency | refractive_index |
|---|------|------|---------|----------|-----------|--------------|--------------|------------------|
| 1 | plane 地面 | point(0,-5,0), n(0,1,0) | (0.3,0.5,0.2) | — | 16 | — | — | — |
| 2 | plane 天花板 | point(0,15,0), n(0,-1,0) | (0.5,0.5,0.5) | — | — | — | — | — |
| 3 | plane 左墙 | point(-15,0,0), n(1,0,0) | (0.7,0.3,0.3) | — | — | — | — | — |
| 4 | plane 右墙 | point(15,0,0), n(-1,0,0) | (0.3,0.5,0.7) | — | — | — | — | — |
| 5 | plane 前墙 | point(0,0,20), n(0,0,-1) | (0.6,0.1,0.3) | — | — | — | — | — |
| 6 | plane 后墙 | point(0,0,-20), n(0,0,1) | (0.6,0.6,0.1) | — | — | — | — | — |
| 7 | Sphere 玻璃球 | center(0,0,5), r=2 | (0.1,0.1,0.1) | (1,1,1) | 128 | 0.2 | 0.8 | 1.5 |
| 8 | Sphere 绿球 | center(-6,0,8), r=2 | (0.2,0.8,0.2) | (0.5,0.5,0.5) | 64 | 0.3 | — | — |
| 9 | Cube 蓝立方 | min(4,-2,6), max(8,2,10) | (0.2,0.6,0.8) | (0.4,0.4,0.4) | 32 | 0.2 | — | — |

**光源**：

| 位置 | 颜色 | 强度 |
|------|------|------|
| (0,14,5) | (1.0,1.0,1.0) | 1.0 |
| (-10,10,0) | (0.9,0.9,0.7) | 0.7 |
| (10,10,0) | (0.2,0.9,0.9) | 0.7 |

- **背景色**：`Scene()` 默认 `(0.1, 0.1, 0.1)`（原始 `main.cpp` 未覆写）。
- **渲染分辨率**：800×600；**递归深度** `max_ray_depth = 5`。
- **`main()` 调用**：`render_scene(main_scene, output_image, 800, 600, 5)`。
- **相机**（硬编码在框架 `render_scene`）：`camera_pos(0,6,-10)`、`look_at(0,0,10)`、`up(0,1,0)`、`fov=60°`；`aspect = W/H`；`scale = tan(fov/2)`；
  - `pixel_x = (2*(x+0.5)/width - 1) * aspect * scale`
  - `pixel_y = (1 - 2*(y+0.5)/height) * scale`
  - `dir = normalize(forward + right*pixel_x + actual_up*pixel_y)`，其中 `forward=normalize(look_at-camera_pos)`、`right=normalize(forward×up)`、`actual_up=right×forward`。

## 怎么运行

```bat
:: 1) 构建（产物 build/tutorials/Hw3SceneTrace.exe）
cmake --build build --target tutorial_hw3_scene

:: 2) 渲染 gfx 复刻图
build\tutorials\Hw3SceneTrace.exe --output tutorials\ray-tracing-framework-scene\output\hw3_gfx_result.png
```

参数：`--output <png>`（默认 `output/hw3_gfx_result.png`，父目录自动创建）、`--help`。分辨率 / 深度 / 采样 / 伽马均**固定**为框架原始口径，故不提供开关。

> 无参数直接双击运行时按默认设置渲染，结束会打印输出路径并停在"按回车键退出"（仅当 stdin 是终端才停，脚本/管道调用不受影响）。
> 渲染过程每 50 行打印一次进度百分比（与课程框架 `render_scene` 的做法一致）；Windows 下启动时会把控制台输出切到 UTF-8，保证中文不乱码。

### 生成"课程框架基准图"（需要 MSVC + OpenCV，属临时验证工具）

基准图由临时驱动 `artifacts/homework3/_verify/baseline.cpp` 生成：它 include 框架 `RayTracer.h`、构造与上面清单**完全一样**的 `Scene`、把 `g_samples_per_axis=1`、`g_use_gamma=false`，调用框架 `render_scene(scene, image, 800, 600, 5)` 并用 `imwrite` 存成 `hw3_framework_baseline.png`。

```bat
:: 在 vcvars64 环境、仓库根目录下：
cl /EHsc /O2 /MD /std:c++17 /I artifacts\homework3\include /I artifacts\homework3\HW3 ^
   artifacts\homework3\_verify\baseline.cpp artifacts\homework3\HW3\RayTracer.cpp ^
   /link /LIBPATH:artifacts\homework3\lib opencv_world4120.lib ^
   /out:artifacts\homework3\_verify\baseline.exe
:: 运行需 opencv_world4120.dll 在 PATH 或同目录（在 artifacts\homework3\bin）
artifacts\homework3\_verify\baseline.exe artifacts\homework3\_verify\hw3_framework_baseline.png
```

### 逐像素比对

比对工具 `artifacts/homework3/_verify/compare.cpp`（同样 MSVC + OpenCV）读取两张 PNG，输出统计、8×8 网格差异计数、R↔B 交换检查，并写差异放大图（×5）。

```bat
artifacts\homework3\_verify\compare.exe ^
   artifacts\homework3\_verify\hw3_framework_baseline.png ^
   tutorials\ray-tracing-framework-scene\output\hw3_gfx_result.png ^
   artifacts\homework3\_verify\hw3_diff_x5.png
```

## 结果（实测）

- 基准图：`artifacts/homework3/_verify/hw3_framework_baseline.png`（800×600，200007 字节）。
- gfx 复刻图：`tutorials/ray-tracing-framework-scene/output/hw3_gfx_result.png`（800×600，1440773 字节）。
- 差异放大图（×5）：`artifacts/homework3/_verify/hw3_diff_x5.png`（800×600，2578 字节）。

原始统计输出（`compare.exe`）：

```
total pixels                       : 480000
identical pixels (all 3 channels)  : 479979  (99.9956%)
differing pixels                   : 21  (0.0044%)
max channel diff (0..255)          : 1
max per-pixel channel diff         : 1
mean channel diff over ALL ch      : 0.000015  (0.000015 gray levels)
mean per-pixel max diff (diff px)  : 1.000000
differing channels                 : 21 / 1440000 (0.0015%)
identical after R<->B swap         : 653  (0.1360%)  [channel-order check]

---- 8x8 grid: differing pixels per cell (cell = 100x75 px) ----
row 0: 0 0 0 0 0 0 0 0
row 1: 0 0 0 0 1 0 0 0
row 2: 0 0 0 0 0 0 0 0
row 3: 0 0 0 0 0 4 2 1
row 4: 0 0 0 2 2 4 2 1
row 5: 0 0 0 1 0 0 0 0
row 6: 0 0 0 0 0 0 0 0
row 7: 1 0 0 0 0 0 0 0
```

## 差异原因分析

- **通道顺序（BGR/RGB）**：**没有问题**。框架写图时把颜色排成 `Vec3f(color[2],color[1],color[0])` 交给 OpenCV（OpenCV 的 Mat 内存按 BGR，`imwrite` 会再转成标准 RGB PNG），我们 `savePng` 直接写 RGB。二者最终 PNG 的**显示颜色一致**——实测"原样比"相同像素 99.9956%，而"R↔B 交换后比"只有 0.136%，说明通道顺序**正确、无串色**。
- **相机公式**：框架的像素→方向公式与 `View::lookAt + setPerspective(60°, aspect, ...) + pixelToRay` **等价**（示例启动自检：25px 抽样下最大方向偏差 ≈ **2.46e-7**，属 float 舍入）。本教程仍**照抄框架公式**以杜绝任何几何来源的偏差。
- **仅 21 个像素差 1 灰阶**：这些是**允许的量化差异**，非 bug。来源：
  1. `float` 舍入——`Eigen::Vector3f::normalized()`（框架）与 `gfx::math::normalize`（我们）在"归一化方向 / 交点"上的末位不同；
  2. 8bit 取整方式——框架 `convertTo(..., 255.0)` 用 OpenCV 的 **round-half-to-even**，我们 `RayTracer` 用 `std::lround`（**round-half-away-from-zero**），在 `c*255` 恰落 `.5` 附近时会差 1；
  3. 求交容差——框架平面/球/盒以 `t > 0.001`、平面平行阈值 `|denom| > 1e-4` 判定；gfx 的 `intersectPlane/intersectSphere` 传 `tMin=0.001` 对齐了距离阈值，但平面**平行阈值**为 `1e-8`（更宽松）——在掠射角下理论上可能有极个别像素翻转（本次未观察到超过 1 灰阶的差异）。
- **差异分布**：21 个差异像素集中在画面**中部偏右**（8×8 网格第 3–4 行、第 3–7 列），正是**玻璃球 / 立方体的轮廓与"接触"边缘**一带——轮廓像素对射线方向的末位误差最敏感，符合预期。
- 结论：**未发现任何真 bug**（无通道串色、无相机公式不等价、无材质/参数搬错）；差异完全是浮点末位与 8bit 取整带来的、可接受的量化噪声，**最大 1 灰阶、占比 0.0044%**。

## 与作业三的关系

框架 `trace_ray` 与 gfx `RayTracer::trace` 的口径**逐条对齐**（法向定向、自交偏移 `±N·1e-4`、环境光 `0.05·diffuse`、Lambert 漫反射、Blinn-Phong 高光、`R=reflect(d,N)`、`eta=inside?n:1/n` 与全反射、`ks+kt<1e-3` 早退、末尾 `clamp(0,1)`）；递归深度口径在本场景（`maxDepth=5`）下二者调用序列完全一致（最深的调用都停在"只算局部光照"那一层）。本教程把这份"对得上"从**数值单点**（见 `tests/test_raytracer.cpp`）推进到了**整张图的逐像素**。
