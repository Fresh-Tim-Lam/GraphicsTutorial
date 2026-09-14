// window.hpp —— gfx 顶层窗口（把 Canvas 像素显示到屏幕）
//
// 用途：创建原生窗口并把 Canvas 帧缓冲整帧上屏（present）。像素 API 见 canvas.hpp。
// 平台说明：当前实现面向 Windows GDI（内部 DIB + BitBlt）；公共头保持平台无关观感。
// 目标读者：gfx 像素模块使用者与实现者。
// 关联文档：docs/design/adr-0001-gfx-math-canvas.md；canvas.hpp；gfx_export.hpp
//
// 状态：生效中（2026-09-09）

#ifndef GFX_WINDOW_HPP
#define GFX_WINDOW_HPP

#include "canvas.hpp"
#include "gfx_export.hpp"

#include <memory>
#include <string>

namespace gfx {

class GFX_API Window {
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
    bool pollEvents();

    bool isOpen() const { return open_; }
    void requestClose();

private:
    struct Impl;// 实现细节，隐藏在窗口类中
    std::unique_ptr<Impl> impl_; // unique_ptr 智能指针，自动管理实现细节内存，避免内存泄漏
    std::unique_ptr<Canvas> canvas_;
    bool open_ = true;
};

} // namespace gfx

#endif // GFX_WINDOW_HPP
