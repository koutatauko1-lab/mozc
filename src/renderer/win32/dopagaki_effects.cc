// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#include "renderer/win32/dopagaki_effects.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "renderer/win32/dopagaki_state.h"

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFrameMs = 1000.0 / 60.0;
// Avoids big jumps after the UI thread was busy.
constexpr double kMaxFramesPerUpdate = 4.0;

// RANKS in preview.html.
constexpr float kRankShake[] = {0, 1, 3, 6, 10};
constexpr int kRankBurst[] = {4, 10, 18, 30, 50};

constexpr uint32_t kBreakColor = 0xFF2A3D;
constexpr uint32_t kBreakShardColor = 0xFF6A3D;

constexpr double kComboLabelPopMs = 160;
constexpr double kComboLabelFadeMs = 400;

int RankIndex(DopagakiRank rank) { return static_cast<int>(rank); }

double EaseOut(double t) { return DopagakiCubicBezier(0, 0, 0.58, 1, t); }

// One dimension of a cubic Bezier curve from 0 to 1.
double SampleCurve(double a1, double a2, double t) {
  const double u = 1 - t;
  return 3 * u * u * t * a1 + 3 * u * t * t * a2 + t * t * t;
}

double SampleCurveDerivative(double a1, double a2, double t) {
  const double u = 1 - t;
  return 3 * u * u * a1 + 6 * u * t * (a2 - a1) + 3 * t * t * (1 - a2);
}

double HueToRgb(double p, double q, double t) {
  if (t < 0) {
    t += 1;
  }
  if (t > 1) {
    t -= 1;
  }
  if (t < 1.0 / 6) {
    return p + (q - p) * 6 * t;
  }
  if (t < 1.0 / 2) {
    return q;
  }
  if (t < 2.0 / 3) {
    return p + (q - p) * (2.0 / 3 - t) * 6;
  }
  return p;
}

struct Keyframe {
  double offset;
  float opacity;
  float scale;
  float rotation_degrees;
  float offset_y;
};

// showBanner() in preview.html.
constexpr Keyframe kRankUpKeyframes[] = {
    {0, 0, 0.3f, 0, 0},
    {0.18, 1, 1.15f, 0, 0},
    {0.7, 1, 1, 0, 0},
    {1, 0, 1.6f, 0, 0},
};
constexpr Keyframe kBreakKeyframes[] = {
    {0, 0, 2.4f, -4, 0},
    {0.12, 1, 1, -4, 0},
    {0.7, 1, 1.04f, -4, 0},
    {1, 0, 0.9f, -6, 30},
};

std::wstring ToWide(const char* text) {
  std::wstring result;
  for (const char* p = text; *p != '\0'; ++p) {
    result.push_back(static_cast<wchar_t>(*p));
  }
  return result;
}

}  // namespace

