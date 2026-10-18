// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#include "renderer/win32/dopagaki_overlay.h"

#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <d2d1effects.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <shellscalingapi.h>
#include <wil/com.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <random>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "renderer/win32/dopagaki_effects.h"
#include "renderer/win32/dopagaki_praise.h"
#include "renderer/win32/dopagaki_state.h"

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

constexpr wchar_t kClassName[] = L"MozcDopagakiOverlay";
constexpr UINT_PTR kTimerId = 1;
// About 60 fps. The actual elapsed time is used for the animation.
constexpr UINT kTimerIntervalMs = 15;
// The overlay is turned off after this many failures (e.g. GPU resets).
constexpr int kMaxFailures = 3;
constexpr float kPi = 3.14159265358979323846f;

// text-shadow values of preview.html (CSS blur radius and y offset).
constexpr float kBannerGlow1 = 30;
constexpr float kBannerGlow2 = 80;
constexpr float kBannerShadowY = 6;
constexpr float kComboLabelGlow = 10;
constexpr float kComboLabelShadowY = 2;
constexpr float kComboLabelFontSize = 22;
// Update 1 (1.5).
constexpr float kPraiseShadowY = 4;
constexpr float kPraiseShadowOpacity = 0.85f;
constexpr float kPraiseMaxWidthRatio = 0.7f;
// The size of the layout box used to measure a word.
constexpr float kPraiseLayoutSize = 10000;

void Log(const wchar_t* message, HRESULT hr) {
  wchar_t buffer[160] = {};
  ::swprintf_s(buffer, std::size(buffer), L"[dopagaki] overlay %ls hr=0x%08lX\n",
               message, static_cast<unsigned long>(hr));
  ::OutputDebugStringW(buffer);
}

int64_t GetMicroseconds() {
  static const int64_t frequency = [] {
    LARGE_INTEGER value = {};
    ::QueryPerformanceFrequency(&value);
    return value.QuadPart > 0 ? value.QuadPart : 1;
  }();
  LARGE_INTEGER counter = {};
  ::QueryPerformanceCounter(&counter);
  return counter.QuadPart / frequency * 1000000 +
         counter.QuadPart % frequency * 1000000 / frequency;
}

// Only used on the UI thread of the renderer.
double NextRandom() {
  static std::mt19937 engine(static_cast<uint32_t>(GetMicroseconds()));
  static std::uniform_real_distribution<double> distribution(0.0, 1.0);
  return distribution(engine);
}

D2D1_COLOR_F ToD2DColor(const DopagakiColor& color, float alpha) {
  return D2D1::ColorF(color.r, color.g, color.b, alpha);
}

bool HasFontFamily(IDWriteFactory* factory, const wchar_t* family) {
  wil::com_ptr_nothrow<IDWriteFontCollection> collection;
  if (FAILED(factory->GetSystemFontCollection(collection.put(), FALSE))) {
    return false;
  }
  UINT32 index = 0;
  BOOL exists = FALSE;
  return SUCCEEDED(collection->FindFamilyName(family, &index, &exists)) &&
         exists;
}

}  // namespace

bool GetDopagakiCaretRect(HWND window, RECT* rect) {
  if (window == nullptr || rect == nullptr) {
    return false;
  }
  const DWORD thread_id = ::GetWindowThreadProcessId(window, nullptr);
  if (thread_id == 0) {
    return false;
  }
  GUITHREADINFO info = {};
  info.cbSize = sizeof(info);
  if (!::GetGUIThreadInfo(thread_id, &info) || info.hwndCaret == nullptr) {
    return false;
  }
  RECT caret = info.rcCaret;
  if (caret.right <= caret.left && caret.bottom <= caret.top) {
    return false;
  }
  ::MapWindowPoints(info.hwndCaret, nullptr, reinterpret_cast<POINT*>(&caret),
                    2);
  *rect = caret;
  return true;
}

DopagakiOverlay::DopagakiOverlay() : effects_(&NextRandom) {}

DopagakiOverlay::~DopagakiOverlay() { Destroy(); }

bool DopagakiOverlay::Prepare(HWND target_window) {
  if (disabled_) {
    return false;
  }
  if (!EnsureWindow()) {
    Log(L"window creation failed", HRESULT_FROM_WIN32(::GetLastError()));
    disabled_ = true;
    return false;
  }
  if (target_window == nullptr) {
    target_window = ::GetForegroundWindow();
  }
  const HMONITOR monitor =
      ::MonitorFromWindow(target_window, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO monitor_info = {};
  monitor_info.cbSize = sizeof(monitor_info);
  if (!::GetMonitorInfoW(monitor, &monitor_info)) {
    return false;
  }
  UINT dpi_x = 96;
  UINT dpi_y = 96;
  if (FAILED(::GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y)) ||
      dpi_x == 0) {
    dpi_x = 96;
  }
  if (monitor != monitor_ ||
      !::EqualRect(&monitor_info.rcMonitor, &monitor_rect_) ||
      dpi_x != dpi_) {
    // Moved to another monitor (or the display settings changed). The effects
    // on the previous monitor are dropped.
    Hide();
    ReleaseDevice();
    effects_.Clear();
    monitor_ = monitor;
    monitor_rect_ = monitor_info.rcMonitor;
    dpi_ = dpi_x;
    ::SetWindowPos(window_, HWND_TOPMOST, monitor_rect_.left, monitor_rect_.top,
                   width_pixels(), height_pixels(), SWP_NOACTIVATE);
  }
  if (!EnsureDevice()) {
    OnDeviceFailure();
    return false;
  }
  const float dips_per_pixel = 96.0f / static_cast<float>(dpi_);
  effects_.SetScreenSize(width_pixels() * dips_per_pixel,
                         height_pixels() * dips_per_pixel);
  BOOL animation = TRUE;
  if (::SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animation, 0)) {
    effects_.SetReducedMotion(!animation);
  }
  return true;
}

D2D1_POINT_2F DopagakiOverlay::ScreenToOverlay(POINT point) const {
  const float dips_per_pixel = 96.0f / static_cast<float>(dpi_);
  return D2D1::Point2F((point.x - monitor_rect_.left) * dips_per_pixel,
                       (point.y - monitor_rect_.top) * dips_per_pixel);
}

