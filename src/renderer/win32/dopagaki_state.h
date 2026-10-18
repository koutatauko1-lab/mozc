// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#ifndef MOZC_RENDERER_WIN32_DOPAGAKI_STATE_H_
#define MOZC_RENDERER_WIN32_DOPAGAKI_STATE_H_

#include <cstdint>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/time/time.h"
#include "protocol/renderer_command.pb.h"

namespace mozc {
namespace renderer {
namespace win32 {

enum class DopagakiRank {
  kB = 0,    // 0-9
  kA = 1,    // 10-29
  kS = 2,    // 30-49
  kSS = 3,   // 50-99
  kSSS = 4,  // 100-
};

// Returns the rank for |combo|.
DopagakiRank GetDopagakiRank(uint64_t combo);
// Returns "B", "A", "S", "SS" or "SSS".
const char* GetDopagakiRankName(DopagakiRank rank);

// What happened as a result of one DopagakiEvent. The effects and the sounds
// are driven by these.
struct DopagakiAction {
  enum class Type {
    kKeys,    // |count| keystrokes were added to the combo.
    kBreak,   // The combo was broken. |count| is the combo before the break.
    kRankUp,  // The rank went up to |rank|.
    // Update 1: the combo passed 200, 300, ... |count| is the highest
    // multiple of 100 passed, and |rank| is kSSS.
    kMilestone,
    // Update 1: the combo passed a multiple of 5. |count| is the highest one
    // passed, and |rank| is the rank of |count|.
    kPraise,
  };
  Type type = Type::kKeys;
  uint64_t count = 0;
  DopagakiRank rank = DopagakiRank::kB;

  friend bool operator==(const DopagakiAction&,
                         const DopagakiAction&) = default;
};

// Turns the cumulative counters of each TIP session into one combo shared by
// the whole PC. This class has no dependency on Windows APIs.
class DopagakiState {
 public:
  // Sessions not seen for this long are forgotten.
  static constexpr absl::Duration kSessionLifetime = absl::Minutes(10);
  // Keystrokes taken over from a session seen for the first time.
  static constexpr uint64_t kMaxKeysFromUnknownSession = 5;

  DopagakiState() = default;
  DopagakiState(const DopagakiState&) = delete;
  DopagakiState& operator=(const DopagakiState&) = delete;

  // Updates the combo with |event| received at |now|. When a break and new
  // keystrokes arrive together, kBreak comes first. Keystrokes give kKeys,
  // then kRankUp, kMilestone and kPraise if they happened.
  std::vector<DopagakiAction> OnEvent(const commands::DopagakiEvent& event,
                                      absl::Time now);

  uint64_t combo() const { return combo_; }
  DopagakiRank rank() const { return GetDopagakiRank(combo_); }
  // Sets the combo to 0 without a break, e.g. when the effects are turned
  // off. The counters of the sessions are kept.
  void ResetCombo() { combo_ = 0; }

 private:
  struct Session {
    uint64_t key_count = 0;
    uint64_t break_count = 0;
    absl::Time last_seen;
  };

  void AddKeys(uint64_t keys, std::vector<DopagakiAction>* actions);
  void ForgetOldSessions(absl::Time now);

  uint64_t combo_ = 0;
  absl::flat_hash_map<uint64_t, Session> sessions_;
};

}  // namespace win32
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_WIN32_DOPAGAKI_STATE_H_
