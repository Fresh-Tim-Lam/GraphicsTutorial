// window_win32.cpp —— gfx::Window 的 Windows GDI 实现（DIB 帧缓冲 + BitBlt 整帧上屏）
//
// 用途：把 Canvas 内容显示到屏幕的实现层。仅当前面向 Win32 GDI。
// 目标读者：gfx 像素模块实现者。
// 关联文档：window.hpp（公共头）；docs/design/adr-0001-gfx-math-canvas.md
//
// 状态：生效中（2026-09-09）

#include "gfx/window.hpp"

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

    ~Impl() {
        if (memDC && oldBitmap) ::SelectObject(memDC, oldBitmap);
        if (dib) ::DeleteObject(dib);
        if (memDC) ::DeleteDC(memDC);
        if (hwnd) ::DestroyWindow(hwnd);
    }
};

namespace {

Window* windowFromHwnd(HWND hwnd) {
    return reinterpret_cast<Window*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CLOSE:
            ::DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (Window* w = windowFromHwnd(hwnd)) w->requestClose();
            return 0;
        case WM_PAINT: {
            // 绘制完全由 present() 驱动；此处仅验证区域，避免闪烁清理
            PAINTSTRUCT ps;
            ::BeginPaint(hwnd, &ps);
            ::EndPaint(hwnd, &ps);
            return 0;
        }
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

    ::SetWindowLongPtrW(impl_->hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(this));
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

} // namespace gfx
