# 光栅化着色管线（顶点属性 → 透视校正插值 → 逐像素着色 → 程序化贴图 → 阴影贴图）

> 主题：把"顶点带 uv/法向/切线 → 三角形光栅化 → 逐像素采样贴图 + 光照"这条 **GPU 里真正跑的路**做进 `gfx`：
> 颜色贴图（albedo）+ 法线贴图（TBN 扰动）+ **阴影贴图**，而且**贴图全部程序化生成，不依赖任何外部图片**。
> 场景是一间开着顶的"展厅"：瓷砖地面 + 四面**不同颜色**的砖墙 + 碳纤维立方体 + 大理石球。
> 实现：[shading.hpp](../../../src/include/gfx/render/shading.hpp)、[shadowmap.hpp](../../../src/include/gfx/render/shadowmap.hpp)、[pattern.hpp](../../../src/include/gfx/render/pattern.hpp)；设计决策：[adr-0014](../../../docs/design/adr-0014-shaded-raster-pipeline.md)、[adr-0015](../../../docs/design/adr-0015-shadow-map.md)、[adr-0016](../../../docs/design/adr-0016-procedural-patterns.md)；测试：[test_shading.cpp](../../../tests/test_shading.cpp)、[test_shadowmap.cpp](../../../tests/test_shadowmap.cpp)、[test_pattern.cpp](../../../tests/test_pattern.cpp)。

## 学到的概念

1. **顶点属性与片元**：光栅化的本质是"把三角形内部的每个像素的属性**插值**出来"，再用一个**片元着色器**逐像素算颜色。这与光线跟踪（每像素一条光线求交）是两条完全不同的路。
2. **透视校正插值**：屏幕空间里 uv 不能直接线性插值 —— 远处那段会被"压缩"。正确做法是插值 `attr/w` 与 `1/w` 再相除。本演示里墙砖与地砖的走向就是靠它才不歪。
3. **深度（NDC z）不需要校正**：z_ndc 是 `1/w` 的仿射函数，在屏幕空间本就线性 —— 所以深度缓冲可以直接用未校正的重心坐标插值。
4. **近平面裁剪**：透视下相机后方的点 `w < 0`，直接除会把它**镜像翻进画面**；必须先按 `w ≥ 近平面` 把三角形裁掉。
5. **切空间要在片元里重建**：插值后的法向与切线不再严格正交，得重新正交化再组 TBN，否则凹凸会歪。
6. **分层**：这个管线不认识"光"和"材质" —— 每像素算什么颜色是**回调**注入的。所以贴图、法线贴图、纯光照都能配同一套管线。
7. **"看得见纹理"靠颜色贴图，不是靠法线贴图**：法线贴图只扰动**光照方向**，表面颜色仍是材质常量 ⇒ 平直或远处的面看起来就是一片纯色。把颜色贴图**乘到 `diffuse` 上**（`diffuse ⊙ albedo`），图案才真正显形 —— 本演示里这一项贡献了 **99.87%** 的像素差异，比法线（56.9%）与阴影（22.9%）都大。
8. **程序化贴图：一个图案同时给出颜色与高度**：`PatternSample { color, height }` 一次求值，颜色图与法线图成对生成（法线图仍走 `makeNormalMap` 的中心差分）。噪声用**整数哈希的值噪声 + fBm / 湍流**叠加，大理石脉络就是"湍流扭曲的正弦条带"。⚠️ 高度场必须是 uv 的**周期函数**（噪声带 `period`、格数取整数），否则 `makeNormalMap` 的环绕差分会在贴图接缝上留一条亮/暗线。
9. **阴影贴图 = "从光源视角再光栅化一遍"**：这条路没有场景求交，所以阴影不能用阴影射线。做法是先用光源的视角把场景**只写深度**地画进一张小图，主 pass 里把每个片元的世界位置投回那个视角比较深度。**复用同一条光栅化管线**，只是换了个 `View`。
10. **自阴影（acne）与两道防线**：被接收的面自己也在贴图里，容差不够就会被判成"被自己挡住"。防线一是**法向偏移**（把采样点抬离自己的面），防线二是**斜率缩放的深度容差**（面与光线越掠射，一个 texel 覆盖的深度范围越大 ∝ tanθ）。只有第一道是不够的；但**法向偏移也不是越大越好** —— 偏移过量会在"墙的轮廓/开口边缘"冒出假暗点（实测 1/2/3 texel → 椒盐像素 149/622/1964）。
11. **"实时"靠预计算 + 按需降分辨率**：阴影贴图只依赖"光源 + 场景"、与相机无关 ⇒ 启动时建一次即可；房间四面是墙 ⇒ 屏幕几乎全被覆盖，逐像素代价正比于像素数 ⇒ **拖动时降到半分辨率、松手立刻重渲全分辨率**（视觉上只是拖动瞬间略糊）。

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
| `--strength s` | 凹凸强度（默认 1.0；**0 = 法线贴图退化成平整法向**，颜色贴图仍在）|
| `--shadow-size n` | 阴影贴图分辨率（默认 `1024`）|
| `--no-shadow` | 关闭阴影贴图（A/B 对照）|
| `--no-map` | 关闭法线贴图（A/B 对照）|
| `--no-albedo` | 关闭颜色贴图（A/B 对照；只剩常量材质色）|
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
| `1` | 切换**法线贴图**（对照看凹凸改变了什么）|
| `2` | 切换**阴影** |
| `3` | 切换**颜色贴图**（一按就回到"一片纯色"，最直观地说明 albedo 的作用）|
| 空格 | 高分辨率（800×600）渲一张，存 `shaded-raster-interactive.png` |
| `Esc` | 退出 |

