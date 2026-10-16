// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#include "renderer/win32/dopagaki_sound_synth.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "protocol/config.pb.h"
#include "testing/gunit.h"

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

using DopagakiConfig = config::Config::DopagakiConfig;

// A deterministic random number generator for tests.
std::function<double()> TestRandom() {
  uint32_t state = 12345;
  return [state]() mutable {
    state = state * 1664525u + 1013904223u;
    return (state >> 8) / 16777216.0;
  };
}

float MaxAbs(const std::vector<float>& samples, size_t from = 0,
             size_t to = SIZE_MAX) {
  float result = 0;
  for (size_t i = from; i < std::min(to, samples.size()); ++i) {
    result = std::max(result, std::abs(samples[i]));
  }
  return result;
}

TEST(DopagakiSoundSynthTest, ToneLengthAndDecay) {
  std::vector<float> samples;
  AddDopagakiTone(DopagakiWaveform::kSquare, 660, 880, 0.06, 0.06, 0,
                  &samples);
  // The duration plus the 20 ms tail.
  EXPECT_EQ(samples.size(), 3840);
  const float peak = MaxAbs(samples, 0, 100);
  EXPECT_GT(peak, 0.05f);
  EXPECT_LT(peak, 0.07f);
  EXPECT_LT(MaxAbs(samples, 3600), 0.001f);
}

TEST(DopagakiSoundSynthTest, ToneFrequency) {
  std::vector<float> samples;
  AddDopagakiTone(DopagakiWaveform::kSine, 1000, 1000, 0.1, 0.5, 0,
                  &samples);
  // 1000 Hz for 0.1 seconds: about 200 sign changes.
  int sign_changes = 0;
  for (size_t i = 1; i < 4800; ++i) {
    if ((samples[i - 1] < 0) != (samples[i] < 0)) {
      ++sign_changes;
    }
  }
  EXPECT_NEAR(sign_changes, 200, 3);
}

TEST(DopagakiSoundSynthTest, ToneDelay) {
  std::vector<float> samples;
  AddDopagakiTone(DopagakiWaveform::kSine, 440, 440, 0.1, 0.5, 0.1, &samples);
  EXPECT_EQ(samples.size(), 4800 + 5760);
  EXPECT_FLOAT_EQ(MaxAbs(samples, 0, 4800), 0);
  EXPECT_GT(MaxAbs(samples, 4800), 0.1f);
}

TEST(DopagakiSoundSynthTest, NoiseIsHighPassed) {
  std::vector<float> samples;
  AddDopagakiNoise(0.03, 0.35, 2500, 0, TestRandom(), &samples);
  EXPECT_EQ(samples.size(), 1440);
  double sum = 0;
  for (const float sample : samples) {
    sum += sample;
  }
  // No DC after the high-pass filter.
  EXPECT_LT(std::abs(sum / samples.size()), 0.01);
  EXPECT_GT(MaxAbs(samples), 0.05f);
}

TEST(DopagakiSoundSynthTest, PicoRankUpIsFourNotes) {
  const std::vector<float> samples = SynthesizeDopagakiSound(
      DopagakiConfig::PICO, DopagakiSoundKind::kRankUp, 2, TestRandom());
  // The last note starts at 0.21 s and lasts 0.09 + 0.02 s.
  EXPECT_EQ(samples.size(), 10080 + 5280);
}

TEST(DopagakiSoundSynthTest, MuteIsSilent) {
  for (const DopagakiSoundKind kind :
       {DopagakiSoundKind::kKey, DopagakiSoundKind::kRankUp,
        DopagakiSoundKind::kBreak}) {
    EXPECT_TRUE(SynthesizeDopagakiSound(DopagakiConfig::MUTE, kind, 4,
                                        TestRandom())
                    .empty());
  }
}

TEST(DopagakiSoundSynthTest, SoundRequestRoundTrip) {
  const uint32_t value = EncodeDopagakiSoundRequest(
      DopagakiConfig::TAIKO, DopagakiSoundKind::kBreak, 3);
  DopagakiConfig::SoundSet sound_set = DopagakiConfig::PICO;
  DopagakiSoundKind kind = DopagakiSoundKind::kKey;
  int rank = 0;
  ASSERT_TRUE(DecodeDopagakiSoundRequest(value, &sound_set, &kind, &rank));
  EXPECT_EQ(sound_set, DopagakiConfig::TAIKO);
  EXPECT_EQ(kind, DopagakiSoundKind::kBreak);
  EXPECT_EQ(rank, 3);
}

TEST(DopagakiSoundSynthTest, InvalidSoundRequests) {
  DopagakiConfig::SoundSet sound_set = DopagakiConfig::PICO;
  DopagakiSoundKind kind = DopagakiSoundKind::kKey;
  int rank = 0;
  // Unknown sound set, kind and rank.
  EXPECT_FALSE(DecodeDopagakiSoundRequest(99 << 16, &sound_set, &kind, &rank));
  EXPECT_FALSE(DecodeDopagakiSoundRequest(7 << 8, &sound_set, &kind, &rank));
  EXPECT_FALSE(DecodeDopagakiSoundRequest(5, &sound_set, &kind, &rank));
  EXPECT_FALSE(
      DecodeDopagakiSoundRequest(1u << 24, &sound_set, &kind, &rank));
}

TEST(DopagakiSoundSynthTest, MechaKeyIsTwiceThePreview) {
  // Kouta asked for a louder key sound of Mecha (0.08 instead of 0.04).
  const std::vector<float> samples = SynthesizeDopagakiSound(
      DopagakiConfig::MECHA, DopagakiSoundKind::kKey, 0, TestRandom());
  // The first samples of the sawtooth are about -0.94.
  const float peak = MaxAbs(samples);
  EXPECT_GT(peak, 0.06f);
  EXPECT_LT(peak, 0.09f);
}

TEST(DopagakiSoundSynthTest, AllSoundsAreAudibleAndSane) {
  for (const DopagakiConfig::SoundSet sound_set :
       {DopagakiConfig::PICO, DopagakiConfig::TYPEWRITER, DopagakiConfig::HIT,
        DopagakiConfig::TAIKO, DopagakiConfig::SPARKLE,
        DopagakiConfig::MECHA}) {
    for (const DopagakiSoundKind kind :
         {DopagakiSoundKind::kKey, DopagakiSoundKind::kRankUp,
          DopagakiSoundKind::kBreak}) {
      for (int rank = 0; rank <= 4; ++rank) {
        const std::vector<float> samples =
            SynthesizeDopagakiSound(sound_set, kind, rank, TestRandom());
        SCOPED_TRACE(testing::Message()
                     << "set=" << sound_set << " kind="
                     << static_cast<int>(kind) << " rank=" << rank);
        ASSERT_FALSE(samples.empty());
        // At most about one second.
        EXPECT_LE(samples.size(), kDopagakiSampleRate);
        const float peak = MaxAbs(samples);
        EXPECT_GT(peak, 0.01f);
        EXPECT_LT(peak, 1.5f);
        for (const float sample : samples) {
          ASSERT_TRUE(std::isfinite(sample));
        }
      }
    }
  }
}

}  // namespace
}  // namespace win32
}  // namespace renderer
}  // namespace mozc
