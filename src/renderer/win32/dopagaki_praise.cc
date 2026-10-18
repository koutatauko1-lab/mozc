// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#include "renderer/win32/dopagaki_praise.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <numeric>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "renderer/win32/dopagaki_state.h"

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

using Style = DopagakiPraiseStyle;
using Ease = DopagakiEase;

constexpr double kPi = 3.14159265358979323846;

// One dimension of a cubic Bezier curve from 0 to 1.
double SampleCurve(double a1, double a2, double t) {
  const double u = 1 - t;
  return 3 * u * u * t * a1 + 3 * u * t * t * a2 + t * t * t;
}

double SampleCurveDerivative(double a1, double a2, double t) {
  const double u = 1 - t;
  return 3 * u * u * a1 + 6 * u * t * (a2 - a1) + 3 * t * t * (1 - a2);
}

double Clamp01(double value) { return std::clamp(value, 0.0, 1.0); }

int TierIndex(DopagakiRank tier) {
  return std::clamp(static_cast<int>(tier), 0, 4);
}

// 1.2. ASCII only.
constexpr std::wstring_view kWordsB[] = {
    L"Nice!",     L"Good!",     L"Cool!",       L"Sweet!",
    L"Nice one!", L"Let's go!", L"Good start!", L"Warming up!",
};
constexpr std::wstring_view kWordsA[] = {
    L"Great!",     L"Awesome!",    L"Smooth!",       L"Well done!",
    L"Way to go!", L"Keep it up!", L"You got this!", L"Go go go!",
    L"Nice combo!", L"Looking good!",
};
constexpr std::wstring_view kWordsS[] = {
    L"Excellent!",  L"Amazing!",     L"Fantastic!", L"Superb!",
    L"Brilliant!",  L"Impressive!",  L"Outstanding!", L"On fire!",
    L"So good!",    L"Keep rolling!",
};
constexpr std::wstring_view kWordsSS[] = {
    L"Incredible!",  L"Unbelievable!", L"Spectacular!", L"Phenomenal!",
    L"Magnificent!", L"Insane!",       L"Wild!",        L"Beast mode!",
    L"Crushing it!", L"Too fast!",     L"Don't stop!",  L"No way!",
};
constexpr std::wstring_view kWordsSSS[] = {
    L"GODLIKE!!",     L"LEGENDARY!!",         L"UNSTOPPABLE!!",
    L"PERFECT!!",     L"GENIUS!!",            L"MASTERPIECE!!",
    L"MYTHICAL!!",    L"UNREAL!!",            L"SUPREME!!",
    L"MIND-BLOWING!!", L"OUT OF THIS WORLD!!", L"HALL OF FAME!!",
    L"TYPING GOD!!",  L"ABSOLUTE LEGEND!!",
};
constexpr std::wstring_view kMilestoneWords[] = {
    L"BEYOND SSS!",     L"ASCENDED!",       L"TRANSCENDENT!",
    L"INFINITE POWER!", L"KEYBOARD MASTER!", L"RECORD BREAKER!",
    L"NO LIMITS!",      L"ULTIMATE!",
};

// PRAISE_SIZE, PRAISE_BURST, PRAISE_IMPACT_BURST, PRAISE_SHAKE and
// COLOR_WEIGHTS in preview.html.
constexpr float kFontSize[] = {56, 64, 74, 86, 100};
constexpr int kBurstCount[] = {6, 10, 16, 24, 36};
constexpr int kImpactBurstCount[] = {8, 12, 18, 26, 36};
constexpr float kShake[] = {1, 2, 3, 5, 7};
constexpr std::array<int, kDopagakiPraiseColorCount> kColorWeights[] = {
    {60, 25, 15, 0, 0},  {45, 25, 20, 10, 0},  {35, 20, 15, 20, 10},
    {25, 15, 15, 25, 20}, {20, 10, 10, 30, 30},
};