void DopagakiOverlay::StartAnimation() {
  if (!IsHealthy()) {
    return;
  }
  last_tick_microseconds_ = GetMicroseconds();
  if (!visible_) {
    ::ShowWindow(window_, SW_SHOWNOACTIVATE);
    visible_ = true;
  }
  BringToTop();
  if (!timer_running_) {
    ::SetTimer(window_, kTimerId, kTimerIntervalMs, nullptr);
    timer_running_ = true;
  }
  if (frame_callback_) {
    frame_callback_();
  }
  if (!DrawFrame()) {
    OnDeviceFailure();
  }
}

void DopagakiOverlay::BringToTop() {
  if (visible_ && window_ != nullptr) {
    ::SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
}

void DopagakiOverlay::Stop() {
  effects_.Clear();
  if (frame_callback_) {
    frame_callback_();
  }
  Hide();
}

void DopagakiOverlay::Destroy() {
  if (window_ != nullptr && ::IsWindow(window_)) {
    if (timer_running_) {
      ::KillTimer(window_, kTimerId);
    }
    ::DestroyWindow(window_);
  }
  timer_running_ = false;
  visible_ = false;
  window_ = nullptr;
  ReleaseDevice();
}

bool DopagakiOverlay::EnsureWindow() {
  if (window_ != nullptr && ::IsWindow(window_)) {
    return true;
  }
  window_ = nullptr;
  visible_ = false;
  timer_running_ = false;
  monitor_ = nullptr;
  ReleaseDevice();

  const HINSTANCE instance = ::GetModuleHandleW(nullptr);
  WNDCLASSEXW window_class = {};
  window_class.cbSize = sizeof(window_class);
  if (!::GetClassInfoExW(instance, kClassName, &window_class)) {
    window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = &DopagakiOverlay::WindowProc;
    window_class.hInstance = instance;
    window_class.lpszClassName = kClassName;
    if (::RegisterClassExW(&window_class) == 0) {
      return false;
    }
  }
  // WS_EX_LAYERED | WS_EX_TRANSPARENT lets mouse clicks pass through.
  // WS_EX_NOREDIRECTIONBITMAP is for DirectComposition.
  window_ = ::CreateWindowExW(
      WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED | WS_EX_TRANSPARENT |
          WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
      kClassName, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, this);
  if (window_ == nullptr) {
    return false;
  }
  // A layered window is not shown until its attributes are set.
  ::SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA);
  return true;
}

bool DopagakiOverlay::IsHealthy() const {
  return !disabled_ && window_ != nullptr && context_ && target_bitmap_ &&
         swap_chain_;
}

bool DopagakiOverlay::EnsureDevice() {
  if (IsHealthy()) {
    return true;
  }
  ReleaseDevice();
  if (width_pixels() <= 0 || height_pixels() <= 0) {
    return false;
  }

  const D3D_FEATURE_LEVEL levels[] = {
      D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0};
  HRESULT hr = ::D3D11CreateDevice(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
      D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
      static_cast<UINT>(std::size(levels)),
      D3D11_SDK_VERSION, d3d_device_.put(), nullptr, nullptr);
  if (FAILED(hr)) {
    Log(L"D3D11CreateDevice failed", hr);
    return false;
  }
  const auto dxgi_device = d3d_device_.try_query<IDXGIDevice>();
  if (!dxgi_device) {
    Log(L"IDXGIDevice is not available", E_NOINTERFACE);
    return false;
  }
  wil::com_ptr_nothrow<IDXGIAdapter> adapter;
  wil::com_ptr_nothrow<IDXGIFactory2> dxgi_factory;
  if (FAILED(hr = dxgi_device->GetAdapter(adapter.put())) ||
      FAILED(hr = adapter->GetParent(IID_PPV_ARGS(dxgi_factory.put())))) {
    Log(L"IDXGIFactory2 is not available", hr);
    return false;
  }

  DXGI_SWAP_CHAIN_DESC1 desc = {};
  desc.Width = static_cast<UINT>(width_pixels());
  desc.Height = static_cast<UINT>(height_pixels());
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.Scaling = DXGI_SCALING_STRETCH;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
  hr = dxgi_factory->CreateSwapChainForComposition(d3d_device_.get(), &desc,
                                                   nullptr, swap_chain_.put());
  if (FAILED(hr)) {
    Log(L"CreateSwapChainForComposition failed", hr);
    return false;
  }

  D2D1_FACTORY_OPTIONS options = {};
  hr = ::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                           __uuidof(ID2D1Factory1), &options,
                           d2d_factory_.put_void());
  if (FAILED(hr) ||
      FAILED(hr = d2d_factory_->CreateDevice(dxgi_device.get(),
                                             d2d_device_.put())) ||
      FAILED(hr = d2d_device_->CreateDeviceContext(
                 D2D1_DEVICE_CONTEXT_OPTIONS_NONE, context_.put()))) {
    Log(L"Direct2D is not available", hr);
    return false;
  }
  context_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
  // ClearType does not work on transparent surfaces.
  context_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

  wil::com_ptr_nothrow<IDXGISurface> surface;
  hr = swap_chain_->GetBuffer(0, IID_PPV_ARGS(surface.put()));
  if (SUCCEEDED(hr)) {
    const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                          D2D1_ALPHA_MODE_PREMULTIPLIED),
        static_cast<float>(dpi_), static_cast<float>(dpi_));
    hr = context_->CreateBitmapFromDxgiSurface(surface.get(), &properties,
                                               target_bitmap_.put());
  }
  if (FAILED(hr)) {
    Log(L"the target bitmap is not available", hr);
    return false;
  }
  context_->SetTarget(target_bitmap_.get());
  if (FAILED(hr = context_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1),
                                                  brush_.put()))) {
    Log(L"CreateSolidColorBrush failed", hr);
    return false;
  }

  hr = ::DWriteCreateFactory(
      DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
      reinterpret_cast<IUnknown**>(dwrite_factory_.put()));
  if (FAILED(hr)) {
    Log(L"DirectWrite is not available", hr);
    return false;
  }
  const wchar_t* combo_label_family =
      HasFontFamily(dwrite_factory_.get(), L"Bahnschrift") ? L"Bahnschrift"
                                                           : L"Segoe UI";
  hr = dwrite_factory_->CreateTextFormat(
      combo_label_family, nullptr, DWRITE_FONT_WEIGHT_BOLD,
      DWRITE_FONT_STYLE_ITALIC, DWRITE_FONT_STRETCH_NORMAL,
      kComboLabelFontSize, L"en-us", combo_label_format_.put());
  if (FAILED(hr)) {
    Log(L"CreateTextFormat failed", hr);
    return false;
  }

  hr = ::DCompositionCreateDevice(dxgi_device.get(),
                                  IID_PPV_ARGS(composition_device_.put()));
  if (FAILED(hr) ||
      FAILED(hr = composition_device_->CreateTargetForHwnd(
                 window_, TRUE, composition_target_.put())) ||
      FAILED(hr = composition_device_->CreateVisual(
                 composition_visual_.put())) ||
      FAILED(hr = composition_visual_->SetContent(swap_chain_.get())) ||
      FAILED(hr = composition_target_->SetRoot(composition_visual_.get())) ||
      FAILED(hr = composition_device_->Commit())) {
    Log(L"DirectComposition is not available", hr);
    return false;
  }
  return true;
}

