// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#include "renderer/win32/dopagaki_praise.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <random>
#include <set>
#include <string_view>
#include <vector>

#include "renderer/win32/dopagaki_state.h"
#include "testing/gunit.h"

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

using Style = DopagakiPraiseStyle;

// A reproducible random function.
DopagakiRandomFunction Seeded(uint32_t seed) {
  auto engine = std::make_shared<std::mt19937>(seed);
  return [engine] {
    return std::uniform_real_distribution<double>(0.0, 1.0)(*engine);
  };
}

constexpr DopagakiRank kTiers[] = {DopagakiRank::kB, DopagakiRank::kA,
                                   DopagakiRank::kS, DopagakiRank::kSS,
                                   DopagakiRank::kSSS};

TEST(DopagakiPraiseTest, Words) {
  const size_t expected[] = {8, 10, 10, 12, 14};
  size_t total = 0;
  for (size_t i = 0; i < std::size(kTiers); ++i) {
    const auto words = GetDopagakiPraiseWords(kTiers[i]);
    EXPECT_EQ(words.size(), expected[i]);
    total += words.size();
  }
  EXPECT_EQ(total, 54u);
  EXPECT_EQ(GetDopagakiMilestoneWords().size(), 8u);

  std::vector<std::wstring_view> all;
  for (const DopagakiRank tier : kTiers) {
    for (const std::wstring_view word : GetDopagakiPraiseWords(tier)) {
      all.push_back(word);
    }
  }
  for (const std::wstring_view word : GetDopagakiMilestoneWords()) {
    all.push_back(word);
  }
  for (const std::wstring_view word : all) {
    EXPECT_FALSE(word.empty());
    for (const wchar_t c : word) {
      EXPECT_GE(c, 0x20);
      EXPECT_LT(c, 0x7F);
    }
  }
  EXPECT_TRUE(GetDopagakiPraiseWords(DopagakiRank::kSSS)[10] ==
              L"OUT OF THIS WORLD!!");
}

TEST(DopagakiPraiseTest, BagShowsEveryWordOnce) {
  const DopagakiRandomFunction random = Seeded(1);
  DopagakiWordBag bag(10);
  for (int round = 0; round < 20; ++round) {
    std::set<size_t> seen;
    for (int i = 0; i < 10; ++i) {
      const size_t index = bag.Next(random);
      EXPECT_LT(index, 10u);
      seen.insert(index);
    }
    EXPECT_EQ(seen.size(), 10u);
  }
}

TEST(DopagakiPraiseTest, BagNeverRepeatsAcrossRefills) {
  const DopagakiRandomFunction random = Seeded(2);
  DopagakiWordBag bag(8);
  size_t last = bag.Next(random);
  for (int i = 0; i < 1000; ++i) {
    const size_t index = bag.Next(random);
    EXPECT_NE(index, last);
    last = index;
  }
}

TEST(DopagakiPraiseTest, StyleIsNeverOneOfTheLastTwo) {
  DopagakiPraisePicker picker(Seeded(3));
  std::vector<Style> history;
  std::set<Style> seen;
  for (int i = 0; i < 1000; ++i) {
    const Style style = picker.NextStyle();
    EXPECT_NE(style, Style::kFade);
    if (history.size() >= 1) {
      EXPECT_NE(style, history[history.size() - 1]);
    }
    if (history.size() >= 2) {
      EXPECT_NE(style, history[history.size() - 2]);
    }
    history.push_back(style);
    seen.insert(style);
  }
  EXPECT_EQ(seen.size(), kDopagakiPraiseStyleCount);
}

TEST(DopagakiPraiseTest, Colors) {
  {
    DopagakiPraisePicker picker(Seeded(4));
    for (int i = 0; i < 1000; ++i) {
      const DopagakiPraiseColor color = picker.NextColor(DopagakiRank::kB);
      EXPECT_NE(color, DopagakiPraiseColor::kGold);
      EXPECT_NE(color, DopagakiPraiseColor::kRainbow);
    }
  }
  {
    DopagakiPraisePicker picker(Seeded(5));
    std::set<DopagakiPraiseColor> seen;
    for (int i = 0; i < 1000; ++i) {
      seen.insert(picker.NextColor(DopagakiRank::kSSS));
    }
    EXPECT_EQ(seen.size(), kDopagakiPraiseColorCount);
  }
  {
    // Without rolling again, the same color would follow with probability
    // 0.235 for S (the sum of the squared weights).
    DopagakiPraisePicker picker(Seeded(6));
    DopagakiPraiseColor last = picker.NextColor(DopagakiRank::kS);
    int same = 0;
    constexpr int kCount = 10000;
    for (int i = 0; i < kCount; ++i) {
      const DopagakiPraiseColor color = picker.NextColor(DopagakiRank::kS);
      if (color == last) {
        ++same;
      }
      last = color;
    }
    EXPECT_LT(same, kCount * 0.15);
  }
}

