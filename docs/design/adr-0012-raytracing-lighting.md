# ADR-0012 — 光照模型与递归光线跟踪（作业三再沉淀为库能力）

> **用途**：记录"光照 + 递归光线跟踪"这套**渲染能力**落位 `gfx` 库的决策：类型与函数落点（`render/`）、与场景解耦的接口约定（求交回调 / 阴影回调）、递归深度口径、超采样与伽马、以及 `render → geom` 的单向依赖放行。
> **目标读者**：实现/使用光照与光线跟踪的人与 AI Agent。
> **关联文档**：[adr-0011](adr-0011-ray-geometry.md)（求交与 BVH 地基）；实现 [lighting.hpp](../../src/include/gfx/render/lighting.hpp) / [raytracer.hpp](../../src/include/gfx/render/raytracer.hpp)；测试 [test_raytracer.cpp](../../tests/test_raytracer.cpp)；教学主题 [ray-tracing](../../tutorials/ray-tracing/README.md)。

> **状态**：✅ 采纳（2026-10-09）。落地：`Material` / `Light` / `shadeLocal` / `offsetAlong` / `kSelfIntersectionEpsilon`（lighting.hpp）；`SurfaceHit` / `RayTracer`（raytracer.hpp）。

## 背景

- 触发材料：课程《作业三》要求实现光线跟踪（含光照、阴影、反射、折射、抗锯齿）。上一阶段（ADR-0011）只沉淀了**纯几何**（求交 + BVH）；本阶段补上**着色与递归**，使 `gfx` 能脱离外部框架自己出图。
- 项目现状：`gfx` 有光栅化/深度/相机/PNG，以及几何层的求交与 BVH；**没有**光照模型与递归跟踪。
- 分层约束：项目规定 `geom/` **只依赖 `math/`**，与 `render/` 零耦合（见 [src/README.md](../../src/README.md)）。本阶段文件放 `render/`，且**单向**依赖 `geom/`（用其 `HitRecord`）与 `math/`——`geom` 仍不得反向依赖 `render`（该约束不破）。

## 决策

### D1：光照与跟踪落 `render/`（`lighting.hpp` / `raytracer.hpp`）

- `render/lighting.hpp`：`Material`（漫反射/高光/自发光 + 高光指数 + 反射率/透射率/折射率）、`Light`（点光源）、`shadeLocal(...)`（局部光照）、`offsetAlong` 与常量 `kSelfIntersectionEpsilon`。
- `render/raytracer.hpp`：`SurfaceHit`（几何 + 材质指针）、`RayTracer`（`trace` 递归 + `render` 出图）。
- 理由：光照与递归跟踪属**渲染**语义（着色、二次光线、像素输出），不是纯几何；放 `render/` 既贴合职责，也守住"`geom` 可单独拆库"的界线。`render` 单向用 `geom::HitRecord` 只是"复用命中数据结构"，不构成 `geom → render` 反向依赖。

### D2：**与场景解耦**——求交由调用方以回调注入

- `RayTracer` 不持有任何图元类型；场景通过 `HitFunction = std::function<bool(const Ray&, SurfaceHit&)>` 注入。
- **阴影测试同理**：`shadeLocal` 的阴影由**调用方**通过回调 `bool(point, dirToLight, distanceToLight)` 注入（`render` 层不该硬编码"场景里有什么"）。跟踪器在实现该回调时**复用同一个 `HitFunction`**（从已偏移的交点沿 `L` 发射射线，命中且 `t < distanceToLight` 判为遮挡）。
- 收益：同一份光照/跟踪代码可配任意求交实现（BVH / 暴力 / 其它图元）；BVH 与跟踪器彼此独立（教学示例中"BVH 版 vs 暴力版"只换一个回调即可对比）。

### D3：命中信息用 `SurfaceHit`（几何 + **材质指针**）

- `struct SurfaceHit { HitRecord geometry; const Material* material = nullptr; };`
- 材质用**指针**（可为 `nullptr` ⇒ 用一份默认材质，0.8 灰）：让"几何求交"与"材质归属"分离，回调只需回传"命中哪个物体的材质"，无需把材质塞进几何层（`HitRecord` 保持在 `geom/`，不引入 render 概念）。
- **命名澄清**：任务伪代码写作 `geom::HitRecord`，实际库中 `HitRecord` 位于**扁平命名空间 `gfx`**（`geom/ray.hpp` 定义在 `namespace gfx`，并无 `gfx::geom` 子命名空间），故实现用 `gfx::HitRecord`。

