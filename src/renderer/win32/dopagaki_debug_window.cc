// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#include "renderer/win32/dopagaki_debug_window.h"

#include <shellscalingapi.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>

#include "renderer/win32/dopagaki_state.h"

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

constexpr wchar_t kClassName[] = L"MozcDopagakiDebugWindow";
constexpr UINT_PTR kHideTimerId = 1;
constexpr UINT kHideDelayMs = 5000;
// Sizes at 96 dpi.
constexpr int kWidth = 300;
constexpr int kHeight = 56;
constexpr int kMargin = 24;
constexpr int kFontHeight = 30;

COLORREF GetRankColor(DopagakiRank rank) {
  switch (rank) {
    case DopagakiRank::kB:
      return RGB(0x4D, 0xA3, 0xFF);
    case DopagakiRank::kA:
      return RGB(0x3B, 0xE3, 0x7A);
    case DopagakiRank::kS:
      return RGB(0xFF, 0xD2, 0x3F);
    case DopagakiRank::kSS:
      return RGB(0xFF, 0x4F, 0xD8);
    case DopagakiRank::kSSS:
      return RGB(0xFF, 0xFF, 0xFF);
  }
  return RGB(0x4D, 0xA3, 0xFF);
}

bool RegisterWindowClass(HINSTANCE instance, WNDPROC window_proc) {
  WNDCLASSEXW window_class = {};
  if (::GetClassInfoExW(instance, kClassName, &window_class)) {
    return true;
  }
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = window_proc;
  window_class.hInstance = instance;
  window_class.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
  window_class.lpszClassName = kClassName;
  return ::RegisterClassExW(&window_class) != 0;
}

}  // namespace

DopagakiDebugWindow::~DopagakiDebugWindow() { Destroy(); }

void DopagakiDebugWindow::Destroy() {
  if (window_ != nullptr && ::IsWindow(window_)) {
    ::DestroyWindow(window_);
  }
  window_ = nullptr;
}

bool DopagakiDebugWindow::EnsureWindow() {
  if (window_ != nullptr && ::IsWindow(window_)) {
    return true;
  }
  const HINSTANCE instance = ::GetModuleHandleW(nullptr);
  if (!RegisterWindowClass(instance, &DopagakiDebugWindow::WindowProc)) {
    return false;
  }
  window_ = ::CreateWindowExW(
      WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
          WS_EX_NOACTIVATE,
      kClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, this);
  if (window_ == nullptr) {
    return false;
  }
  ::SetLayeredWindowAttributes(window_, 0, 230, LWA_ALPHA);
  return true;
}

void DopagakiDebugWindow::Show(uint64_t combo, DopagakiRank rank, bool broke,
                               HWND target_window) {
  if (!EnsureWindow()) {
    return;
  }

  wchar_t buffer[64] = {};
  ::swprintf_s(buffer, std::size(buffer), L"COMBO %llu / %hs%ls",
               static_cast<unsigned long long>(combo),
               GetDopagakiRankName(rank), broke ? L"  BREAK" : L"");
  text_ = buffer;
  color_ = broke ? RGB(0xFF, 0x2A, 0x3D) : GetRankColor(rank);

  if (target_window == nullptr) {
    target_window = ::GetForegroundWindow();
  }
  const HMONITOR monitor =
      ::MonitorFromWindow(target_window, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO monitor_info = {};
  monitor_info.cbSize = sizeof(monitor_info);
  if (!::GetMonitorInfoW(monitor, &monitor_info)) {
    return;
  }
  UINT dpi_x = 96;
  UINT dpi_y = 96;
  if (FAILED(::GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y))) {
    dpi_x = 96;
  }
  const auto scale = [dpi_x](int value) {
    return ::MulDiv(value, static_cast<int>(dpi_x), 96);
  };
  font_height_ = scale(kFontHeight);
  const RECT& work = monitor_info.rcWork;
  const int width = scale(kWidth);
  const int height = scale(kHeight);
  const int x = work.right - width - scale(kMargin);
  const int y = work.bottom - height - scale(kMargin);
  ::SetWindowPos(window_, HWND_TOPMOST, x, y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
  ::InvalidateRect(window_, nullptr, TRUE);
  // Restarts the timer if it is already running.
  ::SetTimer(window_, kHideTimerId, kHideDelayMs, nullptr);
}

void DopagakiDebugWindow::Paint(HDC dc, const RECT& rect) const {
  const HBRUSH background = ::CreateSolidBrush(RGB(0x1D, 0x1B, 0x2A));
  ::FillRect(dc, &rect, background);
  ::DeleteObject(background);

  const HFONT font = ::CreateFontW(
      -font_height_, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
  const HGDIOBJ old_font = ::SelectObject(dc, font);
  ::SetBkMode(dc, TRANSPARENT);
  ::SetTextColor(dc, color_);
  RECT text_rect = rect;
  ::DrawTextW(dc, text_.c_str(), static_cast<int>(text_.size()), &text_rect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  ::SelectObject(dc, old_font);
  ::DeleteObject(font);
}

LRESULT CALLBACK DopagakiDebugWindow::WindowProc(HWND window, UINT message,
                                                 WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    const auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(lparam);
    ::SetWindowLongPtrW(
        window, GWLP_USERDATA,
        reinterpret_cast<LONG_PTR>(create_struct->lpCreateParams));
  }
  const auto* self = reinterpret_cast<DopagakiDebugWindow*>(
      ::GetWindowLongPtrW(window, GWLP_USERDATA));
  switch (message) {
    case WM_PAINT: {
      PAINTSTRUCT paint = {};
      const HDC dc = ::BeginPaint(window, &paint);
      if (dc != nullptr && self != nullptr) {
        RECT client = {};
        ::GetClientRect(window, &client);
        self->Paint(dc, client);
      }
      ::EndPaint(window, &paint);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_TIMER:
      if (wparam == kHideTimerId) {
        ::KillTimer(window, kHideTimerId);
        ::ShowWindow(window, SW_HIDE);
        return 0;
      }
      break;
    case WM_NCHITTEST:
      return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    default:
      break;
  }
  return ::DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace win32
}  // namespace renderer
}  // namespace mozc
