# 光栅化着色管线（顶点属性 → 透视校正插值 → 逐像素着色 → 阴影贴图）

> 主题：把"顶点带 uv/法向/切线 → 三角形光栅化 → 逐像素采样贴图 + 光照"这条 **GPU 里真正跑的路**做进 `gfx`，
> 并让 [normal-mapping](../normal-mapping/README.md) 那份纹理/切空间代码在**光栅化**路径上原样复用；
> 再给这条路补上**阴影**（阴影贴图）—— 它同样是"从另一个视角再光栅化一遍"。
> 实现：[shading.hpp](../../../src/include/gfx/render/shading.hpp)、[shadowmap.hpp](../../../src/include/gfx/render/shadowmap.hpp)；设计决策：[adr-0014](../../../docs/design/adr-0014-shaded-raster-pipeline.md)、[adr-0015](../../../docs/design/adr-0015-shadow-map.md)；测试：[test_shading.cpp](../../../tests/test_shading.cpp)、[test_shadowmap.cpp](../../../tests/test_shadowmap.cpp)。

## 学到的概念

1. **顶点属性与片元**：光栅化的本质是"把三角形内部的每个像素的属性**插值**出来"，再用一个**片元着色器**逐像素算颜色。这与光线跟踪（每像素一条光线求交）是两条完全不同的路。
2. **透视校正插值**：屏幕空间里 uv 不能直接线性插值 —— 远处那段会被"压缩"。正确做法是插值 `attr/w` 与 `1/w` 再相除。本演示里立方体侧面的砖缝走向就是靠它才不歪。
3. **深度（NDC z）不需要校正**：z_ndc 是 `1/w` 的仿射函数，在屏幕空间本就线性 —— 所以深度缓冲可以直接用未校正的重心坐标插值。
4. **近平面裁剪**：透视下相机后方的点 `w < 0`，直接除会把它**镜像翻进画面**；必须先按 `w ≥ 近平面` 把三角形裁掉。
5. **切空间要在片元里重建**：插值后的法向与切线不再严格正交，得重新正交化再组 TBN，否则凹凸会歪。
6. **分层**：这个管线不认识"光"和"材质" —— 每像素算什么颜色是**回调**注入的。所以贴图、法线贴图、纯光照都能配同一套管线。
7. **阴影贴图 = "从光源视角再光栅化一遍"**：这条路没有场景求交，所以阴影不能用阴影射线。做法是先用光源的视角把场景**只写深度**地画进一张小图，主 pass 里把每个片元的世界位置投回那个视角比较深度。**复用同一条光栅化管线**，只是换了个 `View`。
8. **自阴影（acne）与两道防线**：被接收的面自己也在贴图里，容差不够就会被判成"被自己挡住"。防线一是**法向偏移**（把采样点抬离自己的面），防线二是**斜率缩放的深度容差**（面与光线越掠射，一个 texel 覆盖的深度范围越大 ∝ tanθ）。只有第一道是不够的：掠射处 `N·L→0`，法向偏移对**沿光方向的深度**几乎没有贡献。
9. **"实时"靠预计算，不是靠把每帧抠得更快**：阴影贴图只依赖"光源 + 场景"、与相机无关，所以交互时把它挪到启动阶段算一次即可（本演示 2 张 1024² 约 13 ms）。找出"不随视角变化的量"并提前算掉，往往比优化逐像素算式更有效。

## 怎么运行

```bat
cmake --build build --target tutorial_shaded_raster
build\tutorials\ShadedRaster                    :: 默认输出 output/shaded-raster.png
build\tutorials\ShadedRaster --help
```

| 参数 | 作用 |
|------|------|
| `--output <png>` | 结果路径（默认 `output/shaded-raster.png`，父目录自动创建）|
| `--size WxH` | 画布尺寸（默认 `800x600`）|
| `--strength s` | 凹凸强度（默认 1.0；**0 = 贴图退化成平整法向**，可用来对照）|
| `--shadow-size n` | 阴影贴图分辨率（默认 `1024`）|
| `--no-shadow` | 关闭阴影贴图（A/B 对照）|
| `--compare` | 额外渲一张"关闭阴影"的图并统计差异像素与**椒盐像素**（自阴影噪点）数，输出 `output/shaded-raster-no-shadow.png` |