// The fonts of 1.3 (DWRITE_FONT_WEIGHT values). The faces of Arial Black and
// Impact have only one weight, so DirectWrite picks it for any weight.
constexpr DopagakiPraiseFont kArialBlack = {
    {L"Arial Black", L"Segoe UI", nullptr}, 900};
constexpr DopagakiPraiseFont kImpact = {
    {L"Impact", L"Arial Black", nullptr}, 400};
constexpr DopagakiPraiseFont kSegoeBlack = {{L"Segoe UI", nullptr, nullptr},
                                            900};

const DopagakiPraiseStyleInfo kStyles[] = {
    {.style = Style::kPop, .id = "pop", .font = kArialBlack},
    {.style = Style::kSlam,
     .id = "slam",
     .font = kImpact,
     .effect_at = 0.1,
     .effect = DopagakiPraiseEffect::kImpact},
    {.style = Style::kSpin,
     .id = "spin",
     .font = {{L"Segoe UI", nullptr, nullptr}, 900, /*italic=*/true},
     .tilt_range = 5,
     .effect_at = 0.22},
    {.style = Style::kSlide,
     .id = "slide",
     .font = {{L"Bahnschrift", L"Segoe UI", nullptr},
              700,
              /*italic=*/true,
              /*condensed=*/true},
     .effect_at = 0.16,
     .mirrored_by_side = true,
     .afterimages = true},
    {.style = Style::kDrop,
     .id = "drop",
     .font = kArialBlack,
     .duration_ms = 1400,
     .effect_at = 0.18,
     .bottom_pivot = true},
    {.style = Style::kRise,
     .id = "rise",
     .font = kSegoeBlack,
     .duration_ms = 1500},
    {.style = Style::kJelly,
     .id = "jelly",
     .font = {{L"Comic Sans MS", L"Segoe UI", nullptr}, 700},
     .effect_at = 0.12,
     .bottom_pivot = true},
    {.style = Style::kFlip,
     .id = "flip",
     .font = kImpact,
     .effect_at = 0.26,
     .back_opacity = 0.6f},
    {.style = Style::kGlitch,
     .id = "glitch",
     .font = {{L"Consolas", L"Courier New", nullptr}, 700},
     .glitch = true},
    {.style = Style::kWave,
     .id = "wave",
     .font = {{L"Ink Free", L"Segoe Print", L"Segoe UI"}, 400},
     .size_scale = 1.1f,
     .duration_ms = 1500,
     .tilt_range = 5,
     .letters = DopagakiPraiseLetters::kWave},
    {.style = Style::kScatter,
     .id = "scatter",
     .font = kArialBlack,
     .duration_ms = 1500,
     .tilt_range = 5,
     .effect = DopagakiPraiseEffect::kNone,
     .letters = DopagakiPraiseLetters::kScatter},
    {.style = Style::kStamp,
     .id = "stamp",
     .font = kImpact,
     .tilt_base = -12,
     .tilt_range = 4,
     .effect_at = 0.12,
     .effect = DopagakiPraiseEffect::kImpact,
     .frame = true},
    {.style = Style::kFade,
     .id = "fade",
     .font = kSegoeBlack,
     .tilt_range = 0,
     .effect = DopagakiPraiseEffect::kNone},
};
static_assert(std::size(kStyles) == kDopagakiPraiseStyleCount + 1);

// 1.3.1. (fraction of the duration, value, easing of the next segment).
struct Key {
  double t;
  float value;
  Ease ease = Ease::kLinear;
};

struct Tracks {
  std::span<const Key> o;
  std::span<const Key> s;
  std::span<const Key> sx;
  std::span<const Key> sy;
  std::span<const Key> r;
  std::span<const Key> x;
  std::span<const Key> y;
  std::span<const Key> k;
};

constexpr Key kPopO[] = {{0, 0}, {.1, 1}, {.8, 1, Ease::kIn}, {1, 0}};
constexpr Key kPopS[] = {{0, .2f, Ease::kOut},   {.14, 1.25f, Ease::kInOut},
                         {.24, .95f, Ease::kInOut}, {.32, 1},
                         {.8, 1, Ease::kIn},     {1, 1.15f}};

