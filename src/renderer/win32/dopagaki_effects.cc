// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#include "renderer/win32/dopagaki_effects.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "renderer/win32/dopagaki_praise.h"
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

// Update 1 (1.4): the complementary colors of B, A, S and SS.
constexpr uint32_t kPraiseComplement[] = {0xFFA64D, 0xE33BA6, 0x3F6CFF,
                                          0x4FFF76};
constexpr uint32_t kPraiseGoldFill = 0xFFD23F;
constexpr uint32_t kPraiseGoldGlow = 0xFFB300;
// The banner area kept free while a banner is shown (1.6).
constexpr float kBannerAreaLeft = 0.2f;
constexpr float kBannerAreaTop = 0.32f;
constexpr float kBannerAreaRight = 0.8f;
constexpr float kBannerAreaBottom = 0.68f;

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
// Update 1 (1.10): showMilestone() in preview.html.
constexpr Keyframe kMilestoneKeyframes[] = {
    {0, 0, 0.3f, 0, 0},  {0.15, 1, 1.2f, 0, 0}, {0.25, 1, 1, 0, 0},
    {0.78, 1, 1, 0, 0},  {1, 0, 1.5f, 0, 0},
};
constexpr Keyframe kMilestoneSubtitleKeyframes[] = {
    {0, 0, 0.6f, 0, 0},   {0.2, 0, 0.6f, 0, 0}, {0.3, 1, 1.08f, 0, 0},
    {0.36, 1, 1, 0, 0},   {0.78, 1, 1, 0, 0},   {1, 0, 1.2f, 0, 0},
};
constexpr double kMilestoneDurationMs = 1700;

