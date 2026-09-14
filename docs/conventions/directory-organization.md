# directory-organization.md — 目录组织规范

> **用途**：定义 GraphicsTutorial 顶层目录职责、根目录白名单、临时文件铁律与文件落位决策。是"文件该放哪"的最高频决策依据。
> **目标读者**：所有需要新增/移动文件的开发者与 AI Agent。
> **关联文档**：[docs/README.md](../README.md)；[docs-management.md](docs-management.md)；[naming.md](naming.md)。

> 状态：生效中（核验于 2026-09-09）。

## 顶层目录职责表

| 目录 | 职责 |
|------|------|
| [src/](../../src/README.md) | gfx 渲染 API 权威实现（头文件 include/gfx、实现、CMake 库目标） |
| [tutorials/](../../tutorials/README.md) | 树状教学文档 + 每主题可运行演示（md + code，非瀑布编号章节） |
| [tests/](../../tests/README.md) | 正式测试 |
| [benchmarks/](../../benchmarks/README.md) | 正式性能基准（可复现；性能结论的唯一依据，见 [performance.md](performance.md)） |
| [docs/](../../docs/README.md) | 规范文档中心（子目录职责见 docs-management） |
| [scripts/](../../scripts/README.md) | 正式可复用的开发者工具 |
| [resources/](../../resources/README.md) | 随项目分发静态资源 |
| [packaging/](../../packaging/README.md) | 打包发布（唯一入口 build_release.cmd） |
| [artifacts/](../../artifacts/README.md) | 全仓唯一临时/调试产物区（gitignore） |
| [logs/](../../logs/README.md) | 开发调试日志（gitignore） |

## 根目录白名单

`README*`、`AGENT.md`、`LICENSE`、包清单/构建入口（`CMakeLists.txt`、`.gitignore`、`.gitattributes`、CI 配置）。其他一律归位到对应子目录。

## 临时文件铁律（全项目最高频决策）

- 名字带 `tmp`/`debug`/`diag`/`repro`/`shot` 前缀且非正式产物、一次性调试脚本、截图、诊断输出 → 一律进 `artifacts/`。
- **禁止**出现在：仓库根、`src/`、`tutorials/`（正式内容树）、`docs/` 正文目录。
- `.gitignore` 已有根级兜底模式（`/tmp-*.cpp`、`/diag-*.txt`、`/shot-*.png`、`/debug-*.log`）。
- 开发调试日志 → `logs/`（gitignore），命名见 [logging.md](logging.md)。
- **升级路径**：`artifacts/` 里的一次性脚本一旦成为正式产物（测试用例、基准、可复用工具），必须移入 `tests/` / `benchmarks/` / `scripts/`；性能结论的依据**必须**在 `benchmarks/`（见 [performance.md](performance.md)）。

## 文件落位决策

教学/说明 → `tutorials/`（树状主题）；契约规则 → conventions；how-to → guides；理解系统/外部参考 → refs；设计决策 → design；会话 → sessions。docs 内精确映射见 docs-management。

## 任务收尾自检清单（Definition of Done）

- [ ] 新文件落位正确（不在根目录/不混入临时物）
- [ ] 临时文件进了 artifacts/ 且被 gitignore 覆盖
- [ ] 父目录 README 索引已同步（含 docs 登记表，如需）
