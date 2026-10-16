// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#ifndef MOZC_RENDERER_WIN32_DOPAGAKI_EFFECTS_H_
#define MOZC_RENDERER_WIN32_DOPAGAKI_EFFECTS_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "renderer/win32/dopagaki_state.h"

namespace mozc {
namespace renderer {
namespace win32 {

// The effects of docs/dopagaki/preview.html as pure logic. No Windows API is
// used here. Lengths are in DIPs (pixels at 96 dpi) and "per frame" values of
// the preview assume 60 fps; Update() converts them for the elapsed time.

// A color whose components are in [0, 1].
struct DopagakiColor {
  float r = 0;
  float g = 0;
  float b = 0;

  friend bool operator==(const DopagakiColor&, const DopagakiColor&) = default;
};

// 0xRRGGBB.
DopagakiColor DopagakiColorFromRgb(uint32_t rgb);
// hsl() of CSS. |hue| is in degrees. |saturation| and |lightness| are in
// [0, 1].
DopagakiColor DopagakiColorFromHsl(double hue, double saturation,
                                   double lightness);
// cubic-bezier() of CSS evaluated at |t| in [0, 1].
double DopagakiCubicBezier(double x1, double y1, double x2, double y2,
                           double t);

struct DopagakiParticle {
  float x = 0;
  float y = 0;
  float vx = 0;
  float vy = 0;
  float life = 1;  // 1 to 0. Also used as the opacity.
  float decay = 0.02f;
  float size = 3;
  float rotation = 0;  // Radians. Used only for shards.
  DopagakiColor color;
  bool shard = false;  // A thin rectangle instead of a circle.
};

enum class DopagakiBannerKind {
  kRankUp,  // "S RANK!!"
  kBreak,   // "BREAK"
};

struct DopagakiBanner {
  DopagakiBannerKind kind = DopagakiBannerKind::kRankUp;
  std::wstring text;
  DopagakiColor color;
  double elapsed_ms = 0;
  uint64_t id = 0;  // Different for each banner.
};

// The transform of a banner at a moment, applied around the screen center.
struct DopagakiBannerFrame {
  float opacity = 0;
  float scale = 1;
  float rotation_degrees = 0;
  float offset_y = 0;
};

double GetDopagakiBannerDurationMs(DopagakiBannerKind kind);
DopagakiBannerFrame GetDopagakiBannerFrame(DopagakiBannerKind kind,
                                           double elapsed_ms);

// K5: "29 COMBO A" shown near the caret when there is no candidate window.
struct DopagakiComboLabel {
  std::wstring text;
  DopagakiColor color;
  float x = 0;  // Top-left.
  float y = 0;
  double age_ms = 0;
  uint64_t id = 0;  // Changes when |text| or |color| changes.
};

// The candidate window (phase 4) in DIPs on the overlay.
struct DopagakiRect {
  float left = 0;
  float top = 0;
  float right = 0;
  float bottom = 0;
  float corner_radius = 0;

  friend bool operator==(const DopagakiRect&, const DopagakiRect&) = default;
};

// "x42" at the top-right corner of the candidate window.
struct DopagakiBadge {
  std::wstring text;
  DopagakiColor color;
  double age_ms = 0;  // Since the last keystroke.
  uint64_t id = 0;    // Changes when |text| or |color| changes.
};

// 1.5 -> 1 in 160 ms after each keystroke.
float GetDopagakiComboLabelScale(double age_ms);
// Fades out and disappears 2 seconds after the last keystroke.
float GetDopagakiComboLabelOpacity(double age_ms);

DopagakiColor GetDopagakiRankColor(DopagakiRank rank, double hue);

// EDGE in preview.html: the glow along the edges of the monitor.
struct DopagakiEdgeStyle {
  float width = 0;  // 0 means no glow.
  float blur = 0;
  float alpha = 0;
  float sparks_per_frame = 0;
};
DopagakiEdgeStyle GetDopagakiEdgeStyle(DopagakiRank rank);
// The glow of SS and SSS pulses. 1 for the other ranks.
float GetDopagakiEdgePulse(DopagakiRank rank, double edge_time);

class DopagakiEffects {
 public:
  // Returns a number in [0, 1). Replaced in tests.
  using RandomFunction = std::function<double()>;

  static constexpr size_t kMaxParticles = 4000;
  static constexpr double kComboLabelLifetimeMs = 2000;
  static constexpr double kFlashDurationMs = 450;
  // K4: the edge glow and sparks fade out after this idle time ...
  static constexpr double kEdgeIdleMs = 3000;
  // ... in this time.
  static constexpr double kEdgeFadeMs = 1000;

