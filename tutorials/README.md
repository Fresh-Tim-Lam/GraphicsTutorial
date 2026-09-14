# tutorials/ — 教学树（树状组织，非瀑布编号章节）

> **用途**：存放 GraphicsTutorial 的教学/说明文档与配套可运行示例。教学按**树状主题**组织（从底层像素着色的"枝叶"逐级生长为渲染 API 的"主干"），不使用"第 1 章…第 N 章"瀑布编号。
> **目标读者**：学习从 0 造轮子的读者；编写/维护教学的开发者与 AI Agent。
> **关联文档**：[src/](../src/README.md)（权威实现）；[docs-management.md](../docs/conventions/docs-management.md)（docs 组织）；[AGENT.md](../AGENT.md)。

> 状态：生效中（核验于 2026-09-09）。首棵子树随首个模块落地时生长。

## 职责一句话

教学树 = 每节点一个主题目录：`README.md` 讲解（该主题实现思路/原理）+ `code/`（该主题可运行的演示/练习代码）+ 可嵌套子主题目录。

## 组织规则

- **树状而非编号**：主题靠目录嵌套表达依赖/演进关系（如 `window/`、`window/swapchain/`），禁止用 `01-`、`02-` 编号前缀制造虚假线性。
- 每节点含 `README.md`（首段即该主题"是什么/为什么"），可含 `code/`（演示代码，通常小到可单文件跑通）。
- 主题目录除 `README.md` 与 `code/` 外，可按需含：`data/`（样例输入）、`output/`（结果图）、`submit/`（作业/对外提交素材与独立构建脚本）。
- **单一事实源**：教学 `code/` 内的代码为演示/练习用途；同名能力的**权威实现归属 src/**。教学文档需引用权威实现位置，标注"权威实现见 `src/...`"，教学代码与 src 冲突时以 src 为准。
- 代码语言：C++ 标识符英文、注释中文（见 [naming.md](../docs/conventions/naming.md)）。
- 构建：教学 code 若需构建，随 modules 加入根 CMake 的示例目标（骨架期不虚构目标）。

## 树形导航（将随生长更新）

- 根节点：此 README.md。
- [pixels-on-screen/](pixels-on-screen/README.md) —— 把第一个像素画到屏幕（Canvas + Window，第一个可见成果）。
- [math-foundations/](math-foundations/README.md) —— 向量与矩阵：渲染的地基（gfx_math 使用与原理）。
- [polygon-scan/](polygon-scan/README.md) —— 扫描线多边形填充：画线（Bresenham）+ 边表/活性边表填充 + 自写 PNG 输出（作业 1）。

### 教学演示目标（CMake）

| 目标 | 内容 | 运行方式 |
|------|------|---------|
| tutorial_pixels | 像素上屏演示（对角线+定点） | `cmake --build build --target tutorial_pixels` 后运行 `build/tutorials/tutorial_pixels` |
| tutorial_math | 矩阵旋转/平移数学演示 | `cmake --build build --target tutorial_math` 后运行 `build/tutorials/tutorial_math` |
| tutorial_polygon_fill | 多边形扫描转换（画线 + 扫描线填充 + PNG 输出 + 窗口动画），产物名 `PolygonFill` | `cmake --build build --target tutorial_polygon_fill` 后运行 `build/tutorials/PolygonFill --help` |
