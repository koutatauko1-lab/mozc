// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#include "renderer/win32/dopagaki_effects.h"

#include <cstddef>
#include <cstdint>
#include <utility>

#include "renderer/win32/dopagaki_state.h"
#include "testing/gunit.h"

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

constexpr double kOneFrameMs = 1000.0 / 60.0;

// Always returns |value| as the random number.
DopagakiEffects::RandomFunction Fixed(double value) {
  return [value] { return value; };
}

TEST(DopagakiEffectsTest, ColorFromRgb) {
  const DopagakiColor color = DopagakiColorFromRgb(0xFF0080);
  EXPECT_FLOAT_EQ(color.r, 1.0f);
  EXPECT_FLOAT_EQ(color.g, 0.0f);
  EXPECT_NEAR(color.b, 0.502f, 0.001f);
}

TEST(DopagakiEffectsTest, ColorFromHsl) {
  const DopagakiColor red = DopagakiColorFromHsl(0, 1, 0.62);
  EXPECT_NEAR(red.r, 1.0f, 1e-5);
  EXPECT_NEAR(red.g, 0.24f, 1e-5);
  EXPECT_NEAR(red.b, 0.24f, 1e-5);
  const DopagakiColor green = DopagakiColorFromHsl(480, 1, 0.5);  // 120
  EXPECT_NEAR(green.r, 0.0f, 1e-5);
  EXPECT_NEAR(green.g, 1.0f, 1e-5);
  EXPECT_NEAR(green.b, 0.0f, 1e-5);
}

TEST(DopagakiEffectsTest, CubicBezier) {
  EXPECT_DOUBLE_EQ(DopagakiCubicBezier(0.2, 0.8, 0.2, 1, 0), 0);
  EXPECT_DOUBLE_EQ(DopagakiCubicBezier(0.2, 0.8, 0.2, 1, 1), 1);
  // A linear curve.
  EXPECT_NEAR(DopagakiCubicBezier(1.0 / 3, 1.0 / 3, 2.0 / 3, 2.0 / 3, 0.3),
              0.3, 1e-6);
  // This curve rises quickly and is monotonic.
  double previous = 0;
  for (int i = 1; i <= 20; ++i) {
    const double value = DopagakiCubicBezier(0.2, 0.8, 0.2, 1, i / 20.0);
    EXPECT_GE(value, previous);
    previous = value;
  }
  EXPECT_GT(DopagakiCubicBezier(0.2, 0.8, 0.2, 1, 0.3), 0.6);
}

TEST(DopagakiEffectsTest, RankUpBannerFrames) {
  const DopagakiBannerFrame start =
      GetDopagakiBannerFrame(DopagakiBannerKind::kRankUp, 0);
  EXPECT_FLOAT_EQ(start.opacity, 0);
  EXPECT_FLOAT_EQ(start.scale, 0.3f);
  const DopagakiBannerFrame end =
      GetDopagakiBannerFrame(DopagakiBannerKind::kRankUp, 1000);
  EXPECT_FLOAT_EQ(end.opacity, 0);
  EXPECT_FLOAT_EQ(end.scale, 1.6f);
  // Fully visible in the middle.
  const DopagakiBannerFrame middle =
      GetDopagakiBannerFrame(DopagakiBannerKind::kRankUp, 150);
  EXPECT_FLOAT_EQ(middle.opacity, 1);
  EXPECT_DOUBLE_EQ(GetDopagakiBannerDurationMs(DopagakiBannerKind::kRankUp),
                   1000);
}

TEST(DopagakiEffectsTest, BreakBannerFrames) {
  const DopagakiBannerFrame start =
      GetDopagakiBannerFrame(DopagakiBannerKind::kBreak, 0);
  EXPECT_FLOAT_EQ(start.opacity, 0);
  EXPECT_FLOAT_EQ(start.scale, 2.4f);
  EXPECT_FLOAT_EQ(start.rotation_degrees, -4);
  const DopagakiBannerFrame end =
      GetDopagakiBannerFrame(DopagakiBannerKind::kBreak, 1100);
  EXPECT_FLOAT_EQ(end.opacity, 0);
  EXPECT_FLOAT_EQ(end.scale, 0.9f);
  EXPECT_FLOAT_EQ(end.rotation_degrees, -6);
  EXPECT_FLOAT_EQ(end.offset_y, 30);
}