void DopagakiOverlay::ReleaseDevice() {
  praise_sprites_.clear();
  banner_cache_id_ = 0;
  banner_text_ = {};
  combo_label_cache_id_ = 0;
  combo_label_text_ = {};
  badge_cache_id_ = 0;
  badge_text_ = {};
  combo_label_format_.reset();
  if (context_) {
    context_->SetTarget(nullptr);
  }
  target_bitmap_.reset();
  brush_.reset();
  context_.reset();
  d2d_device_.reset();
  d2d_factory_.reset();
  composition_visual_.reset();
  composition_target_.reset();
  composition_device_.reset();
  swap_chain_.reset();
  d3d_device_.reset();
  dwrite_factory_.reset();
}

void DopagakiOverlay::OnDeviceFailure() {
  Hide();
  ReleaseDevice();
  ++failure_count_;
  if (failure_count_ >= kMaxFailures) {
    Log(L"is turned off after repeated failures", E_FAIL);
    disabled_ = true;
    Destroy();
  }
}

void DopagakiOverlay::OnTimer() {
  const int64_t now = GetMicroseconds();
  const double elapsed_ms = (now - last_tick_microseconds_) / 1000.0;
  last_tick_microseconds_ = now;
  effects_.Update(elapsed_ms);
  if (frame_callback_) {
    frame_callback_();
  }
  if (!effects_.IsActive()) {
    Hide();
    return;
  }
  if (!DrawFrame()) {
    OnDeviceFailure();
  }
}

void DopagakiOverlay::Hide() {
  if (timer_running_ && window_ != nullptr) {
    ::KillTimer(window_, kTimerId);
  }
  timer_running_ = false;
  if (!visible_) {
    return;
  }
  // Clears the surface so that the last frame does not appear next time.
  if (context_ && target_bitmap_ && swap_chain_) {
    context_->SetTarget(target_bitmap_.get());
    context_->BeginDraw();
    context_->Clear(D2D1::ColorF(0, 0, 0, 0));
    if (SUCCEEDED(context_->EndDraw())) {
      swap_chain_->Present(0, 0);
    }
  }
  if (window_ != nullptr) {
    ::ShowWindow(window_, SW_HIDE);
  }
  visible_ = false;
  // Nothing is drawn while hidden. The images of the words are not needed.
  praise_sprites_.clear();
}

bool DopagakiOverlay::DrawFrame() {
  if (!IsHealthy()) {
    return false;
  }
  // Text bitmaps are drawn outside of the frame's BeginDraw/EndDraw.
  UpdateBannerCache();
  UpdateComboLabelCache();
  UpdateBadgeCache();
  UpdatePraiseCache();

  context_->SetTarget(target_bitmap_.get());
  context_->BeginDraw();
  context_->Clear(D2D1::ColorF(0, 0, 0, 0));
  const D2D1::Matrix3x2F base =
      D2D1::Matrix3x2F::Translation(effects_.shake_x(), effects_.shake_y());
  const float width = effects_.width();
  const float height = effects_.height();

  // Particles are added up like "lighter" of the canvas in the preview.
  context_->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
  for (const DopagakiParticle& p : effects_.particles()) {
    brush_->SetColor(ToD2DColor(p.color, std::clamp(p.life, 0.0f, 1.0f)));
    if (p.shard) {
      context_->SetTransform(
          D2D1::Matrix3x2F::Rotation(p.rotation * 180 / kPi) *
          D2D1::Matrix3x2F::Translation(p.x, p.y) * base);
      context_->FillRectangle(
          D2D1::RectF(-p.size, -p.size / 3, -p.size + p.size * 2.4f,
                      -p.size / 3 + p.size / 1.5f),
          brush_.get());
    } else {
      context_->SetTransform(base);
      context_->FillEllipse(
          D2D1::Ellipse(D2D1::Point2F(p.x, p.y), p.size, p.size),
          brush_.get());
    }
  }
  DrawEdge();
  context_->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);

  DrawCandidateFrame();

  // K5: "29 COMBO A".
  const auto& label = effects_.combo_label();
  if (label.has_value() && combo_label_text_.bitmap) {
    const float scale = GetDopagakiComboLabelScale(label->age_ms);
    const D2D1_POINT_2F center =
        D2D1::Point2F(label->x + combo_label_text_.text_width / 2,
                      label->y + combo_label_text_.text_height / 2);
    context_->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, center) *
                           base);
    const float left = label->x - combo_label_text_.padding;
    const float top = label->y - combo_label_text_.padding;
    const D2D1_RECT_F rect =
        D2D1::RectF(left, top, left + combo_label_text_.bitmap_width,
                    top + combo_label_text_.bitmap_height);
    context_->DrawBitmap(combo_label_text_.bitmap.get(), &rect,
                         GetDopagakiComboLabelOpacity(label->age_ms),
                         D2D1_INTERPOLATION_MODE_LINEAR, nullptr, nullptr);
  }

  // Update 1: the praise words.
  DrawPraises();

  // The red flash of BREAK.
  const float flash = effects_.flash_opacity();
  if (flash > 0) {
    context_->SetTransform(base);
    brush_->SetColor(ToD2DColor(DopagakiColorFromRgb(0xFF2A3D), flash));
    context_->FillRectangle(D2D1::RectF(-40, -40, width + 40, height + 40),
                            brush_.get());
  }

  // "S RANK!!" or "BREAK" at the center of the screen.
  const auto& banner = effects_.banner();
  if (banner.has_value() && banner_text_.bitmap) {
    const DopagakiBannerFrame frame =
        GetDopagakiBannerFrame(banner->kind, banner->elapsed_ms);
    const D2D1_POINT_2F center = D2D1::Point2F(width / 2, height / 2);
    context_->SetTransform(
        D2D1::Matrix3x2F::Scale(frame.scale, frame.scale, center) *
        D2D1::Matrix3x2F::Rotation(frame.rotation_degrees, center) *
        D2D1::Matrix3x2F::Translation(0, frame.offset_y) * base);
    const float left = center.x - banner_text_.bitmap_width / 2;
    const float top = center.y - banner_text_.bitmap_height / 2;
    const D2D1_RECT_F rect =
        D2D1::RectF(left, top, left + banner_text_.bitmap_width,
                    top + banner_text_.bitmap_height);
    context_->DrawBitmap(banner_text_.bitmap.get(), &rect, frame.opacity,
                         D2D1_INTERPOLATION_MODE_LINEAR, nullptr, nullptr);
  }

  context_->SetTransform(D2D1::Matrix3x2F::Identity());
  HRESULT hr = context_->EndDraw();
  if (FAILED(hr)) {
    Log(L"EndDraw failed", hr);
    return false;
  }
  // Never wait for the GPU on the UI thread. A skipped frame is fine.
  hr = swap_chain_->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
  if (FAILED(hr) && hr != DXGI_ERROR_WAS_STILL_DRAWING) {
    Log(L"Present failed", hr);
    return false;
  }
  return true;
}

