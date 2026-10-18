// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#ifndef MOZC_RENDERER_WIN32_DOPAGAKI_PRAISE_H_
#define MOZC_RENDERER_WIN32_DOPAGAKI_PRAISE_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "renderer/win32/dopagaki_state.h"

namespace mozc {
namespace renderer {
namespace win32 {

// Update 1 "praise words" (docs/dopagaki/update1_praise.md) as pure logic.
// No Windows API is used here. Lengths are in DIPs, times in ms and angles in
// degrees.

// Returns a number in [0, 1). Replaced in tests.
using DopagakiRandomFunction = std::function<double()>;

// cubic-bezier() of CSS evaluated at |t| in [0, 1].
double DopagakiCubicBezier(double x1, double y1, double x2, double y2,
                           double t);

// The easing of a keyframe segment (EASE in preview.html).
enum class DopagakiEase {
  kLinear,
  kIn,     // cubic-bezier(.42, 0, 1, 1)
  kOut,    // cubic-bezier(0, 0, .58, 1)
  kInOut,  // cubic-bezier(.42, 0, .58, 1)
  kBack,   // cubic-bezier(.34, 1.56, .64, 1). Goes beyond 1 and comes back.
};
double DopagakiEaseValue(DopagakiEase ease, double t);

// The 12 styles of 1.3 and the fade used when the Windows animation setting
// is off (1.12).
enum class DopagakiPraiseStyle {
  kPop,
  kSlam,
  kSpin,
  kSlide,
  kDrop,
  kRise,
  kJelly,
  kFlip,
  kGlitch,
  kWave,
  kScatter,
  kStamp,
  kFade,
};
inline constexpr size_t kDopagakiPraiseStyleCount = 12;  // Without kFade.

// The 5 colors of 1.4.
enum class DopagakiPraiseColor {
  kRank,
  kWhite,
  kComplement,
  kGold,
  kRainbow,
};
inline constexpr size_t kDopagakiPraiseColorCount = 5;

// What happens once at a moment of a style (the "効果" column of 1.3).
enum class DopagakiPraiseEffect {
  kNone,
  kBurst,   // Particles (1.7).
  kImpact,  // A shockwave, particles and shaking (1.7).
};

// Styles whose letters move one by one (1.3.2).
enum class DopagakiPraiseLetters {
  kNone,
  kWave,
  kScatter,
};

// A font of 1.3. |families| are tried in order. |weight| is a
// DWRITE_FONT_WEIGHT value.
struct DopagakiPraiseFont {
  std::array<const wchar_t*, 3> families = {};
  int weight = 400;
  bool italic = false;
  bool condensed = false;
};

struct DopagakiPraiseStyleInfo {
  DopagakiPraiseStyle style = DopagakiPraiseStyle::kPop;
  const char* id = "";  // "pop", "slam", ... like preview.html.
  DopagakiPraiseFont font;
  // Multiplied to the font size (1.1 for wave).
  float size_scale = 1;
  // For the letter styles, the minimum (see GetDopagakiPraiseDurationMs).
  double duration_ms = 1300;
  // The tilt is tilt_base + uniform(-tilt_range, tilt_range).
  float tilt_base = 0;
  float tilt_range = 8;
  // When the effect happens, as a fraction of the duration.
  double effect_at = 0;
  DopagakiPraiseEffect effect = DopagakiPraiseEffect::kBurst;
  bool bottom_pivot = false;    // drop and jelly.
  bool mirrored_by_side = false;  // slide.
  bool afterimages = false;     // slide.
  float back_opacity = 1;       // flip: 0.6 while turned over.
  bool glitch = false;
  bool frame = false;           // stamp.
  DopagakiPraiseLetters letters = DopagakiPraiseLetters::kNone;
};
const DopagakiPraiseStyleInfo& GetDopagakiPraiseStyleInfo(
    DopagakiPraiseStyle style);

// The words of 1.2 for |tier|, and the words of the milestones (200, 300 ...).
std::span<const std::wstring_view> GetDopagakiPraiseWords(DopagakiRank tier);
std::span<const std::wstring_view> GetDopagakiMilestoneWords();

// 1.5: the font size before the style's size_scale. |screen_width| is the
// width of the monitor.
float GetDopagakiPraiseFontSize(DopagakiRank tier, float screen_width);
// 1.5: the CSS blur radii of the two glows.
std::array<float, 2> GetDopagakiPraiseGlowRadii(DopagakiRank tier,
                                                float screen_width);
// 1.7.
int GetDopagakiPraiseBurstCount(DopagakiRank tier);
int GetDopagakiPraiseImpactBurstCount(DopagakiRank tier);
float GetDopagakiPraiseShake(DopagakiRank tier);
// 1.4: the color weights of |tier| (rank, white, complement, gold, rainbow).
std::array<int, kDopagakiPraiseColorCount> GetDopagakiPraiseColorWeights(
    DopagakiRank tier);

// 1.2: no word comes again until all words of the bag were shown, and the
// first word after refilling is never the last one (nextFromBag).
class DopagakiWordBag {
 public:
  explicit DopagakiWordBag(size_t size) : size_(size) {}

