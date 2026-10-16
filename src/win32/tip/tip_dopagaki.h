// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#ifndef MOZC_WIN32_TIP_TIP_DOPAGAKI_H_
#define MOZC_WIN32_TIP_TIP_DOPAGAKI_H_

#include <msctf.h>
#include <windows.h>

#include "protocol/config.pb.h"
#include "win32/tip/tip_text_service.h"

namespace mozc {
namespace win32 {
namespace tsf {

// Counts keystrokes and sends the cumulative counters to the renderer.
// Everything here is best effort: it never blocks, never throws and never
// changes how the key event itself is handled.
//
// Depending on the application, TSF calls OnTestKeyDown and OnKeyDown, or
// only OnKeyDown, for one keystroke. Both are hooked and a keystroke seen by
// both is counted only once.
class TipDopagaki {
 public:
  TipDopagaki() = delete;
  TipDopagaki(const TipDopagaki&) = delete;
  TipDopagaki& operator=(const TipDopagaki&) = delete;

  // Must be called at the beginning of ITfKeyEventSink::OnTestKeyDown.
  // |context| may be nullptr.
  static void OnTestKeyDown(TipTextService* text_service, ITfContext* context,
                            WPARAM wparam, LPARAM lparam);
  // Must be called at the beginning of ITfKeyEventSink::OnKeyDown.
  // |context| may be nullptr.
  static void OnKeyDown(TipTextService* text_service, ITfContext* context,
                        WPARAM wparam, LPARAM lparam);

  // Must be called when the renderer callback window receives the renderer
  // callback message. Returns true if it was a Dopagaki request (a sound),
  // which must not be passed to the existing handler.
  static bool OnRendererCallback(WPARAM wparam, LPARAM lparam);

  // Sends the settings chosen from the menu to the renderer so that they take
  // effect at once (phase 6).
  static void SendSettings(const config::Config::DopagakiConfig& settings);
};

}  // namespace tsf
}  // namespace win32
}  // namespace mozc

#endif  // MOZC_WIN32_TIP_TIP_DOPAGAKI_H_