void DopagakiOverlay::DrawEdge() {
  if (!effects_.IsEdgeVisible()) {
    return;
  }
  const DopagakiRank rank = effects_.edge_rank();
  const DopagakiEdgeStyle style = GetDopagakiEdgeStyle(rank);
  const float opacity = effects_.edge_opacity();
  const float pulse = GetDopagakiEdgePulse(rank, effects_.edge_time());
  const float line = style.width * pulse;
  const float glow = style.blur * pulse;
  const bool rainbow = (rank == DopagakiRank::kSSS);
  context_->SetTransform(
      D2D1::Matrix3x2F::Translation(effects_.shake_x(), effects_.shake_y()));

  // The glow (shadowBlur of the canvas), drawn as bands fading out inward.
  const DopagakiColor glow_color =
      rainbow ? DopagakiColorFromHsl(effects_.hue(), 1, 0.6)
              : GetDopagakiRankColor(rank, effects_.hue());
  brush_->SetColor(ToD2DColor(glow_color, 1));
  ID2D1Brush* const solid[4] = {brush_.get(), brush_.get(), brush_.get(),
                                brush_.get()};
  constexpr int kBands = 16;
  const float sigma = std::max(glow / 2, 1.0f);
  for (int i = 0; i < kBands; ++i) {
    const float outer = line + glow * i / kBands;
    const float inner = line + glow * (i + 1) / kBands;
    const float x = (outer + inner) / 2 - line;
    brush_->SetOpacity(style.alpha * 0.7f * opacity *
                       std::exp(-x * x / (2 * sigma * sigma)));
    FillFrame(outer, inner, solid);
  }

  // The lines. SSS uses the turning rainbow.
  ID2D1Brush* line_brushes[4] = {brush_.get(), brush_.get(), brush_.get(),
                                 brush_.get()};
  wil::com_ptr_nothrow<ID2D1LinearGradientBrush> rainbow_brushes[4];
  if (rainbow) {
    const float width = effects_.width();
    const float height = effects_.height();
    const double start_angle = effects_.edge_time();
    const float center_x = width / 2;
    const float center_y = height / 2;
    // createConicGradient(t, W / 2, H / 2) with hsl(k * 60) at k / 6.
    const auto color_at = [=](float x, float y) {
      constexpr double kTwoPi = 2 * 3.14159265358979323846;
      double angle = std::fmod(
          std::atan2(y - center_y, x - center_x) - start_angle, kTwoPi);
      if (angle < 0) {
        angle += kTwoPi;
      }
      return DopagakiColorFromHsl(angle / kTwoPi * 360, 1, 0.6);
    };
    rainbow_brushes[0] = CreateGradientAlong(
        D2D1::Point2F(0, 0), D2D1::Point2F(width, 0), color_at);  // Top.
    rainbow_brushes[1] =
        CreateGradientAlong(D2D1::Point2F(width, 0),
                            D2D1::Point2F(width, height), color_at);  // Right.
    rainbow_brushes[2] =
        CreateGradientAlong(D2D1::Point2F(0, height),
                            D2D1::Point2F(width, height), color_at);  // Bottom.
    rainbow_brushes[3] = CreateGradientAlong(
        D2D1::Point2F(0, 0), D2D1::Point2F(0, height), color_at);  // Left.
    for (int i = 0; i < 4; ++i) {
      if (rainbow_brushes[i]) {
        line_brushes[i] = rainbow_brushes[i].get();
      }
    }
  } else {
    brush_->SetColor(ToD2DColor(GetDopagakiRankColor(rank, 0), 1));
  }
  const auto set_opacity = [&line_brushes](float value) {
    for (ID2D1Brush* brush : line_brushes) {
      brush->SetOpacity(value);
    }
  };
  // The outer line: a stroke of width 2 * |line| on the border of the screen.
  // It also extends outside so that shaking does not show a gap.
  set_opacity(style.alpha * opacity);
  FillFrame(-40, line, line_brushes);
  // The inner line: width 0.8 * |line|, 0.6 * |line| inside.
  set_opacity(std::min(1.0f, style.alpha + 0.2f) * opacity);
  FillFrame(line * 0.2f, line, line_brushes);
  brush_->SetOpacity(1);
}

void DopagakiOverlay::FillFrame(float outer, float inner,
                                ID2D1Brush* const brushes[4]) {
  if (inner <= outer) {
    return;
  }
  const float width = effects_.width();
  const float height = effects_.height();
  // The corners belong to the top and bottom sides so that nothing overlaps.
  context_->FillRectangle(D2D1::RectF(outer, outer, width - outer, inner),
                          brushes[0]);
  context_->FillRectangle(
      D2D1::RectF(width - inner, inner, width - outer, height - inner),
      brushes[1]);
  context_->FillRectangle(
      D2D1::RectF(outer, height - inner, width - outer, height - outer),
      brushes[2]);
  context_->FillRectangle(D2D1::RectF(outer, inner, inner, height - inner),
                          brushes[3]);
}

