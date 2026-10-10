# ADR-0013 — 纹理与法线贴图（分两层落位 + 与渲染路径解耦）

> **用途**：记录"法线贴图"这套能力落位 `gfx` 库的决策：**纹理**与**切空间/法线扰动**分成两个头文件、切空间用解析法、与渲染路径解耦的注入点（`SurfaceHit::shadingNormal`）、几何法向与着色法向的分工、以及"不实现 PNG 解码"的取舍。
> **目标读者**：实现/使用纹理与法线贴图的人与 AI Agent。
> **关联文档**：[adr-0012](adr-0012-raytracing-lighting.md)（光照与递归跟踪）；实现 [texture.hpp](../../src/include/gfx/render/texture.hpp) / [normalmap.hpp](../../src/include/gfx/render/normalmap.hpp) / [raytracer.hpp](../../src/include/gfx/render/raytracer.hpp)；测试 [test_normalmap.cpp](../../tests/test_normalmap.cpp)；教学主题 [normal-mapping](../../tutorials/normal-mapping/README.md)。

> **状态**：✅ 采纳（2026-10-09）。落地：`WrapMode` / `TextureFilter` / `wrapTexelIndex` / `Texture`（texture.hpp）；`TangentBasis` / `decodeNormal` / `perturbNormal` / `sphereTangentBasis` / `planeTangentBasis` / `makeNormalMap` / `checkerHeight` / `brickHeight` / `waveHeight`（normalmap.hpp）；`SurfaceHit::shadingNormal`（raytracer.hpp）。

## 背景

- 触发材料：想给 `gfx` 加"法线贴图"玩一玩（不增加几何量、逐像素扰动表面法向）。
- 项目现状：`gfx` **没有**任何纹理类型；`png.hpp` **只有编码没有解码**；着色只有 `lighting.hpp` 的 `shadeLocal`（光线跟踪在用）；`triangle.hpp` 的光栅化器只能填纯色，**没有**顶点属性插值，也没有逐像素着色。
- 关键判断：法线贴图由三件事组成 —— ①贴图采样、②切空间基（TBN）、③逐像素着色。
  光栅化路径要做 ③ 就得**新加一条着色管线**（顶点属性 + 透视校正插值 + 逐像素着色）；光线跟踪路径已经有 ③（`shadeLocal`），接入点很小。
- 决策取向：**先把与渲染路径无关的公共件（①+②）做扎实**，接到光追上跑通；将来上光栅化时，①+② 可原样复用（本文称"路径 C"）。

## 决策

### D1：两层落位 —— `render/texture.hpp`（纹理）与 `render/normalmap.hpp`（切空间与扰动）

- `render/texture.hpp`：`WrapMode`（Repeat/Clamp/Mirror）、`TextureFilter`（Nearest/Bilinear）、`wrapTexelIndex`、`Texture`（内存 RGB 贴图 + 归一化 uv 采样）。
- `render/normalmap.hpp`：`TangentBasis`、`decodeNormal`、`perturbNormal`、`sphereTangentBasis`/`planeTangentBasis`、`makeNormalMap`（高度场 → 法线图）、三个内置高度场。
- 理由：**纹理是通用的**（将来漫反射贴图/粗糙度贴图同样用），法线贴图只是"把 RGB 当法向用"的一个特例；拆开后 `Texture` 不与"法线"概念耦合。
- 两者都放 `render/`，只依赖 `math/`，不放 `geom/`（几何层不引入"贴图"这种渲染概念，延续 ADR-0012 的分层纪律）。

### D2：切空间用**解析解**，不为网格引入顶点属性

- 球面按经纬度参数化给出切向（`d/dφ`），平面任取一组正交基；两者都能解析算出 TBN，**不需要**给顶点存 tangent/bitangent。
- 由此推导的 uv 口径：球面 `u = 0.5 + atan2(z,x)/2π`、`v = acos(y)/π`；平面由命中点投影到 T/B 上再除平铺尺度。
- 好处：本阶段完全不碰"顶点属性/插值"，演示代码很短；代价是只有解析形状能用（三角网格要等光栅化路径再补 per-vertex TBN）。

### D3：**与渲染路径解耦**——通过 `SurfaceHit::shadingNormal` 注入

