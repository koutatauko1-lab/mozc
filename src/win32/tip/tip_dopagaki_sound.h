// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#ifndef MOZC_WIN32_TIP_TIP_DOPAGAKI_SOUND_H_
#define MOZC_WIN32_TIP_TIP_DOPAGAKI_SOUND_H_

#include "renderer/win32/dopagaki_sound_synth.h"

namespace mozc {
namespace win32 {
namespace tsf {

// Plays the sound effects requested by the renderer, in the process of the
// application being typed in (the renderer's sandbox cannot play sounds).
// One XAudio2 engine per process, created at the first sound. Everything here
// is best effort: if audio is not available, nothing is played.
class TipDopagakiSound {
 public:
  TipDopagakiSound() = delete;
  TipDopagakiSound(const TipDopagakiSound&) = delete;
  TipDopagakiSound& operator=(const TipDopagakiSound&) = delete;

  // Must be called when a text service is activated / deactivated. The
  // engine is shut down when the last one in the process is deactivated so
  // that it never outlives the DLL.
  static void OnActivate();
  static void OnDeactivate();

  // Must be called on a UI thread (not in DllMain).
  static void Play(renderer::win32::DopagakiSoundSet sound_set,
                   renderer::win32::DopagakiSoundKind kind, int rank_index);
};

}  // namespace tsf
}  // namespace win32
}  // namespace mozc

#endif  // MOZC_WIN32_TIP_TIP_DOPAGAKI_SOUND_H_