**为什么能实时**：

1. 光栅化的代价与"屏幕上有多少像素被覆盖"成正比，而不是"每像素发一条光线求交"；
2. **阴影贴图只依赖"光源 + 场景"，与相机无关** —— 启动时建一次（2 张 1024² 约 21 ms）就够，相机怎么转都不重建；
3. 只有相机或开关变化时才重渲（脏标记）；**拖动/按键时自动降到半分辨率**（最近邻放大回窗口），松手那一帧立刻补一张全分辨率的。

实测（单线程，640×480 窗口）：

| 状态 | 渲染分辨率 | 帧耗时 | 帧率 |
|------|-----------|--------|------|
| 拖动/按键中 | 320×240（半分辨率）| ≈ 33 ms | **≈ 30 fps** |
| 静止（全分辨率）| 640×480 | ≈ 129 ms | ≈ 7.7 fps |

> 静止时只在"改动后"渲一帧，所以手感由拖动中的帧率决定；要更高的静止分辨率优先就用 `--size` 调大，要更顺滑就调小（320×240 全分辨率 ≈ 29 fps）。

离线版与交互版**共用** [code/shaded_raster_scene.hpp](code/shaded_raster_scene.hpp)：同一份场景、同一份着色代码、同一份图案定义
（与 [ray-tracing](../../../tutorials/ray-tracing/README.md) 目录里 `ray_tracing_scene.hpp` 的分工一致）。

## 场景

房间 7.2 × 9.6 × 3.4（**开着顶**），全部由三角形拼出来（没有光线求交）；两个光源**各配一张阴影贴图**。
四套外观各由 `pattern.hpp` 的一个图案生成 **320² 的颜色图 + 320² 的法线图**：

| 物体 | 三角形数 | 图案（颜色 + 高度） | 材质 tint |
|------|----------|---------------------|-----------|
| 地面（7.2×9.6 四边形） | 2 | 瓷砖 `tilePattern`（6×8 格，宽缝隙，每块深浅略不同）| 冷灰蓝 |
| 四面墙（各 2） | 8 | 砖墙 `brickPattern`（12×8 块、交错、深缝）| **四面四种颜色**：远墙暖陶土 / 左墙青绿 / 右墙紫罗兰 / 近墙琥珀 |
| 立方体（6 面） | 12 | 碳纤维斜纹 `weavePattern`（3/1 斜纹，上下丝带明暗交替）| 银蓝（高光强）|
| 球（28×44 经纬网格） | 2464 | 大理石 `marblePattern`（湍流扭曲的脉络 + fBm 斑驳）| 象牙白（高光很锐）|

| 光源 | 位置 | 颜色 / 强度 |
|------|------|-------------|
| 主光 | (2.6, 8.0, 3.0) | 暖白 / 1.15 |
| 补光 | (−3.0, 6.0, −2.6) | 暖橙 / 0.55 |

> 两盏灯都在**开口上方、不同水平方向**：这是有意的 —— 阴影贴图把点光源当作平行光近似（离得越远越准），
> 若把灯塞进屋里贴到墙角，这个近似会把"墙挡光"的范围放大到吃掉大半个地面（实测过，见 adr-0015）。
> 环境光取 0.14（高于 `shadeLocal` 的默认 0.05）：四面墙对顶光都是掠射，只靠直接光墙面会死黑。

片元着色：`采样颜色贴图 ⊙ diffuse → 采样法线贴图 → decodeNormal → 重建 TBN → perturbNormal → shadeLocal(Blinn-Phong，阴影回调查阴影贴图，环境光 0.14) → gamma(1/2.2)`。