wil::com_ptr_nothrow<ID2D1LinearGradientBrush>
DopagakiOverlay::CreateGradientAlong(
    D2D1_POINT_2F from, D2D1_POINT_2F to,
    const std::function<DopagakiColor(float x, float y)>& color_at) {
  constexpr int kStops = 13;
  D2D1_GRADIENT_STOP stops[kStops] = {};
  for (int i = 0; i < kStops; ++i) {
    const float t = static_cast<float>(i) / (kStops - 1);
    const float x = from.x + (to.x - from.x) * t;
    const float y = from.y + (to.y - from.y) * t;
    stops[i] = {t, ToD2DColor(color_at(x, y), 1)};
  }
  // Called through ID2D1RenderTarget because ID2D1DeviceContext has another
  // CreateGradientStopCollection.
  ID2D1RenderTarget* const render_target = context_.get();
  wil::com_ptr_nothrow<ID2D1GradientStopCollection> collection;
  wil::com_ptr_nothrow<ID2D1LinearGradientBrush> brush;
  if (FAILED(render_target->CreateGradientStopCollection(stops, kStops,
                                                         collection.put())) ||
      FAILED(render_target->CreateLinearGradientBrush(
          D2D1::LinearGradientBrushProperties(from, to), collection.get(),
          brush.put()))) {
    return nullptr;
  }
  return brush;
}

void DopagakiOverlay::DrawCandidateFrame() {
  if (!effects_.IsCandidateFrameVisible()) {
    return;
  }
  const DopagakiRect& window = *effects_.candidate_window();
  const float opacity = effects_.candidate_frame_opacity();
  const DopagakiRank rank = effects_.edge_rank();
  context_->SetTransform(
      D2D1::Matrix3x2F::Translation(effects_.shake_x(), effects_.shake_y()));
  const auto inflate = [&window](float amount) {
    return D2D1::RoundedRect(
        D2D1::RectF(window.left - amount, window.top - amount,
                    window.right + amount, window.bottom + amount),
        window.corner_radius + amount, window.corner_radius + amount);
  };

  // box-shadow: 0 0 var(--glow) var(--rank) of .cand-wrap in the preview.
  constexpr float kGlow[] = {8, 14, 22, 32, 44};  // RANKS[].glow
  constexpr float kFrameWidth = 3;                 // padding of .cand-wrap
  const float glow = kGlow[static_cast<int>(rank)];
  brush_->SetColor(ToD2DColor(GetDopagakiRankColor(rank, effects_.hue()), 1));
  constexpr int kBands = 12;
  const float sigma = std::max(glow / 2, 1.0f);
  for (int i = 0; i < kBands; ++i) {
    const float inner = kFrameWidth + glow * i / kBands;
    const float outer = kFrameWidth + glow * (i + 1) / kBands;
    const float x = (inner + outer) / 2 - kFrameWidth;
    brush_->SetOpacity(0.6f * opacity *
                       std::exp(-x * x / (2 * sigma * sigma)));
    context_->DrawRoundedRectangle(inflate((inner + outer) / 2), brush_.get(),
                                   outer - inner);
  }

  // The 3px frame. SSS uses the turning rainbow of .cand-wrap.sss.
  const D2D1_ROUNDED_RECT frame = inflate(kFrameWidth / 2);
  if (rank == DopagakiRank::kSSS) {
    const float center_x = (window.left + window.right) / 2;
    const float center_y = (window.top + window.bottom) / 2;
    const double spin = effects_.hue();
    // conic-gradient(from var(--spin), ...): 0 degrees is up, clockwise.
    const auto color_at = [=](float x, float y) {
      constexpr uint32_t kColors[] = {0xFF3B3B, 0xFFB43B, 0xF7FF3B, 0x3BFF7A,
                                      0x3BD8FF, 0x6A3BFF, 0xFF3BD5, 0xFF3B3B};
      constexpr double kRadiansToDegrees = 180 / 3.14159265358979323846;
      const double angle =
          std::atan2(x - center_x, center_y - y) * kRadiansToDegrees;
      const double position =
          std::fmod(angle - spin + 720.0, 360.0) / 360.0 * 7;
      const int index = std::min(static_cast<int>(position), 6);
      const float t = static_cast<float>(position - index);
      const DopagakiColor a = DopagakiColorFromRgb(kColors[index]);
      const DopagakiColor b = DopagakiColorFromRgb(kColors[index + 1]);
      return DopagakiColor{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                           a.b + (b.b - a.b) * t};
    };
    const float left = window.left - kFrameWidth;
    const float top = window.top - kFrameWidth;
    const float right = window.right + kFrameWidth;
    const float bottom = window.bottom + kFrameWidth;
    const float middle_top = window.top + window.corner_radius;
    const float middle_bottom = window.bottom - window.corner_radius;
    struct Side {
      D2D1_RECT_F clip;
      D2D1_POINT_2F from;
      D2D1_POINT_2F to;
    };
    const Side sides[] = {
        {D2D1::RectF(left - 1, top - 1, right + 1, middle_top),
         D2D1::Point2F(left, top), D2D1::Point2F(right, top)},
        {D2D1::RectF(center_x, middle_top, right + 1, middle_bottom),
         D2D1::Point2F(right, top), D2D1::Point2F(right, bottom)},
        {D2D1::RectF(left - 1, middle_bottom, right + 1, bottom + 1),
         D2D1::Point2F(left, bottom), D2D1::Point2F(right, bottom)},
        {D2D1::RectF(left - 1, middle_top, center_x, middle_bottom),
         D2D1::Point2F(left, top), D2D1::Point2F(left, bottom)},
    };
    for (const Side& side : sides) {
      wil::com_ptr_nothrow<ID2D1LinearGradientBrush> gradient =
          CreateGradientAlong(side.from, side.to, color_at);
      if (!gradient) {
        continue;
      }
      gradient->SetOpacity(opacity);
      context_->PushAxisAlignedClip(side.clip, D2D1_ANTIALIAS_MODE_ALIASED);
      context_->DrawRoundedRectangle(frame, gradient.get(), kFrameWidth);
      context_->PopAxisAlignedClip();
    }
  } else {
    brush_->SetOpacity(opacity);
    context_->DrawRoundedRectangle(frame, brush_.get(), kFrameWidth);
  }
  brush_->SetOpacity(1);

  // "x42": .badge { top: -14px; right: -10px } of .cand-wrap.
  const auto& badge = effects_.badge();
  if (badge.has_value() && badge_text_.bitmap) {
    const float text_right = window.right + kFrameWidth + 10;
    const float text_left = text_right - badge_text_.text_width;
    const float text_top = window.top - kFrameWidth - 14;
    const float scale = GetDopagakiComboLabelScale(badge->age_ms);
    const D2D1_POINT_2F center =
        D2D1::Point2F(text_left + badge_text_.text_width / 2,
                      text_top + badge_text_.text_height / 2);
    context_->SetTransform(
        D2D1::Matrix3x2F::Scale(scale, scale, center) *
        D2D1::Matrix3x2F::Translation(effects_.shake_x(), effects_.shake_y()));
    const float left = text_left - badge_text_.padding;
    const float top = text_top - badge_text_.padding;
    const D2D1_RECT_F rect =
        D2D1::RectF(left, top, left + badge_text_.bitmap_width,
                    top + badge_text_.bitmap_height);
    context_->DrawBitmap(badge_text_.bitmap.get(), &rect, opacity,
                         D2D1_INTERPOLATION_MODE_LINEAR, nullptr, nullptr);
  }
}