  // Returns the index of the next word in [0, size).
  size_t Next(const DopagakiRandomFunction& random);

 private:
  size_t size_;
  std::vector<size_t> bag_;
  std::optional<size_t> last_;
};

// Chooses the words, the styles and the colors (1.2 to 1.4). Remembered only
// while the renderer runs.
class DopagakiPraisePicker {
 public:
  explicit DopagakiPraisePicker(DopagakiRandomFunction random);

  std::wstring_view NextWord(DopagakiRank tier);
  std::wstring_view NextMilestoneWord();
  // Never the same style as the last two.
  DopagakiPraiseStyle NextStyle();
  // By the weights of |tier|. Rolled again once when it is the same as the
  // last color.
  DopagakiPraiseColor NextColor(DopagakiRank tier);

 private:
  DopagakiRandomFunction random_;
  std::vector<DopagakiWordBag> bags_;
  DopagakiWordBag milestone_bag_;
  std::vector<DopagakiPraiseStyle> recent_styles_;
  std::optional<DopagakiPraiseColor> last_color_;
};

// The transform of a whole word at a moment (1.3.1). The scale "s" is already
// multiplied into scale_x and scale_y.
struct DopagakiPraiseFrame {
  float opacity = 1;
  float scale_x = 1;
  float scale_y = 1;
  float rotation = 0;  // Degrees.
  float offset_x = 0;
  float offset_y = 0;
  float skew_x = 0;    // Degrees. x' = x + tan(skew_x) * y.
};
// |t| is the fraction of the duration in [0, 1]. |side| is +1 when the word
// is on the right half of the screen and -1 on the left half (slide only).
DopagakiPraiseFrame GetDopagakiPraiseFrame(DopagakiPraiseStyle style, double t,
                                           int side);

// The random values of one letter of scatter, chosen when the word appears.
struct DopagakiPraiseLetterRandom {
  float dx0 = 0;
  float dy0 = 0;
  float rotation0 = 0;
  float dx_end = 0;
  float dy_end = 0;
  float rotation_end = 0;
};
DopagakiPraiseLetterRandom MakeDopagakiPraiseLetterRandom(
    const DopagakiRandomFunction& random);

// The transform of letter |i| of |n| at |elapsed_ms| (1.3.2).
struct DopagakiPraiseLetterFrame {
  float opacity = 1;
  float scale = 1;
  float rotation = 0;  // Degrees.
  float dx = 0;
  float dy = 0;
};
DopagakiPraiseLetterFrame GetDopagakiPraiseLetterFrame(
    DopagakiPraiseStyle style, size_t i, size_t n, double elapsed_ms,
    double duration_ms, const DopagakiPraiseLetterRandom& random);

// |letter_count| counts the spaces too.
double GetDopagakiPraiseDurationMs(DopagakiPraiseStyle style,
                                   size_t letter_count);

struct DopagakiBox {
  float left = 0;
  float top = 0;
  float right = 0;
  float bottom = 0;

  float width() const { return right - left; }
  float height() const { return bottom - top; }
  DopagakiBox Inflated(float margin) const {
    return {left - margin, top - margin, right + margin, bottom + margin};
  }
  friend bool operator==(const DopagakiBox&, const DopagakiBox&) = default;
};
// The area where |a| and |b| overlap.
float GetDopagakiBoxOverlap(const DopagakiBox& a, const DopagakiBox& b);

// 1.6: the width and the height of the box of a word of |width| x |height|
// tilted by |tilt_degrees|.
std::array<float, 2> GetDopagakiPraiseBoxSize(float width, float height,
                                              float tilt_degrees);

struct DopagakiPraisePosition {
  float x = 0;  // The center.
  float y = 0;
  DopagakiBox box;
};
// 1.6 (pickPosition): a box of |box_width| x |box_height| on the screen that
// does not overlap |avoid|, or the one with the least overlap after 16 tries.
DopagakiPraisePosition PickDopagakiPraisePosition(
    float box_width, float box_height, float screen_width, float screen_height,
    std::span<const DopagakiBox> avoid, const DopagakiRandomFunction& random);

}  // namespace win32
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_WIN32_DOPAGAKI_PRAISE_H_
