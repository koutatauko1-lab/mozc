// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#ifndef MOZC_WIN32_TIP_TIP_DOPAGAKI_MENU_H_
#define MOZC_WIN32_TIP_TIP_DOPAGAKI_MENU_H_

#include <windows.h>

#include <optional>

#include "protocol/config.pb.h"

namespace mozc {
namespace win32 {
namespace tsf {

// The Dopagaki items in the right-click menu of the input mode icon on the
// taskbar (phase 6): "ドパガキ演出" (ON/OFF, K6) and the sound sets.
// The settings are saved by mozc_server as Config::dopagaki_config.
class TipDopagakiMenu {
 public:
  using Settings = config::Config::DopagakiConfig;

  TipDopagakiMenu() = delete;
  TipDopagakiMenu(const TipDopagakiMenu&) = delete;
  TipDopagakiMenu& operator=(const TipDopagakiMenu&) = delete;

  // Returns true for TipLangBarCallback::kDopagaki* items.
  static bool IsMenuItem(UINT item_id);
  // Returns TF_LBMENUF_CHECKED, TF_LBMENUF_RADIOCHECKED or 0 for a Dopagaki
  // item with |settings|.
  static DWORD GetMenuItemFlags(UINT item_id, const Settings& settings);
  // Reads the current settings from mozc_server. Returns the defaults if it
  // fails.
  static Settings LoadSettings();
  // Applies the selected Dopagaki item and saves the settings through
  // mozc_server. Returns the new settings, or nullopt if nothing was saved.
  static std::optional<Settings> OnMenuSelect(UINT item_id);
};

}  // namespace tsf
}  // namespace win32
}  // namespace mozc

#endif  // MOZC_WIN32_TIP_TIP_DOPAGAKI_MENU_H_
