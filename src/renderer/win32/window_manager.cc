// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "renderer/win32/window_manager.h"

#include <windows.h>

#include <d2d1.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/time/clock.h"
#include "base/const.h"
#include "base/coordinates.h"
#include "base/win32/win_util.h"
#include "client/client_interface.h"
#include "config/config_handler.h"
#include "protocol/candidate_window.pb.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "protocol/renderer_command.pb.h"
#include "renderer/win32/candidate_window.h"
#include "renderer/win32/dopagaki_effects.h"
#include "renderer/win32/dopagaki_overlay.h"
#include "renderer/win32/dopagaki_praise.h"
#include "renderer/win32/dopagaki_state.h"
#include "renderer/win32/indicator_window.h"
#include "renderer/win32/infolist_window.h"
#include "renderer/win32/win32_dpi_util.h"
#include "renderer/win32/win32_renderer_util.h"
#include "renderer/window_util.h"

namespace mozc {
namespace renderer {
namespace win32 {

namespace {

constexpr uint32_t kHideWindowDelay = 500;  // msec
const POINT kInvalidMousePosition = {-65535, -65535};

}  // namespace

WindowManager::WindowManager()
    : dopagaki_state_(std::make_unique<DopagakiState>()),
      dopagaki_overlay_(std::make_unique<DopagakiOverlay>()),
      main_window_(std::make_unique<CandidateWindow>()),
      cascading_window_(std::make_unique<CandidateWindow>()),
      indicator_window_(std::make_unique<IndicatorWindow>()),
      infolist_window_(std::make_unique<InfolistWindow>()),
      layout_manager_(std::make_unique<LayoutManager>()),
      send_command_interface_(nullptr),
      last_position_(kInvalidMousePosition),
      candidates_finger_print_(0),
      thread_id_(0) {}

WindowManager::~WindowManager() {}

void WindowManager::Initialize() {
  dopagaki_overlay_->SetFrameCallback([this] { OnDopagakiFrame(); });
  DCHECK(!main_window_->IsWindow());
  DCHECK(!cascading_window_->IsWindow());
  DCHECK(!infolist_window_->IsWindow());

  main_window_->Create(nullptr);
  main_window_->ShowWindow(SW_HIDE);
  cascading_window_->Create(nullptr);
  cascading_window_->ShowWindow(SW_HIDE);
  indicator_window_->Initialize();
  infolist_window_->Create(nullptr);
  infolist_window_->ShowWindow(SW_HIDE);
  // Dopagaki: the settings saved by mozc_server. Later changes come with
  // DopagakiEvent::settings.
  ApplyDopagakiSettings(
      config::ConfigHandler::GetSharedConfig()->dopagaki_config());
}

void WindowManager::AsyncHideAllWindows() {
  cascading_window_->ShowWindowAsync(SW_HIDE);
  main_window_->ShowWindowAsync(SW_HIDE);
  infolist_window_->ShowWindowAsync(SW_HIDE);
}

void WindowManager::AsyncQuitAllWindows() {
  cascading_window_->PostMessage(WM_CLOSE, 0, 0);
  main_window_->PostMessage(WM_CLOSE, 0, 0);
  infolist_window_->PostMessage(WM_CLOSE, 0, 0);
}

void WindowManager::DestroyAllWindows() {
  if (main_window_->IsWindow()) {
    main_window_->DestroyWindow();
  }
  if (cascading_window_->IsWindow()) {
    cascading_window_->DestroyWindow();
  }
  indicator_window_->Destroy();
  if (infolist_window_->IsWindow()) {
    infolist_window_->DestroyWindow();
  }
  dopagaki_overlay_->Destroy();
}

void WindowManager::HideAllWindows() {
  main_window_->ShowWindow(SW_HIDE);
  cascading_window_->ShowWindow(SW_HIDE);
  indicator_window_->Hide();
  infolist_window_->DelayHide(0);
}

// TODO(yukawa): Refactor this method by making a new method in LayoutManager
//   with unit tests so that LayoutManager can handle both composition windows
//   and candidate windows.
void WindowManager::UpdateLayout(const commands::RendererCommand& command) {
  typedef mozc::commands::RendererCommand::ApplicationInfo ApplicationInfo;

  // Hide all UI elements if |command.visible()| is false.
  if (!command.visible()) {
    cascading_window_->ShowWindow(SW_HIDE);
    main_window_->ShowWindow(SW_HIDE);
    indicator_window_->Hide();
    infolist_window_->DelayHide(0);
    return;
  }

  // We assume |output| exists in the renderer command
  // for all |RendererCommand::UPDATE| renderer messages.
  DCHECK(command.has_output());
  const commands::Output& output = command.output();

  // We assume |application_info| exists in the renderer command
  // for all |RendererCommand::UPDATE| renderer messages.
  DCHECK(command.has_application_info());

  const commands::RendererCommand::ApplicationInfo& app_info =
      command.application_info();

  (void)app_info.target_window_handle();
  bool show_candidate =
      ((app_info.ui_visibilities() & ApplicationInfo::ShowCandidateWindow) ==
       ApplicationInfo::ShowCandidateWindow);
  bool show_suggest =
      ((app_info.ui_visibilities() & ApplicationInfo::ShowSuggestWindow) ==
       ApplicationInfo::ShowSuggestWindow);

  CandidateWindowLayout candidate_layout;

  bool is_suggest = false;
  bool is_convert_or_predict = false;
  if (output.has_candidate_window() &&
      output.candidate_window().has_category()) {
    switch (output.candidate_window().category()) {
      case commands::SUGGESTION:
        is_suggest = true;
        break;
      case commands::CONVERSION:
      case commands::PREDICTION:
        is_convert_or_predict = true;
        break;
      default:
        // do nothing.
        break;
    }
  }

  // Currently the indicator will be displayed if and only if no other window
  // (suggestion, prediction, nor conversion) is not displayed.
  if (is_suggest || is_convert_or_predict) {
    indicator_window_->Hide();
  } else if (app_info.has_indicator_info()) {
    indicator_window_->OnUpdate(command, layout_manager_.get());
  }

  if (!output.has_candidate_window()) {
    // Hide candidate windows because there is no candidate to be displayed.
    cascading_window_->ShowWindow(SW_HIDE);
    main_window_->ShowWindow(SW_HIDE);
    infolist_window_->DelayHide(0);
    return;
  }

  if (is_suggest && !show_suggest) {
    // The candidate list is for suggestion but the visibility bit is off.
    cascading_window_->ShowWindow(SW_HIDE);
    main_window_->ShowWindow(SW_HIDE);
    infolist_window_->DelayHide(0);
    return;
  }

  if (is_convert_or_predict && !show_candidate) {
    // The candidate list is for conversion or prediction but the visibility
    // bit is off.
    cascading_window_->ShowWindow(SW_HIDE);
    main_window_->ShowWindow(SW_HIDE);
    infolist_window_->DelayHide(0);
    return;
  }

  const commands::CandidateWindow& candidate_window = output.candidate_window();
  if (candidate_window.candidate_size() == 0) {
    cascading_window_->ShowWindow(SW_HIDE);
    main_window_->ShowWindow(SW_HIDE);
    infolist_window_->DelayHide(0);
    return;
  }

  if (!candidate_layout.initialized()) {
    candidate_layout.Clear();
    layout_manager_->LayoutCandidateWindow(app_info, &candidate_layout);
  }

  if (!candidate_layout.initialized()) {
    cascading_window_->ShowWindow(SW_HIDE);
    main_window_->ShowWindow(SW_HIDE);
    infolist_window_->DelayHide(0);
    return;
  }

  // Currently, we do not use finger print.
  bool candidate_changed = true;

  const Point target_point(candidate_layout.position().x,
                           candidate_layout.position().y);

  // Sync both windows to the DPI of the monitor where the candidate window
  // is about to be placed. This makes their first-frame layout correct when
  // the target app is on a different-DPI monitor than where the windows were
  // last shown — WM_DPICHANGED would otherwise fire only during the
  // subsequent SetWindowPos, after the size has already been computed at
  // the stale DPI.
  const uint32_t target_dpi = GetDpiForPoint(target_point.x, target_point.y);
  main_window_->UpdateDpi(target_dpi);
  infolist_window_->UpdateDpi(target_dpi);

  if (candidate_changed &&
      (candidate_window.display_type() == commands::MAIN)) {
    main_window_->UpdateLayout(candidate_window);
  }
  const Size main_window_size = main_window_->GetLayoutSize();

  // Obtain the monitor's working area
  Rect working_area;
  {
    CRect area;
    if (GetWorkingAreaFromPoint(CPoint(target_point.x, target_point.y),
                                &area)) {
      working_area = Rect(area.left, area.top, area.Width(), area.Height());
    }
  }

  // We prefer the left position of candidate strings is aligned to
  // that of preedit.
  const Point main_window_zero_point(
      main_window_->GetCandidateColumnInClientCord().Left(), 0);

  Rect main_window_rect;
  {
    // Equating |exclusion_area| with |preedit_rect| generally works well and
    // makes most of users happy.
    const CRect rect(candidate_layout.exclude_region());
    const Rect preedit_rect(rect.left, rect.top, rect.Width(), rect.Height());
    const bool vertical = (LayoutManager::GetWritingDirection(app_info) ==
                           LayoutManager::VERTICAL_WRITING);
    // Sometimes |target_point| is set to the top-left of the exclusion area
    // but WindowUtil does not support this case yet.
    // As a workaround, use |preedit_rect.Bottom()| for y-coordinate of the
    // |target_point|.
    // TODO(yukawa): Fix WindowUtil to support this case.
    // TODO(yukawa): Add more unit tests.
    Point new_target_point = target_point;
    if (!vertical) {
      new_target_point.y = preedit_rect.Bottom();
    }
    main_window_rect =
        WindowUtil::GetWindowRectForMainWindowFromTargetPointAndPreedit(
            new_target_point, preedit_rect, main_window_size,
            main_window_zero_point, working_area, vertical);
  }

  const DWORD set_windows_pos_flags = SWP_NOACTIVATE | SWP_SHOWWINDOW;
  main_window_->SetWindowPos(HWND_TOPMOST, main_window_rect.Left(),
                             main_window_rect.Top(), main_window_rect.Width(),
                             main_window_rect.Height(), set_windows_pos_flags);
  // Dopagaki: the position without shaking.
  dopagaki_main_window_rect_ = main_window_rect;
  dopagaki_cascading_window_rect_.reset();
  dopagaki_shake_offset_ = {0, 0};
  // This trick ensures that the window is certainly shown as 'inactivated'
  // in terms of visual effect on DWM-enabled desktop.
  main_window_->SendMessageW(WM_NCACTIVATE, FALSE);

  bool cascading_visible = false;

  if (candidate_window.has_sub_candidate_window() &&
      candidate_window.sub_candidate_window().display_type() ==
          commands::CASCADE) {
    (void)candidate_window.sub_candidate_window();
    cascading_visible = true;
  }

  bool infolist_visible = false;
  if (command.output().has_candidate_window() &&
      command.output().candidate_window().has_usages() &&
      command.output().candidate_window().usages().information_size() > 0) {
    infolist_visible = true;
  }

  if (infolist_visible && !cascading_visible) {
    if (candidate_changed) {
      infolist_window_->UpdateLayout(candidate_window);
      infolist_window_->Invalidate();
    }

    // Align infolist window
    const Rect infolist_rect = WindowUtil::GetWindowRectForInfolistWindow(
        infolist_window_->GetLayoutSize(), main_window_rect, working_area);
    infolist_window_->MoveWindow(infolist_rect.Left(), infolist_rect.Top(),
                                 infolist_rect.Width(), infolist_rect.Height(),
                                 TRUE);
    infolist_window_->SetWindowPos(
        HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    if (candidate_window.has_focused_index() &&
        candidate_window.candidate_size() > 0) {
      const int focused_row = candidate_window.focused_index() -
                              candidate_window.candidate(0).index();
      if (candidate_window.candidate_size() >= focused_row &&
          candidate_window.candidate(focused_row).has_information_id()) {
        const uint32_t delay =
            std::max(static_cast<uint32_t>(0),
                     command.output().candidate_window().usages().delay());
        infolist_window_->DelayShow(delay);
      } else {
        infolist_window_->DelayHide(kHideWindowDelay);
      }
    } else {
      infolist_window_->DelayHide(kHideWindowDelay);
    }
  } else {
    // Hide infolist window immediately.
    infolist_window_->DelayHide(0);
  }

  if (cascading_visible) {
    const commands::CandidateWindow& sub_candidate_window =
        candidate_window.sub_candidate_window();

    if (candidate_changed) {
      cascading_window_->UpdateLayout(sub_candidate_window);
    }

    // Put the cascading window right to the selected row of this candidate
    // window.
    const Rect selected_row = main_window_->GetSelectionRectInScreenCord();
    const Rect selected_row_with_window_border(
        Point(main_window_rect.Left(), selected_row.Top()),
        Size(main_window_rect.Right() - main_window_rect.Left(),
             selected_row.Top() - selected_row.Bottom()));

    // We prefer the top of client area of the cascading window is
    // aligned to the top of selected candidate in the candidate window.
    const Point cascading_window_zero_point(
        0, cascading_window_->GetFirstRowInClientCord().Top());

    const Size cascading_window_size = cascading_window_->GetLayoutSize();

    // cascading window should be in the same working area as the main window.
    const Rect cascading_window_rect =
        WindowUtil::GetWindowRectForCascadingWindow(
            selected_row_with_window_border, cascading_window_size,
            cascading_window_zero_point, working_area);

    cascading_window_->SetWindowPos(
        HWND_TOPMOST, cascading_window_rect.Left(), cascading_window_rect.Top(),
        cascading_window_rect.Width(), cascading_window_rect.Height(),
        set_windows_pos_flags);
    dopagaki_cascading_window_rect_ = cascading_window_rect;
    // This trick ensures that the window is certainly shown as 'inactivated'
    // in terms of visual effect on DWM-enabled desktop.
    cascading_window_->SendMessageW(WM_NCACTIVATE, FALSE);
    if (candidate_changed) {
      main_window_->Invalidate();
      cascading_window_->Invalidate();
    }
  } else {
    // no cascading window
    if (candidate_changed) {
      main_window_->Invalidate();
    }
    cascading_window_->ShowWindow(SW_HIDE);
  }
  // Dopagaki: keep the effects above the candidate windows.
  dopagaki_overlay_->BringToTop();
}

bool WindowManager::IsAvailable() const {
  return main_window_->IsWindow() && cascading_window_->IsWindow() &&
         infolist_window_->IsWindow();
}

void WindowManager::SetSendCommandInterface(
    client::SendCommandInterface* send_command_interface) {
  main_window_->SetSendCommandInterface(send_command_interface);
  cascading_window_->SetSendCommandInterface(send_command_interface);
  infolist_window_->SetSendCommandInterface(send_command_interface);
}

void WindowManager::PreTranslateMessage(const MSG& message) {
  if (message.message != WM_MOUSEMOVE) {
    return;
  }

  // Window manager sometimes generates WM_MOUSEMOVE message when the contents
  // under the mouse cursor has been changed (e.g. the window is moved) so that
  // the mouse handler can update its cursor image based on the contents to
  // which the cursor is newly pointing.
  // See http://blogs.msdn.com/b/oldnewthing/archive/2003/10/01/55108.aspx for
  // details about such kind of phantom WM_MOUSEMOVE.  See also b/3104996.
  // Here we compares the screen coordinate of the mouse cursor with the last
  // one to determine if this WM_MOUSEMOVE is an artificial one or not.
  // If the coordinate is the same, this is an artificial WM_MOUSEMOVE.
  bool is_moving = true;
  const CPoint cursor_pos_in_client_coords(GET_X_LPARAM(message.lParam),
                                           GET_Y_LPARAM(message.lParam));
  CPoint cursor_pos_in_logical_coords;
  if (layout_manager_->ClientPointToScreen(message.hwnd,
                                           cursor_pos_in_client_coords,
                                           &cursor_pos_in_logical_coords)) {
    // Since the renderer process is DPI-aware, we can safely use this
    // (logical) coordinates as if it is real (physical) screen coordinates.
    if (cursor_pos_in_logical_coords == last_position_) {
      is_moving = false;
    }
    last_position_ = cursor_pos_in_logical_coords;
  }

  // Notify candidate windows if the cursor is moving or not so that they can
  // filter unnecessary WM_MOUSEMOVE events.
  main_window_->set_mouse_moving(is_moving);
  cascading_window_->set_mouse_moving(is_moving);
}

void WindowManager::OnDopagakiEvent(const commands::DopagakiEvent& event) {
  if (event.has_settings()) {
    ApplyDopagakiSettings(event.settings());
  }
  // The counters are followed even while the effects are off, so that the
  // keystrokes typed meanwhile do not appear at once when turned on again.
  const std::vector<DopagakiAction> actions =
      dopagaki_state_->OnEvent(event, absl::Now());
  if (!dopagaki_enabled_) {
    // K6: no effects and no sounds. The combo starts from 0 when turned on.
    dopagaki_state_->ResetCombo();
    return;
  }
  if (actions.empty()) {
    return;
  }

  // Sound (phase 5): one sound per event. A keystroke that raised the rank
  // plays the rank-up sound instead of the key sound. BREAK also plays when
  // the combo was already 0.
  DopagakiSoundKind sound_kind = DopagakiSoundKind::kKey;
  for (const DopagakiAction& action : actions) {
    if (action.type == DopagakiAction::Type::kRankUp) {
      sound_kind = DopagakiSoundKind::kRankUp;
      break;
    }
    if (action.type == DopagakiAction::Type::kBreak) {
      sound_kind = DopagakiSoundKind::kBreak;
    }
  }
  RequestDopagakiSound(event.receiver_window_handle(), sound_kind);

  const HWND target_window =
      WinUtil::DecodeWindowHandle(event.target_window_handle());
  if (!dopagaki_overlay_->Prepare(target_window)) {
    // The overlay is not available. Only the combo is counted.
    return;
  }
  DopagakiEffects& effects = dopagaki_overlay_->effects();

  // Particles come from the top-right corner of the candidate window, or from
  // the combo label near the caret (K5) when there is no candidate window.
  D2D1_POINT_2F origin = {};
  std::optional<D2D1_POINT_2F> label_position;
  const std::optional<DopagakiRect> candidate_rect =
      GetDopagakiCandidateWindowRect();
  effects.SetCandidateWindow(candidate_rect);
  if (candidate_rect.has_value()) {
    // right - 10, top + 4 of .cand-wrap (3px around the window).
    origin = D2D1::Point2F(candidate_rect->right + 3 - 10,
                           candidate_rect->top - 3 + 4);
  } else {
    label_position = GetDopagakiComboLabelPosition(target_window);
    origin = D2D1::Point2F(label_position->x + 40, label_position->y + 12);
  }
  // Update 1: praise words keep away from these (1.6).
  std::vector<DopagakiBox> praise_avoid;
  if (std::any_of(actions.begin(), actions.end(),
                  [](const DopagakiAction& action) {
                    return action.type == DopagakiAction::Type::kPraise;
                  })) {
    praise_avoid = GetDopagakiPraiseAvoidBoxes(target_window, candidate_rect);
  }

  for (const DopagakiAction& action : actions) {
    switch (action.type) {
      case DopagakiAction::Type::kKeys:
        // Keystrokes merged into one event get up to 3 bursts.
        for (uint64_t i = 0; i < std::min<uint64_t>(action.count, 3); ++i) {
          effects.OnKey(action.rank, origin.x, origin.y);
        }
        break;
      case DopagakiAction::Type::kRankUp:
        effects.OnRankUp(action.rank);
        break;
      case DopagakiAction::Type::kBreak:
        // With combo 0, only the sound is played (phase 5).
        if (action.count > 0) {
          effects.OnBreak(origin.x, origin.y);
        }
        break;
      case DopagakiAction::Type::kMilestone:
        // Update 1: the big banner of 200, 300, ... comes in phase U3.
        break;
      case DopagakiAction::Type::kPraise:
        // Update 1. After kRankUp, so that the new banner is kept free. If
        // it fails, only this word is not shown.
        dopagaki_overlay_->AddPraise(action.rank, praise_avoid);
        break;
    }
  }
  const DopagakiRank rank = dopagaki_state_->rank();
  // The edge glow follows the current rank (phase 3).
  effects.SetEdgeRank(rank);
  // "x42" on the candidate window pops on every keystroke (phase 4).
  effects.SetBadge(dopagaki_state_->combo(), rank, /*pop=*/true);
  main_window_->SetDopagakiRank(rank, effects.hue());
  cascading_window_->SetDopagakiRank(rank, effects.hue());
  if (label_position.has_value() && dopagaki_state_->combo() > 0) {
    effects.ShowComboLabel(dopagaki_state_->combo(), rank, label_position->x,
                           label_position->y);
  } else {
    // With the candidate window, "x42" shows the combo instead.
    effects.HideComboLabel();
  }
  dopagaki_overlay_->StartAnimation();
}

std::vector<DopagakiBox> WindowManager::GetDopagakiPraiseAvoidBoxes(
    HWND target_window,
    const std::optional<DopagakiRect>& candidate_rect) const {
  std::vector<DopagakiBox> boxes;
  if (candidate_rect.has_value()) {
    boxes.push_back(DopagakiBox{candidate_rect->left, candidate_rect->top,
                                candidate_rect->right, candidate_rect->bottom}
                        .Inflated(30));
  }
  RECT caret = {};
  if (GetDopagakiCaretRect(target_window, &caret)) {
    const D2D1_POINT_2F top_left =
        dopagaki_overlay_->ScreenToOverlay({caret.left, caret.top});
    const D2D1_POINT_2F bottom_right =
        dopagaki_overlay_->ScreenToOverlay({caret.right, caret.bottom});
    boxes.push_back(
        DopagakiBox{top_left.x, top_left.y, bottom_right.x, bottom_right.y}
            .Inflated(60));
  } else {
    // Where "29 COMBO A" is shown.
    const D2D1_POINT_2F label = GetDopagakiComboLabelPosition(target_window);
    boxes.push_back(
        DopagakiBox{label.x, label.y, label.x + 200, label.y + 50}.Inflated(
            30));
  }
  return boxes;
}

void WindowManager::ApplyDopagakiSettings(
    const config::Config::DopagakiConfig& settings) {
  dopagaki_sound_set_ = settings.sound_set();
  const bool was_enabled = dopagaki_enabled_;
  dopagaki_enabled_ = settings.enabled();
  if (was_enabled && !dopagaki_enabled_) {
    // Turned off: everything on the screen disappears at once, and the
    // candidate window goes back to the colors of rank B.
    dopagaki_state_->ResetCombo();
    dopagaki_overlay_->Stop();
    main_window_->SetDopagakiRank(DopagakiRank::kB, 0);
    cascading_window_->SetDopagakiRank(DopagakiRank::kB, 0);
  }
  const std::wstring message =
      L"[dopagaki] settings: enabled=" +
      std::to_wstring(dopagaki_enabled_ ? 1 : 0) + L" sound_set=" +
      std::to_wstring(static_cast<int>(dopagaki_sound_set_)) + L"\n";
  ::OutputDebugStringW(message.c_str());
}

void WindowManager::RequestDopagakiSound(uint32_t receiver_window_handle,
                                         DopagakiSoundKind kind) {
  if (dopagaki_sound_set_ == config::Config::DopagakiConfig::MUTE) {
    return;
  }
  const HWND receiver = WinUtil::DecodeWindowHandle(receiver_window_handle);
  if (receiver == nullptr) {
    return;
  }
  // The same message as the renderer callback (e.g. clicking a candidate),
  // which the TIP accepts from this sandboxed process.
  static const UINT message =
      ::RegisterWindowMessageW(kMessageReceiverMessageName);
  if (message == 0) {
    return;
  }
  ::PostMessageW(receiver, message, kDopagakiSoundRequest,
                 EncodeDopagakiSoundRequest(
                     dopagaki_sound_set_, kind,
                     static_cast<int>(dopagaki_state_->rank())));
}

std::optional<DopagakiRect> WindowManager::GetDopagakiCandidateWindowRect()
    const {
  if (!main_window_->IsWindow() || !main_window_->IsWindowVisible() ||
      !dopagaki_main_window_rect_.has_value()) {
    return std::nullopt;
  }
  // The position without shaking. The overlay shakes the frame by itself.
  const Rect& rect = *dopagaki_main_window_rect_;
  const D2D1_POINT_2F top_left =
      dopagaki_overlay_->ScreenToOverlay({rect.Left(), rect.Top()});
  const D2D1_POINT_2F bottom_right =
      dopagaki_overlay_->ScreenToOverlay({rect.Right(), rect.Bottom()});
  // Windows 11 rounds the corners with a radius of 8 DIPs.
  return DopagakiRect{
      .left = top_left.x,
      .top = top_left.y,
      .right = bottom_right.x,
      .bottom = bottom_right.y,
      .corner_radius = main_window_->dopagaki_rounded_corners() ? 8.0f : 0.0f};
}

void WindowManager::OnDopagakiFrame() {
  DopagakiEffects& effects = dopagaki_overlay_->effects();
  effects.SetCandidateWindow(GetDopagakiCandidateWindowRect());

  // Shake the candidate windows together with the effects. Only our own
  // windows are moved.
  const float pixels_per_dip = dopagaki_overlay_->pixels_per_dip();
  const POINT offset = {std::lround(effects.shake_x() * pixels_per_dip),
                        std::lround(effects.shake_y() * pixels_per_dip)};
  if (offset.x != dopagaki_shake_offset_.x ||
      offset.y != dopagaki_shake_offset_.y) {
    constexpr UINT kFlags = SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE;
    if (dopagaki_main_window_rect_.has_value() && main_window_->IsWindow() &&
        main_window_->IsWindowVisible()) {
      main_window_->SetWindowPos(
          nullptr, dopagaki_main_window_rect_->Left() + offset.x,
          dopagaki_main_window_rect_->Top() + offset.y, 0, 0, kFlags);
    }
    if (dopagaki_cascading_window_rect_.has_value() &&
        cascading_window_->IsWindow() && cascading_window_->IsWindowVisible()) {
      cascading_window_->SetWindowPos(
          nullptr, dopagaki_cascading_window_rect_->Left() + offset.x,
          dopagaki_cascading_window_rect_->Top() + offset.y, 0, 0, kFlags);
    }
    dopagaki_shake_offset_ = offset;
  }

  // The selected row of SSS follows the turning hue at about 30 fps.
  if (dopagaki_state_->rank() == DopagakiRank::kSSS &&
      (++dopagaki_frame_count_ % 2) == 0) {
    main_window_->SetDopagakiRank(DopagakiRank::kSSS, effects.hue());
    cascading_window_->SetDopagakiRank(DopagakiRank::kSSS, effects.hue());
  }
}

D2D1_POINT_2F WindowManager::GetDopagakiComboLabelPosition(
    HWND target_window) const {
  const DopagakiEffects& effects = dopagaki_overlay_->effects();
  D2D1_POINT_2F position = {};
  RECT caret = {};
  if (GetDopagakiCaretRect(target_window, &caret)) {
    // A little above and to the right of the caret.
    const D2D1_POINT_2F point =
        dopagaki_overlay_->ScreenToOverlay({caret.right, caret.top});
    position = D2D1::Point2F(point.x + 12, point.y - 34);
  } else {
    // The application does not tell the caret position. Use the bottom-right
    // area of the window being typed in.
    HWND root = target_window != nullptr
                    ? ::GetAncestor(target_window, GA_ROOT)
                    : ::GetForegroundWindow();
    RECT rect = {};
    if (root != nullptr && ::GetWindowRect(root, &rect)) {
      const D2D1_POINT_2F point =
          dopagaki_overlay_->ScreenToOverlay({rect.right, rect.bottom});
      position = D2D1::Point2F(point.x - 240, point.y - 80);
    } else {
      position =
          D2D1::Point2F(effects.width() - 260, effects.height() - 100);
    }
  }
  position.x = std::clamp(position.x, 8.0f, std::max(8.0f, effects.width() - 200));
  position.y =
      std::clamp(position.y, 8.0f, std::max(8.0f, effects.height() - 50));
  return position;
}

}  // namespace win32
}  // namespace renderer
}  // namespace mozc
