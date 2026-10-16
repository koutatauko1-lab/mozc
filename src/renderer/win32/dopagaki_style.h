// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#ifndef MOZC_RENDERER_WIN32_DOPAGAKI_STYLE_H_
#define MOZC_RENDERER_WIN32_DOPAGAKI_STYLE_H_

#include "protocol/renderer_style.pb.h"
#include "renderer/win32/dopagaki_effects.h"
#include "renderer/win32/dopagaki_state.h"

namespace mozc {
namespace renderer {
namespace win32 {

// The colors of the Dopagaki candidate window (section 2.4), applied on top
// of renderer_style_dark.textproto.
void ApplyDopagakiStyle(RendererStyle* style);

// The horizontal gradient of the selected row: the rank color on the left and
// 40% of it mixed with #7B2CFF on the right. Both are darkened to 70% so that
// the light text stays readable.
struct DopagakiSelectionColors {
  DopagakiColor left;
  DopagakiColor right;
};
DopagakiSelectionColors GetDopagakiSelectionColors(DopagakiRank rank,
                                                   double hue);

}  // namespace win32
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_WIN32_DOPAGAKI_STYLE_H_
