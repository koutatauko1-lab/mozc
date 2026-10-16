// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#ifndef MOZC_RENDERER_WIN32_DOPAGAKI_OVERLAY_H_
#define MOZC_RENDERER_WIN32_DOPAGAKI_OVERLAY_H_

#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wil/com.h>
#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "renderer/win32/dopagaki_effects.h"

namespace mozc {
namespace renderer {
namespace win32 {

// Returns the caret rectangle of the GUI thread of |window| in screen
// coordinates. Returns false if the application does not use the system caret.
bool GetDopagakiCaretRect(HWND window, RECT* rect);

// A transparent window that covers the monitor being typed on and draws
// DopagakiEffects with Direct2D on DirectComposition. It never takes focus and
// lets mouse clicks pass through. While there is nothing to draw, the window
// is hidden and the timer is stopped.
//
// If something fails (e.g. no GPU), only this overlay stops working; the
// renderer keeps running.
class DopagakiOverlay {
 public:
  DopagakiOverlay();
  DopagakiOverlay(const DopagakiOverlay&) = delete;
  DopagakiOverlay& operator=(const DopagakiOverlay&) = delete;
  ~DopagakiOverlay();

  // Must be called on the UI thread before adding effects. Moves the overlay
  // to the monitor of |target_window| (may be nullptr) if needed. Returns false
  // if the overlay is not available.
  bool Prepare(HWND target_window);
  // Converts a point in screen coordinates (physical pixels) to DIPs on the
  // overlay.
  D2D1_POINT_2F ScreenToOverlay(POINT point) const;
  DopagakiEffects& effects() { return effects_; }
  float pixels_per_dip() const { return static_cast<float>(dpi_) / 96.0f; }
  // Called before each frame is drawn, after the effects are updated. Used to
  // follow the candidate window and to shake it.
  void SetFrameCallback(std::function<void()> callback) {
    frame_callback_ = std::move(callback);
  }
  // Shows the overlay and draws frames until there is nothing to draw.
  void StartAnimation();
  // Puts the overlay above other topmost windows (e.g. the candidate window).
  void BringToTop();
  void Destroy();

 private:
  struct GlowText {
    wil::com_ptr_nothrow<ID2D1Bitmap1> bitmap;
    float padding = 0;  // Space around the text for the glow, in DIPs.
    float text_width = 0;
    float text_height = 0;
    float bitmap_width = 0;
    float bitmap_height = 0;
  };

  static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam,
                                     LPARAM lparam);
  bool EnsureWindow();
  bool EnsureDevice();
  void ReleaseDevice();
  void OnDeviceFailure();
  void OnTimer();
  bool DrawFrame();
  // The glow along the edges of the monitor (phase 3).
  void DrawEdge();
  // Fills the band between |inner| and |outer| DIPs from the edges, like a
  // picture frame. |brushes| are for the top, right, bottom and left sides.
  void FillFrame(float outer, float inner, ID2D1Brush* const brushes[4]);
  // A conic gradient of the preview approximated by a linear gradient along a
  // side from |from| to |to|. |color_at| returns the color at a point.
  wil::com_ptr_nothrow<ID2D1LinearGradientBrush> CreateGradientAlong(
      D2D1_POINT_2F from, D2D1_POINT_2F to,
      const std::function<DopagakiColor(float x, float y)>& color_at);
  // The glowing frame and "x42" of the candidate window (phase 4).
  void DrawCandidateFrame();
  void UpdateBadgeCache();
  void Hide();
  bool IsHealthy() const;
  void UpdateBannerCache();
  void UpdateComboLabelCache();
  // Draws |text| with glows of CSS blur radii |blur1| and |blur2| (0 for
  // none) and a black shadow |shadow_y| below, like text-shadow of the
  // preview.
  bool CreateGlowText(const std::wstring& text, IDWriteTextFormat* format,
                      const D2D1_COLOR_F& color, float blur1, float blur2,
                      float shadow_y, GlowText* glow_text);
  int width_pixels() const { return monitor_rect_.right - monitor_rect_.left; }
  int height_pixels() const {
    return monitor_rect_.bottom - monitor_rect_.top;
  }

  DopagakiEffects effects_;
  HWND window_ = nullptr;
  bool disabled_ = false;
  int failure_count_ = 0;
  HMONITOR monitor_ = nullptr;
  RECT monitor_rect_ = {};
  UINT dpi_ = 96;
  bool visible_ = false;
  bool timer_running_ = false;
  int64_t last_tick_microseconds_ = 0;

  wil::com_ptr_nothrow<ID3D11Device> d3d_device_;
  wil::com_ptr_nothrow<IDXGISwapChain1> swap_chain_;
  wil::com_ptr_nothrow<IDCompositionDevice> composition_device_;
  wil::com_ptr_nothrow<IDCompositionTarget> composition_target_;
  wil::com_ptr_nothrow<IDCompositionVisual> composition_visual_;
  wil::com_ptr_nothrow<ID2D1Factory1> d2d_factory_;
  wil::com_ptr_nothrow<ID2D1Device> d2d_device_;
  wil::com_ptr_nothrow<ID2D1DeviceContext> context_;
  wil::com_ptr_nothrow<ID2D1Bitmap1> target_bitmap_;
  wil::com_ptr_nothrow<ID2D1SolidColorBrush> brush_;
  wil::com_ptr_nothrow<IDWriteFactory> dwrite_factory_;
  wil::com_ptr_nothrow<IDWriteTextFormat> combo_label_format_;

  uint64_t banner_cache_id_ = 0;
  GlowText banner_text_;
  uint64_t combo_label_cache_id_ = 0;
  GlowText combo_label_text_;
  uint64_t badge_cache_id_ = 0;
  GlowText badge_text_;
  std::function<void()> frame_callback_;
};

}  // namespace win32
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_WIN32_DOPAGAKI_OVERLAY_H_