constexpr Key kSlamO[] = {{0, 0}, {.06, 1}, {.8, 1, Ease::kIn}, {1, 0}};
constexpr Key kSlamS[] = {{0, 3, Ease::kIn},       {.1, .9f, Ease::kOut},
                          {.16, 1.08f, Ease::kInOut}, {.22, 1},
                          {.8, 1, Ease::kIn},      {1, .9f}};
constexpr Key kSlamY[] = {
    {0, -40, Ease::kIn}, {.1, 0}, {.8, 0, Ease::kIn}, {1, 10}};

constexpr Key kSpinO[] = {{0, 0}, {.12, 1}, {.78, 1, Ease::kIn}, {1, 0}};
constexpr Key kSpinS[] = {{0, 0, Ease::kOut}, {.22, 1.1f, Ease::kInOut},
                          {.3, 1},            {.78, 1, Ease::kIn},
                          {1, .2f}};
constexpr Key kSpinR[] = {{0, -540, Ease::kOut}, {.22, 10, Ease::kInOut},
                          {.3, 0},               {.78, 0, Ease::kIn},
                          {1, 360}};

constexpr Key kSlideO[] = {{0, 0}, {.08, 1}, {.8, 1}, {1, 0}};
constexpr Key kSlideX[] = {{0, 420, Ease::kOut}, {.16, -20, Ease::kInOut},
                           {.24, 0},             {.8, 0, Ease::kIn},
                           {1, -320}};
constexpr Key kSlideK[] = {{0, 20, Ease::kOut}, {.16, -8, Ease::kInOut},
                           {.24, 0},            {.8, 0, Ease::kIn},
                           {1, 15}};

constexpr Key kDropO[] = {{0, 0}, {.06, 1}, {.82, 1, Ease::kIn}, {1, 0}};
constexpr Key kDropY[] = {{0, -260, Ease::kIn}, {.18, 0, Ease::kOut},
                          {.26, -60, Ease::kIn}, {.34, 0, Ease::kOut},
                          {.4, -18, Ease::kIn},  {.46, 0},
                          {.82, 0, Ease::kIn},   {1, 20}};
constexpr Key kDropSx[] = {{0, .9f},    {.18, 1.25f, Ease::kOut},
                           {.26, .95f}, {.34, 1.12f},
                           {.4, 1},     {.46, 1.05f},
                           {.52, 1}};
constexpr Key kDropSy[] = {{0, 1.15f},  {.18, .75f, Ease::kOut},
                           {.26, 1.08f}, {.34, .88f},
                           {.4, 1},     {.46, .95f},
                           {.52, 1}};

constexpr Key kRiseO[] = {{0, 0}, {.18, 1, Ease::kOut}, {.8, 1}, {1, 0}};
constexpr Key kRiseY[] = {{0, 30, Ease::kOut}, {.18, 0}, {1, -90}};
constexpr Key kRiseS[] = {{0, .9f, Ease::kOut}, {.18, 1}};
constexpr Key kRiseR[] = {{0, -3, Ease::kInOut},
                          {.25, 3, Ease::kInOut},
                          {.5, -3, Ease::kInOut},
                          {.75, 3, Ease::kInOut},
                          {1, -3}};

constexpr Key kJellyO[] = {{0, 0}, {.06, 1}, {.82, 1, Ease::kIn}, {1, 0}};
constexpr Key kJellySx[] = {
    {0, 0, Ease::kOut},        {.12, 1.3f, Ease::kInOut},
    {.2, .8f, Ease::kInOut},   {.28, 1.15f, Ease::kInOut},
    {.36, .93f, Ease::kInOut}, {.44, 1.04f, Ease::kInOut},
    {.52, 1},                  {.82, 1, Ease::kIn},
    {1, 1.4f}};
