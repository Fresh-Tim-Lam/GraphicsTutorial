# glossary.md — 术语表

> **用途**：统一本仓库（教程/文档/代码）使用的术语定义，防止一词多义。
> **目标读者**：撰写/阅读教程与文档的读者、开发者与 AI Agent。
> **关联文档**：[docs/README.md](../README.md)；[naming.md](../conventions/naming.md)。

> 状态：生效中（核验于 2026-09-09）。首批教学术语已登记。

| 术语 | 定义 | 首见位置 |
|------|------|---------|
| gfx | 本项目交付的图形渲染 API 的命名空间与库名 | src/、AGENT.md |
| gfx::math | gfx 下的数学子库命名空间（vec/mat，头文件子库） | src/include/gfx/math/ |
| Canvas（画布） | 内存像素帧缓冲：把像素写到指定坐标的离屏缓冲区 | src/include/gfx/core/canvas.hpp |
| Color | 像素颜色类型（RGBA8：r/g/b/a 各 8bit） | src/include/gfx/core/color.hpp |
| putPixel | 向 Canvas 指定 (x, y) 写入一个像素 | gfx/core/canvas.hpp |
| present（上屏） | 把一帧 Canvas 内容整体提交到屏幕（DIB + BitBlt） | gfx/render/window.hpp |
| 帧缓冲（framebuffer） | 决定画面中每个像素颜色的内存区域，绘制完后统一提交 | tutorials/pixels-on-screen |
| 齐次坐标（homogeneous） | 2D 用 Vec3(x,y,1)、3D 用 Vec4(x,y,z,1) 以便矩阵表达平移 | tutorials/math-foundations |
| 行主序（row-major） | 矩阵元素按行连续存放（本库 mat 的存储约定） | gfx/math/mat.hpp |
| 行字面量（row literal） | 用 `{{第0行},{第1行},...}` 直接书写矩阵，如 `Mat3{{1,0,tx},{0,1,ty},{0,0,1}}` | gfx/math/mat.hpp |
| 平移（translation） | 由矩阵第 3/4 列承载的位置偏移（齐次坐标下用矩阵表达） | gfx/math/transform.hpp |
| 旋转（rotation） | 绕原点/坐标轴的角度变换，弧度制、逆时针为正 | gfx/math/transform.hpp |
| 绕点旋转（pivot rotation） | 绕某点（3D 为绕过该点的轴）旋转，即 T(c)·R·T(−c)；即 `rotate(rad, center)` 重载，组合版 `rotateViaPrimitives` | gfx/math/transform.hpp |
| 换基（change of basis） | 以新基向量为列构造矩阵 B，在"新空间 ↔ 当前空间"之间换算（p_当前 = B·p_新空间；正交时 B⁻¹ = Bᵀ） | gfx/math/transform.hpp |
| 正交基（orthonormal frame） | 两两垂直且均为单位长的基向量组；由旋转轴生成右手系 (u, v, axis) 用 `frameFromAxis` | gfx/math/transform.hpp |
| 基元旋转 vs 组合旋转 | 绕坐标轴的旋转（`rotate(rad, Axis::X/Y/Z)`，一个函数 + 枚举选轴）是基元；绕任意轴可用"换基+基元+换回"组合得到（rotateViaBasis），或闭式 Rodrigues（rotate） | gfx/math/transform.hpp |
| 坐标轴枚举（Axis） | `math::Axis{X,Y,Z}`：用参数而非三个函数来选择旋转轴 | gfx/math/transform.hpp |
| 翻转/镜像（flip） | 对某坐标轴取负（如 flipX 负 x），等价 scale(-1, 1) | gfx/math/transform.hpp |
| 变换组合（矩阵） | `Mat` 的乘法：`A * B` 先作用 B 再作用 A（数学复合） | tutorials/math-foundations |
| 变换组合（链） | `Transform` 的 `then()` / `a * b`：书写顺序 = 作用顺序（先 a 后 b）；底层为新步骤左乘 | gfx/math/transform.hpp |
| 变换链（Transform） | 记录连续变换步骤、延迟求值的 math::Transform&lt;N&gt;；单点逐次 mat-vec、批点先折叠 | gfx/math/transform.hpp |
| 点 apply 变换 | 几何层写法：点/点批 apply 一个 Transform（`p.apply(t)`、`points.apply(t,out)`） | gfx/geom/point.hpp |
| 视图（view） | 零拷贝的内存描述（指针+长度[+步长]），如点批引用外部顶点数组 | gfx/geom/point.hpp |
| 步长（stride） | 相邻点/顶点的字节间隔；AoS 交错布局（顶点带额外字段）时必须显式给出 | gfx/geom/point.hpp |
| 越界裁剪 | putPixel 对超出画布边界的坐标直接忽略（当前策略） | gfx/core/canvas.hpp |
| DIB / BitBlt | Windows GDI 机制：DIB=设备无关位图，BitBlt=块级位图拷贝上屏 | tutorials/pixels-on-screen |