- `SurfaceHit` 新增可选成员 `math::Vec3 shadingNormal`（零向量 = 未提供）。
- 求交回调（`RayTracer::HitFunction`）在命中后自行算 uv → 采样法线图 → TBN 变换，把结果写进 `shadingNormal`。
- `RayTracer` 只做一件事：**光照用 `shadingNormal`，其余一律用几何法向**。
- 理由：既不把纹理/切空间塞进跟踪器（跟踪器仍然不知道贴图的存在），也不用改 `HitFunction` 签名 —— 零向量即"老行为"，向后兼容（ADR-0012 的测试全部原样通过）。

### D4：几何法向 vs 着色法向的**分工**（本决策的核心）

| 用途 | 用哪个法向 | 原因 |
|------|-----------|------|
| 内外判定（`dot(dir,N) > 0` ⇒ 翻转） | **几何** | 用扰动法向会让透明体进出判定出错 |
| 自交偏移、反射方向、折射方向 | **几何** | 用扰动法向可能把二次光线算到表面下方，产生漏光/自交噪点 |
| 局部光照（N·L、Blinn-Phong 半程向量） | **着色** | 这正是法线贴图要的效果 |

- **已知代价**：`shadeLocal` 内部的阴影射线起点偏移也用它拿到的法向（即着色法向），极端凹凸下有轻微自阴影（acne）风险。当前强度下未观察到问题；若将来要严格，应给 `shadeLocal` 传入独立的"偏移用法向"。
- 内侧命中时着色法向要**跟着几何法向一起翻转**（否则透明体内部光照会反）。

### D5：贴图来源 —— 程序化优先；**不做 PNG 解码**

- 提供 `makeNormalMap(size, height, strength)`：任意高度场 → 中心差分求切向斜率 → 切线空间法向 → 编码 `n*0.5+0.5` 存入 RGB。内置棋格/砖缝/波纹三个周期高度场。
- **不做 PNG 解码**：真实 PNG 用 zlib 压缩的 deflate，读它要自己写 inflate（几百行 + 扫描线过滤器/调色板/交错），与"法线贴图"本身无关。真要读文件，走**简易格式**（PPM/PGM 或原始浮点数组）并用外部脚本转换。
- `strength = 0` 时贴图退化为平整法向 `(0,0,1)` ⇒ 应等价于"不用贴图"（用测试与演示双重锁定，见下）。

### D6：数值与约定

- 纹理像素以 **0..1 的线性 float 三元组**存储（与材质/光照口径一致，避免采样点反复做 8bit 转换）。
- uv 约定：u 沿 +X、v 沿 +Y，像素中心在 `(i+0.5)/n`；`Bilinear` 的四个采样点**各自做环绕** ⇒ 平铺接缝不会取错。
- 切空间约定：**OpenGL 风格**，+X = 切线 T（沿 +u）、+Y = 副切线 B、+Z = 几何法向 N，且**右手系**（`B = N × T`）。方向搞错会让凹凸上下/左右镜像，故用测试锁定正交性、单位长与右手性。
- 高度函数应当是 uv 的**周期函数**（否则贴图接缝处中心差分跨过边界，会出现一条法线突变）。

## 验证（可复现）

- 单元测试 `ctest -R test_normalmap`（**565 checks，0 failures**）：三种环绕的索引映射、最近邻/双线性采样位置（2×2 已知纹理）、`decodeNormal` 三个基向、`makeNormalMap`（常数高度 ⇒ 全平整；已知正弦高度 ⇒ 与解析斜率一致，含 `strength=0` ⇒ 平整）、球/平面切空间的正交性/单位长/右手性、`perturbNormal`（平整 ⇒ 还原几何法向；45° 倾斜 ⇒ 与原法向夹角 45°）、以及跟踪器的着色法向接口（零向量 ⇒ 与基线逐分量一致；歪向光源 ⇒ 变亮）。
- `ctest --test-dir build`：**14/14 通过**（原 13 + `test_normalmap`）。
- 教学演示实跑（800×600、3×3、`maxDepth=6`，共 4320000 条主光线）：
  - 开启法线贴图 vs `--no-map`：**395686 / 480000 像素不同（82.43%）**，最大通道差 **214**，平均差 **44.81** —— 效果显著；
  - `--strength 0` vs `--no-map`：仅 **2 / 480000** 个像素不同且最大差 **1** 灰阶（浮点末位舍入）⇒ 链路无系统性误差，`strength=0` 确实退化为"贴图不存在"。