constexpr Key kJellySy[] = {
    {0, 0, Ease::kOut},        {.12, .7f, Ease::kInOut},
    {.2, 1.2f, Ease::kInOut},  {.28, .85f, Ease::kInOut},
    {.36, 1.07f, Ease::kInOut}, {.44, .96f, Ease::kInOut},
    {.52, 1},                  {.82, 1, Ease::kIn},
    {1, 0}};

constexpr Key kFlipO[] = {{0, 0}, {.04, 1}, {.82, 1, Ease::kIn}, {1, 0}};
constexpr Key kFlipSx[] = {{0, 0, Ease::kOut},     {.1, -.85f, Ease::kIn},
                           {.18, 0, Ease::kOut},   {.26, 1.1f, Ease::kInOut},
                           {.32, 1},               {.82, 1, Ease::kIn},
                           {1, 0}};

constexpr Key kGlitchO[] = {{0, 0}, {.03, 1}, {.85, 1}, {1, 0}};

constexpr Key kStampO[] = {{0, 0}, {.05, 1}, {.82, 1, Ease::kIn}, {1, 0}};
constexpr Key kStampS[] = {{0, 2.6f, Ease::kIn},
                           {.12, .92f, Ease::kOut},
                           {.18, 1.04f, Ease::kInOut},
                           {.24, 1}};

constexpr Key kFadeO[] = {{0, 0}, {.15, 1}, {.8, 1}, {1, 0}};

Tracks GetTracks(Style style) {
  switch (style) {
    case Style::kPop:
      return {.o = kPopO, .s = kPopS};
    case Style::kSlam:
      return {.o = kSlamO, .s = kSlamS, .y = kSlamY};
    case Style::kSpin:
      return {.o = kSpinO, .s = kSpinS, .r = kSpinR};
    case Style::kSlide:
      return {.o = kSlideO, .x = kSlideX, .k = kSlideK};
    case Style::kDrop:
      return {.o = kDropO, .sx = kDropSx, .sy = kDropSy, .y = kDropY};
    case Style::kRise:
      return {.o = kRiseO, .s = kRiseS, .r = kRiseR, .y = kRiseY};
    case Style::kJelly:
      return {.o = kJellyO, .sx = kJellySx, .sy = kJellySy};
    case Style::kFlip:
      return {.o = kFlipO, .sx = kFlipSx};
    case Style::kGlitch:
      return {.o = kGlitchO};
    case Style::kStamp:
      return {.o = kStampO, .s = kStampS};
    case Style::kFade:
      return {.o = kFadeO};
    case Style::kWave:
    case Style::kScatter:
      // The letters move one by one (GetDopagakiPraiseLetterFrame).
      return {};
  }
  return {};
}

// track() in preview.html.
float Track(std::span<const Key> keys, double t, float default_value) {
  if (keys.empty()) {
    return default_value;
  }
  if (t <= keys.front().t) {
    return keys.front().value;
  }
  for (size_t i = 0; i + 1 < keys.size(); ++i) {
    const Key& a = keys[i];
    const Key& b = keys[i + 1];
    if (t <= b.t) {
      const double span = b.t - a.t;
      const double u = span > 0 ? (t - a.t) / span : 1;
      return static_cast<float>(a.value + (b.value - a.value) *
                                              DopagakiEaseValue(a.ease, u));
    }
  }
  return keys.back().value;
}

size_t RandomIndex(const DopagakiRandomFunction& random, size_t size) {
  if (size == 0) {
    return 0;
  }
  return std::min(static_cast<size_t>(random() * static_cast<double>(size)),
                  size - 1);
}

float RandomSign(const DopagakiRandomFunction& random) {
  return random() < 0.5 ? -1.0f : 1.0f;
}

}  // namespace

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

double DopagakiEaseValue(DopagakiEase ease, double t) {
  switch (ease) {
    case Ease::kLinear:
      return Clamp01(t);
    case Ease::kIn:
      return DopagakiCubicBezier(.42, 0, 1, 1, t);
    case Ease::kOut:
      return DopagakiCubicBezier(0, 0, .58, 1, t);
    case Ease::kInOut:
      return DopagakiCubicBezier(.42, 0, .58, 1, t);
    case Ease::kBack:
      return DopagakiCubicBezier(.34, 1.56, .64, 1, t);
  }
  return Clamp01(t);
}

