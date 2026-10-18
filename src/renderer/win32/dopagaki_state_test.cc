// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#include "renderer/win32/dopagaki_state.h"

#include <cstdint>

#include "absl/time/time.h"
#include "protocol/renderer_command.pb.h"
#include "testing/gmock.h"
#include "testing/gunit.h"

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using Type = DopagakiAction::Type;

constexpr uint64_t kSession1 = 0x1111;
constexpr uint64_t kSession2 = 0x2222;

commands::DopagakiEvent MakeEvent(uint64_t session_id, uint64_t key_count,
                                  uint64_t break_count,
                                  uint64_t keys_since_break) {
  commands::DopagakiEvent event;
  event.set_session_id(session_id);
  event.set_key_count(key_count);
  event.set_break_count(break_count);
  event.set_keys_since_break(keys_since_break);
  return event;
}

DopagakiAction Keys(uint64_t count, DopagakiRank rank = DopagakiRank::kB) {
  return {Type::kKeys, count, rank};
}
DopagakiAction Break(uint64_t combo) { return {Type::kBreak, combo}; }
DopagakiAction RankUp(uint64_t combo, DopagakiRank rank) {
  return {Type::kRankUp, combo, rank};
}
DopagakiAction Praise(uint64_t combo, DopagakiRank rank = DopagakiRank::kB) {
  return {Type::kPraise, combo, rank};
}
DopagakiAction Milestone(uint64_t combo) {
  return {Type::kMilestone, combo, DopagakiRank::kSSS};
}

class DopagakiStateTest : public ::testing::Test {
 protected:
  // Sends key_count = 1, 2, ..., |n| for a new session.
  void TypeKeys(DopagakiState& state, uint64_t session_id, uint64_t n) {
    for (uint64_t i = 1; i <= n; ++i) {
      state.OnEvent(MakeEvent(session_id, i, 0, i), now_);
    }
  }

  absl::Time now_ = absl::FromUnixSeconds(1700000000);
};

TEST_F(DopagakiStateTest, Rank) {
  EXPECT_EQ(GetDopagakiRank(0), DopagakiRank::kB);
  EXPECT_EQ(GetDopagakiRank(9), DopagakiRank::kB);
  EXPECT_EQ(GetDopagakiRank(10), DopagakiRank::kA);
  EXPECT_EQ(GetDopagakiRank(29), DopagakiRank::kA);
  EXPECT_EQ(GetDopagakiRank(30), DopagakiRank::kS);
  EXPECT_EQ(GetDopagakiRank(49), DopagakiRank::kS);
  EXPECT_EQ(GetDopagakiRank(50), DopagakiRank::kSS);
  EXPECT_EQ(GetDopagakiRank(99), DopagakiRank::kSS);
  EXPECT_EQ(GetDopagakiRank(100), DopagakiRank::kSSS);
  EXPECT_STREQ(GetDopagakiRankName(DopagakiRank::kSSS), "SSS");
}

TEST_F(DopagakiStateTest, AddsKeysOneByOne) {
  DopagakiState state;
  EXPECT_THAT(state.OnEvent(MakeEvent(kSession1, 1, 0, 1), now_),
              ElementsAre(Keys(1)));
  EXPECT_THAT(state.OnEvent(MakeEvent(kSession1, 2, 0, 2), now_),
              ElementsAre(Keys(1)));
  EXPECT_THAT(state.OnEvent(MakeEvent(kSession1, 3, 0, 3), now_),
              ElementsAre(Keys(1)));
  EXPECT_EQ(state.combo(), 3);
}

TEST_F(DopagakiStateTest, OverwrittenEventsAreMerged) {
  DopagakiState state;
  state.OnEvent(MakeEvent(kSession1, 1, 0, 1), now_);
  // key_count 2 and 3 were overwritten by 4.
  EXPECT_THAT(state.OnEvent(MakeEvent(kSession1, 4, 0, 4), now_),
              ElementsAre(Keys(3)));
  EXPECT_EQ(state.combo(), 4);
}

