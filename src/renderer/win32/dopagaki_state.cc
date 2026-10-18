// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#include "renderer/win32/dopagaki_state.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/time/time.h"
#include "protocol/renderer_command.pb.h"

namespace mozc {
namespace renderer {
namespace win32 {

DopagakiRank GetDopagakiRank(uint64_t combo) {
  if (combo >= 100) {
    return DopagakiRank::kSSS;
  }
  if (combo >= 50) {
    return DopagakiRank::kSS;
  }
  if (combo >= 30) {
    return DopagakiRank::kS;
  }
  if (combo >= 10) {
    return DopagakiRank::kA;
  }
  return DopagakiRank::kB;
}

const char* GetDopagakiRankName(DopagakiRank rank) {
  switch (rank) {
    case DopagakiRank::kB:
      return "B";
    case DopagakiRank::kA:
      return "A";
    case DopagakiRank::kS:
      return "S";
    case DopagakiRank::kSS:
      return "SS";
    case DopagakiRank::kSSS:
      return "SSS";
  }
  return "B";
}

std::vector<DopagakiAction> DopagakiState::OnEvent(
    const commands::DopagakiEvent& event, absl::Time now) {
  std::vector<DopagakiAction> actions;
  ForgetOldSessions(now);
  if (!event.has_session_id() || !event.has_key_count() ||
      !event.has_break_count()) {
    // e.g. an event that only carries settings.
    return actions;
  }
  const uint64_t key_count = event.key_count();
  const uint64_t break_count = event.break_count();

  const auto [it, inserted] = sessions_.try_emplace(event.session_id());
  Session& session = it->second;
  const Session previous = session;
  session.key_count = key_count;
  session.break_count = break_count;
  session.last_seen = now;

  if (inserted) {
    if (key_count + break_count == 1) {
      // A brand-new session. This is its very first keystroke.
      if (break_count == 1) {
        actions.push_back({DopagakiAction::Type::kBreak, combo_});
        combo_ = 0;
      } else {
        AddKeys(1, &actions);
      }
      return actions;
    }
    // The renderer may have been restarted while the TIP kept counting. Do not
    // break, and take over only a few keystrokes.
    AddKeys(std::min(event.keys_since_break(), kMaxKeysFromUnknownSession),
            &actions);
    return actions;
  }

  if (key_count < previous.key_count || break_count < previous.break_count) {
    // Should not happen. Just follow the new counters.
    return actions;
  }
  const uint64_t new_keys = key_count - previous.key_count;
  if (break_count > previous.break_count) {
    // Several breaks may arrive at once. They are treated as one.
    actions.push_back({DopagakiAction::Type::kBreak, combo_});
    combo_ = 0;
    AddKeys(std::min(event.keys_since_break(), new_keys), &actions);
    return actions;
  }
  AddKeys(new_keys, &actions);
  return actions;
}

void DopagakiState::AddKeys(uint64_t keys,
                            std::vector<DopagakiAction>* actions) {
  if (keys == 0) {
    return;
  }
  const uint64_t before_combo = combo_;
  const DopagakiRank before = rank();
  combo_ += keys;
  const DopagakiRank after = rank();
  actions->push_back({DopagakiAction::Type::kKeys, keys, after});
  if (after > before) {
    // Only the highest rank even if several ranks are passed at once.
    actions->push_back({DopagakiAction::Type::kRankUp, combo_, after});
  }
  // Update 1: also only the highest multiple passed.
  const uint64_t hundreds = combo_ / 100 * 100;
  if (combo_ / 100 > before_combo / 100 && hundreds >= 200) {
    actions->push_back(
        {DopagakiAction::Type::kMilestone, hundreds, DopagakiRank::kSSS});
  }
  if (combo_ / 5 > before_combo / 5) {
    const uint64_t fives = combo_ / 5 * 5;
    actions->push_back(
        {DopagakiAction::Type::kPraise, fives, GetDopagakiRank(fives)});
  }
}

void DopagakiState::ForgetOldSessions(absl::Time now) {
  absl::erase_if(sessions_, [now](const auto& entry) {
    return now - entry.second.last_seen > kSessionLifetime;
  });
}

}  // namespace win32
}  // namespace renderer
}  // namespace mozc
