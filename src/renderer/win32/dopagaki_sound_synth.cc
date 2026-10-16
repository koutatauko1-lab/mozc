// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#include "renderer/win32/dopagaki_sound_synth.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <vector>

#include "protocol/config.pb.h"

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

using DopagakiConfig = config::Config::DopagakiConfig;

constexpr double kPi = 3.14159265358979323846;
// o.stop(t + dur + .02) in the preview.
constexpr double kToneTail = 0.02;
// exponentialRampToValueAtTime(0.0001, ...) of the gain.
constexpr double kEndGain = 0.0001;

void EnsureSize(size_t size, std::vector<float>* samples) {
  if (samples->size() < size) {
    samples->resize(size, 0.0f);
  }
}

// Removes the aliasing at a jump of a square or sawtooth wave.
double PolyBlep(double t, double dt) {
  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1;
  }
  if (t > 1 - dt) {
    t = (t - 1) / dt;
    return t * t + t + t + 1;
  }
  return 0;
}

double Oscillate(DopagakiWaveform waveform, double phase, double dt) {
  switch (waveform) {
    case DopagakiWaveform::kSine:
      return std::sin(2 * kPi * phase);
    case DopagakiWaveform::kSquare: {
      double value = phase < 0.5 ? 1.0 : -1.0;
      value += PolyBlep(phase, dt);
      value -= PolyBlep(std::fmod(phase + 0.5, 1.0), dt);
      return value;
    }
    case DopagakiWaveform::kSawtooth:
      return 2 * phase - 1 - PolyBlep(phase, dt);
    case DopagakiWaveform::kTriangle:
      return 1 - 4 * std::abs(phase - 0.5);
  }
  return 0;
}

}  // namespace

// tone() in the preview: the frequency goes from |f0| to |f1| and the gain
// from |volume| to 0.0001, both exponentially.
void AddDopagakiTone(DopagakiWaveform waveform, double f0, double f1,
                     double duration, double volume, double delay,
                     std::vector<float>* samples) {
  if (duration <= 0 || volume <= 0 || f0 <= 0) {
    return;
  }
  constexpr double kRate = kDopagakiSampleRate;
  const size_t start = static_cast<size_t>(std::lround(delay * kRate));
  const size_t length =
      static_cast<size_t>(std::lround((duration + kToneTail) * kRate));
  EnsureSize(start + length, samples);
  const double frequency_ratio = std::max(f1, 1.0) / f0;
  const double gain_ratio = kEndGain / volume;
  double phase = 0;
  for (size_t i = 0; i < length; ++i) {
    const double progress = std::min(i / kRate / duration, 1.0);
    const double frequency = f0 * std::pow(frequency_ratio, progress);
    const double gain = volume * std::pow(gain_ratio, progress);
    const double dt = frequency / kRate;
    (*samples)[start + i] +=
        static_cast<float>(gain * Oscillate(waveform, phase, dt));
    phase += dt;
    phase -= std::floor(phase);
  }
}

// noise() in the preview: white noise fading out linearly, through a
// high-pass BiquadFilter (Q = 1 dB, the default of Web Audio).
void AddDopagakiNoise(double duration, double volume, double highpass,
                      double delay, const std::function<double()>& random,
                      std::vector<float>* samples) {
  if (duration <= 0 || volume <= 0) {
    return;
  }
  constexpr double kRate = kDopagakiSampleRate;
  const size_t start = static_cast<size_t>(std::lround(delay * kRate));
  const size_t length = static_cast<size_t>(std::floor(kRate * duration));
  EnsureSize(start + length, samples);

  const double w0 = 2 * kPi * highpass / kRate;
  const double cos_w0 = std::cos(w0);
  const double alpha = std::sin(w0) / (2 * std::pow(10.0, 1.0 / 20));
  const double a0 = 1 + alpha;
  const double b0 = (1 + cos_w0) / 2 / a0;
  const double b1 = -(1 + cos_w0) / a0;
  const double b2 = (1 + cos_w0) / 2 / a0;
  const double a1 = -2 * cos_w0 / a0;
  const double a2 = (1 - alpha) / a0;
  double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  for (size_t i = 0; i < length; ++i) {
    const double x =
        (random() * 2 - 1) * (1 - static_cast<double>(i) / length);
    const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    (*samples)[start + i] += static_cast<float>(volume * y);
  }
}

uint32_t EncodeDopagakiSoundRequest(DopagakiSoundSet sound_set,
                                    DopagakiSoundKind kind, int rank_index) {
  return (static_cast<uint32_t>(sound_set) & 0xFF) << 16 |
         (static_cast<uint32_t>(kind) & 0xFF) << 8 |
         (static_cast<uint32_t>(rank_index) & 0xFF);
}

bool DecodeDopagakiSoundRequest(uint32_t value, DopagakiSoundSet* sound_set,
                                DopagakiSoundKind* kind, int* rank_index) {
  const int set_value = static_cast<int>((value >> 16) & 0xFF);
  const int kind_value = static_cast<int>((value >> 8) & 0xFF);
  const int rank_value = static_cast<int>(value & 0xFF);
  if ((value >> 24) != 0 || !DopagakiConfig::SoundSet_IsValid(set_value) ||
      kind_value > static_cast<int>(DopagakiSoundKind::kBreak) ||
      rank_value > 4) {
    return false;
  }
  *sound_set = static_cast<DopagakiSoundSet>(set_value);
  *kind = static_cast<DopagakiSoundKind>(kind_value);
  *rank_index = rank_value;
  return true;
}