TEST_F(DopagakiStateTest, Break) {
  DopagakiState state;
  TypeKeys(state, kSession1, 5);
  EXPECT_THAT(state.OnEvent(MakeEvent(kSession1, 5, 1, 0), now_),
              ElementsAre(Break(5)));
  EXPECT_EQ(state.combo(), 0);
  EXPECT_EQ(state.rank(), DopagakiRank::kB);
  EXPECT_THAT(state.OnEvent(MakeEvent(kSession1, 6, 1, 1), now_),
              ElementsAre(Keys(1)));
  EXPECT_EQ(state.combo(), 1);
}

TEST_F(DopagakiStateTest, BreakWhenComboIsZero) {
  DopagakiState state;
  TypeKeys(state, kSession1, 2);
  state.OnEvent(MakeEvent(kSession1, 2, 1, 0), now_);
  EXPECT_THAT(state.OnEvent(MakeEvent(kSession1, 2, 2, 0), now_),
              ElementsAre(Break(0)));
}

TEST_F(DopagakiStateTest, BreakAndKeysArriveTogether) {
  DopagakiState state;
  TypeKeys(state, kSession1, 5);
  // BackSpace, then 2 keystrokes, merged into one event.
  EXPECT_THAT(state.OnEvent(MakeEvent(kSession1, 7, 1, 2), now_),
              ElementsAre(Break(5), Keys(2)));
  EXPECT_EQ(state.combo(), 2);
}

TEST_F(DopagakiStateTest, RankUp) {
  DopagakiState state;
  TypeKeys(state, kSession1, 9);
  EXPECT_THAT(state.OnEvent(MakeEvent(kSession1, 10, 0, 10), now_),
              ElementsAre(Keys(1, DopagakiRank::kA),
                          RankUp(10, DopagakiRank::kA),
                          Praise(10, DopagakiRank::kA)));
  EXPECT_THAT(state.OnEvent(MakeEvent(kSession1, 11, 0, 11), now_),
              ElementsAre(Keys(1, DopagakiRank::kA)));
}

TEST_F(DopagakiStateTest, RankUpSkipsToTheHighestRank) {
  DopagakiState state;
  TypeKeys(state, kSession1, 9);
  EXPECT_THAT(state.OnEvent(MakeEvent(kSession1, 120, 0, 120), now_),
              ElementsAre(Keys(111, DopagakiRank::kSSS),
                          RankUp(120, DopagakiRank::kSSS),
                          Praise(120, DopagakiRank::kSSS)));
}

TEST_F(DopagakiStateTest, PraiseEveryFive) {
  DopagakiState state;
  for (uint64_t i = 1; i <= 4; ++i) {
    EXPECT_THAT(state.OnEvent(MakeEvent(kSession1, i, 0, i), now_),
                ElementsAre(Keys(1)));
  }
  EXPECT_THAT(state.OnEvent(MakeEvent(kSession1, 5, 0, 5), now_),
              ElementsAre(Keys(1), Praise(5)));
  EXPECT_THAT(state.OnEvent(MakeEvent(kSession1, 6, 0, 6), now_),
              ElementsAre(Keys(1)));
}

TEST_F(DopagakiStateTest, OnlyOnePraiseForMergedKeystrokes) {
  DopagakiState state;
  TypeKeys(state, kSession1, 3);
  EXPECT_THAT(state.OnEvent(MakeEvent(kSession1, 12, 0, 12), now_),
              ElementsAre(Keys(9, DopagakiRank::kA),
                          RankUp(12, DopagakiRank::kA),
                          Praise(10, DopagakiRank::kA)));

  DopagakiState state2;
  TypeKeys(state2, kSession1, 3);
  EXPECT_THAT(state2.OnEvent(MakeEvent(kSession1, 17, 0, 17), now_),
              ElementsAre(Keys(14, DopagakiRank::kA),
                          RankUp(17, DopagakiRank::kA),
                          Praise(15, DopagakiRank::kA)));
}

TEST_F(DopagakiStateTest, NoMilestoneAtHundred) {
  DopagakiState state;
  TypeKeys(state, kSession1, 99);
  EXPECT_THAT(state.OnEvent(MakeEvent(kSession1, 100, 0, 100), now_),
              ElementsAre(Keys(1, DopagakiRank::kSSS),
                          RankUp(100, DopagakiRank::kSSS),
                          Praise(100, DopagakiRank::kSSS)));
}