  explicit DopagakiEffects(RandomFunction random);
  DopagakiEffects(const DopagakiEffects&) = delete;
  DopagakiEffects& operator=(const DopagakiEffects&) = delete;

  void SetScreenSize(float width, float height);
  // True when the Windows animation setting is off. Disables shaking.
  void SetReducedMotion(bool reduced_motion);
  void Clear();

  // One keystroke. Particles come from (|origin_x|, |origin_y|).
  void OnKey(DopagakiRank rank, float origin_x, float origin_y);
  // The rank went up to |rank|.
  void OnRankUp(DopagakiRank rank);
  // The combo was broken (only when the combo was not 0).
  void OnBreak(float origin_x, float origin_y);
  void ShowComboLabel(uint64_t combo, DopagakiRank rank, float x, float y);
  void HideComboLabel();
  // The current rank, which decides the edge glow.
  void SetEdgeRank(DopagakiRank rank);
  // The candidate window to decorate, or nullopt when it is hidden.
  void SetCandidateWindow(std::optional<DopagakiRect> rect);
  // Updates "x42". |pop| restarts the 1.5 -> 1 animation.
  void SetBadge(uint64_t combo, DopagakiRank rank, bool pop);

  // Advances all the effects by |elapsed_ms|.
  void Update(double elapsed_ms);
  // False when there is nothing to draw.
  bool IsActive() const;

  float width() const { return width_; }
  float height() const { return height_; }
  const std::vector<DopagakiParticle>& particles() const { return particles_; }
  const std::optional<DopagakiBanner>& banner() const { return banner_; }
  const std::optional<DopagakiComboLabel>& combo_label() const {
    return combo_label_;
  }
  float flash_opacity() const;
  // The shake offset of this frame.
  float shake_x() const { return shake_x_; }
  float shake_y() const { return shake_y_; }
  float shake_amount() const { return shake_amount_; }
  double hue() const { return hue_; }
  DopagakiRank edge_rank() const { return edge_rank_; }
  // "t" of drawEdge() in the preview. +0.05 per frame.
  double edge_time() const { return edge_time_; }
  // 1 while typing, fades to 0 after kEdgeIdleMs (K4).
  float edge_opacity() const { return GetIdleOpacity(); }
  bool IsEdgeVisible() const;
  const std::optional<DopagakiRect>& candidate_window() const {
    return candidate_window_;
  }
  const std::optional<DopagakiBadge>& badge() const { return badge_; }
  // The glowing frame and the badge of the candidate window also fade out
  // with K4.
  float candidate_frame_opacity() const { return GetIdleOpacity(); }
  bool IsCandidateFrameVisible() const;

 private:
  struct BurstOptions {
    float speed = 6;
    float size = 3;
    float lift = 1.5f;
    DopagakiRank rank = DopagakiRank::kB;
    std::optional<DopagakiColor> color;  // Uses the rank color if empty.
    bool shard = false;
  };

  void Burst(float x, float y, int count, const BurstOptions& options);
  // Sparks flying inward from random points on the edges.
  void SpawnEdgeSparks(double frames);
  float GetIdleOpacity() const;
  void SetBadgeColor(DopagakiColor color);
  DopagakiColor GetParticleColor(DopagakiRank rank);
  void StartBanner(DopagakiBannerKind kind, std::wstring text,
                   DopagakiColor color);

  RandomFunction random_;
  float width_ = 1920;
  float height_ = 1080;
  bool reduced_motion_ = false;
  std::vector<DopagakiParticle> particles_;
  std::optional<DopagakiBanner> banner_;
  std::optional<DopagakiComboLabel> combo_label_;
  double flash_elapsed_ms_ = kFlashDurationMs;
  float shake_amount_ = 0;
  float shake_x_ = 0;
  float shake_y_ = 0;
  double hue_ = 0;
  uint64_t next_id_ = 1;
  DopagakiRank edge_rank_ = DopagakiRank::kB;
  double edge_time_ = 0;
  // Time since the last keystroke. Starts as faded out.
  double idle_ms_ = kEdgeIdleMs + kEdgeFadeMs;
  std::optional<DopagakiRect> candidate_window_;
  std::optional<DopagakiBadge> badge_;
  DopagakiRank badge_rank_ = DopagakiRank::kB;
  double badge_hue_ = 0;
};

}  // namespace win32
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_WIN32_DOPAGAKI_EFFECTS_H_