void DopagakiOverlay::UpdateBadgeCache() {
  const auto& badge = effects_.badge();
  if (!badge.has_value()) {
    badge_cache_id_ = 0;
    badge_text_ = {};
    return;
  }
  if (badge->id == badge_cache_id_) {
    return;
  }
  badge_cache_id_ = badge->id;
  badge_text_ = {};
  // text-shadow: 0 0 10px var(--rank), 0 2px 0 #000 like the combo label.
  CreateGlowText(badge->text, combo_label_format_.get(),
                 ToD2DColor(badge->color, 1), kComboLabelGlow, 0,
                 kComboLabelShadowY, &badge_text_);
}

void DopagakiOverlay::UpdateBannerCache() {
  const auto& banner = effects_.banner();
  if (!banner.has_value()) {
    banner_cache_id_ = 0;
    banner_text_ = {};
    return;
  }
  if (banner->id == banner_cache_id_) {
    return;
  }
  banner_cache_id_ = banner->id;
  banner_text_ = {};
  // font-size: clamp(64px, 18vw, 220px) in the preview.
  const float font_size = std::clamp(effects_.width() * 0.18f, 64.0f, 220.0f);
  const bool has_impact = HasFontFamily(dwrite_factory_.get(), L"Impact");
  wil::com_ptr_nothrow<IDWriteTextFormat> format;
  if (FAILED(dwrite_factory_->CreateTextFormat(
          has_impact ? L"Impact" : L"Arial Black", nullptr,
          has_impact ? DWRITE_FONT_WEIGHT_NORMAL : DWRITE_FONT_WEIGHT_BLACK,
          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, font_size,
          L"en-us", format.put()))) {
    return;
  }
  CreateGlowText(banner->text, format.get(), ToD2DColor(banner->color, 1),
                 kBannerGlow1, kBannerGlow2, kBannerShadowY, &banner_text_);
}

void DopagakiOverlay::UpdateComboLabelCache() {
  const auto& label = effects_.combo_label();
  if (!label.has_value()) {
    combo_label_cache_id_ = 0;
    combo_label_text_ = {};
    return;
  }
  if (label->id == combo_label_cache_id_) {
    return;
  }
  combo_label_cache_id_ = label->id;
  combo_label_text_ = {};
  CreateGlowText(label->text, combo_label_format_.get(),
                 ToD2DColor(label->color, 1), kComboLabelGlow, 0,
                 kComboLabelShadowY, &combo_label_text_);
}

bool DopagakiOverlay::CreateGlowText(const std::wstring& text,
                                     IDWriteTextFormat* format,
                                     const D2D1_COLOR_F& color, float blur1,
                                     float blur2, float shadow_y,
                                     GlowText* glow_text) {
  wil::com_ptr_nothrow<IDWriteTextLayout> layout;
  if (format == nullptr ||
      FAILED(dwrite_factory_->CreateTextLayout(
          text.c_str(), static_cast<UINT32>(text.size()), format, 10000,
          10000, layout.put()))) {
    return false;
  }
  DWRITE_TEXT_METRICS metrics = {};
  if (FAILED(layout->GetMetrics(&metrics))) {
    return false;
  }
  GlowText result;
  result.text_width = metrics.widthIncludingTrailingWhitespace;
  result.text_height = metrics.height;
  // A Gaussian blur of CSS radius r has sigma r / 2 and reaches about 3 sigma.
  result.padding = std::max(blur1, blur2) * 1.5f + shadow_y + 4;
  result.bitmap_width = result.text_width + result.padding * 2;
  result.bitmap_height = result.text_height + result.padding * 2;

  const float scale = static_cast<float>(dpi_) / 96.0f;
  const D2D1_SIZE_U pixel_size = D2D1::SizeU(
      static_cast<UINT32>(std::ceil(result.bitmap_width * scale)),
      static_cast<UINT32>(std::ceil(result.bitmap_height * scale)));
  const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_TARGET,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                        D2D1_ALPHA_MODE_PREMULTIPLIED),
      static_cast<float>(dpi_), static_cast<float>(dpi_));
  wil::com_ptr_nothrow<ID2D1Bitmap1> text_bitmap;
  if (FAILED(context_->CreateBitmap(pixel_size, nullptr, 0, &properties,
                                    text_bitmap.put())) ||
      FAILED(context_->CreateBitmap(pixel_size, nullptr, 0, &properties,
                                    result.bitmap.put()))) {
    return false;
  }
  const D2D1_POINT_2F origin =
      D2D1::Point2F(result.padding - metrics.left, result.padding);

  // The text alone, used as the source of the glow.
  context_->SetTarget(text_bitmap.get());
  context_->BeginDraw();
  context_->SetTransform(D2D1::Matrix3x2F::Identity());
  context_->Clear(D2D1::ColorF(0, 0, 0, 0));
  brush_->SetColor(color);
  context_->DrawTextLayout(origin, layout.get(), brush_.get());
  HRESULT hr = context_->EndDraw();

  // Glows, the black shadow and the text.
  if (SUCCEEDED(hr)) {
    context_->SetTarget(result.bitmap.get());
    context_->BeginDraw();
    context_->SetTransform(D2D1::Matrix3x2F::Identity());
    context_->Clear(D2D1::ColorF(0, 0, 0, 0));
    for (const float blur : {blur2, blur1}) {
      if (blur <= 0) {
        continue;
      }
      wil::com_ptr_nothrow<ID2D1Effect> effect;
      if (SUCCEEDED(
              context_->CreateEffect(CLSID_D2D1GaussianBlur, effect.put()))) {
        effect->SetInput(0, text_bitmap.get());
        effect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, blur / 2);
        context_->DrawImage(effect.get());
      }
    }
    if (shadow_y > 0) {
      brush_->SetColor(D2D1::ColorF(0, 0, 0, 1));
      context_->DrawTextLayout(D2D1::Point2F(origin.x, origin.y + shadow_y),
                               layout.get(), brush_.get());
    }
    brush_->SetColor(color);
    context_->DrawTextLayout(origin, layout.get(), brush_.get());
    hr = context_->EndDraw();
  }
  context_->SetTarget(target_bitmap_.get());
  if (FAILED(hr)) {
    Log(L"text drawing failed", hr);
    return false;
  }
  *glow_text = std::move(result);
  return true;
}