### D4：递归深度口径（**对规格第 1 条的必要修正**）

- 规格第 1 条写"`--depth; if (depth <= 0) return 零向量;`"（先减后判），但同文测试项 5 要求"`depth = 1` ⇒ 只有局部光照；`depth <= 0` ⇒ 零向量"——两者**不能同时成立**（先减后判时 `depth=1` 会直接变 `0` 而返回黑）。
- 实现取**语义等价且与测试项 5 自洽**的口径：**进入本层消耗 1 层预算，预算耗尽（传入 `depth ≤ 0`）返回黑**；即 `--depth; if (depth < 0) return 零向量;`。
  - 传入 `depth = 1`：消耗后为 `0` ⇒ 正常算局部光照，但**不**发二次光线（反射/折射条件 `depth > 1`）⇒ 只有局部光照。
  - 传入 `depth ≤ 0`：消耗后为负 ⇒ 返回零向量。
  - 反射/折射需**消耗后仍 ≥ 2 层**（传入 `depth ≥ 3`）才发射——与规格第 7/8 条的 `depth > 1` 判定一致。
- 用测试锁定：`trace(ray,1) == 仅局部光照`、`trace(ray,0) == trace(ray,-k) == 零向量`、`trace(ray,1) != trace(ray,5)`。

### D5：公式口径与课程（作业三）一致，便于数值对齐

- 法向定向：`N·d > 0 ⇒ N = -N`，`inside = true`；自交偏移：反射/阴影起点 `P + N·eps`，透射起点 `P - N·eps`（`eps = 1e-4`）。
- 漫反射 `diffuse ⊙ light.color · intensity · max(0, N·L)`；高光（Blinn-Phong）`specular ⊙ light.color · intensity · pow(max(0, N·H), shininess)`，`H = normalize(L + viewDir)`。
- 环境光 `0.05 · diffuse`；反射 `R = reflect(d, N)`；折射 `eta = inside ? n : 1/n`，`refract` 返回 `false` ⇒ **全反射，不加折射光**（不算错）。
- 合成 `local + ks·reflected + kt·transmitted`，末尾 `clamp(0,1)`；`ks + kt < 1e-3` 时早退为局部光照（避免无意义递归）。

### D6：出图——超采样（子像素中心）+ 可选伽马

- `render(Canvas&, RayGenerator&&, maxDepth)`：由调用方给"像素 → 世界射线"生成器（与 `View::pixelToRay` 配合，不硬编码相机）。
- `setSamplesPerAxis(n)`：每像素 `n×n` 条射线，子采样位置取**子像素中心**，结果取平均；`n=1` 时即逐像素中心采样（与"直接单射线渲染"逐像素一致，测试锁定）。
- 颜色 `[0,1] → 8bit`：`lround(c·255)`；`setGammaCorrect(true)` 时先 `pow(c, 1/2.2)`（注释写明是"线性 → 近似 sRGB"），**默认关闭**。

### D7：`render/render.hpp` 总入口挂上新头；不新增源清单条目

- `render/render.hpp` 增补 `#include` 两个新头，并把层语义注释更新为"**单向**依赖 `geom/`"。
- `src/CMakeLists.txt` 中的 `gfx` 目标源清单**未改动**（不在本次允许修改范围）；新头为纯 `inline`/模板，随 `render.hpp` 传递包含即可，无需单独编译单元。

## 后果

- 新增公共 API（`render` 层）：`gfx::Material` / `gfx::Light` / `gfx::shadeLocal` / `gfx::offsetAlong` / `gfx::kSelfIntersectionEpsilon` / `gfx::kShadowTolerance`；`gfx::SurfaceHit` / `gfx::RayTracer`（`trace` / `render` / `setBackground` / `setSamplesPerAxis` / `setGammaCorrect`）。
- 新增测试 `tests/test_raytracer.cpp`（**33 checks**，0 failures）；新增教学主题 [ray-tracing](../../tutorials/ray-tracing/README.md) 与演示目标 `tutorial_ray_tracing`（产物 `RayTracing`）。
- **`gfx` 自立出图**：教学示例用 gfx 自身（View + BVH + shadeLocal + RayTracer + savePng）渲染出 800×600 的光线跟踪图，脱离任何课程脚手架。
- 不改动既有模块行为：`geom/` / `math/` / `core/` 既有文件未改；`render/` 既有文件只增补 `render.hpp`（加 include 与注释）。`geom` 仍只依赖 `math/`。