TEST(DopagakiPraiseTest, BackEasingOvershoots) {
  double max = 0;
  for (int i = 0; i <= 100; ++i) {
    max = std::max(max, DopagakiEaseValue(DopagakiEase::kBack, i / 100.0));
  }
  EXPECT_GT(max, 1.05);
  EXPECT_DOUBLE_EQ(DopagakiEaseValue(DopagakiEase::kBack, 1), 1);
  EXPECT_DOUBLE_EQ(DopagakiEaseValue(DopagakiEase::kIn, 0), 0);
}

TEST(DopagakiPraiseTest, Keyframes) {
  const DopagakiPraiseFrame pop = GetDopagakiPraiseFrame(Style::kPop, 0.14, 1);
  EXPECT_NEAR(pop.scale_x, 1.25f, 1e-5);
  EXPECT_NEAR(pop.scale_y, 1.25f, 1e-5);
  EXPECT_NEAR(pop.opacity, 1, 1e-5);
  const DopagakiPraiseFrame pop_start =
      GetDopagakiPraiseFrame(Style::kPop, 0, 1);
  EXPECT_NEAR(pop_start.opacity, 0, 1e-5);
  EXPECT_NEAR(pop_start.scale_x, 0.2f, 1e-5);

  const DopagakiPraiseFrame slam = GetDopagakiPraiseFrame(Style::kSlam, 0.1, 1);
  EXPECT_NEAR(slam.scale_x, 0.9f, 1e-5);
  EXPECT_NEAR(slam.offset_y, 0, 1e-5);

  const DopagakiPraiseFrame drop =
      GetDopagakiPraiseFrame(Style::kDrop, 0.18, 1);
  EXPECT_NEAR(drop.offset_y, 0, 1e-5);
  EXPECT_NEAR(drop.scale_x, 1.25f, 1e-5);
  EXPECT_NEAR(drop.scale_y, 0.75f, 1e-5);

  const DopagakiPraiseFrame flip = GetDopagakiPraiseFrame(Style::kFlip, 0.1, 1);
  EXPECT_NEAR(flip.scale_x, -0.85f, 1e-5);
  EXPECT_NEAR(flip.scale_y, 1, 1e-5);

  EXPECT_NEAR(GetDopagakiPraiseFrame(Style::kSlide, 0, 1).offset_x, 420, 1e-4);
  EXPECT_NEAR(GetDopagakiPraiseFrame(Style::kSlide, 0, -1).offset_x, -420,
              1e-4);
  EXPECT_NEAR(GetDopagakiPraiseFrame(Style::kSlide, 0, -1).skew_x, -20, 1e-4);
  // Only slide is mirrored.
  EXPECT_NEAR(GetDopagakiPraiseFrame(Style::kSlam, 0, -1).offset_y, -40, 1e-4);

  for (size_t i = 0; i <= kDopagakiPraiseStyleCount; ++i) {
    const Style style = static_cast<Style>(i);
    if (GetDopagakiPraiseStyleInfo(style).letters !=
        DopagakiPraiseLetters::kNone) {
      continue;
    }
    EXPECT_NEAR(GetDopagakiPraiseFrame(style, 1, 1).opacity, 0, 1e-5)
        << GetDopagakiPraiseStyleInfo(style).id;
  }
}

TEST(DopagakiPraiseTest, StyleInfo) {
  for (size_t i = 0; i <= kDopagakiPraiseStyleCount; ++i) {
    const Style style = static_cast<Style>(i);
    const DopagakiPraiseStyleInfo& info = GetDopagakiPraiseStyleInfo(style);
    EXPECT_EQ(info.style, style);
    EXPECT_NE(info.font.families[0], nullptr);
  }
  EXPECT_STREQ(GetDopagakiPraiseStyleInfo(Style::kPop).id, "pop");
  EXPECT_STREQ(GetDopagakiPraiseStyleInfo(Style::kStamp).id, "stamp");
  EXPECT_EQ(GetDopagakiPraiseStyleInfo(Style::kStamp).tilt_base, -12);
  EXPECT_EQ(GetDopagakiPraiseStyleInfo(Style::kSlam).effect,
            DopagakiPraiseEffect::kImpact);
}