const DopagakiPraiseStyleInfo& GetDopagakiPraiseStyleInfo(
    DopagakiPraiseStyle style) {
  const size_t index = static_cast<size_t>(style);
  return kStyles[index < std::size(kStyles) ? index : 0];
}

std::span<const std::wstring_view> GetDopagakiPraiseWords(DopagakiRank tier) {
  switch (tier) {
    case DopagakiRank::kB:
      return kWordsB;
    case DopagakiRank::kA:
      return kWordsA;
    case DopagakiRank::kS:
      return kWordsS;
    case DopagakiRank::kSS:
      return kWordsSS;
    case DopagakiRank::kSSS:
      return kWordsSSS;
  }
  return kWordsB;
}

std::span<const std::wstring_view> GetDopagakiMilestoneWords() {
  return kMilestoneWords;
}

float GetDopagakiPraiseFontSize(DopagakiRank tier, float screen_width) {
  const float k = std::clamp(screen_width / 1600.0f, 0.5f, 1.2f);
  return kFontSize[TierIndex(tier)] * k;
}

std::array<float, 2> GetDopagakiPraiseGlowRadii(DopagakiRank tier,
                                                float screen_width) {
  const float k = std::clamp(screen_width / 1600.0f, 0.5f, 1.2f);
  const float t = static_cast<float>(TierIndex(tier));
  const float scale = std::max(0.6f, k);
  return {(10 + 2 * t) * scale, (24 + 6 * t) * scale};
}

int GetDopagakiPraiseBurstCount(DopagakiRank tier) {
  return kBurstCount[TierIndex(tier)];
}

int GetDopagakiPraiseImpactBurstCount(DopagakiRank tier) {
  return kImpactBurstCount[TierIndex(tier)];
}

float GetDopagakiPraiseShake(DopagakiRank tier) {
  return kShake[TierIndex(tier)];
}

std::array<int, kDopagakiPraiseColorCount> GetDopagakiPraiseColorWeights(
    DopagakiRank tier) {
  return kColorWeights[TierIndex(tier)];
}

size_t DopagakiWordBag::Next(const DopagakiRandomFunction& random) {
  if (size_ == 0) {
    return 0;
  }
  if (bag_.empty()) {
    std::vector<size_t> indices(size_);
    std::iota(indices.begin(), indices.end(), 0);
    for (size_t i = indices.size() - 1; i > 0; --i) {
      std::swap(indices[i], indices[RandomIndex(random, i + 1)]);
    }
    // The last one is drawn first. It must not be the word just shown.
    if (indices.size() > 1 && last_.has_value() && indices.back() == *last_) {
      std::swap(indices.front(), indices.back());
    }
    bag_ = std::move(indices);
  }
  const size_t index = bag_.back();
  bag_.pop_back();
  last_ = index;
  return index;
}

DopagakiPraisePicker::DopagakiPraisePicker(DopagakiRandomFunction random)
    : random_(std::move(random)),
      milestone_bag_(std::size(kMilestoneWords)) {
  for (int tier = 0; tier <= 4; ++tier) {
    bags_.emplace_back(
        GetDopagakiPraiseWords(static_cast<DopagakiRank>(tier)).size());
  }
}

std::wstring_view DopagakiPraisePicker::NextWord(DopagakiRank tier) {
  const std::span<const std::wstring_view> words =
      GetDopagakiPraiseWords(tier);
  return words[bags_[TierIndex(tier)].Next(random_)];
}

std::wstring_view DopagakiPraisePicker::NextMilestoneWord() {
  return kMilestoneWords[milestone_bag_.Next(random_)];
}

