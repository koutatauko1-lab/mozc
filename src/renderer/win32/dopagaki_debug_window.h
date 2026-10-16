// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#ifndef MOZC_RENDERER_WIN32_DOPAGAKI_DEBUG_WINDOW_H_
#define MOZC_RENDERER_WIN32_DOPAGAKI_DEBUG_WINDOW_H_

#include <windows.h>

#include <cstdint>
#include <string>

#include "renderer/win32/dopagaki_state.h"

namespace mozc {
namespace renderer {
namespace win32 {

// TEMPORARY (phase 1 only): a small window that shows "COMBO 12 / A" near the
// bottom-right corner of the monitor being typed on. It never takes focus and
// lets mouse clicks pass through. Will be replaced by the overlay in phase 2.
class DopagakiDebugWindow {
 public:
  DopagakiDebugWindow() = default;
  DopagakiDebugWindow(const DopagakiDebugWindow&) = delete;
  DopagakiDebugWindow& operator=(const DopagakiDebugWindow&) = delete;
  ~DopagakiDebugWindow();

  // Must be called on the UI thread of the renderer. |target_window| is used
  // to choose the monitor and may be nullptr.
  void Show(uint64_t combo, DopagakiRank rank, bool broke, HWND target_window);
  void Destroy();

 private:
  static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam,
                                     LPARAM lparam);
  bool EnsureWindow();
  void Paint(HDC dc, const RECT& rect) const;

  HWND window_ = nullptr;
  std::wstring text_;
  COLORREF color_ = RGB(0x4D, 0xA3, 0xFF);
  int font_height_ = 28;
};

}  // namespace win32
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_WIN32_DOPAGAKI_DEBUG_WINDOW_H_
