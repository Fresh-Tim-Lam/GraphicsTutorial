# AGENT.md — Agent 接入指南

> **用途**：本仓库是 Windows 端「图形渲染 API 从 0 造轮子」的教学向项目。本文件是 AI Agent 与新维护者的**第一站**：说明仓库是什么、Agent 的职责边界、命令、目录导航与必读规范。
> **目标读者**：AI Agent（每次开始工作前必读）；新加入的维护者。
> **关联文档**：[docs/README.md](docs/README.md)（总索引）；[directory-organization.md](docs/conventions/directory-organization.md)（目录职责）；[meta-rules.md](docs/conventions/meta-rules.md)（规范元规则）；[collaboration.md](docs/guides/collaboration.md)（协作细则）。

> 状态：生效中（核验于 2026-09-09）。

## 1. Role（角色）

本仓库目标：从最基础 Windows 屏幕像素着色出发，**从 0 构建**可被外部调用的 C++ 图形渲染 API（命名空间 `gfx`，动态库 + 头文件），并配树状教学文档。教学与实现是一体两面：每个模块演进后，**必须**同步维护/新增教学文档（见 [tutorials/](tutorials/README.md) 约定）。

Agent 职责：
- 遵守 [Conventions 表](#5-conventions) 中与本次改动相关的全部规范；
- 改代码的同时维护文档索引（增删文件必同步 README）；
- 收尾做"文件组织自检"（见 [Boundaries](#6-boundaries)）。

## 2. TechStack（技术栈）

| 项 | 值 |
|----|----|
| 语言 | C/C++（C++17） |
| 目标平台 | Windows（当前阶段） |
| 构建 | CMake（≥ 3.16），MSVC / clang-cl 兼容 |
| 图形依赖 | 无第三方图形库；仅 Windows 最基础屏幕像素着色（从 0 造轮子） |
| 交付形态 | 动态库（DLL + 导入库）+ 头文件，命名空间 `gfx` |
| 打包发布 | packaging/ 唯一入口 `build_release.cmd` |
| 测试 | 单测框架待模块落地后选定（tests/README.md） |

## 3. Commands（常用命令）

| 用途 | 命令 |
|------|------|
| 配置 | `cmake -S . -B build` |
| 构建 | `cmake --build build --config Release` |
| 运行示例 | 见 tutorials/ 对应主题 code 目录说明 |
| 测试 | `ctest --test-dir build -C Release` |
| 打包发布 API | `packaging\build_release.cmd` |

> 详源：`docs/guides/operations.md`；此处只列常用项，两处冲突以 operations.md 为准。

## 4. Architecture（目录导航）

| 目录 | 职责 |
|------|------|
| [src/](src/README.md) | `gfx` API 权威实现，随模块演进（头文件 + 实现） |
| [tutorials/](tutorials/README.md) | **树状教学**：主题目录含 `.md` 讲解 + `code/` 可运行示例（非瀑布编号章节） |
| [tests/](tests/README.md) | 单元 / 集成测试 |
| [docs/](docs/README.md) | 规范文档中心（conventions / guides / reference / design / sessions …） |
| [scripts/](scripts/README.md) | 可复用开发者工具（正式，不留临时脚本） |
| [resources/](resources/README.md) | 随项目分发的静态资源（图标/文案/示例/截图） |
| [packaging/](packaging/README.md) | 打包发布配置与脚本，唯一入口 `build_release.cmd` |
| [artifacts/](artifacts/README.md) | 全仓唯一临时/调试产物区（gitignore） |
| [logs/](logs/README.md) | 开发调试日志（gitignore） |

数据流（一句话，随开发补充）：Windows 最基础像素着色 → 封装进 `gfx` 窗口/画布/绘制原语 → 上层按模块扩展成渲染 API → 教学树以"先现象后原理"方式讲解每层实现。

## 5. Conventions（何时必读哪份规范）

| 场景 | 必读文档 |
|------|---------|
| 文件放哪个目录 / 临时文件归属 | [directory-organization.md](docs/conventions/directory-organization.md) |
| 新增/移动/删除文档、docs 子目录 | [docs-management.md](docs/conventions/docs-management.md) |
| 制定或修改任何规则 | [meta-rules.md](docs/conventions/meta-rules.md) |
| 涉及版本号 | [version.md](docs/conventions/version.md) |
| 涉及日志 | [logging.md](docs/conventions/logging.md) |
| 文件/符号命名、语言习惯 | [naming.md](docs/conventions/naming.md) |
| 与人协作、提问边界 | [collaboration.md](docs/guides/collaboration.md) |

## 6. Boundaries（协作边界）

### Always（必须主动做）
- 新增/移动/删除文档或目录后，**同步父目录 README 索引**；新增 docs 子目录登记到 docs-management 登记表。
- 生成物（gitignore 掉的可再生物）过期时**重新生成**，不手改、不提交。
- 模块演进后：更新 src/ 实现的同时，维护或新增 tutorials/ 树中对应教学节点与 `docs/reference/architecture.md` 数据流描述。
- 任务收尾做**文件组织自检**：文件落位正确、根目录无兜底模式产物、索引已同步。

### Ask First（先问用户）
- 结构性重构、移动/删除正式文件、改目录组织。
- 打包/发布/加依赖/改构建（涉及 CMakeLists 目标结构、packaging 产物形态）。
- 引入第三方库、新增图形后端、偏离"仅 Windows 基础像素着色"约束。
- 教学树目录结构与 src/ 模块的对应关系的结构性变更。

### Never（禁止）
- 不手改构建产物与生成物；不提交 secrets；不擅自 push / 强推 / 破坏性 git。
- 不在 CMakeLists / packaging 中硬编码版本号（版本只出自唯一事实源 `src/gfx/version.hpp`）。
- 不在仓库根、src/、docs/ 正文目录放临时文件（一律进 artifacts/ 或 logs/）。

## 7. Common Pitfalls（已知陷阱）

- 教学树是**树状组织**而非"第 1 章…第 N 章"瀑布编号——新增内容按主题挂树，不按数字续章节。
- "从 0 造轮子" = 不用现成图形库；示例/教学里出现 OpenGL/DirectX 等 API 属越界，需先问。
- tutorials/ 教学节点的 code/ 与 src/ 权威实现的关系：**src/ 是唯一事实源**；教学 code 为演示/练习用途（其对应权威实现归属 src/）。详见 tutorials/README.md。
- docs/ 根只允许 `README.md` + `to-dolist.md`，新文档按决策表落位。

## 8. References（参考链接）

- [docs/README.md](docs/README.md) — 文档总索引
- [src/README.md](src/README.md) — src 内部约定
- [tutorials/README.md](tutorials/README.md) — 教学树约定
- [docs/guides/operations.md](docs/guides/operations.md) — 操作手册（构建/测试/打包）
- [docs/reference/architecture.md](docs/reference/architecture.md) — 架构与数据流
