// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#include "renderer/win32/dopagaki_style.h"

#include "protocol/renderer_style.pb.h"
#include "renderer/win32/dopagaki_effects.h"
#include "renderer/win32/dopagaki_state.h"
#include "testing/gunit.h"

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

TEST(DopagakiStyleTest, ApplyDopagakiStyle) {
  RendererStyle style;
  style.add_footer_border_colors();
  ApplyDopagakiStyle(&style);
  // #1D1B2A
  EXPECT_EQ(style.candidate_style().background_color().r(), 0x1D);
  EXPECT_EQ(style.candidate_style().background_color().g(), 0x1B);
  EXPECT_EQ(style.candidate_style().background_color().b(), 0x2A);
  EXPECT_EQ(style.shortcut_style().background_color().b(), 0x2A);
  // #7C7A92
  EXPECT_EQ(style.shortcut_style().foreground_color().r(), 0x7C);
  EXPECT_EQ(style.shortcut_style().foreground_color().g(), 0x7A);
  EXPECT_EQ(style.shortcut_style().foreground_color().b(), 0x92);
  // #2A2838
  EXPECT_EQ(style.border_color().r(), 0x2A);
  EXPECT_EQ(style.footer_border_colors(0).b(), 0x38);
}

TEST(DopagakiStyleTest, SelectionColors) {
  const DopagakiSelectionColors colors =
      GetDopagakiSelectionColors(DopagakiRank::kA, 0);
  const DopagakiColor green = DopagakiColorFromRgb(0x3BE37A);
  const DopagakiColor purple = DopagakiColorFromRgb(0x7B2CFF);
  EXPECT_NEAR(colors.left.r, green.r * 0.7f, 1e-5);
  EXPECT_NEAR(colors.left.g, green.g * 0.7f, 1e-5);
  EXPECT_NEAR(colors.left.b, green.b * 0.7f, 1e-5);
  EXPECT_NEAR(colors.right.r, (green.r * 0.4f + purple.r * 0.6f) * 0.7f,
              1e-5);
  EXPECT_NEAR(colors.right.b, (green.b * 0.4f + purple.b * 0.6f) * 0.7f,
              1e-5);
}

TEST(DopagakiStyleTest, SelectionColorsOfSssFollowTheHue) {
  const DopagakiSelectionColors red =
      GetDopagakiSelectionColors(DopagakiRank::kSSS, 0);
  const DopagakiSelectionColors green =
      GetDopagakiSelectionColors(DopagakiRank::kSSS, 120);
  EXPECT_GT(red.left.r, red.left.g);
  EXPECT_GT(green.left.g, green.left.r);
}

}  // namespace
}  // namespace win32
}  // namespace renderer
}  // namespace mozc
