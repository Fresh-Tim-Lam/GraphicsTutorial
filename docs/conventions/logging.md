# logging.md — 日志规范

> **用途**：规定调试日志与正式日志的归属、命名与清理规则。
> **目标读者**：需要写临时日志排查问题的开发者与 AI Agent。
> **关联文档**：[directory-organization.md](directory-organization.md)；[logs/](../../logs/README.md)。

> 状态：生效中（核验于 2026-09-09）。

## 规则

1. **调试日志**统一进 `logs/`（目录整体 gitignore，仅保留 README.md），文件名含时间或场景前缀，如 `logs/2026-09-09_swapchain-diag.txt`。
2. **临时诊断/一次性输出**进 `artifacts/`（见 directory-organization 临时文件铁律），不混入 logs/ 正式结构。
3. **正式运行日志**：gfx 为库，默认不主动落盘；调用方决定日志去向。若未来需要运行日志，按模块约定并登记回本文档。
4. 日志内容注意：不得含 secrets/敏感路径；过期自清理。

## 防呆

- 根级兜底模式已含 `/debug-*.log`（见 .gitignore）——仓库根出现日志即违规。
