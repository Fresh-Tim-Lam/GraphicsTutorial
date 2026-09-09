# logs/ — 开发调试日志（gitignore）

> **用途**：开发调试日志的统一定位目录。
> **目标读者**：排查问题时需要临时日志的开发者与 AI Agent。
> **关联文档**：[logging.md](../docs/conventions/logging.md)（日志规范）。

> 状态：生效中（核验于 2026-09-09）。

## 规则

- 本目录整体被 .gitignore 忽略，`README.md` 为保留文件。
- 调试日志文件名带时间或场景前缀（规则见 logging.md）。
- 正式运行日志按模块约定另行存放。
