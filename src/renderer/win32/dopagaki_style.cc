// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#include "renderer/win32/dopagaki_style.h"

#include <cstdint>

#include "protocol/renderer_style.pb.h"
#include "renderer/win32/dopagaki_effects.h"
#include "renderer/win32/dopagaki_state.h"

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

constexpr uint32_t kBackground = 0x1D1B2A;
constexpr uint32_t kSeparator = 0x2A2838;
constexpr uint32_t kNumber = 0x7C7A92;
constexpr uint32_t kPurple = 0x7B2CFF;
constexpr float kSelectionBrightness = 0.7f;

void SetColor(uint32_t rgb, RendererStyle::RGBAColor* color) {
  color->set_r((rgb >> 16) & 0xFF);
  color->set_g((rgb >> 8) & 0xFF);
  color->set_b(rgb & 0xFF);
}

DopagakiColor Scale(const DopagakiColor& color, float factor) {
  return {color.r * factor, color.g * factor, color.b * factor};
}

}  // namespace

void ApplyDopagakiStyle(RendererStyle* style) {
  SetColor(kSeparator, style->mutable_border_color());
  SetColor(kBackground, style->mutable_candidate_style()->mutable_background_color());
  SetColor(kBackground, style->mutable_shortcut_style()->mutable_background_color());
  SetColor(kNumber, style->mutable_shortcut_style()->mutable_foreground_color());
  SetColor(kBackground, style->mutable_footer_top_color());
  SetColor(kBackground, style->mutable_footer_bottom_color());
  for (RendererStyle::RGBAColor& color :
       *style->mutable_footer_border_colors()) {
    SetColor(kSeparator, &color);
  }
  SetColor(kBackground, style->mutable_scrollbar_background_color());
  SetColor(kNumber, style->mutable_scrollbar_indicator_color());
}

DopagakiSelectionColors GetDopagakiSelectionColors(DopagakiRank rank,
                                                   double hue) {
  const DopagakiColor left = GetDopagakiRankColor(rank, hue);
  // color-mix(in srgb, rank 40%, #7B2CFF)
  const DopagakiColor purple = DopagakiColorFromRgb(kPurple);
  const DopagakiColor right = {left.r * 0.4f + purple.r * 0.6f,
                               left.g * 0.4f + purple.g * 0.6f,
                               left.b * 0.4f + purple.b * 0.6f};
  return {Scale(left, kSelectionBrightness),
          Scale(right, kSelectionBrightness)};
}

}  // namespace win32
}  // namespace renderer
}  // namespace mozc
