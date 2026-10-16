// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#include "win32/tip/tip_dopagaki.h"

#include <inputscope.h>
#include <msctf.h>
#include <wil/com.h>
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>

#include "absl/types/span.h"
#include "base/win32/win_util.h"
#include "protocol/renderer_command.pb.h"
#include "renderer/win32/dopagaki_sound_synth.h"
#include "renderer/win32/win32_renderer_client.h"
#include "win32/tip/tip_dopagaki_sound.h"
#include "win32/tip/tip_input_mode_manager.h"
#include "win32/tip/tip_status.h"
#include "win32/tip/tip_text_service.h"
#include "win32/tip/tip_thread_context.h"

namespace mozc {
namespace win32 {
namespace tsf {
namespace {

using ::mozc::commands::DopagakiEvent;
using ::mozc::renderer::win32::Win32RendererClient;

// Virtual keys that may be missing in older SDK headers.
constexpr UINT kVkImeOn = 0x16;
constexpr UINT kVkImeOff = 0x1A;
// VK_DBE_ALPHANUMERIC ... VK_DBE_NOROMAN. Keys such as "英数",
// "カタカナ/ひらがな" and "半角/全角" on Japanese keyboards.
constexpr UINT kVkDbeFirst = 0xF0;
constexpr UINT kVkDbeLast = 0xF6;

// A keystroke reported again within this time is treated as the same one.
constexpr int64_t kSameKeystrokeMicroseconds = 50000;
// ... and within this time if the message time is also the same.
constexpr int64_t kSameMessageMicroseconds = 1000000;

enum class KeyRoute { kTestKeyDown, kKeyDown };

// A keystroke counted recently. Used to count a keystroke reported by both
// OnTestKeyDown and OnKeyDown only once.
struct RecentKey {
  uint64_t signature = 0;  // wparam, lparam and thread. 0 means empty.
  LONG message_time = 0;
  int64_t microseconds = 0;
};

// Everything below is guarded by |g_lock|. The lock is held only for a few
// arithmetic operations.
SRWLOCK g_lock = SRWLOCK_INIT;
// Counters of this process (one set per loaded TIP DLL).
uint64_t g_key_count = 0;
uint64_t g_break_count = 0;
uint64_t g_keys_since_break = 0;
RecentKey g_recent_keys[8];
size_t g_recent_key_index = 0;

// SplitMix64. Used only to mix bits.
uint64_t Mix(uint64_t x) {
  x += 0x9E3779B97F4A7C15ULL;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
  return x ^ (x >> 31);
}

uint64_t GetSessionId() {
  static const uint64_t session_id = [] {
    LARGE_INTEGER counter = {};
    ::QueryPerformanceCounter(&counter);
    const uint64_t id =
        Mix(static_cast<uint64_t>(counter.QuadPart) ^
            (static_cast<uint64_t>(::GetCurrentProcessId()) << 32) ^
            ::GetTickCount64());
    return id == 0 ? 1 : id;
  }();
  return session_id;
}

uint64_t GetKeySignature(WPARAM wparam, LPARAM lparam) {
  uint64_t signature = Mix(static_cast<uint64_t>(wparam));
  signature = Mix(signature ^ static_cast<uint32_t>(lparam));
  signature = Mix(signature ^ ::GetCurrentThreadId());
  return signature == 0 ? 1 : signature;
}

int64_t GetMicroseconds() {
  static const int64_t frequency = [] {
    LARGE_INTEGER value = {};
    ::QueryPerformanceFrequency(&value);
    return value.QuadPart > 0 ? value.QuadPart : 1;
  }();
  LARGE_INTEGER counter = {};
  ::QueryPerformanceCounter(&counter);
  return counter.QuadPart / frequency * 1000000 +
         counter.QuadPart % frequency * 1000000 / frequency;
}

// Returns true if this keystroke was already counted. Otherwise remembers it.
// Must be called with |g_lock| held.
bool IsCountedKeystrokeLocked(uint64_t signature, LONG message_time,
                              int64_t microseconds) {
  for (const RecentKey& recent : g_recent_keys) {
    if (recent.signature != signature) {
      continue;
    }
    // The same WM_KEYDOWN reported twice has the same message time. The time
    // window is a fallback for applications whose message time is not
    // updated.
    const int64_t elapsed = microseconds - recent.microseconds;
    if (elapsed < kSameKeystrokeMicroseconds ||
        (recent.message_time == message_time &&
         elapsed < kSameMessageMicroseconds)) {
      return true;
    }
  }
  g_recent_keys[g_recent_key_index] = {signature, message_time, microseconds};
  g_recent_key_index = (g_recent_key_index + 1) % std::size(g_recent_keys);
  return false;
}

// K3: keys that are never counted.
bool IsIgnoredKey(UINT vk) {
  switch (vk) {
    case VK_SHIFT:
    case VK_CONTROL:
    case VK_MENU:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
    case VK_APPS:
    case VK_CAPITAL:
    case VK_TAB:
    case VK_ESCAPE:
    case VK_NUMLOCK:
    case VK_SCROLL:
    case VK_KANA:
    case VK_KANJI:
    case kVkImeOn:
    case kVkImeOff:
    case VK_PROCESSKEY:
      return true;
    default:
      return kVkDbeFirst <= vk && vk <= kVkDbeLast;
  }
}

// K2: password fields and contexts where the keyboard is disabled.
bool IsPrivateContext(TipTextService* text_service, ITfContext* context) {
  if (context != nullptr && TipStatus::IsDisabledContext(context)) {
    return true;
  }
  TipThreadContext* thread_context = text_service->GetThreadContext();
  if (thread_context == nullptr) {
    return false;
  }
  const absl::Span<const InputScope> input_scope =
      thread_context->GetInputModeManager()->input_scope();
  return std::find(input_scope.begin(), input_scope.end(), IS_PASSWORD) !=
         input_scope.end();
}

HWND GetTargetWindow(ITfContext* context) {
  if (context != nullptr) {
    wil::com_ptr_nothrow<ITfContextView> context_view;
    if (SUCCEEDED(context->GetActiveView(&context_view)) && context_view) {
      HWND window = nullptr;
      if (SUCCEEDED(context_view->GetWnd(&window)) && window != nullptr) {
        return window;
      }
    }
  }
  return ::GetForegroundWindow();
}

// |route| is not used for counting, but kept to tell where a keystroke came
// from when debugging.
void CountKeyDown(TipTextService* text_service, ITfContext* context,
                  WPARAM wparam, LPARAM lparam, KeyRoute /*route*/) {
  if (text_service == nullptr) {
    return;
  }
  // K1: bit 30 is set when the key was already down (auto-repeat).
  if ((static_cast<uint32_t>(lparam) & (1u << 30)) != 0) {
    return;
  }
  const UINT vk = LOWORD(wparam);
  if (IsIgnoredKey(vk)) {
    return;
  }
  if (IsPrivateContext(text_service, context)) {
    return;
  }

  const uint64_t signature = GetKeySignature(wparam, lparam);
  const LONG message_time = ::GetMessageTime();
  const int64_t microseconds = GetMicroseconds();
  const bool is_break = (vk == VK_BACK);
  DopagakiEvent event;
  {
    ::AcquireSRWLockExclusive(&g_lock);
    const bool counted =
        IsCountedKeystrokeLocked(signature, message_time, microseconds);
    if (!counted) {
      if (is_break) {
        ++g_break_count;
        g_keys_since_break = 0;
      } else {
        ++g_key_count;
        ++g_keys_since_break;
      }
      event.set_key_count(g_key_count);
      event.set_break_count(g_break_count);
      event.set_keys_since_break(g_keys_since_break);
    }
    ::ReleaseSRWLockExclusive(&g_lock);
    if (counted) {
      return;
    }
  }
  event.set_session_id(GetSessionId());
  event.set_target_window_handle(
      WinUtil::EncodeWindowHandle(GetTargetWindow(context)));
  // The renderer asks this window to play sounds.
  event.set_receiver_window_handle(
      WinUtil::EncodeWindowHandle(text_service->renderer_callback_window_handle()));
  Win32RendererClient::OnDopagakiEvent(event);
}

}  // namespace

void TipDopagaki::OnTestKeyDown(TipTextService* text_service,
                                ITfContext* context, WPARAM wparam,
                                LPARAM lparam) {
  CountKeyDown(text_service, context, wparam, lparam, KeyRoute::kTestKeyDown);
}

void TipDopagaki::OnKeyDown(TipTextService* text_service, ITfContext* context,
                            WPARAM wparam, LPARAM lparam) {
  CountKeyDown(text_service, context, wparam, lparam, KeyRoute::kKeyDown);
}

bool TipDopagaki::OnRendererCallback(WPARAM wparam, LPARAM lparam) {
  if (wparam != renderer::win32::kDopagakiSoundRequest) {
    return false;
  }
  renderer::win32::DopagakiSoundSet sound_set;
  renderer::win32::DopagakiSoundKind kind;
  int rank_index = 0;
  if (renderer::win32::DecodeDopagakiSoundRequest(
          static_cast<uint32_t>(lparam), &sound_set, &kind, &rank_index)) {
    TipDopagakiSound::Play(sound_set, kind, rank_index);
  }
  return true;
}

void TipDopagaki::SendSettings(const config::Config::DopagakiConfig& settings) {
  // Without the counters, the renderer only applies the settings.
  DopagakiEvent event;
  *event.mutable_settings() = settings;
  Win32RendererClient::OnDopagakiEvent(event);
}

}  // namespace tsf
}  // namespace win32
}  // namespace mozc