## 结果（实测）

800×600，实跑：

```
  三角形数      : 2486
  着色片元数    : 634198
```

| 对比 | 结果 |
|------|------|
| **颜色贴图** 开 vs `--no-albedo` | **479371 / 480000 像素不同（99.87%）**，平均差 **38.9** ⇒ 画面差异的**最大来源**（此前根本没有颜色贴图，正是"地面看不出纹理"的根因）|
| **法线贴图** 开 vs `--no-map` | **272923 / 480000 像素不同（56.86%）**，平均差 29.3 ⇒ 缝隙/砖缝的立体感 |
| **阴影** 开 vs `--no-shadow` | **109876 / 480000 像素不同（22.89%）**，最大通道差 198，平均差 10.7 |
| 其中**椒盐像素**（4 邻域都无差异者 = 自阴影噪点的形态） | **149 个（0.031%）**，集中在墙的轮廓/开口边缘 |
| **不变式**：阴影图比对照图**更亮**的像素数 | **0** —— 阴影只会减少某个光源的贡献，绝不会增加 |

> 阴影占比（22.9%）比"没墙的开放场景"（5.3%）大得多：**墙本身就是遮挡物**，灯从开口照进来必然在地面近墙处留下成片影子 —— 这是（正交近似的）方向光下的正确行为。可用独立探针核对：地面 29.5%（主光）/ 38.5%（补光）被挡，四面墙只有 0~10%（物体投上去的影子）。

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
    Material m = material;                                   // 颜色贴图乘到 diffuse 上
    m.diffuse = m.diffuse * albedo->sample(f.uv.x, f.uv.y);   // （逐分量相乘）
    const Vec3 n = perturbNormal(basis, decodeNormal(normalMap->sample(f.uv.x, f.uv.y)));
    return shadeLocal(f.position, n, viewDir(f), m, lights, shadowTest, 0.14f);
}, &depth);
```

程序化贴图（一次生成配对的颜色图 + 法线图）：

```cpp
// 图案：给归一化 uv，返回颜色（0..1）与高度；色调用材质 tint 提供，故这里偏中性
PatternMaps brick = makePatternMaps(320, [](float u, float v) {
    return brickPattern(u, v, /*columns=*/12, /*rows=*/8, /*mortarWidth=*/0.16f);
}, /*normalStrength=*/1.0f);
// brick.albedo / brick.normal 交给 Texture::sample 即可
```

阴影贴图（每个光源一张）：

```cpp
// 1) 光源视角：正交投影覆盖整个场景（等效平行光），只写深度
View lightView;
lightView.setViewport(1024, 1024);
lightView.setOrthographic(-8, 8, -8, 8, 0.1f, 18.0f);
lightView.lookAt(light.position, sceneCenter, up);

ShadowMap shadow;
shadow.configure(lightView, 1024);
shadow.setNormalOffset((2.0f * 8.0f / 1024) * 1.0f);   // 沿法向抬起 1 个 texel（过量会在轮廓处冒假暗点）
shadow.build(allTriangles);                             // 深度 pass：内部再跑一遍 fillTriangleShaded

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
| 颜色贴图接入点 | 命中后按 uv 采样，乘到材质上 | 片元里按插值 uv 采样，乘到 `Material::diffuse` 上 |
| 阴影 | 阴影光线（真实遮挡，逐像素求交） | **阴影贴图**（光源视角只写深度 pass + 主 pass 深度比较）|
| 共同件 | `Texture`（采样）、`TangentBasis`/`perturbNormal`/`makeNormalMap`、`pattern.hpp` 的图案、`shadeLocal` —— **两边通用** ||

## 关联文档

- 设计决策：[adr-0014](../../../docs/design/adr-0014-shaded-raster-pipeline.md)（着色管线）、[adr-0015](../../../docs/design/adr-0015-shadow-map.md)（阴影贴图）、[adr-0016](../../../docs/design/adr-0016-procedural-patterns.md)（程序化图案）
- 上游：[adr-0013](../../../docs/design/adr-0013-normal-mapping.md)（纹理与法线贴图）、[adr-0008](../../../docs/design/adr-0008-triangle-raster.md)（三角形光栅化基线）、[adr-0006](../../../docs/design/adr-0006-camera-view.md)（View）
- 测试：[test_shading.cpp](../../../tests/test_shading.cpp)、[test_shadowmap.cpp](../../../tests/test_shadowmap.cpp)、[test_pattern.cpp](../../../tests/test_pattern.cpp)
