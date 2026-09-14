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

## 约定

- 断言宏：`TEST_CHECK(expr)`、`TEST_NEAR(a, b, eps)`；测试程序 `main` 返回 `gfx::test::summary()`。
- 需要 GUI/像素上屏验证的演示放 tutorials/code（不属自动测试）；Canvas 纯内存逻辑才做自动测试。
- 运行：`ctest --test-dir build -C Release --output-on-failure`（或单测 `-R test_math`）。