bool DopagakiOverlay::AddPraise(DopagakiRank tier,
                                std::span<const DopagakiBox> avoid) {
  if (!IsHealthy() || !dwrite_factory_) {
    return false;
  }
  // Phase U1 uses only "pop" with the rank color. The 12 styles and the 5
  // colors come in phase U2.
  DopagakiPraiseChoice choice = effects_.ChoosePraise(
      tier, DopagakiPraiseStyle::kPop, DopagakiPraiseColor::kRank);
  PraiseText text;
  if (!MeasurePraise(choice, &text)) {
    return false;
  }
  // 1.5: not wider than 70% of the monitor.
  const float max_width = effects_.width() * kPraiseMaxWidthRatio;
  if (text.width > max_width) {
    choice.font_size *= max_width / text.width;
    if (!MeasurePraise(choice, &text)) {
      return false;
    }
  }
  PraiseSprite sprite;
  if (!CreatePraiseSprite(choice, text, &sprite)) {
    return false;
  }
  sprite.id = effects_.AddPraise(choice, text.width, text.height, avoid);
  praise_sprites_.push_back(std::move(sprite));
  return true;
}

bool DopagakiOverlay::MeasurePraise(const DopagakiPraiseChoice& choice,
                                    PraiseText* text) {
  if (choice.font_size <= 0 || choice.word.empty()) {
    return false;
  }
  const DopagakiPraiseFont& font =
      GetDopagakiPraiseStyleInfo(choice.style).font;
  const wchar_t* family = L"Segoe UI";
  for (const wchar_t* candidate : font.families) {
    if (candidate != nullptr &&
        HasFontFamily(dwrite_factory_.get(), candidate)) {
      family = candidate;
      break;
    }
  }
  wil::com_ptr_nothrow<IDWriteTextFormat> format;
  if (FAILED(dwrite_factory_->CreateTextFormat(
          family, nullptr, static_cast<DWRITE_FONT_WEIGHT>(font.weight),
          font.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
          font.condensed ? DWRITE_FONT_STRETCH_CONDENSED
                         : DWRITE_FONT_STRETCH_NORMAL,
          choice.font_size, L"en-us", format.put()))) {
    return false;
  }
  wil::com_ptr_nothrow<IDWriteTextLayout> layout;
  if (FAILED(dwrite_factory_->CreateTextLayout(
          choice.word.c_str(), static_cast<UINT32>(choice.word.size()),
          format.get(), kPraiseLayoutSize, kPraiseLayoutSize,
          layout.put()))) {
    return false;
  }
  DWRITE_TEXT_METRICS metrics = {};
  DWRITE_OVERHANG_METRICS overhang = {};
  if (FAILED(layout->GetMetrics(&metrics)) ||
      FAILED(layout->GetOverhangMetrics(&overhang))) {
    return false;
  }
  // The ink, like actualBoundingBoxAscent/Descent of the preview. The
  // overhangs are relative to the layout box. Falls back to the line box if
  // they look wrong.
  float top = -overhang.top;
  float bottom = kPraiseLayoutSize + overhang.bottom;
  if (!(bottom > top) || top < -choice.font_size ||
      bottom > metrics.height + choice.font_size) {
    top = 0;
    bottom = metrics.height;
  }
  const float margin = choice.font_size * 0.06f;
  top -= margin;
  bottom += margin;
  text->layout = std::move(layout);
  text->width = std::max(1.0f, metrics.width);
  text->height = bottom - top;
  text->center_x = metrics.left + metrics.width / 2;
  text->center_y = (top + bottom) / 2;
  return true;
}

