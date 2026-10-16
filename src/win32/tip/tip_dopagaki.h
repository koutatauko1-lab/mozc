// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#ifndef MOZC_WIN32_TIP_TIP_DOPAGAKI_H_
#define MOZC_WIN32_TIP_TIP_DOPAGAKI_H_

#include <msctf.h>
#include <windows.h>

#include "win32/tip/tip_text_service.h"

namespace mozc {
namespace win32 {
namespace tsf {

// Counts keystrokes and sends the cumulative counters to the renderer.
// Everything here is best effort: it never blocks, never throws and never
// changes how the key event itself is handled.
class TipDopagaki {
 public:
  TipDopagaki() = delete;
  TipDopagaki(const TipDopagaki&) = delete;
  TipDopagaki& operator=(const TipDopagaki&) = delete;

  // Must be called at the beginning of ITfKeyEventSink::OnTestKeyDown.
  // |context| may be nullptr.
  static void OnTestKeyDown(TipTextService* text_service, ITfContext* context,
                            WPARAM wparam, LPARAM lparam);
};

}  // namespace tsf
}  // namespace win32
}  // namespace mozc

#endif  // MOZC_WIN32_TIP_TIP_DOPAGAKI_H_