### 窗口交互版（实时调相机）

```bat
cmake --build build --target tutorial_shaded_raster_interactive
build\tutorials\ShadedRasterInteractive                 :: 640x480 预览窗口
build\tutorials\ShadedRasterInteractive --shot          :: 不建窗口，直接用初始相机出一张 800x600
build\tutorials\ShadedRasterInteractive --help
```

| 操作 | 作用 |
|------|------|
| 左键拖动 | 转视角（yaw / pitch）|
| 右键拖动 | 平移（沿视线的右 / 上方向）|
| `W` / `S` | 上升 / 下降（世界竖直）|
| `A` / `D` | 左右平移 |
| 滚轮 | 前后推拉 |
| `R` | 相机复位 |
| `1` | 切换**法线贴图**（对照看凹凸到底改变了什么）|
| `2` | 切换**阴影** |
| 空格 | 高分辨率（800×600）渲一张，存 `shaded-raster-interactive.png` |
| `Esc` | 退出 |

**为什么光栅化这条路能实时**（与光线跟踪那条路的关键差别）：

1. 光栅化的代价与"屏幕上有多少像素被覆盖"成正比，而不是"每像素发一条光线求交"；
2. 更关键：**阴影贴图只依赖"光源 + 场景"，与相机无关** —— 所以启动时建一次（2 张 1024² 约 13 ms）就够，
   相机怎么转都没必要重建。于是每帧只剩下主 pass 的光栅化 + 逐像素着色。
3. 只有相机或开关变化时才重渲（脏标记）；不动鼠标时 CPU 基本空闲。

实测（单线程；每像素约 0.27 µs，帧耗时与分辨率近似**线性**）：

| 预览尺寸 | 帧耗时 | 帧率 |
|----------|--------|------|
| 320×240 | ≈ 21 ms | ≈ 47 fps |
| 480×360 | ≈ 47 ms | ≈ 21 fps |
| 640×480（默认）| ≈ 85 ms | ≈ 12 fps |
| 960×720 | ≈ 183 ms | ≈ 5.5 fps |

> 想更顺滑就 `--size 480x360`；反正不动时不出帧，按空格随时能出 800×600 的高分辨率图。

离线版与交互版**共用** [code/shaded_raster_scene.hpp](code/shaded_raster_scene.hpp)：同一份场景、同一份着色代码
（与 [ray-tracing](../../../tutorials/ray-tracing/README.md) 目录里 `ray_tracing_scene.hpp` 的分工一致）。

## 场景

三样东西**全部由三角形拼出来**（没有光线求交）；两个光源**各配一张阴影贴图**：

| 物体 | 三角形数 | 法线贴图 |
|------|----------|----------|
| 地面（6×6 四边形） | 2 | 棋格（`checkerHeight`，按世界尺度平铺）|
| 立方体（6 个面） | 12 | 砖缝（`brickHeight`，每面自带法向与切线）|
| 球（28×44 经纬网格） | 2464 | 波纹（`waveHeight`，切空间与 `normalmap.hpp` 的球面口径一致）|

| 光源 | 位置 | 颜色 / 强度 | 阴影贴图 |
|------|------|-------------|----------|
| 主光 | (3.5, 4.5, 4.0) | 暖白 / 1.10 | 1024²，正交 ±10、深度 0.1~24 |
| 补光 | (−4.0, 2.5, 1.5) | 暖橙 / 0.50 | 同上 |

片元着色：`采样法线贴图 → decodeNormal → 重建 TBN → perturbNormal → shadeLocal(Blinn-Phong，阴影回调查阴影贴图) → gamma(1/2.2)`。

## 结果（实测）

800×600，实跑：

```
  三角形数      : 2478
  着色片元数    : 436550
```

