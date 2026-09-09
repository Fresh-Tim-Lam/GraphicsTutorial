# to-dolist.md — 活跃待办

> **用途**：记录当前活跃、未关闭的工作项（项目级 TODO）。事项完成即移除；跨会话承接靠 [sessions/](sessions/README.md)。
> **目标读者**：仓库维护者 / AI Agent（每次开工前可扫一眼）。
> **关联文档**：[docs/README.md](README.md)（docs 总索引）。

> 状态：生效中（核验于 2026-09-09）。

## 待办

- [ ] **首个 gfx 模块落地**（如 window / 屏幕像素输出）：src/CMakeLists.txt 定义 `gfx` SHARED 目标、创建 version.hpp、补 tests 框架、细化 packaging/build_release.cmd 实际拷贝逻辑。
- [ ] 教学树首节点：在 [tutorials/](../tutorials/README.md) 挂首个主题（md + code），对应首个模块。
- [ ] 测试框架选定（见 [tests/](../tests/README.md) 约定）并登记 operations.md。
- [ ] naming.md 语言习惯细化（代码标识符/中文注释边界样例）。
- [ ] 首次正式 commit 前的逐项自检（见 docs/conventions/directory-organization.md 自检清单）。