DopagakiPraiseStyle DopagakiPraisePicker::NextStyle() {
  std::vector<Style> pool;
  for (size_t i = 0; i < kDopagakiPraiseStyleCount; ++i) {
    const Style style = static_cast<Style>(i);
    if (std::find(recent_styles_.begin(), recent_styles_.end(), style) ==
        recent_styles_.end()) {
      pool.push_back(style);
    }
  }
  const Style style = pool[RandomIndex(random_, pool.size())];
  recent_styles_.push_back(style);
  if (recent_styles_.size() > 2) {
    recent_styles_.erase(recent_styles_.begin());
  }
  return style;
}

DopagakiPraiseColor DopagakiPraisePicker::NextColor(DopagakiRank tier) {
  const std::array<int, kDopagakiPraiseColorCount> weights =
      GetDopagakiPraiseColorWeights(tier);
  const auto roll = [&]() {
    double r = random_() * std::accumulate(weights.begin(), weights.end(), 0);
    for (size_t i = 0; i < weights.size(); ++i) {
      r -= weights[i];
      if (r < 0) {
        return static_cast<DopagakiPraiseColor>(i);
      }
    }
    return DopagakiPraiseColor::kRank;
  };
  DopagakiPraiseColor color = roll();
  if (last_color_.has_value() && color == *last_color_) {
    color = roll();
  }
  last_color_ = color;
  return color;
}

DopagakiPraiseFrame GetDopagakiPraiseFrame(DopagakiPraiseStyle style, double t,
                                           int side) {
  const Tracks tracks = GetTracks(style);
  const float mirror =
      GetDopagakiPraiseStyleInfo(style).mirrored_by_side && side < 0 ? -1.0f
                                                                     : 1.0f;
  const float s = Track(tracks.s, t, 1);
  DopagakiPraiseFrame frame;
  frame.opacity = Track(tracks.o, t, 1);
  frame.scale_x = s * Track(tracks.sx, t, 1);
  frame.scale_y = s * Track(tracks.sy, t, 1);
  frame.rotation = Track(tracks.r, t, 0);
  frame.offset_x = Track(tracks.x, t, 0) * mirror;
  frame.offset_y = Track(tracks.y, t, 0);
  frame.skew_x = Track(tracks.k, t, 0) * mirror;
  return frame;
}

DopagakiPraiseLetterRandom MakeDopagakiPraiseLetterRandom(
    const DopagakiRandomFunction& random) {
  DopagakiPraiseLetterRandom result;
  const double angle = random() * 2 * kPi;
  const double distance = 250 + random() * 150;
  result.dx0 = static_cast<float>(std::cos(angle) * distance);
  result.dy0 = static_cast<float>(std::sin(angle) * distance * 0.7);
  result.rotation0 = RandomSign(random) * static_cast<float>(90 + random() * 180);
  const double end_angle = random() * 2 * kPi;
  const double end_distance = 260 + random() * 160;
  result.dx_end = static_cast<float>(std::cos(end_angle) * end_distance);
  result.dy_end = static_cast<float>(std::sin(end_angle) * end_distance);
  result.rotation_end =
      RandomSign(random) * static_cast<float>(180 + random() * 180);
  return result;
}