TEST_F(DopagakiStateTest, MilestoneFromTwoHundred) {
  DopagakiState state;
  TypeKeys(state, kSession1, 199);
  EXPECT_THAT(state.OnEvent(MakeEvent(kSession1, 200, 0, 200), now_),
              ElementsAre(Keys(1, DopagakiRank::kSSS), Milestone(200),
                          Praise(200, DopagakiRank::kSSS)));
  for (uint64_t i = 201; i <= 299; ++i) {
    state.OnEvent(MakeEvent(kSession1, i, 0, i), now_);
  }
  EXPECT_EQ(state.combo(), 299);
  EXPECT_THAT(state.OnEvent(MakeEvent(kSession1, 300, 0, 300), now_),
              ElementsAre(Keys(1, DopagakiRank::kSSS), Milestone(300),
                          Praise(300, DopagakiRank::kSSS)));
}

TEST_F(DopagakiStateTest, PraiseAfterBreak) {
  DopagakiState state;
  TypeKeys(state, kSession1, 7);
  state.OnEvent(MakeEvent(kSession1, 7, 1, 0), now_);
  for (uint64_t i = 1; i <= 4; ++i) {
    state.OnEvent(MakeEvent(kSession1, 7 + i, 1, i), now_);
  }
  EXPECT_THAT(state.OnEvent(MakeEvent(kSession1, 12, 1, 5), now_),
              ElementsAre(Keys(1), Praise(5)));

  // A break and 5 keystrokes merged into one event.
  DopagakiState state2;
  TypeKeys(state2, kSession1, 7);
  EXPECT_THAT(state2.OnEvent(MakeEvent(kSession1, 12, 1, 5), now_),
              ElementsAre(Break(7), Keys(5), Praise(5)));
}

TEST_F(DopagakiStateTest, ComboIsSharedBySessions) {
  DopagakiState state;
  TypeKeys(state, kSession1, 3);
  TypeKeys(state, kSession2, 2);
  EXPECT_EQ(state.combo(), 5);
  // A break in another session breaks the shared combo.
  EXPECT_THAT(state.OnEvent(MakeEvent(kSession1, 3, 1, 0), now_),
              ElementsAre(Break(5)));
}

TEST_F(DopagakiStateTest, UnknownSessionIsLimited) {
  DopagakiState state;
  // e.g. the renderer was restarted while the TIP was counting.
  EXPECT_THAT(state.OnEvent(MakeEvent(kSession1, 100, 3, 40), now_),
              ElementsAre(Keys(5), Praise(5)));
  EXPECT_EQ(state.combo(), 5);
  EXPECT_THAT(state.OnEvent(MakeEvent(kSession1, 101, 3, 41), now_),
              ElementsAre(Keys(1)));
}

TEST_F(DopagakiStateTest, FirstKeyOfNewSessionIsBackSpace) {
  DopagakiState state;
  TypeKeys(state, kSession1, 4);
  EXPECT_THAT(state.OnEvent(MakeEvent(kSession2, 0, 1, 0), now_),
              ElementsAre(Break(4)));
  EXPECT_EQ(state.combo(), 0);
}

TEST_F(DopagakiStateTest, OldSessionsAreForgotten) {
  DopagakiState state;
  TypeKeys(state, kSession1, 3);
  const absl::Time later = now_ + absl::Minutes(11);
  // Treated as an unknown session again.
  EXPECT_THAT(state.OnEvent(MakeEvent(kSession1, 50, 0, 50), later),
              ElementsAre(Keys(5), Praise(5)));
  EXPECT_EQ(state.combo(), 8);
}

TEST_F(DopagakiStateTest, EventWithoutCountersIsIgnored) {
  DopagakiState state;
  commands::DopagakiEvent event;
  event.set_session_id(kSession1);
  event.mutable_settings()->set_enabled(false);
  EXPECT_THAT(state.OnEvent(event, now_), IsEmpty());
  EXPECT_EQ(state.combo(), 0);
}

TEST_F(DopagakiStateTest, DecreasingCountersAreIgnored) {
  DopagakiState state;
  TypeKeys(state, kSession1, 5);
  EXPECT_THAT(state.OnEvent(MakeEvent(kSession1, 2, 0, 2), now_), IsEmpty());
  EXPECT_EQ(state.combo(), 5);
}

}  // namespace
}  // namespace win32
}  // namespace renderer
}  // namespace mozc