// Linear between the keyframes at |progress| in [0, 1].
DopagakiBannerFrame InterpolateKeyframes(std::span<const Keyframe> keyframes,
                                         double progress) {
  size_t index = 0;
  while (index + 2 < keyframes.size() &&
         progress > keyframes[index + 1].offset) {
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

double GetDopagakiBannerDurationMs(DopagakiBannerKind kind) {
  switch (kind) {
    case DopagakiBannerKind::kRankUp:
      return 1000;
    case DopagakiBannerKind::kBreak:
      return 1100;
    case DopagakiBannerKind::kMilestone:
      return kMilestoneDurationMs;
  }
  return 1000;
}

DopagakiBannerFrame GetDopagakiBannerFrame(DopagakiBannerKind kind,
                                           double elapsed_ms) {
  const double t = std::clamp(
      elapsed_ms / GetDopagakiBannerDurationMs(kind), 0.0, 1.0);
  // The easing of the whole animation. Keyframes are linear in between.
  const double progress = DopagakiCubicBezier(0.2, 0.8, 0.2, 1, t);
  switch (kind) {
    case DopagakiBannerKind::kRankUp:
      return InterpolateKeyframes(kRankUpKeyframes, progress);
    case DopagakiBannerKind::kBreak:
      return InterpolateKeyframes(kBreakKeyframes, progress);
    case DopagakiBannerKind::kMilestone:
      return InterpolateKeyframes(kMilestoneKeyframes, progress);
  }
  return InterpolateKeyframes(kRankUpKeyframes, progress);
}

DopagakiBannerFrame GetDopagakiMilestoneSubtitleFrame(double elapsed_ms) {
  // Linear as a whole.
  return InterpolateKeyframes(
      kMilestoneSubtitleKeyframes,
      std::clamp(elapsed_ms / kMilestoneDurationMs, 0.0, 1.0));
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

DopagakiEdgeStyle GetDopagakiEdgeStyle(DopagakiRank rank) {
  switch (rank) {
    case DopagakiRank::kB:
      return {};
    case DopagakiRank::kA:
      return {.width = 3, .blur = 14, .alpha = 0.45f};
    case DopagakiRank::kS:
      return {.width = 6, .blur = 22, .alpha = 0.7f, .sparks_per_frame = 0.06f};
    case DopagakiRank::kSS:
      return {.width = 9, .blur = 30, .alpha = 0.85f, .sparks_per_frame = 0.6f};
    case DopagakiRank::kSSS:
      return {.width = 13, .blur = 42, .alpha = 1, .sparks_per_frame = 2.2f};
  }
  return {};
}

float GetDopagakiEdgePulse(DopagakiRank rank, double edge_time) {
  switch (rank) {
    case DopagakiRank::kSS:
      return static_cast<float>(1 + 0.35 * std::sin(edge_time * 2));
    case DopagakiRank::kSSS:
      return static_cast<float>(1 + 0.35 * std::sin(edge_time * 3));
    default:
      return 1;
  }
}

DopagakiPraiseView GetDopagakiPraiseView(const DopagakiPraise& praise) {
  DopagakiPraiseView view;
  view.elapsed_ms = praise.crumbling ? praise.frozen_ms : praise.elapsed_ms;
  view.t = praise.duration_ms > 0
               ? std::clamp(view.elapsed_ms / praise.duration_ms, 0.0, 1.0)
               : 1.0;
  if (praise.crumbling) {
    // crumblePraises() in preview.html.
    const double u = std::clamp(
        praise.crumble_ms / DopagakiEffects::kPraiseCrumbleMs, 0.0, 1.0);
    const double seconds = praise.crumble_ms / 1000;
    view.fall_y = static_cast<float>(0.5 * 1667 * seconds * seconds);
    view.fall_rotation = static_cast<float>(praise.crumble_rotation * u);
    view.opacity =
        static_cast<float>(1 - DopagakiEaseValue(DopagakiEase::kIn, u));
  }
  if (praise.exiting) {
    view.opacity *= static_cast<float>(std::clamp(
        1 - praise.exit_ms / DopagakiEffects::kPraiseExitMs, 0.0, 1.0));
  }
  return view;
}

bool IsDopagakiPraiseGlitching(const DopagakiPraise& praise, double t) {
  if (!GetDopagakiPraiseStyleInfo(praise.choice.style).glitch ||
      praise.crumbling) {
    return false;
  }
  return (t >= 0 && t <= 0.28) || (t >= 0.8 && t <= 1);
}

float GetDopagakiGlitchSplit(double t) {
  if (t < 0.5) {
    return static_cast<float>(2 + 12 * (1 - t / 0.28));
  }
  return static_cast<float>(2 + 12 * ((t - 0.8) / 0.2));
}

DopagakiEffects::DopagakiEffects(RandomFunction random)
    : random_(std::move(random)), praise_picker_(random_) {}

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
  white_flash_elapsed_ms_ = kWhiteFlashDurationMs;
  shake_amount_ = 0;
  shake_x_ = 0;
  shake_y_ = 0;
  edge_rank_ = DopagakiRank::kB;
  idle_ms_ = kEdgeIdleMs + kEdgeFadeMs;
  candidate_window_.reset();
  badge_.reset();
  praises_.clear();
  rings_.clear();
}

void DopagakiEffects::SetEdgeRank(DopagakiRank rank) { edge_rank_ = rank; }

void DopagakiEffects::SetCandidateWindow(std::optional<DopagakiRect> rect) {
  candidate_window_ = rect;
}

void DopagakiEffects::SetBadge(uint64_t combo, DopagakiRank rank, bool pop) {
  std::wstring text = L"x" + std::to_wstring(combo);
  badge_rank_ = rank;
  badge_hue_ = hue_;
  const DopagakiColor color = GetDopagakiRankColor(rank, hue_);
  if (!badge_.has_value() || badge_->text != text) {
    const uint64_t id = next_id_++;
    badge_ = DopagakiBadge{.text = std::move(text), .color = color, .id = id};
  } else {
    SetBadgeColor(color);
  }
  if (pop) {
    badge_->age_ms = 0;
  }
}

void DopagakiEffects::SetBadgeColor(DopagakiColor color) {
  if (badge_.has_value() && badge_->color != color) {
    badge_->color = color;
    badge_->id = next_id_++;
  }
}

bool DopagakiEffects::IsCandidateFrameVisible() const {
  return candidate_window_.has_value() && GetIdleOpacity() > 0;
}

float DopagakiEffects::GetIdleOpacity() const {
  if (idle_ms_ <= kEdgeIdleMs) {
    return 1;
  }
  const double faded = (idle_ms_ - kEdgeIdleMs) / kEdgeFadeMs;
  // A tiny remainder from adding up frame times counts as fully faded out.
  if (faded >= 1 - 1e-6) {
    return 0;
  }
  return static_cast<float>(1 - faded);
}

bool DopagakiEffects::IsEdgeVisible() const {
  return GetDopagakiEdgeStyle(edge_rank_).width > 0 && edge_opacity() > 0;
}

// hit() in preview.html.
void DopagakiEffects::OnKey(DopagakiRank rank, float origin_x,
                            float origin_y) {
  idle_ms_ = 0;
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
  idle_ms_ = 0;
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
  // Update 1 (1.9): the praise words crumble.
  for (DopagakiPraise& praise : praises_) {
    if (praise.crumbling || praise.exiting) {
      continue;
    }
    praise.crumbling = true;
    praise.crumble_ms = 0;
    praise.frozen_ms =
        std::max(0.0, std::min(praise.elapsed_ms, praise.duration_ms - 1));
    const float sign = random_() < 0.5 ? -1.0f : 1.0f;
    praise.crumble_rotation =
        sign * static_cast<float>(30 + random_() * 30);
    Burst(praise.x, praise.y, 12,
          {.speed = 8,
           .size = 4,
           .color = DopagakiColorFromRgb(kBreakColor),
           .shard = true});
  }
}

// showMilestone() in preview.html.
void DopagakiEffects::OnMilestone(uint64_t combo) {
  StartBanner(DopagakiBannerKind::kMilestone,
              std::to_wstring(combo) + L" COMBO!!",
              DopagakiColorFromHsl(hue_, 1, 0.62));
  banner_->subtitle = std::wstring(praise_picker_.NextMilestoneWord());
  banner_->hue = hue_;
  // Fireworks at 12 random places, each particle with a random hue.
  for (int i = 0; i < 12; ++i) {
    const float x = static_cast<float>(random_() * width_);
    const float y = static_cast<float>(random_() * height_);
    Burst(x, y, 40,
          {.speed = 9, .size = 4, .rainbow_hue = 0.0, .rainbow_range = 360});
  }
  white_flash_elapsed_ms_ = 0;
  shake_amount_ = std::max(shake_amount_, 16.0f);
}

DopagakiPraiseChoice DopagakiEffects::ChoosePraise(
    DopagakiRank tier, std::optional<DopagakiPraiseStyle> forced_style,
    std::optional<DopagakiPraiseColor> forced_color) {
  DopagakiPraiseChoice choice;
  choice.tier = tier;
  choice.word = std::wstring(praise_picker_.NextWord(tier));
  if (reduced_motion_) {
    // 1.12: only fades.
    choice.style = DopagakiPraiseStyle::kFade;
  } else {
    choice.style = forced_style.has_value() ? *forced_style
                                            : praise_picker_.NextStyle();
  }
  choice.color = forced_color.has_value() ? *forced_color
                                          : praise_picker_.NextColor(tier);
  choice.font_size = GetDopagakiPraiseFontSize(tier, width_) *
                     GetDopagakiPraiseStyleInfo(choice.style).size_scale;
  choice.hue = hue_;

  // makePaint() in preview.html.
  const DopagakiColor rank_color = GetDopagakiRankColor(tier, hue_);
  switch (choice.color) {
    case DopagakiPraiseColor::kRank:
      choice.fill = choice.glow = rank_color;
      choice.particle = rank_color;
      break;
    case DopagakiPraiseColor::kWhite:
      choice.fill = DopagakiColorFromRgb(0xFFFFFF);
      choice.glow = rank_color;
      choice.particle = rank_color;
      break;
    case DopagakiPraiseColor::kComplement: {
      const DopagakiColor color =
          tier == DopagakiRank::kSSS
              ? DopagakiColorFromHsl(hue_ + 180, 1, 0.62)
              : DopagakiColorFromRgb(kPraiseComplement[RankIndex(tier)]);
      choice.fill = choice.glow = color;
      choice.particle = color;
      break;
    }
    case DopagakiPraiseColor::kGold:
      choice.fill = DopagakiColorFromRgb(kPraiseGoldFill);
      choice.glow = DopagakiColorFromRgb(kPraiseGoldGlow);
      choice.particle = DopagakiColorFromRgb(kPraiseGoldFill);
      break;
    case DopagakiPraiseColor::kRainbow:
      choice.fill = DopagakiColorFromHsl(hue_, 1, 0.62);
      choice.glow = DopagakiColorFromRgb(0xFFFFFF);
      choice.particle = std::nullopt;
      break;
  }
  return choice;
}

uint64_t DopagakiEffects::AddPraise(const DopagakiPraiseChoice& choice,
                                    float width, float height,
                                    std::span<const float> letter_x,
                                    std::span<const DopagakiBox> avoid) {
  const DopagakiPraiseStyleInfo& info =
      GetDopagakiPraiseStyleInfo(choice.style);
  DopagakiPraise praise;
  praise.choice = choice;
  praise.width = width;
  praise.height = height;
  praise.tilt = info.tilt_base +
                static_cast<float>(random_() * 2 - 1) * info.tilt_range;
  if (info.letters != DopagakiPraiseLetters::kNone) {
    praise.letter_x.assign(letter_x.begin(), letter_x.end());
    praise.letter_x.resize(choice.word.size(), 0);
    if (info.letters == DopagakiPraiseLetters::kScatter) {
      for (size_t i = 0; i < choice.word.size(); ++i) {
        praise.letter_random.push_back(MakeDopagakiPraiseLetterRandom(random_));
      }
    }
  }
  if (info.glitch) {
    RollGlitch(praise);
  }

  // pickPosition() in preview.html.
  std::vector<DopagakiBox> boxes(avoid.begin(), avoid.end());
  if (banner_.has_value()) {
    boxes.push_back({width_ * kBannerAreaLeft, height_ * kBannerAreaTop,
                     width_ * kBannerAreaRight, height_ * kBannerAreaBottom});
  }
  for (const DopagakiPraise& other : praises_) {
    if (!other.crumbling && !other.exiting) {
      boxes.push_back(other.box.Inflated(10));
    }
  }
  const std::array<float, 2> box_size =
      GetDopagakiPraiseBoxSize(width, height, praise.tilt);
  const DopagakiPraisePosition position = PickDopagakiPraisePosition(
      box_size[0], box_size[1], width_, height_, boxes, random_);
  praise.x = position.x;
  praise.y = position.y;
  praise.box = position.box;
  praise.side = position.x < width_ / 2 ? -1 : 1;
  praise.duration_ms =
      GetDopagakiPraiseDurationMs(choice.style, choice.word.size());
  praise.id = next_id_++;
  praises_.push_back(std::move(praise));

  // 1.8 (UK3): the oldest one goes away when there are too many.
  size_t alive = 0;
  for (const DopagakiPraise& p : praises_) {
    if (!p.crumbling && !p.exiting) {
      ++alive;
    }
  }
  if (alive > kMaxPraises) {
    for (DopagakiPraise& p : praises_) {
      if (!p.crumbling && !p.exiting) {
        p.exiting = true;
        p.exit_ms = 0;
        break;
      }
    }
  }
  return praises_.back().id;
}

void DopagakiEffects::UpdatePraises(double elapsed_ms) {
  for (DopagakiPraise& praise : praises_) {
    if (praise.crumbling) {
      praise.crumble_ms += elapsed_ms;
    } else {
      praise.elapsed_ms += elapsed_ms;
      const DopagakiPraiseStyleInfo& info =
          GetDopagakiPraiseStyleInfo(praise.choice.style);
      if (!praise.effect_fired &&
          praise.elapsed_ms >= info.effect_at * praise.duration_ms) {
        praise.effect_fired = true;
        FirePraiseEffect(praise);
      }
      // scatter: the letters fly apart 350 ms before the end (1.3.2).
      if (info.letters == DopagakiPraiseLetters::kScatter &&
          !praise.exploded &&
          praise.elapsed_ms >= praise.duration_ms - 350) {
        praise.exploded = true;
        const double radians = praise.tilt * kPi / 180;
        BurstOptions options = GetPraiseBurstOptions(praise);
        options.speed = 6;
        options.size = 3;
        for (size_t i = 0; i < praise.choice.word.size(); ++i) {
          if (praise.choice.word[i] == L' ') {
            continue;
          }
          const float lx = praise.letter_x[i];
          Burst(praise.x + lx * static_cast<float>(std::cos(radians)),
                praise.y + lx * static_cast<float>(std::sin(radians)), 4,
                options);
        }
      }
      const double t = praise.duration_ms > 0
                           ? praise.elapsed_ms / praise.duration_ms
                           : 1.0;
      if (IsDopagakiPraiseGlitching(praise, t) &&
          praise.elapsed_ms >= praise.glitch_next_ms) {
        RollGlitch(praise);
      }
    }
    if (praise.exiting) {
      praise.exit_ms += elapsed_ms;
    }
  }
  std::erase_if(praises_, [](const DopagakiPraise& praise) {
    if (praise.crumbling) {
      return praise.crumble_ms >= kPraiseCrumbleMs;
    }
    return praise.elapsed_ms >= praise.duration_ms ||
           (praise.exiting && praise.exit_ms >= kPraiseExitMs);
  });

  for (DopagakiRing& ring : rings_) {
    ring.elapsed_ms += elapsed_ms;
  }
  std::erase_if(rings_, [](const DopagakiRing& ring) {
    return ring.elapsed_ms >= kRingDurationMs;
  });
}

void DopagakiEffects::RollGlitch(DopagakiPraise& praise) {
  praise.glitch_next_ms = praise.elapsed_ms + kGlitchRollMs;
  praise.glitch_x = static_cast<float>((random_() - 0.5) * 20);
  praise.glitch_skew = static_cast<float>((random_() - 0.5) * 20);
  praise.glitch_flicker = static_cast<float>(0.4 + random_() * 0.6);
  for (float& band : praise.glitch_bands) {
    band = static_cast<float>((random_() - 0.5) * 48);
  }
}

DopagakiEffects::BurstOptions DopagakiEffects::GetPraiseBurstOptions(
    const DopagakiPraise& praise) const {
  BurstOptions options;
  options.color = praise.choice.particle;
  if (!praise.choice.particle.has_value()) {
    options.rainbow_hue = praise.choice.hue;
  }
  return options;
}

// firePraiseFx() in preview.html.
void DopagakiEffects::FirePraiseEffect(const DopagakiPraise& praise) {
  const DopagakiPraiseStyleInfo& info =
      GetDopagakiPraiseStyleInfo(praise.choice.style);
  const DopagakiRank tier = praise.choice.tier;
  const float t = static_cast<float>(RankIndex(tier));
  BurstOptions options = GetPraiseBurstOptions(praise);
  switch (info.effect) {
    case DopagakiPraiseEffect::kNone:
      return;
    case DopagakiPraiseEffect::kBurst:
      options.speed = 5 + 0.8f * t;
      options.size = 2.5f + 0.5f * t;
      Burst(praise.x, praise.y, GetDopagakiPraiseBurstCount(tier), options);
      return;
    case DopagakiPraiseEffect::kImpact:
      // 1.12: no shockwave and no shaking with the Windows animation setting
      // off. The particles are fine.
      if (!reduced_motion_) {
        rings_.push_back({.x = praise.x,
                          .y = praise.y,
                          .radius_from = praise.width * 0.3f,
                          .radius_to = praise.width * 0.9f,
                          .color = praise.choice.particle.value_or(
                              DopagakiColorFromRgb(0xFFFFFF))});
        shake_amount_ = std::max(shake_amount_, GetDopagakiPraiseShake(tier));
      }
      options.speed = 6 + t;
      options.size = 3 + 0.5f * t;
      Burst(praise.x, praise.y, GetDopagakiPraiseImpactBurstCount(tier),
            options);
      return;
  }
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
  white_flash_elapsed_ms_ = std::min(white_flash_elapsed_ms_ + elapsed_ms,
                                     kWhiteFlashDurationMs);
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

  // drawEdge() in preview.html. New sparks move from the next frame.
  edge_time_ += 0.05 * frames;
  idle_ms_ = std::min(idle_ms_ + elapsed_ms, kEdgeIdleMs + kEdgeFadeMs);
  SpawnEdgeSparks(frames);

  if (badge_.has_value()) {
    badge_->age_ms += elapsed_ms;
    // The SSS badge follows the turning hue. Re-colored every 12 degrees to
    // avoid drawing the text every frame.
    if (badge_rank_ == DopagakiRank::kSSS) {
      double diff = std::fmod(hue_ - badge_hue_ + 360.0, 360.0);
      if (diff >= 12) {
        badge_hue_ = hue_;
        SetBadgeColor(GetDopagakiRankColor(DopagakiRank::kSSS, hue_));
      }
    }
  }

  UpdatePraises(elapsed_ms);
}

bool DopagakiEffects::IsActive() const {
  return !particles_.empty() || banner_.has_value() ||
         combo_label_.has_value() || flash_opacity() > 0 ||
         white_flash_opacity() > 0 ||
         shake_amount_ > 0.2f || IsEdgeVisible() ||
         IsCandidateFrameVisible() || !praises_.empty() || !rings_.empty();
}

void DopagakiEffects::SpawnEdgeSparks(double frames) {
  if (!IsEdgeVisible()) {
    return;
  }
  // Fewer sparks when the Windows animation setting is off, and they fade out
  // with the glow (K4).
  double count = GetDopagakiEdgeStyle(edge_rank_).sparks_per_frame * frames *
                 (reduced_motion_ ? 0.3 : 1.0) * edge_opacity();
  const int r = RankIndex(edge_rank_);
  while (count > 0 && particles_.size() < kMaxParticles) {
    // A fraction of a spark is spawned with that probability.
    if (count < 1 && random_() > count) {
      break;
    }
    // edgePoint(): a random point on the edges and the inward direction.
    const double perimeter = 2.0 * (width_ + height_);
    const double d = random_() * perimeter;
    float x = 0;
    float y = 0;
    float nx = 0;
    float ny = 0;
    if (d < width_) {
      x = static_cast<float>(d);
      ny = 1;
    } else if (d < width_ + height_) {
      x = width_;
      y = static_cast<float>(d - width_);
      nx = -1;
    } else if (d < 2.0 * width_ + height_) {
      x = static_cast<float>(d - width_ - height_);
      y = height_;
      ny = -1;
    } else {
      y = static_cast<float>(d - 2.0 * width_ - height_);
      nx = 1;
    }
    const float speed = static_cast<float>(4 + random_() * (4 + r * 2));
    const float spread = static_cast<float>((random_() - 0.5) * 1.4);
    DopagakiParticle p;
    p.x = x;
    p.y = y;
    p.vx = nx * speed + (ny != 0 ? spread * speed : 0);
    p.vy = ny * speed + (nx != 0 ? spread * speed : 0) - 1;
    p.life = 1;
    p.decay = static_cast<float>(0.025 + random_() * 0.03);
    p.size = static_cast<float>(1.5 + random_() * 2);
    p.color = GetParticleColor(edge_rank_);
    p.shard = true;
    p.rotation = std::atan2(ny, nx);
    particles_.push_back(p);
    count -= 1;
  }
}

float DopagakiEffects::flash_opacity() const {
  if (flash_elapsed_ms_ >= kFlashDurationMs) {
    return 0;
  }
  return static_cast<float>(0.75 *
                            (1 - EaseOut(flash_elapsed_ms_ / kFlashDurationMs)));
}

float DopagakiEffects::white_flash_opacity() const {
  if (white_flash_elapsed_ms_ >= kWhiteFlashDurationMs) {
    return 0;
  }
  return static_cast<float>(
      0.35 * (1 - EaseOut(white_flash_elapsed_ms_ / kWhiteFlashDurationMs)));
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
    if (options.rainbow_hue.has_value()) {
      p.color = DopagakiColorFromHsl(
          *options.rainbow_hue + random_() * options.rainbow_range, 1, 0.62);
    } else {
      p.color = options.color.has_value() ? *options.color
                                          : GetParticleColor(options.rank);
    }
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