TEST(DopagakiEffectsTest, ComboLabelScaleAndOpacity) {
  EXPECT_FLOAT_EQ(GetDopagakiComboLabelScale(0), 1.5f);
  EXPECT_FLOAT_EQ(GetDopagakiComboLabelScale(160), 1.0f);
  EXPECT_FLOAT_EQ(GetDopagakiComboLabelOpacity(0), 1.0f);
  EXPECT_FLOAT_EQ(GetDopagakiComboLabelOpacity(1600), 1.0f);
  EXPECT_NEAR(GetDopagakiComboLabelOpacity(1800), 0.5f, 1e-5);
  EXPECT_FLOAT_EQ(GetDopagakiComboLabelOpacity(2000), 0.0f);
}

TEST(DopagakiEffectsTest, KeyBurstsByRank) {
  // 0.9 never triggers the extra fireworks of SS and SSS.
  for (const auto [rank, count] :
       {std::pair{DopagakiRank::kB, 4}, std::pair{DopagakiRank::kA, 10},
        std::pair{DopagakiRank::kS, 18}, std::pair{DopagakiRank::kSS, 30},
        std::pair{DopagakiRank::kSSS, 50}}) {
    DopagakiEffects effects(Fixed(0.9));
    effects.OnKey(rank, 100, 100);
    EXPECT_EQ(effects.particles().size(), count);
  }
}

TEST(DopagakiEffectsTest, ExtraFireworksAtHighRanks) {
  DopagakiEffects ss(Fixed(0.1));  // 0.1 < 0.2
  ss.OnKey(DopagakiRank::kSS, 100, 100);
  EXPECT_EQ(ss.particles().size(), 60);

  DopagakiEffects sss(Fixed(0.3));  // 0.3 < 0.5
  sss.OnKey(DopagakiRank::kSSS, 100, 100);
  EXPECT_EQ(sss.particles().size(), 100);

  DopagakiEffects s(Fixed(0.0));  // Never for S.
  s.OnKey(DopagakiRank::kS, 100, 100);
  EXPECT_EQ(s.particles().size(), 18);
}

TEST(DopagakiEffectsTest, ShakeOnKey) {
  DopagakiEffects effects(Fixed(0.9));
  effects.OnKey(DopagakiRank::kS, 100, 100);
  EXPECT_FLOAT_EQ(effects.shake_amount(), 3);
  // A lower rank does not reduce the shake.
  effects.OnKey(DopagakiRank::kA, 100, 100);
  EXPECT_FLOAT_EQ(effects.shake_amount(), 3);
}

TEST(DopagakiEffectsTest, RankUp) {
  DopagakiEffects effects(Fixed(0.9));
  effects.OnRankUp(DopagakiRank::kSS);
  ASSERT_TRUE(effects.banner().has_value());
  EXPECT_EQ(effects.banner()->text, L"SS RANK!!");
  EXPECT_EQ(effects.banner()->kind, DopagakiBannerKind::kRankUp);
  EXPECT_EQ(effects.banner()->color, DopagakiColorFromRgb(0xFF4FD8));
  EXPECT_EQ(effects.particles().size(), (2 + 3 * 2) * 30);
  EXPECT_FLOAT_EQ(effects.shake_amount(), 6 * 2.2f + 4);
}

TEST(DopagakiEffectsTest, Break) {
  DopagakiEffects effects(Fixed(0.9));
  effects.OnBreak(100, 100);
  EXPECT_EQ(effects.particles().size(), 140);
  for (const DopagakiParticle& particle : effects.particles()) {
    EXPECT_TRUE(particle.shard);
  }
  EXPECT_FLOAT_EQ(effects.flash_opacity(), 0.75f);
  ASSERT_TRUE(effects.banner().has_value());
  EXPECT_EQ(effects.banner()->text, L"BREAK");
  EXPECT_FLOAT_EQ(effects.shake_amount(), 14);

  effects.Update(DopagakiEffects::kFlashDurationMs);
  EXPECT_FLOAT_EQ(effects.flash_opacity(), 0);
}

TEST(DopagakiEffectsTest, ParticlePhysics) {
  DopagakiEffects effects(Fixed(0.0));
  // With 0: angle 0, speed 4 * 0.4, decay 0.012, size 2.5 * 0.5.
  effects.OnKey(DopagakiRank::kB, 100, 100);
  const DopagakiParticle before = effects.particles().front();
  EXPECT_FLOAT_EQ(before.vx, 1.6f);
  EXPECT_FLOAT_EQ(before.vy, -1.5f);
  effects.Update(kOneFrameMs);
  const DopagakiParticle after = effects.particles().front();
  EXPECT_FLOAT_EQ(after.x, 101.6f);
  EXPECT_FLOAT_EQ(after.y, 98.5f);
  EXPECT_NEAR(after.vy, -1.32f, 1e-5);
  EXPECT_NEAR(after.vx, 1.6f * 0.985f, 1e-5);
  EXPECT_NEAR(after.life, 1 - 0.012f, 1e-5);
}