| 对比 | 结果 |
|------|------|
| `--strength 1` vs `--strength 0` | **259715 / 480000 像素不同（54.11%）**，最大通道差 **171** ⇒ 贴图经"采样 → TBN 扰动 → 逐像素光照"确实生效 |
| 开阴影 vs `--no-shadow` | **25555 / 480000 像素不同（5.32%）**，最大通道差 **188**，平均差 3.10 |
| 其中**椒盐像素**（4 邻域都无差异者 = 自阴影 acne 的形态） | **95 个（0.0198%）**，全部落在立方体轮廓上、对比度约 11%（低于可见阈值）|
| **不变式**：阴影图比对照图**更亮**的像素数 | **0** —— 阴影只会减少某个光源的贡献，绝不会增加 |

产物：`output/shaded-raster.png`（`--compare` 时另出 `output/shaded-raster-no-shadow.png`）。

## 管线怎么用（核心 API）

```cpp
View view;                                   // 世界 → 像素（含投影）
view.setViewport(800, 600);
view.lookAt(eye, target, up);
view.setPerspective(fov, aspect, 0.1f, 100.0f);

DepthBuffer depth(800, 600);
ShadingTriangle tri{ {p0, n0, t0, uv0}, {p1, n1, t1, uv1}, {p2, n2, t2, uv2} };

fillTriangleShaded(canvas, view, tri, [&](const Fragment& f) -> math::Vec3 {
    // f.position / f.normal / f.tangent / f.uv 都已完成**透视校正**插值
    return shadeLocal(f.position, f.normal, viewDir(f), material, lights, shadowTest);
}, &depth);
```

阴影贴图（每个光源一张）：

```cpp
// 1) 光源视角：正交投影覆盖整个场景（等效平行光），只写深度
View lightView;
lightView.setViewport(1024, 1024);
lightView.setOrthographic(-10, 10, -10, 10, 0.1f, 24.0f);
lightView.lookAt(light.position, sceneCenter, up);

ShadowMap shadow;
shadow.configure(lightView, 1024);
shadow.setNormalOffset(2.0f * (20.0f / 1024));   // 沿法向抬起 2 个 texel（治自阴影）
shadow.build(allTriangles);                       // 深度 pass：内部再跑一遍 fillTriangleShaded

// 2) 主 pass：片元里查询该光源是否被挡（一张贴图只服务一个光源 ⇒ 多光源要各建一张）
auto shadowTest = [&](const Vec3& p, const Vec3& dirToLight, float) {
    return shadow.isShadowed(p, shadingNormal);   // 用**着色法向**偏移，凹凸引起的自阴影也一并压掉
};
```

## 与光线跟踪那条路的关系

| | 光线跟踪（[ray-tracing](../ray-tracing/README.md)） | 光栅化着色管线（本主题） |
|---|---|---|
| 怎么定颜色 | 每像素发一条光线 **求交**，命中后递归 | 三角形**投影 + 插值**，逐像素跑片元着色器 |
| 法线贴图接入点 | `SurfaceHit::shadingNormal`（只影响光照） | `Fragment` 里的 normal/tangent/uv → 回调里扰动 |
| 阴影 | 阴影光线（真实遮挡，逐像素求交） | **阴影贴图**（光源视角只写深度 pass + 主 pass 深度比较）|
| 共同件 | `Texture`（采样）、`TangentBasis`/`perturbNormal`/`makeNormalMap`、`shadeLocal` —— **两边通用** ||

## 关联文档

- 设计决策：[adr-0014](../../../docs/design/adr-0014-shaded-raster-pipeline.md)（着色管线）、[adr-0015](../../../docs/design/adr-0015-shadow-map.md)（阴影贴图）
- 上游：[adr-0013](../../../docs/design/adr-0013-normal-mapping.md)（纹理与法线贴图）、[adr-0008](../../../docs/design/adr-0008-triangle-raster.md)（三角形光栅化基线）、[adr-0006](../../../docs/design/adr-0006-camera-view.md)（View）
- 测试：[test_shading.cpp](../../../tests/test_shading.cpp)、[test_shadowmap.cpp](../../../tests/test_shadowmap.cpp)
