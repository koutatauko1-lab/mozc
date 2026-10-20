// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#include "win32/tip/tip_dopagaki_menu.h"

#include <ctfutb.h>
#include <windows.h>

#include <memory>
#include <optional>

#include "client/client.h"
#include "client/client_interface.h"
#include "protocol/config.pb.h"
#include "win32/tip/tip_lang_bar_callback.h"

namespace mozc {
namespace win32 {
namespace tsf {
namespace {

using Settings = TipDopagakiMenu::Settings;

// Returns the sound set of a sound item, or nullopt for other items.
std::optional<Settings::SoundSet> GetSoundSet(UINT item_id) {
  switch (item_id) {
    case TipLangBarCallback::kDopagakiSoundPico:
      return Settings::PICO;
    case TipLangBarCallback::kDopagakiSoundTypewriter:
      return Settings::TYPEWRITER;
    case TipLangBarCallback::kDopagakiSoundHit:
      return Settings::HIT;
    case TipLangBarCallback::kDopagakiSoundTaiko:
      return Settings::TAIKO;
    case TipLangBarCallback::kDopagakiSoundSparkle:
      return Settings::SPARKLE;
    case TipLangBarCallback::kDopagakiSoundMecha:
      return Settings::MECHA;
    case TipLangBarCallback::kDopagakiSoundMute:
      return Settings::MUTE;
    default:
      return std::nullopt;
  }
}

// Returns a client of mozc_server that never shows an error dialog, because
// the menu is shown inside the application.
std::unique_ptr<client::ClientInterface> NewClient() {
  std::unique_ptr<client::ClientInterface> client(
      client::ClientFactory::NewClient());
  if (client != nullptr) {
    client->set_suppress_error_dialog(true);
  }
  return client;
}

}  // namespace

bool TipDopagakiMenu::IsMenuItem(UINT item_id) {
  return item_id == TipLangBarCallback::kDopagakiToggle ||
         item_id == TipLangBarCallback::kDopagakiPraiseToggle ||
         GetSoundSet(item_id).has_value();
}

DWORD TipDopagakiMenu::GetMenuItemFlags(UINT item_id,
                                        const Settings& settings) {
  if (item_id == TipLangBarCallback::kDopagakiToggle) {
    return settings.enabled() ? TF_LBMENUF_CHECKED : 0;
  }
  if (item_id == TipLangBarCallback::kDopagakiPraiseToggle) {
    return settings.praise_enabled() ? TF_LBMENUF_CHECKED : 0;
  }
  const std::optional<Settings::SoundSet> sound_set = GetSoundSet(item_id);
  if (sound_set.has_value() && *sound_set == settings.sound_set()) {
    return TF_LBMENUF_RADIOCHECKED;
  }
  return 0;
}

Settings TipDopagakiMenu::LoadSettings() {
  std::unique_ptr<client::ClientInterface> client = NewClient();
  config::Config config;
  if (client == nullptr || !client->GetConfig(&config)) {
    return Settings();
  }
  return config.dopagaki_config();
}

std::optional<Settings> TipDopagakiMenu::OnMenuSelect(UINT item_id) {
  if (!IsMenuItem(item_id)) {
    return std::nullopt;
  }
  std::unique_ptr<client::ClientInterface> client = NewClient();
  config::Config config;
  if (client == nullptr || !client->GetConfig(&config)) {
    return std::nullopt;
  }
  Settings* settings = config.mutable_dopagaki_config();
  if (item_id == TipLangBarCallback::kDopagakiToggle) {
    settings->set_enabled(!settings->enabled());
  } else if (item_id == TipLangBarCallback::kDopagakiPraiseToggle) {
    settings->set_praise_enabled(!settings->praise_enabled());
  } else {
    settings->set_sound_set(*GetSoundSet(item_id));
  }
  if (!client->SetConfig(config)) {
    return std::nullopt;
  }
  return *settings;
}

}  // namespace tsf
}  // namespace win32
}  // namespace mozc
