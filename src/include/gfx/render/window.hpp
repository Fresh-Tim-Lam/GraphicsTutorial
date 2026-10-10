// window.hpp —— gfx 顶层窗口（把 Canvas 像素显示到屏幕）
//
// 用途：创建原生窗口并把 Canvas 帧缓冲整帧上屏（present）。像素 API 见 gfx/core/canvas.hpp。
// 平台说明：当前实现面向 Windows GDI（内部 DIB + BitBlt）；公共头保持平台无关观感。
// 目标读者：gfx 像素模块使用者与实现者。
// 关联文档：docs/design/adr-0001-gfx-math-canvas.md；gfx/core/canvas.hpp；gfx/core/gfx_export.hpp
//
// 状态：生效中（2026-09-09）

#ifndef GFX_WINDOW_HPP
#define GFX_WINDOW_HPP

#include "gfx/core/canvas.hpp"
#include "gfx/core/gfx_export.hpp"

#include <array>
#include <memory>
#include <string>

namespace gfx {

// 鼠标状态（客户区像素坐标）
// @note dx/dy/wheel 是**本帧增量**：每次 pollEvents() 开始时清零，处理完本帧消息后累积；
//       拖拽旋转/缩放视角正是靠这三个增量。
struct MouseState {
    int x = 0;
    int y = 0;
    int dx = 0;          // 本帧水平移动增量
    int dy = 0;          // 本帧垂直移动增量
    int wheel = 0;       // 本帧滚轮增量（正 = 向前滚）
    bool leftDown = false;
    bool rightDown = false;
    bool middleDown = false;
};

// 非字符键（字母/数字/空格直接用 char 查询：keyPressed('W')、keyDown(' ')）
enum class Key {
    Space, Enter, Escape, Left, Right, Up, Down, Plus, Minus, Shift, Control, Tab
};

class GFX_API Window {
public:
    // 平台实现细节（定义在 window_win32.cpp）：仅为让窗口过程能直接更新输入状态而公开**声明**，
    // 使用者不需要关心（仍是不完整类型，无法在本头文件外实例化）。
    struct Impl;

private:
    std::unique_ptr<Impl> impl_; // unique_ptr 智能指针，自动管理实现细节内存，避免内存泄漏
    std::unique_ptr<Canvas> canvas_;
    bool open_ = true;

public:
    // 以指定客户区尺寸与标题创建窗口；title 按 UTF-8 输入（内部转宽字符）。
    Window(const char* title, int clientWidth, int clientHeight);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Canvas& canvas() { return *canvas_; }
    const Canvas& canvas() const { return *canvas_; }

    // 把 Canvas 内容整体显示到窗口（一次调用 = 一帧）
    void present();

    // 泵取窗口消息；返回 false 表示用户请求关闭
    // @note 进入时会清空"本帧增量"（鼠标 dx/dy/wheel 与"刚按下"表），再累积本帧消息
    bool pollEvents();

    // ---- 输入查询（在 pollEvents() 之后读取）----
    const MouseState& mouse() const;

    // 本帧是否**刚按下**（单次触发）：字符键大小写不敏感（'W' / 'w'、'0'-'9'、' '）
    bool keyPressed(char key) const;
    bool keyPressed(Key key) const;

    // 当前是否**按住**（用于连续调整，如方向键微调坐标）
    bool keyDown(char key) const;
    bool keyDown(Key key) const;

    bool isOpen() const { return open_; }
    void requestClose();
};

} // namespace gfx

#endif // GFX_WINDOW_HPP
