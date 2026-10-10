// window_win32.cpp —— gfx::Window 的 Windows GDI 实现（DIB 帧缓冲 + BitBlt 整帧上屏）
//
// 用途：把 Canvas 内容显示到屏幕的实现层。仅当前面向 Win32 GDI。
// 目标读者：gfx 像素模块实现者。
// 关联文档：window.hpp（公共头）；docs/design/adr-0001-gfx-math-canvas.md
//
// 状态：生效中（2026-09-09）

#include "gfx/render/window.hpp"

#ifndef _WIN32
#  error "gfx::Window 当前仅支持 Windows（Win32 GDI）"
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstring>

namespace gfx {

namespace {

const wchar_t* kWindowClassName = L"GraphicsTutorialWindow哈哈哈";

// UTF-8 -> UTF-16（窄标题转宽字符，供 CreateWindowExW 使用）
std::wstring utf8ToWide(const char* utf8) {
    if (utf8 == nullptr) return L"";
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    std::wstring ws(static_cast<std::size_t>(len > 0 ? len - 1 : 0), L'\0');
    if (len > 0) ::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &ws[0], len);
    return ws;
}

} // namespace

// 平台相关内部状态（在公共头中保持 opaque）
struct Window::Impl {
    HWND hwnd = nullptr;
    HDC memDC = nullptr;          // 内存 DC（承载 DIB）
    HBITMAP dib = nullptr;        // DIB 位图（32bpp，top-down）
    HBITMAP oldBitmap = nullptr;  // 选入前的旧位图
    std::uint32_t* dibBits = nullptr; // DIB 像素首地址（BGRA 布局，行主序）
    int clientWidth = 0;
    int clientHeight = 0;
    bool quitReceived = false;

    // ---- 输入状态（消息处理直接写这里；Window 的查询方法转发过来）----
    Window* owner = nullptr;                  // 回指，供 WM_DESTROY 调用 requestClose()
    MouseState mouse;                         // 鼠标状态（dx/dy/wheel 为本帧增量）
    std::array<bool, 256> keyDown{};          // 按住表（下标 = 虚拟键码）
    std::array<bool, 256> keyPressed{};       // 本帧"刚按下"表

    ~Impl() {
        if (memDC && oldBitmap) ::SelectObject(memDC, oldBitmap);
        if (dib) ::DeleteObject(dib);
        if (memDC) ::DeleteDC(memDC);
        if (hwnd) ::DestroyWindow(hwnd);
    }
};

