# win32-api-notes.md — Win32 / MinGW 头文件与类型速查

> **用途**：摘录阅读 Windows/MinGW 头文件（如 `stringapiset.h`）时遇到的名称与类型释义，供本项目（gfx::Window 的 Win32 实现）查阅。
> **目标读者**：阅读 Win32 头文件、维护 window_win32.cpp 的开发者与 AI Agent。
> **关联文档**：[src/window_win32.cpp](../../src/window_win32.cpp)（实际用法）；[refs/README.md](README.md)。

> 状态：生效中（核验于 2026-09-09）。属速查摘录；权威定义以 MS 官方文档 / 工具链头文件为准。

## 1. 头文件家族

| 名称 | 含义 | 备注 |
|------|------|------|
| `apiset.h` | Windows **API Set（API 集）** 机制声明：提供 `WINAPI_FAMILY_PARTITION(...)` 等宏，标注函数属于哪个 API 家族分区（`WINAPI_PARTITION_DESKTOP` 桌面 / `WINAPI_PARTITION_APP` 应用） | API Set 是 Win10 起的**虚拟 DLL 转发**：链接期 DLL 名可能只是逻辑名，运行时由系统转发到真实实现 |
| `apisetcconv.h` | API Set 各头共用的**调用约定**定义（统一 `WINAPI` 等修饰，保证声明一致） | 保证不同 apiset 头跨平台/跨配置一致 |
| `minwindef.h` | Windows **基础类型**定义：`BYTE/WORD/DWORD/BOOL/LPBOOL/PWINBOOL` 等 | 不含 GUI 部分（GUI 类型在 `windef.h`） |
| `winnls.h` | **NLS（国家语言支持）**：`LCID`、代码页常量（`CP_UTF8` 等）、多字节/宽字符转换相关声明 | 编码转换与本地化比较的声明来源 |

## 2. 类型与常量

| 名称 | 含义 |
|------|------|
| `DWORD` | **32 位无符号整数**（Double Word，对应 `unsigned long`）；Windows 最常用类型别名 |
| `UINT` | 32 位无符号整数（此处用于 `CodePage`，如 `CP_UTF8`） |
| `LCID` | **Language Code Identifier**：32 位语言/区域标识（低 16 位语言 ID + 排序 ID）。例：`0x0409` = 英语(美国) |
| `WINBOOL` | Windows 布尔类型（`int`），非 C++ `bool` |
| `LPCCH / LPSTR / LPWSTR / LPCWCH / PCNZWCH` | 指针类型命名规律：`LP`=Long Pointer，`C`=const，`W`=宽字符，`CH`=字符指针 |

## 3. 与本项目的关联

`stringapiset.h` 声明的编码转换函数正是本项目窗口标题转换所用（[window_win32.cpp](../../src/window_win32.cpp)）：

- `MultiByteToWideChar(CodePage, dwFlags, lpMultiByteStr, cbMultiByte, lpWideCharStr, cchWideChar)`
  —— 本项目以 `CP_UTF8` 把 UTF-8 标题转成 UTF-16 供 `CreateWindowExW` 使用；
- `WideCharToMultiByte(...)` —— 反向转换（当前未用）；
- `CompareStringW / GetStringTypeExW` —— 依赖 `LCID` 的本地化字符串比较（当前未用）。

> 教学提示：`API Set` 机制解释了为什么很多 Win32 函数不必关心"真实 DLL 在哪"——系统在运行时完成转发。