TEST(DopagakiEffectsTest, EverythingEnds) {
  DopagakiEffects effects(Fixed(0.9));
  effects.SetScreenSize(10000, 10000);
  effects.OnKey(DopagakiRank::kS, 5000, 5000);
  effects.OnRankUp(DopagakiRank::kS);
  effects.OnBreak(5000, 5000);
  effects.ShowComboLabel(42, DopagakiRank::kS, 10, 10);
  EXPECT_TRUE(effects.IsActive());
  for (int i = 0; i < 60 * 5; ++i) {
    effects.Update(kOneFrameMs);
  }
  EXPECT_FALSE(effects.IsActive());
  EXPECT_TRUE(effects.particles().empty());
  EXPECT_FALSE(effects.banner().has_value());
  EXPECT_FALSE(effects.combo_label().has_value());
  EXPECT_FLOAT_EQ(effects.shake_x(), 0);
}

TEST(DopagakiEffectsTest, ReducedMotionDoesNotShake) {
  DopagakiEffects effects(Fixed(0.9));
  effects.SetReducedMotion(true);
  effects.OnBreak(100, 100);
  effects.Update(kOneFrameMs);
  EXPECT_FLOAT_EQ(effects.shake_x(), 0);
  EXPECT_FLOAT_EQ(effects.shake_y(), 0);
}

TEST(DopagakiEffectsTest, ShakeMovesWithinTheAmount) {
  DopagakiEffects effects(Fixed(0.9));
  effects.OnBreak(100, 100);
  effects.Update(kOneFrameMs);
  // (0.9 - 0.5) * 14 * 2
  EXPECT_NEAR(effects.shake_x(), 11.2f, 1e-4);
  EXPECT_NEAR(effects.shake_amount(), 14 * 0.86f, 1e-4);
}

TEST(DopagakiEffectsTest, ComboLabel) {
  DopagakiEffects effects(Fixed(0.9));
  effects.ShowComboLabel(29, DopagakiRank::kA, 10, 20);
  ASSERT_TRUE(effects.combo_label().has_value());
  EXPECT_EQ(effects.combo_label()->text, L"29 COMBO A");
  EXPECT_EQ(effects.combo_label()->color, DopagakiColorFromRgb(0x3BE37A));
  const uint64_t first_id = effects.combo_label()->id;

  effects.Update(500);
  // The same text keeps the id but restarts the timer.
  effects.ShowComboLabel(29, DopagakiRank::kA, 30, 40);
  EXPECT_EQ(effects.combo_label()->id, first_id);
  EXPECT_DOUBLE_EQ(effects.combo_label()->age_ms, 0);
  EXPECT_FLOAT_EQ(effects.combo_label()->x, 30);

  effects.ShowComboLabel(30, DopagakiRank::kS, 30, 40);
  EXPECT_NE(effects.combo_label()->id, first_id);

  effects.HideComboLabel();
  EXPECT_FALSE(effects.combo_label().has_value());
}

TEST(DopagakiEffectsTest, EdgeStyle) {
  EXPECT_FLOAT_EQ(GetDopagakiEdgeStyle(DopagakiRank::kB).width, 0);
  const DopagakiEdgeStyle a = GetDopagakiEdgeStyle(DopagakiRank::kA);
  EXPECT_FLOAT_EQ(a.width, 3);
  EXPECT_FLOAT_EQ(a.blur, 14);
  EXPECT_FLOAT_EQ(a.alpha, 0.45f);
  EXPECT_FLOAT_EQ(a.sparks_per_frame, 0);
  const DopagakiEdgeStyle sss = GetDopagakiEdgeStyle(DopagakiRank::kSSS);
  EXPECT_FLOAT_EQ(sss.width, 13);
  EXPECT_FLOAT_EQ(sss.blur, 42);
  EXPECT_FLOAT_EQ(sss.alpha, 1);
  EXPECT_FLOAT_EQ(sss.sparks_per_frame, 2.2f);
}

TEST(DopagakiEffectsTest, EdgePulse) {
  constexpr double kHalfPi = 1.5707963267948966;
  EXPECT_FLOAT_EQ(GetDopagakiEdgePulse(DopagakiRank::kS, kHalfPi / 2), 1);
  // sin(t * 2) == 1
  EXPECT_NEAR(GetDopagakiEdgePulse(DopagakiRank::kSS, kHalfPi / 2), 1.35f,
              1e-5);
  // sin(t * 3) == 1
  EXPECT_NEAR(GetDopagakiEdgePulse(DopagakiRank::kSSS, kHalfPi / 3), 1.35f,
              1e-5);
}