// SOUNDS in the preview. |r| is the rank index.
std::vector<float> SynthesizeDopagakiSound(
    DopagakiSoundSet sound_set, DopagakiSoundKind kind, int rank_index,
    const std::function<double()>& random) {
  constexpr DopagakiWaveform kSine = DopagakiWaveform::kSine;
  constexpr DopagakiWaveform kSquare = DopagakiWaveform::kSquare;
  constexpr DopagakiWaveform kSawtooth = DopagakiWaveform::kSawtooth;
  constexpr DopagakiWaveform kTriangle = DopagakiWaveform::kTriangle;
  const double r = rank_index;
  std::vector<float> s;
  const auto tone = [&s](DopagakiWaveform waveform, double f0, double f1,
                         double duration, double volume, double delay = 0) {
    AddDopagakiTone(waveform, f0, f1, duration, volume, delay, &s);
  };
  const auto noise = [&s, &random](double duration, double volume,
                                   double highpass, double delay = 0) {
    AddDopagakiNoise(duration, volume, highpass, delay, random, &s);
  };

  switch (sound_set) {
    case DopagakiConfig::PICO:
      switch (kind) {
        case DopagakiSoundKind::kKey:
          tone(kSquare, 660 + r * 120 + random() * 80, 880 + r * 160, 0.06,
               0.06);
          break;
        case DopagakiSoundKind::kRankUp: {
          const double notes[] = {523, 659, 784, 1046};
          for (int i = 0; i < 4; ++i) {
            tone(kSquare, notes[i], notes[i], 0.09, 0.07, i * 0.07);
          }
          break;
        }
        case DopagakiSoundKind::kBreak:
          tone(kSquare, 440, 55, 0.5, 0.09);
          break;
      }
      break;
    case DopagakiConfig::TYPEWRITER:
      switch (kind) {
        case DopagakiSoundKind::kKey:
          noise(0.03, 0.35, 2500);
          break;
        case DopagakiSoundKind::kRankUp:
          tone(kSine, 1760, 1760, 0.6, 0.12);
          tone(kSine, 2637, 2637, 0.5, 0.05);
          break;
        case DopagakiSoundKind::kBreak:
          noise(0.25, 0.5, 300);
          tone(kTriangle, 180, 60, 0.35, 0.12);
          break;
      }
      break;
    case DopagakiConfig::HIT:
      switch (kind) {
        case DopagakiSoundKind::kKey:
          tone(kSine, 160 + r * 20, 50, 0.09, 0.25);
          noise(0.04, 0.18, 1800);
          break;
        case DopagakiSoundKind::kRankUp:
          tone(kSine, 90, 40, 0.4, 0.35);
          noise(0.3, 0.3, 600);
          break;
        case DopagakiSoundKind::kBreak:
          noise(0.5, 0.55, 200);
          tone(kSawtooth, 220, 40, 0.6, 0.12);
          break;
      }
      break;
    case DopagakiConfig::TAIKO:
      switch (kind) {
        case DopagakiSoundKind::kKey:
          tone(kSine, 140 + r * 15, 70, 0.16, 0.35);
          break;
        case DopagakiSoundKind::kRankUp:
          for (const double delay : {0.0, 0.14, 0.28}) {
            tone(kSine, 110, 55, 0.3, 0.45, delay);
          }
          break;
        case DopagakiSoundKind::kBreak:
          tone(kSine, 90, 35, 0.9, 0.5);
          noise(0.15, 0.2, 900, 0.02);
          break;
      }
      break;
    case DopagakiConfig::SPARKLE:
      switch (kind) {
        case DopagakiSoundKind::kKey:
          tone(kSine, 1500 + random() * 900 + r * 200, 2400 + r * 200, 0.09,
               0.05);
          break;
        case DopagakiSoundKind::kRankUp: {
          const double notes[] = {1046, 1318, 1568, 2093, 2637};
          for (int i = 0; i < 5; ++i) {
            tone(kSine, notes[i], notes[i], 0.25, 0.06, i * 0.05);
          }
          break;
        }
        case DopagakiSoundKind::kBreak: {
          const double notes[] = {1568, 1174, 880, 587};
          for (int i = 0; i < 4; ++i) {
            tone(kTriangle, notes[i], notes[i] * 0.7, 0.2, 0.07, i * 0.08);
          }
          break;
        }
      }
      break;
    case DopagakiConfig::MECHA:
      switch (kind) {
        case DopagakiSoundKind::kKey:
          tone(kSawtooth, 1400 + r * 200, 300, 0.07, 0.04);
          break;
        case DopagakiSoundKind::kRankUp:
          tone(kSawtooth, 200, 2400, 0.35, 0.06);
          tone(kSquare, 300, 3000, 0.35, 0.03);
          break;
        case DopagakiSoundKind::kBreak:
          tone(kSawtooth, 1800, 40, 0.6, 0.08);
          noise(0.4, 0.3, 500);
          break;
      }
      break;
    case DopagakiConfig::MUTE:
    default:
      break;
  }
  return s;
}

}  // namespace win32
}  // namespace renderer
}  // namespace mozc