TEST(DopagakiPraiseTest, LetterFrames) {
  const DopagakiPraiseLetterRandom random =
      MakeDopagakiPraiseLetterRandom(Seeded(7));
  const DopagakiPraiseLetterFrame wave_start =
      GetDopagakiPraiseLetterFrame(Style::kWave, 0, 5, 0, 1500, random);
  EXPECT_NEAR(wave_start.opacity, 0, 1e-5);
  const DopagakiPraiseLetterFrame wave_in =
      GetDopagakiPraiseLetterFrame(Style::kWave, 0, 5, 220, 1500, random);
  EXPECT_NEAR(wave_in.opacity, 1, 1e-5);

  for (size_t i = 0; i < 5; ++i) {
    const double settled = 380 + 15.0 * i;
    const DopagakiPraiseLetterFrame scatter = GetDopagakiPraiseLetterFrame(
        Style::kScatter, i, 5, settled + 10, 1500, random);
    EXPECT_NEAR(scatter.dx, 0, 1e-3);
    EXPECT_NEAR(scatter.dy, 0, 1e-3);
    EXPECT_NEAR(scatter.rotation, 0, 1e-3);
    EXPECT_NEAR(scatter.opacity, 1, 1e-5);
  }
  // Flying away at the end.
  const DopagakiPraiseLetterFrame end =
      GetDopagakiPraiseLetterFrame(Style::kScatter, 0, 5, 1500, 1500, random);
  EXPECT_NEAR(end.opacity, 0, 1e-5);
}

TEST(DopagakiPraiseTest, Duration) {
  EXPECT_DOUBLE_EQ(GetDopagakiPraiseDurationMs(Style::kWave, 19), 1945);
  EXPECT_DOUBLE_EQ(GetDopagakiPraiseDurationMs(Style::kWave, 5), 1500);
  EXPECT_DOUBLE_EQ(GetDopagakiPraiseDurationMs(Style::kPop, 19), 1300);
  EXPECT_DOUBLE_EQ(GetDopagakiPraiseDurationMs(Style::kDrop, 5), 1400);
}

TEST(DopagakiPraiseTest, Sizes) {
  EXPECT_NEAR(GetDopagakiPraiseFontSize(DopagakiRank::kB, 1920), 67.2f, 1e-3);
  EXPECT_NEAR(GetDopagakiPraiseFontSize(DopagakiRank::kSSS, 1920), 120, 1e-3);
  // k is at most 1.2 and at least 0.5.
  EXPECT_NEAR(GetDopagakiPraiseFontSize(DopagakiRank::kB, 3840), 67.2f, 1e-3);
  EXPECT_NEAR(GetDopagakiPraiseFontSize(DopagakiRank::kB, 400), 28, 1e-3);
  const std::array<float, 2> glow =
      GetDopagakiPraiseGlowRadii(DopagakiRank::kS, 1600);
  EXPECT_NEAR(glow[0], 14, 1e-4);
  EXPECT_NEAR(glow[1], 36, 1e-4);
  const std::array<float, 2> box = GetDopagakiPraiseBoxSize(100, 40, 0);
  EXPECT_NEAR(box[0], 135, 1e-3);
  EXPECT_NEAR(box[1], 66, 1e-3);
}

TEST(DopagakiPraiseTest, PositionKeepsAway) {
  const DopagakiRandomFunction random = Seeded(8);
  // The left part is taken.
  const DopagakiBox avoid[] = {{0, 0, 600, 1080}};
  for (int i = 0; i < 200; ++i) {
    const DopagakiPraisePosition position =
        PickDopagakiPraisePosition(200, 100, 1920, 1080, avoid, random);
    EXPECT_FLOAT_EQ(GetDopagakiBoxOverlap(position.box, avoid[0]), 0);
    EXPECT_GE(position.box.left, 16);
    EXPECT_GE(position.box.top, 16);
    EXPECT_LE(position.box.right, 1920 - 16);
    EXPECT_LE(position.box.bottom, 1080 - 16);
  }
}

TEST(DopagakiPraiseTest, PositionWhenEverythingIsTaken) {
  const DopagakiRandomFunction random = Seeded(9);
  const DopagakiBox avoid[] = {{0, 0, 1920, 1080}};
  const DopagakiPraisePosition position =
      PickDopagakiPraisePosition(200, 100, 1920, 1080, avoid, random);
  EXPECT_NEAR(position.box.width(), 200, 1e-3);
  EXPECT_NEAR(position.box.height(), 100, 1e-3);
  EXPECT_GE(position.box.left, 0);
  EXPECT_LE(position.box.right, 1920);

  // A box larger than the screen is placed at the margin.
  const DopagakiPraisePosition large =
      PickDopagakiPraisePosition(3000, 100, 1920, 1080, {}, random);
  EXPECT_FLOAT_EQ(large.box.left, 16);
}

}  // namespace
}  // namespace win32
}  // namespace renderer
}  // namespace mozc
