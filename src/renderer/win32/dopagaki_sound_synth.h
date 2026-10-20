// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#ifndef MOZC_RENDERER_WIN32_DOPAGAKI_SOUND_SYNTH_H_
#define MOZC_RENDERER_WIN32_DOPAGAKI_SOUND_SYNTH_H_

#include <cstdint>
#include <functional>
#include <vector>

#include "protocol/config.pb.h"

namespace mozc {
namespace renderer {
namespace win32 {

// Synthesizes the sound effects of SOUNDS in docs/dopagaki/preview.html
// (section 2.5) as mono float samples. No Windows API is used.

using DopagakiSoundSet = config::Config::DopagakiConfig::SoundSet;

// One sound per event, in this priority (update 1, 1.11): kBreak >
// kMilestone > kRankUpPraise > kRankUp > kPraise > kKey. The values are sent
// to the TIP and must not change.
enum class DopagakiSoundKind {
  kKey = 0,     // Every keystroke.
  kRankUp = 1,  // A keystroke that raised the rank (instead of kKey).
  kBreak = 2,   // BackSpace.
  // Update 1.
  kPraise = 3,        // A praise word (a multiple of 5).
  kRankUpPraise = 4,  // A rank-up with a praise word.
  kMilestone = 5,     // 200, 300, ...
};

inline constexpr int kDopagakiSampleRate = 48000;

// tone() and noise() of the preview, mixed into |samples| (which grows as
// needed). Times are in seconds.
enum class DopagakiWaveform { kSine, kSquare, kSawtooth, kTriangle };
void AddDopagakiTone(DopagakiWaveform waveform, double f0, double f1,
                     double duration, double volume, double delay,
                     std::vector<float>* samples);
void AddDopagakiNoise(double duration, double volume, double highpass,
                      double delay, const std::function<double()>& random,
                      std::vector<float>* samples);

// Returns an empty vector for MUTE. |rank_index| is 0 (B) to 4 (SSS).
// |random| returns a number in [0, 1).
std::vector<float> SynthesizeDopagakiSound(
    DopagakiSoundSet sound_set, DopagakiSoundKind kind, int rank_index,
    const std::function<double()>& random);

// The renderer runs in a sandbox that cannot play sounds. It asks the TIP of
// the application being typed in to play them, by posting the message of
// kMessageReceiverMessageName (used for the renderer callback) to the
// renderer callback window with WPARAM = kDopagakiSoundRequest and LPARAM =
// EncodeDopagakiSoundRequest(...).
inline constexpr uint32_t kDopagakiSoundRequest = 0x44504741;  // "DPGA"
uint32_t EncodeDopagakiSoundRequest(DopagakiSoundSet sound_set,
                                    DopagakiSoundKind kind, int rank_index);
// Returns false if |value| is not a valid request.
bool DecodeDopagakiSoundRequest(uint32_t value, DopagakiSoundSet* sound_set,
                                DopagakiSoundKind* kind, int* rank_index);

}  // namespace win32
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_WIN32_DOPAGAKI_SOUND_SYNTH_H_
