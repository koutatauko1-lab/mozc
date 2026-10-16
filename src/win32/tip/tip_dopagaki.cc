// Dopagaki IME (a fork of Mozc). Distributed under the same BSD 3-Clause
// license as Mozc. See LICENSE at the top of the repository.

#include "win32/tip/tip_dopagaki.h"

#include <inputscope.h>
#include <msctf.h>
#include <wil/com.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <iterator>

#include "absl/types/span.h"
#include "base/win32/win_util.h"
#include "protocol/renderer_command.pb.h"
#include "renderer/win32/win32_renderer_client.h"
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

// Counters of this process (one set per loaded TIP DLL).
std::atomic<uint64_t> g_key_count = 0;
std::atomic<uint64_t> g_break_count = 0;
std::atomic<uint64_t> g_keys_since_break = 0;
// Signature of the last counted key event, used to drop duplicated calls of
// OnTestKeyDown for the same physical keystroke.
std::atomic<uint64_t> g_last_key_signature = 0;

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
  signature = Mix(signature ^ static_cast<uint32_t>(::GetMessageTime()));
  return Mix(signature ^ ::GetCurrentThreadId());
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

// Writes a log for DebugView. Never write what was typed here.
void DebugLog(const DopagakiEvent& event, const wchar_t* what) {
  wchar_t buffer[160] = {};
  ::swprintf_s(buffer, std::size(buffer),
               L"[dopagaki] tip %ls keys=%llu breaks=%llu since=%llu\n", what,
               static_cast<unsigned long long>(event.key_count()),
               static_cast<unsigned long long>(event.break_count()),
               static_cast<unsigned long long>(event.keys_since_break()));
  ::OutputDebugStringW(buffer);
}

}  // namespace

void TipDopagaki::OnTestKeyDown(TipTextService* text_service,
                                ITfContext* context, WPARAM wparam,
                                LPARAM lparam) {
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
  if (g_last_key_signature.exchange(signature) == signature) {
    ::OutputDebugStringW(L"[dopagaki] tip duplicated key event skipped\n");
    return;
  }

  DopagakiEvent event;
  event.set_session_id(GetSessionId());
  if (vk == VK_BACK) {
    g_keys_since_break = 0;
    event.set_break_count(++g_break_count);
    event.set_key_count(g_key_count.load());
    event.set_keys_since_break(0);
  } else {
    event.set_key_count(++g_key_count);
    event.set_keys_since_break(++g_keys_since_break);
    event.set_break_count(g_break_count.load());
  }
  event.set_target_window_handle(
      WinUtil::EncodeWindowHandle(GetTargetWindow(context)));
  DebugLog(event, vk == VK_BACK ? L"break" : L"key");
  Win32RendererClient::OnDopagakiEvent(event);
}

}  // namespace tsf
}  // namespace win32
}  // namespace mozc