// makeSprite() in preview.html: the glows in the glow color, the black
// shadow and the word.
bool DopagakiOverlay::CreatePraiseSprite(const DopagakiPraiseChoice& choice,
                                         const PraiseText& text,
                                         PraiseSprite* sprite) {
  DWRITE_TEXT_METRICS metrics = {};
  if (!text.layout || FAILED(text.layout->GetMetrics(&metrics))) {
    return false;
  }
  const std::array<float, 2> glow =
      GetDopagakiPraiseGlowRadii(choice.tier, effects_.width());
  const float padding =
      std::ceil(std::max(glow[0], glow[1]) * 1.5f + kPraiseShadowY + 6);
  PraiseSprite result;
  result.bitmap_width = metrics.widthIncludingTrailingWhitespace + padding * 2;
  result.bitmap_height = metrics.height + padding * 2;
  const D2D1_POINT_2F origin =
      D2D1::Point2F(padding - metrics.left, padding);
  result.anchor_x = origin.x + text.center_x;
  result.anchor_y = origin.y + text.center_y;

  const float scale = static_cast<float>(dpi_) / 96.0f;
  const D2D1_SIZE_U pixel_size = D2D1::SizeU(
      static_cast<UINT32>(std::ceil(result.bitmap_width * scale)),
      static_cast<UINT32>(std::ceil(result.bitmap_height * scale)));
  const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_TARGET,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                        D2D1_ALPHA_MODE_PREMULTIPLIED),
      static_cast<float>(dpi_), static_cast<float>(dpi_));
  wil::com_ptr_nothrow<ID2D1Bitmap1> glow_source;
  if (FAILED(context_->CreateBitmap(pixel_size, nullptr, 0, &properties,
                                    glow_source.put())) ||
      FAILED(context_->CreateBitmap(pixel_size, nullptr, 0, &properties,
                                    result.bitmap.put()))) {
    return false;
  }

  // The word in the glow color, used as the source of the glows.
  context_->SetTarget(glow_source.get());
  context_->BeginDraw();
  context_->SetTransform(D2D1::Matrix3x2F::Identity());
  context_->Clear(D2D1::ColorF(0, 0, 0, 0));
  brush_->SetColor(ToD2DColor(choice.glow, 1));
  context_->DrawTextLayout(origin, text.layout.get(), brush_.get());
  HRESULT hr = context_->EndDraw();

  if (SUCCEEDED(hr)) {
    context_->SetTarget(result.bitmap.get());
    context_->BeginDraw();
    context_->SetTransform(D2D1::Matrix3x2F::Identity());
    context_->Clear(D2D1::ColorF(0, 0, 0, 0));
    for (const float blur : {glow[1], glow[0]}) {
      wil::com_ptr_nothrow<ID2D1Effect> effect;
      if (SUCCEEDED(
              context_->CreateEffect(CLSID_D2D1GaussianBlur, effect.put()))) {
        effect->SetInput(0, glow_source.get());
        effect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, blur / 2);
        context_->DrawImage(effect.get());
      }
    }
    brush_->SetColor(D2D1::ColorF(0, 0, 0, kPraiseShadowOpacity));
    context_->DrawTextLayout(D2D1::Point2F(origin.x, origin.y + kPraiseShadowY),
                             text.layout.get(), brush_.get());
    // Gold and rainbow gradients come in phase U2. Until then, |fill|.
    brush_->SetColor(ToD2DColor(choice.fill, 1));
    context_->DrawTextLayout(origin, text.layout.get(), brush_.get());
    hr = context_->EndDraw();
  }
  context_->SetTarget(target_bitmap_.get());
  if (FAILED(hr)) {
    Log(L"praise drawing failed", hr);
    return false;
  }
  *sprite = std::move(result);
  return true;
}

void DopagakiOverlay::UpdatePraiseCache() {
  const std::vector<DopagakiPraise>& praises = effects_.praises();
  std::erase_if(praise_sprites_, [&praises](const PraiseSprite& sprite) {
    return std::none_of(praises.begin(), praises.end(),
                        [&sprite](const DopagakiPraise& praise) {
                          return praise.id == sprite.id;
                        });
  });
}

// drawPraise() in preview.html (1.3.4).
void DopagakiOverlay::DrawPraises() {
  for (const DopagakiPraise& praise : effects_.praises()) {
    const auto sprite = std::find_if(
        praise_sprites_.begin(), praise_sprites_.end(),
        [&praise](const PraiseSprite& s) { return s.id == praise.id; });
    if (sprite == praise_sprites_.end() || !sprite->bitmap) {
      continue;
    }
    const DopagakiPraiseStyleInfo& info =
        GetDopagakiPraiseStyleInfo(praise.choice.style);
    const DopagakiPraiseView view = GetDopagakiPraiseView(praise);
    const DopagakiPraiseFrame frame =
        GetDopagakiPraiseFrame(praise.choice.style, view.t, praise.side);
    float opacity = frame.opacity * view.opacity;
    if (frame.scale_x < 0) {
      opacity *= info.back_opacity;
    }
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    // A zero scale cannot be drawn (and there is nothing to see).
    if (opacity <= 0 || std::abs(frame.scale_x) < 1e-3f ||
        std::abs(frame.scale_y) < 1e-3f) {
      continue;
    }
    const float pivot_y = info.bottom_pivot ? praise.height / 2 : 0;
    context_->SetTransform(
        D2D1::Matrix3x2F::Scale(frame.scale_x, frame.scale_y,
                                D2D1::Point2F(0, pivot_y)) *
        D2D1::Matrix3x2F::Skew(frame.skew_x, 0) *
        D2D1::Matrix3x2F::Rotation(frame.rotation) *
        D2D1::Matrix3x2F::Translation(frame.offset_x, frame.offset_y) *
        D2D1::Matrix3x2F::Rotation(praise.tilt) *
        D2D1::Matrix3x2F::Rotation(view.fall_rotation) *
        D2D1::Matrix3x2F::Translation(0, view.fall_y) *
        D2D1::Matrix3x2F::Translation(praise.x + effects_.shake_x(),
                                      praise.y + effects_.shake_y()));
    const D2D1_RECT_F rect = D2D1::RectF(
        -sprite->anchor_x, -sprite->anchor_y,
        -sprite->anchor_x + sprite->bitmap_width,
        -sprite->anchor_y + sprite->bitmap_height);
    context_->DrawBitmap(sprite->bitmap.get(), &rect, opacity,
                         D2D1_INTERPOLATION_MODE_LINEAR, nullptr, nullptr);
  }
}

LRESULT CALLBACK DopagakiOverlay::WindowProc(HWND window, UINT message,
                                             WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    const auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(lparam);
    ::SetWindowLongPtrW(
        window, GWLP_USERDATA,
        reinterpret_cast<LONG_PTR>(create_struct->lpCreateParams));
  }
  auto* self = reinterpret_cast<DopagakiOverlay*>(
      ::GetWindowLongPtrW(window, GWLP_USERDATA));
  switch (message) {
    case WM_TIMER:
      if (wparam == kTimerId && self != nullptr && self->window_ == window) {
        self->OnTimer();
        return 0;
      }
      break;
    case WM_NCHITTEST:
      return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_DISPLAYCHANGE:
      if (self != nullptr && self->window_ == window) {
        // Forces Prepare() to look at the monitor again.
        self->monitor_ = nullptr;
      }
      break;
    case WM_DPICHANGED:
      // The overlay is moved and resized by Prepare().
      return 0;
    case WM_NCDESTROY:
      ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
      break;
    default:
      break;
  }
  return ::DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace win32
}  // namespace renderer
}  // namespace mozc