namespace {

// 窗口句柄 → Impl（GWLP_USERDATA 存 Impl*，消息处理即可直接更新输入状态）
Window::Impl* implFromHwnd(HWND hwnd) {
    return reinterpret_cast<Window::Impl*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

// 字符键 → 虚拟键码（'A'-'Z' 大小写不敏感 / '0'-'9' / 空格）
int charToVirtualKey(char key) {
    if (key >= 'a' && key <= 'z') key = static_cast<char>(key - 'a' + 'A');
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) return key;
    if (key == ' ') return VK_SPACE;
    return 0;
}

int keyToVirtualKey(Key key) {
    switch (key) {
        case Key::Space:   return VK_SPACE;
        case Key::Enter:   return VK_RETURN;
        case Key::Escape:  return VK_ESCAPE;
        case Key::Left:    return VK_LEFT;
        case Key::Right:   return VK_RIGHT;
        case Key::Up:      return VK_UP;
        case Key::Down:    return VK_DOWN;
        case Key::Plus:    return VK_OEM_PLUS;
        case Key::Minus:   return VK_OEM_MINUS;
        case Key::Shift:   return VK_SHIFT;
        case Key::Control: return VK_CONTROL;
        case Key::Tab:     return VK_TAB;
    }
    return 0;
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Window::Impl* impl = implFromHwnd(hwnd);
    switch (msg) {
        case WM_CLOSE:
            ::DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (impl && impl->owner) impl->owner->requestClose();
            return 0;
        case WM_PAINT: {
            // 绘制完全由 present() 驱动；此处仅验证区域，避免闪烁清理
            PAINTSTRUCT ps;
            ::BeginPaint(hwnd, &ps);
            ::EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE:
            if (impl) {
                const int x = static_cast<short>(LOWORD(lParam));
                const int y = static_cast<short>(HIWORD(lParam));
                impl->mouse.dx += x - impl->mouse.x;   // 累积本帧位移（拖拽旋转用）
                impl->mouse.dy += y - impl->mouse.y;
                impl->mouse.x = x;
                impl->mouse.y = y;
            }
            return 0;
        case WM_LBUTTONDOWN: case WM_LBUTTONUP:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP:
            if (impl) {
                const bool down = (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN);
                if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) impl->mouse.leftDown = down;
                if (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP) impl->mouse.rightDown = down;
                if (msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP) impl->mouse.middleDown = down;
                impl->mouse.x = static_cast<short>(LOWORD(lParam));
                impl->mouse.y = static_cast<short>(HIWORD(lParam));
            }
            return 0;
        case WM_MOUSEWHEEL:
            if (impl) impl->mouse.wheel += GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (impl) {
                const int vk = static_cast<int>(wParam);
                if (vk >= 0 && vk < 256) {
                    if (!impl->keyDown[static_cast<std::size_t>(vk)])
                        impl->keyPressed[static_cast<std::size_t>(vk)] = true;   // 忽略系统自动重复
                    impl->keyDown[static_cast<std::size_t>(vk)] = true;
                }
            }
            return 0;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (impl) {
                const int vk = static_cast<int>(wParam);
                if (vk >= 0 && vk < 256) impl->keyDown[static_cast<std::size_t>(vk)] = false;
            }
            return 0;
        default:
            return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// 窗口客户区尺寸换算（去掉标题栏/边框）
bool computeClientOriginSize(int wantW, int wantH, int& outX, int& outY, int& outW, int& outH) {
    RECT rect{0, 0, wantW, wantH};
    const DWORD style = WS_OVERLAPPEDWINDOW;
    if (!::AdjustWindowRectEx(&rect, style, FALSE, 0)) return false;
    outW = rect.right - rect.left;
    outH = rect.bottom - rect.top;
    outX = (::GetSystemMetrics(SM_CXSCREEN) - outW) / 2;
    outY = (::GetSystemMetrics(SM_CYSCREEN) - outH) / 2;
    return true;
}

} // namespace

Window::Window(const char* title, int clientWidth, int clientHeight)
    : impl_(std::make_unique<Impl>()),
      canvas_(std::make_unique<Canvas>(clientWidth, clientHeight)) {

    if (clientWidth <= 0 || clientHeight <= 0) {
        open_ = false;
        return;
    }

    // 注册窗口类（进程内仅一次）
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &windowProc;
        wc.hInstance = ::GetModuleHandleW(nullptr);
        wc.hCursor = ::LoadCursorA(nullptr, IDC_ARROW);
        wc.lpszClassName = kWindowClassName;
        classRegistered = ::RegisterClassExW(&wc) != 0;
        if (!classRegistered) {
            open_ = false;
            return;
        }
    }

    int x, y, winW, winH;
    if (!computeClientOriginSize(clientWidth, clientHeight, x, y, winW, winH)) {
        open_ = false;
        return;
    }

    const std::wstring titleW = utf8ToWide(title);
    impl_->hwnd = ::CreateWindowExW(0, kWindowClassName, titleW.c_str(),
                                    WS_OVERLAPPEDWINDOW, x, y, winW, winH,
                                    nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    if (!impl_->hwnd) {
        open_ = false;
        return;
    }

    // GWLP_USERDATA 存 Impl*（消息处理要直接更新输入状态），并回指 Window 以便请求关闭
    impl_->owner = this;
    ::SetWindowLongPtrW(impl_->hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(impl_.get()));
    impl_->clientWidth = clientWidth;
    impl_->clientHeight = clientHeight;

    // 创建 32bpp DIB（负高度 => top-down，行序与 Canvas 一致）
    HDC screenDC = ::GetDC(nullptr);
    impl_->memDC = ::CreateCompatibleDC(screenDC);
    ::ReleaseDC(nullptr, screenDC);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = clientWidth;
    bmi.bmiHeader.biHeight = -clientHeight;     // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    impl_->dib = ::CreateDIBSection(impl_->memDC, &bmi, DIB_RGB_COLORS,
                                    reinterpret_cast<void**>(&impl_->dibBits), nullptr, 0);
    if (!impl_->dib || !impl_->dibBits) {
        open_ = false;
        return;
    }
    impl_->oldBitmap = static_cast<HBITMAP>(
        ::SelectObject(impl_->memDC, impl_->dib));

    ::ShowWindow(impl_->hwnd, SW_SHOW);
    ::UpdateWindow(impl_->hwnd);
}

Window::~Window() {
    // Impl 与 Canvas 经 unique_ptr 自动释放；impl_ 析构会清理 GDI 资源并销毁窗口
}

void Window::present() {
    if (!open_ || !impl_ || !impl_->hwnd || !impl_->dibBits) return;

    // 1) Canvas(RGBA8 结构体) -> DIB(32bpp BGRA 布局)
    //    32bpp BI_RGB 的像素字 = 0x00RRGGBB（小端内存序即 B,G,R,0）
    const Color* src = canvas_->data();
    const int total = impl_->clientWidth * impl_->clientHeight;
    for (int i = 0; i < total; ++i) {
        const Color& c = src[i];
        impl_->dibBits[i] = (static_cast<std::uint32_t>(c.r) << 16)
                          | (static_cast<std::uint32_t>(c.g) << 8)
                          |  static_cast<std::uint32_t>(c.b);
    }

    // 2) 整帧 BitBlt：内存 DC(DIB) -> 窗口 DC
    HDC winDC = ::GetDC(impl_->hwnd);
    if (winDC) {
        ::BitBlt(winDC, 0, 0, impl_->clientWidth, impl_->clientHeight,
                 impl_->memDC, 0, 0, SRCCOPY);
        ::ReleaseDC(impl_->hwnd, winDC);
    }
}

bool Window::pollEvents() {
    if (!impl_ || !impl_->hwnd) return false;

    // 进入新一帧：清空"增量"与"刚按下"（leftDown / x / y 这类**状态**保留）
    impl_->mouse.dx = 0;
    impl_->mouse.dy = 0;
    impl_->mouse.wheel = 0;
    impl_->keyPressed.fill(false);

    MSG msg;
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            impl_->quitReceived = true;
            continue;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    if (impl_->quitReceived) open_ = false;
    return open_;
}

void Window::requestClose() {
    open_ = false;
}

// ---- 输入查询（转发到 Impl 的输入状态）----

const MouseState& Window::mouse() const {
    return impl_->mouse;
}

bool Window::keyPressed(char key) const {
    const int vk = charToVirtualKey(key);
    return vk > 0 && impl_ && impl_->keyPressed[static_cast<std::size_t>(vk)];
}

bool Window::keyPressed(Key key) const {
    const int vk = keyToVirtualKey(key);
    return vk > 0 && impl_ && impl_->keyPressed[static_cast<std::size_t>(vk)];
}

bool Window::keyDown(char key) const {
    const int vk = charToVirtualKey(key);
    return vk > 0 && impl_ && impl_->keyDown[static_cast<std::size_t>(vk)];
}

bool Window::keyDown(Key key) const {
    const int vk = keyToVirtualKey(key);
    return vk > 0 && impl_ && impl_->keyDown[static_cast<std::size_t>(vk)];
}

} // namespace gfx