DopagakiPraiseLetterFrame GetDopagakiPraiseLetterFrame(
    DopagakiPraiseStyle style, size_t i, size_t n, double elapsed_ms,
    double duration_ms, const DopagakiPraiseLetterRandom& random) {
  const double T = elapsed_ms;
  const double index = static_cast<double>(i);
  DopagakiPraiseLetterFrame frame;
  if (style == Style::kWave) {
    const double a = Clamp01((T - 35 * index) / 220);
    const double ea = DopagakiEaseValue(Ease::kBack, a);
    double opacity = std::min(1.0, a / 0.4);
    double dy = 34 * (1 - ea);
    const double scale = 0.6 + 0.4 * ea;
    const double amp = 9 * Clamp01((T - 35 * index - 220) / 200);
    dy += -amp * std::sin(2 * kPi * T / 700 - 0.55 * index);
    const double t0 =
        duration_ms - 300 - 20 * (static_cast<double>(std::max<size_t>(n, 1)) - 1);
    const double e = Clamp01((T - t0 - 20 * index) / 300);
    const double ee = DopagakiEaseValue(Ease::kIn, e);
    opacity *= 1 - ee;
    dy += 40 * ee;
    frame.opacity = static_cast<float>(opacity);
    frame.scale = static_cast<float>(scale);
    frame.dy = static_cast<float>(dy);
    return frame;
  }
  if (style == Style::kScatter) {
    const double a = Clamp01((T - 15 * index) / 380);
    const double ea = DopagakiEaseValue(Ease::kBack, a);
    double dx = (1 - ea) * random.dx0;
    double dy = (1 - ea) * random.dy0;
    double rotation = (1 - ea) * random.rotation0;
    double scale = 0.4 + 0.6 * ea;
    double opacity = std::min(1.0, a / 0.4);
    const double e = Clamp01((T - (duration_ms - 350)) / 350);
    const double ee = DopagakiEaseValue(Ease::kOut, e);
    dx += ee * random.dx_end;
    dy += ee * random.dy_end;
    rotation += ee * random.rotation_end;
    scale *= 1 + 0.3 * ee;
    opacity *= 1 - DopagakiEaseValue(Ease::kIn, e);
    frame.opacity = static_cast<float>(opacity);
    frame.scale = static_cast<float>(scale);
    frame.rotation = static_cast<float>(rotation);
    frame.dx = static_cast<float>(dx);
    frame.dy = static_cast<float>(dy);
    return frame;
  }
  return frame;
}

double GetDopagakiPraiseDurationMs(DopagakiPraiseStyle style,
                                   size_t letter_count) {
  const DopagakiPraiseStyleInfo& info = GetDopagakiPraiseStyleInfo(style);
  if (info.letters != DopagakiPraiseLetters::kNone) {
    return std::max(info.duration_ms,
                    900 + 55 * static_cast<double>(letter_count));
  }
  return info.duration_ms;
}

float GetDopagakiBoxOverlap(const DopagakiBox& a, const DopagakiBox& b) {
  const float width =
      std::max(0.0f, std::min(a.right, b.right) - std::max(a.left, b.left));
  const float height =
      std::max(0.0f, std::min(a.bottom, b.bottom) - std::max(a.top, b.top));
  return width * height;
}

std::array<float, 2> GetDopagakiPraiseBoxSize(float width, float height,
                                              float tilt_degrees) {
  const double radians = tilt_degrees * kPi / 180;
  const float c = static_cast<float>(std::abs(std::cos(radians)));
  const float s = static_cast<float>(std::abs(std::sin(radians)));
  return {(width * c + height * s) * 1.15f + 20,
          (width * s + height * c) * 1.15f + 20};
}

DopagakiPraisePosition PickDopagakiPraisePosition(
    float box_width, float box_height, float screen_width, float screen_height,
    std::span<const DopagakiBox> avoid, const DopagakiRandomFunction& random) {
  constexpr float kMargin = 16;
  constexpr int kTries = 16;
  std::optional<DopagakiPraisePosition> best;
  float best_overlap = 0;
  for (int i = 0; i < kTries; ++i) {
    const float x =
        kMargin + box_width / 2 +
        static_cast<float>(random()) *
            std::max(0.0f, screen_width - 2 * kMargin - box_width);
    const float y =
        kMargin + box_height / 2 +
        static_cast<float>(random()) *
            std::max(0.0f, screen_height - 2 * kMargin - box_height);
    const DopagakiPraisePosition position = {
        x, y,
        {x - box_width / 2, y - box_height / 2, x + box_width / 2,
         y + box_height / 2}};
    float overlap = 0;
    for (const DopagakiBox& box : avoid) {
      overlap += GetDopagakiBoxOverlap(position.box, box);
    }
    if (overlap == 0) {
      return position;
    }
    if (!best.has_value() || overlap < best_overlap) {
      best = position;
      best_overlap = overlap;
    }
  }
  return *best;
}

}  // namespace win32
}  // namespace renderer
}  // namespace mozc