TEST(DopagakiEffectsTest, EdgeFadesOutWhenIdle) {
  DopagakiEffects effects(Fixed(0.9));
  // Nothing is shown before the first keystroke.
  effects.SetEdgeRank(DopagakiRank::kA);
  EXPECT_FALSE(effects.IsEdgeVisible());

  effects.OnKey(DopagakiRank::kA, 100, 100);
  EXPECT_FLOAT_EQ(effects.edge_opacity(), 1);
  // Particles are gone after one second, but the edge is still visible.
  for (int i = 0; i < 60; ++i) {
    effects.Update(kOneFrameMs);
  }
  EXPECT_TRUE(effects.particles().empty());
  EXPECT_TRUE(effects.IsActive());
  EXPECT_FLOAT_EQ(effects.edge_opacity(), 1);

  effects.Update(DopagakiEffects::kEdgeIdleMs - 1000);  // 3 seconds idle.
  EXPECT_FLOAT_EQ(effects.edge_opacity(), 1);
  effects.Update(500);
  EXPECT_NEAR(effects.edge_opacity(), 0.5f, 1e-5);
  effects.Update(500);
  EXPECT_FLOAT_EQ(effects.edge_opacity(), 0);
  EXPECT_FALSE(effects.IsActive());

  // The next keystroke brings it back at once.
  effects.OnKey(DopagakiRank::kA, 100, 100);
  EXPECT_FLOAT_EQ(effects.edge_opacity(), 1);
  EXPECT_TRUE(effects.IsEdgeVisible());
}

TEST(DopagakiEffectsTest, NoEdgeAtRankB) {
  DopagakiEffects effects(Fixed(0.9));
  effects.SetEdgeRank(DopagakiRank::kB);
  effects.OnKey(DopagakiRank::kB, 100, 100);
  EXPECT_FALSE(effects.IsEdgeVisible());
}

TEST(DopagakiEffectsTest, EdgeTime) {
  DopagakiEffects effects(Fixed(0.9));
  effects.Update(kOneFrameMs);
  effects.Update(kOneFrameMs);
  EXPECT_NEAR(effects.edge_time(), 0.1, 1e-9);
}

TEST(DopagakiEffectsTest, EdgeSparks) {
  // SSS: 2.2 sparks per frame. With 0.9, the 0.2 spark is not spawned.
  DopagakiEffects effects(Fixed(0.9));
  effects.SetScreenSize(1000, 500);
  effects.SetEdgeRank(DopagakiRank::kSSS);
  effects.OnKey(DopagakiRank::kSSS, 100, 100);
  const size_t key_particles = effects.particles().size();
  effects.Update(kOneFrameMs);
  ASSERT_EQ(effects.particles().size(), key_particles + 2);
  // 0.9 of the perimeter is on the left edge, flying to the right.
  const DopagakiParticle& spark = effects.particles().back();
  EXPECT_TRUE(spark.shard);
  EXPECT_FLOAT_EQ(spark.x, 0);
  EXPECT_GT(spark.vx, 0);

  // With 0.1, the 0.2 spark is also spawned.
  DopagakiEffects more(Fixed(0.1));
  more.SetEdgeRank(DopagakiRank::kSSS);
  more.OnKey(DopagakiRank::kSSS, 100, 100);
  const size_t more_key_particles = more.particles().size();
  more.Update(kOneFrameMs);
  EXPECT_EQ(more.particles().size(), more_key_particles + 3);
}

TEST(DopagakiEffectsTest, FewerEdgeSparksWithReducedMotion) {
  // 2.2 * 0.3 = 0.66 sparks per frame.
  DopagakiEffects effects(Fixed(0.9));
  effects.SetReducedMotion(true);
  effects.SetEdgeRank(DopagakiRank::kSSS);
  effects.OnKey(DopagakiRank::kSSS, 100, 100);
  const size_t key_particles = effects.particles().size();
  effects.Update(kOneFrameMs);
  EXPECT_EQ(effects.particles().size(), key_particles);

  DopagakiEffects lucky(Fixed(0.5));
  lucky.SetReducedMotion(true);
  lucky.SetEdgeRank(DopagakiRank::kSSS);
  lucky.OnKey(DopagakiRank::kSSS, 100, 100);
  const size_t lucky_key_particles = lucky.particles().size();
  lucky.Update(kOneFrameMs);
  EXPECT_EQ(lucky.particles().size(), lucky_key_particles + 1);
}

TEST(DopagakiEffectsTest, ParticlesAreLimited) {
  DopagakiEffects effects(Fixed(0.9));
  for (int i = 0; i < 100; ++i) {
    effects.OnBreak(100, 100);
  }
  EXPECT_EQ(effects.particles().size(), DopagakiEffects::kMaxParticles);
}

}  // namespace
}  // namespace win32
}  // namespace renderer
}  // namespace mozc
