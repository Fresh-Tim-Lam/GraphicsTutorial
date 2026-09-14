# api-docs.md — 接口说明文档维护规范

> **用途**：规定 gfx 图形库与 gfx_math 子库**对外公共 API 的说明文档**如何书写与维护（权威源、注释格式、变更同步、生成物纪律），保证"使用者可直接调用"。
> **目标读者**：新增/修改 gfx 或 gfx_math 公共接口的开发者与 AI Agent；需要引用 API 写教程/示例的人。
> **关联文档**：[directory-organization.md](directory-organization.md)（目录/生成物规则）；[version.md](version.md)（版本语义）；[meta-rules.md](meta-rules.md)（规范元规则）；[docs-management.md](docs-management.md)（文档头部三要素）；[src/README.md](../../src/README.md)。

> 状态：生效中（核验于 2026-09-09）。

## 1. 适用范围

本规范约束的对象 = 对外交付的公共 API（`src/include/gfx/` 下、以 `GFX_API` 导出或属 gfx_math 头文件子库的符号），及其"使用说明"的三种载体：

| 载体 | 例子 |
|------|------|
| 公共头文件内注释 | Canvas、Window、Color、gfx_math 各 vec/mat 头文件 |
| 教学树/教程中的用法说明 | tutorials/ 下 md 与 code/ |
| 调用方接入说明 | packaging/release 的 include/链接/运行时说明（operations.md、packaging/README.md） |

## 2. 权威源与格式策略

- **格式**：公共头文件内每个公开符号使用 **Doxygen 风格块注释**（`/** @brief ... @param ... @return ... */`），使未来可用工具生成独立接口手册；当前**不引入生成工具/第三方依赖**。
- **权威源 = 头文件内注释**：一切"接口说明"以头文件注释为唯一事实源；教程/接入说明只引用、不整段复制签名，防双源漂移（冲突以头文件为准，并在文档标注"权威说明见 `src/include/gfx/...`"）。
- **生成物纪律**：若未来启用 Doxygen/手册生成，产物属生成物——不手改、不入 docs 正文，输出到 artifacts/ 或独立 build 目录（见 directory-organization.md）。

## 3. 注释内容要素

- 每个公共符号必须有 `@brief`：一句话独立可读的用途（禁止"详见 xxx"式）。
- 函数/方法标注 `@param`、`@return`；涉及坐标/颜色空间/边界时写清约定（如 Canvas 越界即忽略）。
- 前置条件/生命周期/平台限制写 `@note`（如 Window 仅 Win32、DIB 上屏行为）。
- 弃用接口标 `@deprecated` 并给出替代符号；文件顶部块注释保持"用途/目标读者/关联文档"三要素风格（与 docs-management 头部三要素对齐）。

## 4. 场景触发（规则依附场景）

- **[新增公共符号]**：实现时必须同步写齐注释，否则视为未完成（回归 meta-rules 违规分级）；如需教学/示例则同步 tutorials 用法。
- **[修改接口语义]**：1 轮对话内同步头文件注释与所有使用处（tutorials code/、调用示例、glossary 涉及术语）；破坏性变更须按 [version.md](version.md) 升版本号。
- **[删除/弃用接口]**：禁止静默删除——先 `@deprecated` 过渡并更新引用，移除须先 Ask（AGENT.md Boundaries）。
- **[行为边界细化]**：越界策略、矩阵可逆性、窗口生命周期等行为变化必须写进 `@note` 并在文档中提示版本。

## 5. 与教学树/调用方说明的同步

- tutorials 中 API 用法：引用权威头文件注释与源码位置，标注"权威实现见 `src/...`"，不复制整份签名。
- packaging/调用示例：公共接口签名变化时，须核对示例可编译并更新 `docs/guides/operations.md` 中的接入说明。

## 6. 违规后果

- 公共 API 缺注释 / 注释与实现不一致 / 跨会话未同步 → 按 meta-rules 违规分级处理（轻度：先补更新；重度：结论视为未生效需重新确认）。