DopagakiColor DopagakiColorFromRgb(uint32_t rgb) {
  return {((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f,
          (rgb & 0xFF) / 255.0f};
}

DopagakiColor DopagakiColorFromHsl(double hue, double saturation,
                                   double lightness) {
  double h = std::fmod(hue, 360.0);
  if (h < 0) {
    h += 360;
  }
  h /= 360;
  if (saturation <= 0) {
    const float l = static_cast<float>(lightness);
    return {l, l, l};
  }
  const double q = lightness < 0.5 ? lightness * (1 + saturation)
                                   : lightness + saturation - lightness * saturation;
  const double p = 2 * lightness - q;
  return {static_cast<float>(HueToRgb(p, q, h + 1.0 / 3)),
          static_cast<float>(HueToRgb(p, q, h)),
          static_cast<float>(HueToRgb(p, q, h - 1.0 / 3))};
}

double DopagakiCubicBezier(double x1, double y1, double x2, double y2,
                           double t) {
  if (t <= 0) {
    return 0;
  }
  if (t >= 1) {
    return 1;
  }
  // Finds the curve parameter whose x is |t| (Newton's method first).
  double u = t;
  for (int i = 0; i < 8; ++i) {
    const double error = SampleCurve(x1, x2, u) - t;
    if (std::abs(error) < 1e-7) {
      return SampleCurve(y1, y2, u);
    }
    const double derivative = SampleCurveDerivative(x1, x2, u);
    if (std::abs(derivative) < 1e-6) {
      break;
    }
    u -= error / derivative;
  }
  // Falls back to bisection.
  double low = 0;
  double high = 1;
  u = t;
  for (int i = 0; i < 60; ++i) {
    const double x = SampleCurve(x1, x2, u);
    if (std::abs(x - t) < 1e-7) {
      break;
    }
    if (x < t) {
      low = u;
    } else {
      high = u;
    }
    u = (low + high) / 2;
  }
  return SampleCurve(y1, y2, u);
}

double GetDopagakiBannerDurationMs(DopagakiBannerKind kind) {
  return kind == DopagakiBannerKind::kRankUp ? 1000 : 1100;
}

DopagakiBannerFrame GetDopagakiBannerFrame(DopagakiBannerKind kind,
                                           double elapsed_ms) {
  const double t = std::clamp(
      elapsed_ms / GetDopagakiBannerDurationMs(kind), 0.0, 1.0);
  // The easing of the whole animation. Keyframes are linear in between.
  const double progress = DopagakiCubicBezier(0.2, 0.8, 0.2, 1, t);
  const Keyframe* keyframes = kind == DopagakiBannerKind::kRankUp
                                  ? kRankUpKeyframes
                                  : kBreakKeyframes;
  constexpr size_t kCount = std::size(kRankUpKeyframes);
  static_assert(kCount == std::size(kBreakKeyframes));
  size_t index = 0;
  while (index + 2 < kCount && progress > keyframes[index + 1].offset) {
    ++index;
  }
  const Keyframe& from = keyframes[index];
  const Keyframe& to = keyframes[index + 1];
  const float local = static_cast<float>(
      std::clamp((progress - from.offset) / (to.offset - from.offset), 0.0,
                 1.0));
  const auto lerp = [local](float a, float b) { return a + (b - a) * local; };
  return {lerp(from.opacity, to.opacity), lerp(from.scale, to.scale),
          lerp(from.rotation_degrees, to.rotation_degrees),
          lerp(from.offset_y, to.offset_y)};
}

float GetDopagakiComboLabelScale(double age_ms) {
  if (age_ms >= kComboLabelPopMs) {
    return 1;
  }
  return static_cast<float>(1.5 - 0.5 * EaseOut(age_ms / kComboLabelPopMs));
}

float GetDopagakiComboLabelOpacity(double age_ms) {
  constexpr double kFadeStartMs =
      DopagakiEffects::kComboLabelLifetimeMs - kComboLabelFadeMs;
  if (age_ms <= kFadeStartMs) {
    return 1;
  }
  if (age_ms >= DopagakiEffects::kComboLabelLifetimeMs) {
    return 0;
  }
  return static_cast<float>(
      (DopagakiEffects::kComboLabelLifetimeMs - age_ms) / kComboLabelFadeMs);
}

DopagakiColor GetDopagakiRankColor(DopagakiRank rank, double hue) {
  switch (rank) {
    case DopagakiRank::kB:
      return DopagakiColorFromRgb(0x4DA3FF);
    case DopagakiRank::kA:
      return DopagakiColorFromRgb(0x3BE37A);
    case DopagakiRank::kS:
      return DopagakiColorFromRgb(0xFFD23F);
    case DopagakiRank::kSS:
      return DopagakiColorFromRgb(0xFF4FD8);
    case DopagakiRank::kSSS:
      return DopagakiColorFromHsl(hue, 1, 0.62);
  }
  return DopagakiColorFromRgb(0x4DA3FF);
}

DopagakiEffects::DopagakiEffects(RandomFunction random)
    : random_(std::move(random)) {}

void DopagakiEffects::SetScreenSize(float width, float height) {
  width_ = std::max(width, 1.0f);
  height_ = std::max(height, 1.0f);
}

void DopagakiEffects::SetReducedMotion(bool reduced_motion) {
  reduced_motion_ = reduced_motion;
}

void DopagakiEffects::Clear() {
  particles_.clear();
  banner_.reset();
  combo_label_.reset();
  flash_elapsed_ms_ = kFlashDurationMs;
  shake_amount_ = 0;
  shake_x_ = 0;
  shake_y_ = 0;
}

// hit() in preview.html.
void DopagakiEffects::OnKey(DopagakiRank rank, float origin_x,
                            float origin_y) {
  const int r = RankIndex(rank);
  Burst(origin_x, origin_y, kRankBurst[r],
        {.speed = 4 + r * 1.6f, .size = 2.5f + r * 0.6f, .rank = rank});
  if (r >= 3 && random_() < (r == 4 ? 0.5 : 0.2)) {
    // Fireworks at a random place in the upper 60% of the screen.
    const float x = static_cast<float>(random_() * width_);
    const float y = static_cast<float>(random_() * height_ * 0.6);
    Burst(x, y, kRankBurst[r],
          {.speed = 7, .size = 3.5f, .lift = 2, .rank = rank});
  }
  shake_amount_ = std::max(shake_amount_, kRankShake[r]);
}

void DopagakiEffects::OnRankUp(DopagakiRank rank) {
  const int r = RankIndex(rank);
  StartBanner(DopagakiBannerKind::kRankUp,
              ToWide(GetDopagakiRankName(rank)) + L" RANK!!",
              GetDopagakiRankColor(rank, hue_));
  for (int i = 0; i < 2 + r * 2; ++i) {
    const float x = static_cast<float>(random_() * width_);
    const float y = static_cast<float>(random_() * height_);
    Burst(x, y, 30, {.speed = 8, .size = 4, .rank = rank});
  }
  shake_amount_ = kRankShake[r] * 2.2f + 4;
}

// breakCombo() in preview.html.
void DopagakiEffects::OnBreak(float origin_x, float origin_y) {
  Burst(origin_x, origin_y, 60,
        {.speed = 10,
         .size = 5,
         .color = DopagakiColorFromRgb(kBreakColor),
         .shard = true});
  Burst(width_ / 2, height_ / 2, 80,
        {.speed = 14,
         .size = 6,
         .color = DopagakiColorFromRgb(kBreakShardColor),
         .shard = true});
  flash_elapsed_ms_ = 0;
  StartBanner(DopagakiBannerKind::kBreak, L"BREAK",
              DopagakiColorFromRgb(kBreakColor));
  shake_amount_ = 14;
}

void DopagakiEffects::ShowComboLabel(uint64_t combo, DopagakiRank rank,
                                     float x, float y) {
  std::wstring text = std::to_wstring(combo) + L" COMBO " +
                      ToWide(GetDopagakiRankName(rank));
  const DopagakiColor color = GetDopagakiRankColor(rank, hue_);
  if (!combo_label_.has_value() || combo_label_->text != text ||
      combo_label_->color != color) {
    const uint64_t id = next_id_++;
    combo_label_ = DopagakiComboLabel{
        .text = std::move(text), .color = color, .id = id};
  }
  combo_label_->x = x;
  combo_label_->y = y;
  combo_label_->age_ms = 0;
}

void DopagakiEffects::HideComboLabel() { combo_label_.reset(); }

void DopagakiEffects::Update(double elapsed_ms) {
  if (elapsed_ms <= 0) {
    return;
  }
  const double frames = std::min(elapsed_ms / kFrameMs, kMaxFramesPerUpdate);
  const float f = static_cast<float>(frames);
  hue_ = std::fmod(hue_ + 3 * frames, 360.0);

  // tick() in preview.html.
  const float friction = static_cast<float>(std::pow(0.985, frames));
  for (DopagakiParticle& p : particles_) {
    p.x += p.vx * f;
    p.y += p.vy * f;
    p.vy += 0.18f * f;
    p.vx *= friction;
    p.life -= p.decay * f;
    p.rotation += 0.2f * f;
  }
  std::erase_if(particles_, [this](const DopagakiParticle& p) {
    // Particles that left the screen are no longer drawn either.
    return p.life <= 0 || p.y > height_ + 100 || p.x < -200 ||
           p.x > width_ + 200;
  });

  if (banner_.has_value()) {
    banner_->elapsed_ms += elapsed_ms;
    if (banner_->elapsed_ms >= GetDopagakiBannerDurationMs(banner_->kind)) {
      banner_.reset();
    }
  }
  flash_elapsed_ms_ = std::min(flash_elapsed_ms_ + elapsed_ms,
                               static_cast<double>(kFlashDurationMs));
  if (combo_label_.has_value()) {
    combo_label_->age_ms += elapsed_ms;
    if (combo_label_->age_ms >= kComboLabelLifetimeMs) {
      combo_label_.reset();
    }
  }

  if (shake_amount_ > 0.2f && !reduced_motion_) {
    shake_x_ = static_cast<float>((random_() - 0.5) * shake_amount_ * 2);
    shake_y_ = static_cast<float>((random_() - 0.5) * shake_amount_ * 2);
    shake_amount_ *= static_cast<float>(std::pow(0.86, frames));
  } else {
    shake_x_ = 0;
    shake_y_ = 0;
    shake_amount_ = 0;
  }
}

bool DopagakiEffects::IsActive() const {
  return !particles_.empty() || banner_.has_value() ||
         combo_label_.has_value() || flash_opacity() > 0 ||
         shake_amount_ > 0.2f;
}

float DopagakiEffects::flash_opacity() const {
  if (flash_elapsed_ms_ >= kFlashDurationMs) {
    return 0;
  }
  return static_cast<float>(0.75 *
                            (1 - EaseOut(flash_elapsed_ms_ / kFlashDurationMs)));
}

// burst() in preview.html.
void DopagakiEffects::Burst(float x, float y, int count,
                            const BurstOptions& options) {
  for (int i = 0; i < count && particles_.size() < kMaxParticles; ++i) {
    const double angle = random_() * 2 * kPi;
    const double speed = options.speed * (0.4 + random_());
    DopagakiParticle p;
    p.x = x;
    p.y = y;
    p.vx = static_cast<float>(std::cos(angle) * speed);
    p.vy = static_cast<float>(std::sin(angle) * speed - options.lift);
    p.life = 1;
    p.decay = static_cast<float>(0.012 + random_() * 0.02);
    p.size = static_cast<float>(options.size * (0.5 + random_()));
    p.color = options.color.has_value() ? *options.color
                                        : GetParticleColor(options.rank);
    p.shard = options.shard;
    p.rotation = static_cast<float>(random_() * 6);
    particles_.push_back(p);
  }
}

// colorFor() in preview.html.
DopagakiColor DopagakiEffects::GetParticleColor(DopagakiRank rank) {
  if (rank == DopagakiRank::kSSS) {
    return DopagakiColorFromHsl(hue_ + random_() * 120, 1, 0.62);
  }
  return GetDopagakiRankColor(rank, hue_);
}

void DopagakiEffects::StartBanner(DopagakiBannerKind kind, std::wstring text,
                                  DopagakiColor color) {
  // A new banner replaces the current one.
  banner_ = DopagakiBanner{.kind = kind,
                           .text = std::move(text),
                           .color = color,
                           .elapsed_ms = 0,
                           .id = next_id_++};
}

}  // namespace win32
}  // namespace renderer
}  // namespace mozc
