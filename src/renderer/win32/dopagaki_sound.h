// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#ifndef MOZC_RENDERER_WIN32_DOPAGAKI_SOUND_H_
#define MOZC_RENDERER_WIN32_DOPAGAKI_SOUND_H_

#include <memory>
#include <random>

#include "renderer/win32/dopagaki_sound_synth.h"
#include "renderer/win32/dopagaki_state.h"

namespace mozc {
namespace renderer {
namespace win32 {

// Plays the sound effects with XAudio2. Several sounds can play at the same
// time so that fast typing does not cut them off. The volume follows the
// Windows volume. If audio is not available, only the sound stops working.
class DopagakiSound {
 public:
  DopagakiSound();
  DopagakiSound(const DopagakiSound&) = delete;
  DopagakiSound& operator=(const DopagakiSound&) = delete;
  ~DopagakiSound();

  // Must be called on the UI thread of the renderer.
  void Play(DopagakiSoundSet sound_set, DopagakiSoundKind kind,
            DopagakiRank rank);
  void Shutdown();

 private:
  class Engine;

  bool EnsureEngine();

  std::unique_ptr<Engine> engine_;
  int failure_count_ = 0;
  bool disabled_ = false;
  std::mt19937 random_engine_;
  std::uniform_real_distribution<double> distribution_;
};

}  // namespace win32
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_WIN32_DOPAGAKI_SOUND_H_
