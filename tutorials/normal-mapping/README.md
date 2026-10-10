# 法线贴图（Normal Mapping）

> 主题：**不增加几何量、只逐像素扰动表面法向**，让光滑表面看起来有凹凸；并顺带把"凹凸会扭曲镜面反射"这件事直观呈现出来。
> 实现：[normalmap.hpp](../../../src/include/gfx/render/normalmap.hpp)（切空间 + 高度场→法线图）+ [texture.hpp](../../../src/include/gfx/render/texture.hpp)（纹理与采样）+ `SurfaceHit::shadingNormal`（[raytracer.hpp](../../../src/include/gfx/render/raytracer.hpp)）。
> 设计决策：[adr-0013](../../../docs/design/adr-0013-normal-mapping.md)。测试：[test_normalmap.cpp](../../../tests/test_normalmap.cpp)。

## 学到的概念

1. **法线贴图是什么**：贴图里存的不是颜色，而是**切线空间的法向**（RGB 编码 `n*0.5+0.5`）。着色时把几何法向换成"贴图给的 + 变换到世界空间"的法向，表面就有了伪凹凸。
2. **为什么需要切空间基（TBN）**：贴图是"相对表面"的，必须知道该点的切线 T、副切线 B、法向 N 才能把它变到世界空间。TBN 必须是**右手系**（`B = N × T`），否则凹凸会镜像。
3. **几何法向 vs 着色法向**：几何法向负责"光线数学"（内外判定 / 自交偏移 / 反射 / 折射），着色法向只管光照。混用会把二次光线算到表面下方，或让透明体进出判定出错。
4. **高度场 → 法线**：`n_tangent ∝ (-∂h/∂u, -∂h/∂v, 1)`。程序化贴图就是"写个高度函数 + 中心差分"。
5. **凹凸会改变镜面反射**：因为光照用了扰动后的法向，反射方向也随之变化 —— 光滑球上的波纹会让反射像被"揉皱"。

## 怎么运行

```bat
cmake --build build --target tutorial_normal_mapping
build\tutorials\NormalMapping                     :: 默认输出 output/normal-mapping.png
build\tutorials\NormalMapping --help
```

| 参数 | 作用 |
|------|------|
| `--output <png>` | 结果路径（默认 `output/normal-mapping.png`，父目录自动创建）|
| `--size WxH` | 画布尺寸（默认 `800x600`）|
| `--samples n` | 每像素 n×n 超采样（默认 3）|
| `--depth d` | 递归深度（默认 6）|
| `--strength s` | 凹凸强度（默认 1.0；**0 = 贴图退化成平整法向**）|
| `--no-map` | 全部关闭法线贴图（A/B 对照）|
| `--compare` | 额外渲一张"关掉贴图"的图并统计差异像素（默认关闭）|

## 场景

三颗**完全相同**的球（同位置排布、同材质），外加一整块平面地面 —— 画面差异**只来自法线贴图**：

| 位置 | 内容 |
|------|------|
| 左球 | **对照组**：不贴法线图 |
| 中球 | 砖缝法线图（`brickHeight`，错缝排列 + 斜边）|
| 右球 | 波纹法线图（`waveHeight`，单向正弦，`uvScale = 1.5`）|
| 地面 | 棋格法线图（`checkerHeight`，`tileSize = 1.6` 世界单位/uv）|

材质统一为：漫反射 0.55 灰 + 高光 `shininess = 96` + **`reflectivity = 0.25`**（留一点反射，好看清"凹凸如何揉皱反射"）。
两个点光源：暖白主光 + 暖色补光。

## 结果（实测）

800×600、3×3 超采样、`maxDepth = 6`（共 4320000 条主光线），本机实跑：

```
  渲染耗时      : 1794.48 ms
  对照（关闭法线贴图）: 395686 / 480000 个像素不同（占 82.4346%），最大通道差 214，平均差 44.8094
```

另外做了一次"退化一致性"检查：

| 对比 | 结果 |
|------|------|
| 开启贴图 vs `--no-map` | **82.43%** 像素不同，最大通道差 **214**（效果显著）|
| `--strength 0` vs `--no-map` | 仅 **2 / 480000** 个像素不同、最大差 **1** 灰阶（浮点末位舍入）⇒ 链路无系统性误差 |

产物：`output/normal-mapping.png`；加 `--compare` 还会生成 `output/normal-mapping-no-map.png`（对照图）。

## 实现要点（三步）

```cpp
// ① 命中后按形状算出 uv 与切空间（球/平面都是解析解）
TangentBasis tb = gfx::sphereTangentBasis(hit.point, sphere.center);
const float u = 0.5f + atan2(dir.z, dir.x) / (2*pi);
const float v = acos(dir.y) / pi;

// ② 采样 + 解码（贴图 → 切线空间法向）
const Vec3 nTangent = gfx::decodeNormal(normalMap.sample(u, v));

// ③ 变换到世界空间，写进 SurfaceHit（**只影响光照**）
out.shadingNormal = gfx::perturbNormal(tb, nTangent);
```

生成贴图则是一行：

```cpp
Texture map = gfx::makeNormalMap(256, [](float u, float v) {
    return gfx::brickHeight(u, v, /*rows=*/5, /*columns=*/4);
}, /*strength=*/1.0f);
```

要点：高度函数应是 uv 的**周期函数**（否则贴图接缝处中心差分跨边界，会留下一条法线突变）；`Bilinear` 采样对四个采样点**各自做环绕**，所以平铺时接缝也不会取错。

## 与后续（光栅化路径）的关系

本主题刻意做成**路径无关**的两层：

| 层 | 文件 | 光追用 | 将来光栅化用 |
|----|------|--------|--------------|
| 纹理（采样 / 环绕 / 滤波） | `texture.hpp` | ✅ | ✅ 同一份 |
| 切空间 + 扰动 + 高度场 | `normalmap.hpp` | ✅ | ✅ 同一份 |
| 注入点 | `SurfaceHit::shadingNormal` | ✅ | 光栅化侧需另做"顶点属性 + 透视校正插值 + 逐像素着色"管线 |

也就是说：**将来补光栅化着色管线时，本主题的两个头文件可以原样复用**，只需另写插值与逐像素循环。

## 关联文档

- 设计决策：[adr-0013-normal-mapping.md](../../../docs/design/adr-0013-normal-mapping.md)
- 上层渲染能力：[adr-0012-raytracing-lighting.md](../../../docs/design/adr-0012-raytracing-lighting.md)（光照与递归跟踪）、[ray-tracing/](../ray-tracing/README.md)
- 测试：[test_normalmap.cpp](../../../tests/test_normalmap.cpp)