## 验证（可复现）

- 单元测试 `ctest -R test_raytracer`（**33 checks，0 failures**）：局部光照手算 / 阴影（回调 + 端到端）/ 镜面反射（打红面 vs 打空）/ 折射（透明球穿透 + 全反射无折射光）/ 递归深度（1 仅局部、≤0 黑）/ 背景 / 作业三 7 组数值对齐 / 超采样（n=1≠n=3，n=1 与单射线差 0）。
- `ctest --test-dir build`：**13/13 通过**（原 12 + `test_raytracer`）。
- 教学演示实跑（800×600、3×3、`maxDepth=6`，场景含"半透明三角网格 + 5 光源"，用 `--brute` 开启对照）：BVH 精确求交 241.27 次/像素 vs 暴力法 510.82 次/像素（剪枝到 ~47%，该计数确定性可复现）；`--brute` 下两图**逐像素完全一致**（0 不同像素）。**如实说明**：本机墙钟比值在 **0.45–1.00** 间波动（同一场景重复测量，差异来自机器负载），**故不宣称任何加速比**；曾记录的"约 9%""快 54%""基本持平"均不作为结论，需更大场景才见稳定收益（详见 [ray-tracing/README.md](../../tutorials/ray-tracing/README.md)）。

## 与课程框架的等价性验证（HW3 原始场景，整图逐像素）

除上面的数值单点对齐外，本阶段另做了一次**整图等价性验证**：把老师《作业三》框架**原始 `main.cpp`** 构造的场景（房间 6 面 + 玻璃球 + 绿球 + 蓝立方 + 3 点光源）用 gfx **逐项复刻**，并以**照抄框架 `render_scene`** 的相机/像素→射线公式渲染，与"框架自己渲染的同一场景（关超采样、关伽马）"做逐像素比对。

- 基准图（框架渲染）：`artifacts/homework3/_verify/hw3_framework_baseline.png`（800×600）。
- gfx 复刻图：`tutorials/ray-tracing-framework-scene/output/hw3_gfx_result.png`（800×600）。
- 教学主题：[ray-tracing-framework-scene](../../tutorials/ray-tracing-framework-scene/README.md)；演示目标 `tutorial_hw3_scene`（产物 `Hw3SceneTrace`）；临时驱动 `artifacts/homework3/_verify/{baseline.cpp,compare.cpp}`。

**实测结果**：480000 像素中 **479979 完全相同（99.9956%）**，**21 个像素差 1 灰阶**，最大通道差 = 1，整体平均通道差 ≈ 1.5e-5。R↔B 交换后相同像素骤降到 0.136% ⇒ **通道顺序正确、无串色**。相机公式与 `View::pixelToRay` 自检最大方向偏差 ≈ 2.5e-7（等价）。

**结论**：差异全部是**浮点末位 + 8bit 取整方式**（OpenCV `convertTo` 的 round-half-to-even vs gfx `std::lround` 的 round-half-away-from-zero）造成的可接受量化噪声，集中在玻璃球/立方体轮廓边缘；**未发现真 bug**（无通道串色、无相机公式不等价、无材质参数搬错）。

## 待细化

- **材质扩充**：仅 Phong/Blinn-Phong 与理想镜面/规则透射；粗糙镜面（微表面）、纹理、法线贴图未做。
- **光源类型**：仅点光源；面光源/环境光/HDRI 未做。
- **采样策略**：超采样为规则网格；随机抖动（jitter）/重要性采样/路径跟踪未做。
- **并行**：`render` 单线程逐像素；多线程/瓦片（tile）并行未做。
- **进度回调**：`render` 未提供每行回调（教学示例自行打印统计），如需实时进度可后续加可选参数。
