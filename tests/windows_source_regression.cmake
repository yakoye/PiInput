if(NOT DEFINED PIINPUT_SOURCE_DIR)
    message(FATAL_ERROR "PIINPUT_SOURCE_DIR was not provided")
endif()

file(READ "${PIINPUT_SOURCE_DIR}/CMakeLists.txt" cmake_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/profile_tool.cpp" profile_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/profile_registration.h" registration_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/piinput_tsf_guids.h" guid_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/installer/main.cpp" installer_main_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/installer/stable_runtime.cpp" stable_runtime_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/preview/main.cpp" preview_text)
file(READ "${PIINPUT_SOURCE_DIR}/scripts/windows/package-release.ps1" package_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/dllmain.cpp" dllmain_text)
file(READ "${PIINPUT_SOURCE_DIR}/scripts/dev/repair-registration.ps1" repair_text)
file(READ "${PIINPUT_SOURCE_DIR}/scripts/dev/refresh-installed-dev.ps1" refresh_text)
file(READ "${PIINPUT_SOURCE_DIR}/scripts/dev/uninstall-dev.ps1" uninstall_text)
file(READ "${PIINPUT_SOURCE_DIR}/scripts/windows/resolve-installed-dev.ps1" resolver_text)
file(READ "${PIINPUT_SOURCE_DIR}/scripts/dev/install-dev.ps1" install_text)
file(READ "${PIINPUT_SOURCE_DIR}/scripts/dev/set-candidate-page-size.ps1" candidate_settings_script_text)
file(READ "${PIINPUT_SOURCE_DIR}/scripts/dev/setup-dev.ps1" setup_text)
# The text service that actually ships. These assertions used to read a copy
# left behind when the implementation moved into the stable shim -- it
# compiled into nothing, so they guarded a file no user could run. The ones
# that only described the old in-DLL engine were removed with it: that work
# lives in the Host now and is covered by the Host tests.
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/stable_text_service.cpp" text_service_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/stable_text_service.h" text_service_header_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/candidate_window.cpp" candidate_window_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/candidate_window.h" candidate_window_header_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/host/pipe_server.cpp" host_pipe_server_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/host/candidate_presenter.cpp" candidate_presenter_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/host/main.cpp" host_main_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/diagnostics/main.cpp" diagnostics_main_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/settings/main.cpp" settings_main_text)

if(NOT diagnostics_main_text MATCHES "CoInitializeEx" OR
   NOT diagnostics_main_text MATCHES "CoUninitialize")
    message(FATAL_ERROR
        "Diagnostics must initialize COM before querying the live TSF profile")
endif()
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/shim_pipe_transport.cpp" shim_transport_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/shim_connection_policy.h" shim_connection_policy_text)
string(FIND "${shim_transport_text}" "5000ULL" shim_legacy_five_second_wait)
string(FIND "${shim_connection_policy_text}" "ShimConnectionPolicy" shim_connection_policy_type)
string(FIND "${shim_transport_text}" "connection_policy_.plan_after_exchange_failure" shim_connection_plan)
string(FIND "${shim_transport_text}" "attempt.wait_budget_ms" shim_connection_budget)
string(FIND "${shim_transport_text}" "if (!start_host()) return std::nullopt" shim_launch_fail_fast)
if(NOT shim_legacy_five_second_wait EQUAL -1 OR
   shim_connection_policy_type EQUAL -1 OR
   shim_connection_plan EQUAL -1 OR
   shim_connection_budget EQUAL -1 OR
   shim_launch_fail_fast EQUAL -1)
    message(FATAL_ERROR
        "Stable TSF transport must use one sub-second cold-start window, fail fast when launch fails, and never stall every queued key for 5000 ms")
endif()
if(NOT EXISTS "${PIINPUT_SOURCE_DIR}/platform/windows/host/host_instance.cpp" OR
   NOT EXISTS "${PIINPUT_SOURCE_DIR}/platform/windows/host/host_instance.h")
    message(FATAL_ERROR
        "PiInput Host must acquire its cross-process singleton before loading dictionaries")
endif()
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/host/host_instance.cpp" host_instance_text)

if(NOT EXISTS "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/stable_text_service.cpp" OR
   NOT EXISTS "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/stable_text_service.h")
    message(FATAL_ERROR "Stable PiInput TSF Shim sources are missing")
endif()
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/stable_text_service.cpp" stable_text_service_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/stable_text_service.h" stable_text_service_header_text)
if(NOT EXISTS "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/shim_ui_control.h")
    message(FATAL_ERROR "Stable TSF Host-to-Shim UI control channel is missing")
endif()
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/shim_ui_control.h" shim_ui_control_text)
if(NOT EXISTS "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/composition_edit_policy.h")
    message(FATAL_ERROR "Stable TSF composition edit policy is missing")
endif()
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/composition_edit_policy.h" composition_edit_policy_text)
if(NOT stable_text_service_text MATCHES "composition_edit_policy\\(commit, cancel\\)" OR
   NOT stable_text_service_text MATCHES "policy.finalize_before_selection" OR
   NOT composition_edit_policy_text MATCHES "selection_failure_is_fatal")
    message(FATAL_ERROR
        "Stable TSF commits must finalize text before optional caret placement")
endif()
foreach(required_focus_token IN ITEMS
        "thread_manager_->GetFocus"
        "document->GetTop"
        "pending_contexts_.emplace(request.sequence, PendingContext{request.session_id, context})"
        "pipe_client_->send_focus(request, false)")
    string(FIND "${stable_text_service_text}" "${required_focus_token}" focus_token_position)
    if(focus_token_position LESS 0)
        message(FATAL_ERROR
            "Stable TSF focus recovery must bind Resume to the active context; missing ${required_focus_token}")
    endif()
endforeach()
string(FIND "${stable_text_service_text}"
    "if (!mirror_.connected())" focus_resume_only_when_disconnected)
string(FIND "${stable_text_service_text}"
    "mirror_.discard_composition()" external_termination_discard)
if(focus_resume_only_when_disconnected LESS 0 OR external_termination_discard LESS 0)
    message(FATAL_ERROR
        "Stable TSF focus recovery must not replay an already-connected composition, and external termination must discard stale raw input")
endif()
if(NOT stable_text_service_text MATCHES "terminated_range->SetText")
    message(FATAL_ERROR
        "An application-terminated composition must delete its uncommitted raw letters instead of leaving them in the document")
endif()
if(NOT shim_ui_control_text MATCHES "host_cancel_composition_message" OR
   NOT shim_ui_control_text MATCHES "FindWindowExW" OR
   NOT host_main_text MATCHES "notify_shim_cancel_composition" OR
   NOT stable_text_service_text MATCHES "cancel_from_host_ui")
    message(FATAL_ERROR
        "Dismissing the candidate context menu must cancel the matching TSF composition immediately")
endif()
string(REGEX MATCH
    "if \(!foreground_\)[^{]*\\{[^}]*release_pending_contexts"
    focus_loss_discards_requests "${stable_text_service_text}")
if(focus_loss_discards_requests)
    message(FATAL_ERROR
        "Losing foreground focus must not discard in-flight edits and create duplicate restored input")
endif()
if(NOT stable_text_service_text MATCHES "recovery = mirror_\\.complete_edit" OR
   NOT stable_text_service_text MATCHES "request_resume\\(recovery_context, \\*recovery\\)")
    message(FATAL_ERROR
        "Failed TSF commit/cancel edits must restore the Host from the trusted Composition mirror")
endif()
if(NOT stable_text_service_text MATCHES "TF_ES_ASYNC \\| TF_ES_READWRITE" OR
   NOT stable_text_service_text MATCHES "complete_deferred_edit")
    message(FATAL_ERROR
        "Stable TSF commit/cancel must fall back to a completion-aware asynchronous edit session")
endif()
if(NOT stable_text_service_text MATCHES "is_current_update" OR
   NOT stable_text_service_text MATCHES "deferred_updates_\.busy" OR
   NOT stable_text_service_text MATCHES "deferred_updates_\.defer" OR
   NOT stable_text_service_text MATCHES "deferred_updates_\.complete" OR
   NOT stable_text_service_text MATCHES "host_replay_update_message")
    message(FATAL_ERROR
        "Deferred TSF updates must reject obsolete edits and coalesce busy-window updates to the latest composition")
endif()
string(REGEX MATCH
    "reply->action == HostAction::commit[^}]*clear_deferred_updates\(\)"
    commit_clears_deferred_updates "${stable_text_service_text}")
string(REGEX MATCH
    "reply->action == HostAction::cancel[^}]*clear_deferred_updates\(\)"
    cancel_clears_deferred_updates "${stable_text_service_text}")
if(NOT commit_clears_deferred_updates OR NOT cancel_clears_deferred_updates)
    message(FATAL_ERROR
        "Commit and cancel must discard deferred composition updates before the next composition starts")
endif()
string(FIND "${stable_text_service_text}"
    "if ((commit || cancel) && request != nullptr)" final_edit_barrier_completion)
string(FIND "${stable_text_service_text}"
    "(void)deferred_updates_.begin(request);" final_edit_barrier_begin)
if(final_edit_barrier_completion LESS 0 OR final_edit_barrier_begin LESS 0)
    message(FATAL_ERROR
        "An asynchronous commit/cancel must hold later composition updates until finalization completes")
endif()
string(FIND "${stable_text_service_text}" "bind_context(context)" bind_context_position)
string(FIND "${stable_text_service_text}" "mirror_.reset_session(session_id_)" reset_context_position)
string(FIND "${stable_text_service_text}" "same_com_identity(active_context_, context)" identity_context_position)
if(bind_context_position LESS 0 OR reset_context_position LESS 0 OR identity_context_position LESS 0)
    message(FATAL_ERROR
        "Stable TSF must isolate Host/composition state when one process switches text contexts")
endif()
if(NOT stable_text_service_text MATCHES "same_com_identity" OR
   NOT stable_text_service_text MATCHES "found->second\.session_id == envelope\.session_id")
    message(FATAL_ERROR
        "Stable TSF must compare COM identities and cannot let a stale session consume a new reply slot")
endif()
if(NOT stable_text_service_text MATCHES "focused_context\(\)" OR
   NOT stable_text_service_text MATCHES "request_update_edit" OR
   NOT stable_text_service_text MATCHES "deferred_request_")
    message(FATAL_ERROR
        "Stable TSF must recover Shift context and asynchronously apply temporarily denied updates")
endif()
string(FIND "${cmake_text}" "platform/windows/tsf/stable_text_service.cpp" stable_source_position)
string(FIND "${cmake_text}" "target_link_libraries(PiInputTSF PRIVATE\n        piinput_core" legacy_core_link_position)
if(stable_source_position LESS 0 OR NOT legacy_core_link_position LESS 0)
    message(FATAL_ERROR "PiInputTSF must build the stable Shim without linking piinput_core")
endif()
foreach(forbidden_shim_token IN ITEMS
        "piinput/engine.h"
        "piinput/lexicon.h"
        "piinput/english_lexicon.h"
        "candidate_window.h"
        "load_lexicon"
        "load_engine")
    string(FIND "${stable_text_service_text}${stable_text_service_header_text}"
        "${forbidden_shim_token}" forbidden_shim_position)
    if(NOT forbidden_shim_position LESS 0)
        message(FATAL_ERROR "Stable TSF Shim contains forbidden engine/UI token: ${forbidden_shim_token}")
    endif()
endforeach()
foreach(required_host_caret_token IN ITEMS
        "HostMessageType::caret"
        "decode_host_caret_update"
        "presenter_->stage"
        "presenter_->show_at")
    string(FIND "${host_pipe_server_text}" "${required_host_caret_token}" host_caret_token_position)
    if(host_caret_token_position LESS 0)
        message(FATAL_ERROR
            "PiInput Host must stage candidates and apply same-generation caret updates; missing ${required_host_caret_token}")
    endif()
endforeach()
if(NOT candidate_presenter_text MATCHES "update\.has_text_caret" OR
   NOT candidate_presenter_text MATCHES "show_at_text_caret" OR
   NOT candidate_presenter_text MATCHES "show_near_caret")
    message(FATAL_ERROR
        "Candidate presenter must prefer TSF text geometry and retain the mouse fallback")
endif()
string(FIND "${candidate_presenter_text}" "bool CandidatePresenter::stage(" presenter_stage_start)
string(FIND "${candidate_presenter_text}" "bool CandidatePresenter::show_at(" presenter_show_at_start)
if(presenter_stage_start LESS 0 OR presenter_show_at_start LESS_EQUAL presenter_stage_start)
    message(FATAL_ERROR "Could not isolate CandidatePresenter::stage")
endif()
math(EXPR presenter_stage_length "${presenter_show_at_start} - ${presenter_stage_start}")
string(SUBSTRING "${candidate_presenter_text}" ${presenter_stage_start}
    ${presenter_stage_length} presenter_stage_text)
string(FIND "${presenter_stage_text}" "window_.hide();" presenter_stage_hide)
string(FIND "${presenter_stage_text}" "candidate_session_changed" presenter_session_reset)
if(NOT presenter_stage_hide LESS 0 AND presenter_session_reset LESS 0)
    message(FATAL_ERROR
        "Candidate snapshots may reset geometry only when the active application session changes")
endif()
if(NOT host_main_text MATCHES "SetProcessDpiAwarenessContext" OR
   NOT host_main_text MATCHES "DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2" OR
   NOT candidate_window_text MATCHES "WM_DPICHANGED" OR
   NOT candidate_window_text MATCHES "GetDpiForWindow")
    message(FATAL_ERROR
        "The out-of-process candidate Host must be per-monitor-DPI aware and rescale its window")
endif()
if(NOT stable_text_service_text MATCHES "capture_composition_caret" OR
   NOT stable_text_service_text MATCHES "send_candidate_anchor")
    message(FATAL_ERROR
        "Stable Shim must capture the TSF caret inside the successful composition edit session")
endif()
if(NOT shim_transport_text MATCHES "resolve_current_host" OR
   NOT shim_transport_text MATCHES "CREATE_NO_WINDOW \\| DETACHED_PROCESS" OR
   NOT installer_main_text MATCHES "remove_host_autostart\\(\\)")
    message(FATAL_ERROR
        "PiInput must recover its current Host, launch it invisibly, and remove legacy login startup")
endif()
string(FIND "${host_main_text}" "HostInstanceLock instance_lock" host_lock_position)
string(FIND "${host_main_text}" "runtime.load(" host_runtime_load_position)
if(host_lock_position LESS 0 OR host_runtime_load_position LESS 0 OR
   NOT host_lock_position LESS host_runtime_load_position OR
   NOT host_instance_text MATCHES "CreateMutexW")
    message(FATAL_ERROR
        "PiInput Host must win the singleton lock before loading the shared lexicon")
endif()
if(shim_legacy_five_second_wait GREATER_EQUAL 0 OR
   shim_connection_policy_type LESS 0 OR
   shim_connection_budget LESS 0)
    message(FATAL_ERROR
        "Stable Shim must use the bounded cold-start connection policy instead of the legacy five-second wait")
endif()
string(FIND "${candidate_window_text}" "move_to_target_monitor" candidate_target_move_position)
string(FIND "${candidate_window_text}" "GetDpiForWindow(window_)" candidate_target_dpi_position)
string(FIND "${candidate_window_text}" "anchor_gap" candidate_anchor_gap_position)
if(candidate_target_move_position LESS 0 OR candidate_target_dpi_position LESS 0 OR
   candidate_anchor_gap_position LESS 0)
    message(FATAL_ERROR
        "Candidate popup must resolve target-monitor DPI before sizing and applying the caret gap")
endif()
if(NOT installer_main_text MATCHES "versioned_shim" OR
    NOT stable_runtime_text MATCHES "ERROR_SHARING_VIOLATION" OR
    NOT installer_main_text MATCHES "can_reuse_registered_stable_shim" OR
    installer_main_text MATCHES "files_are_identical\\(packaged_shim, previous_dll\\)")
    message(FATAL_ERROR
        "The permanent COM entry must prefer Runtime/Shim even when an old versioned DLL has identical bytes")
endif()
if(NOT stable_text_service_header_text MATCHES "ShiftToggleState" OR
   NOT stable_text_service_text MATCHES "shift_toggle_\.on_shift_down" OR
   NOT stable_text_service_text MATCHES "shift_toggle_\.on_shift_up")
    message(FATAL_ERROR "Stable Shim must use the tested standalone-Shift state machine")
endif()
string(FIND "${stable_text_service_text}" "if (is_ascii_letter(wparam))" shim_letter_gate_position)
string(FIND "${stable_text_service_text}"
    "if (!mirror_.connected() && mirror_.raw().empty() && pending_contexts_.empty())"
    shim_disconnected_gate_position)
if(shim_letter_gate_position LESS 0 OR shim_disconnected_gate_position LESS 0 OR
   NOT shim_letter_gate_position LESS shim_disconnected_gate_position)
    message(FATAL_ERROR
        "Stable Shim must queue the first Chinese letter while the resident Host is becoming ready")
endif()
if(NOT stable_text_service_text MATCHES "HostKeyKind::previous_candidate" OR
   NOT stable_text_service_text MATCHES "HostKeyKind::next_candidate")
    message(FATAL_ERROR "Stable Shim must expose left/right candidate navigation")
endif()

string(FIND "${text_service_text}" "STDMETHODIMP TextService::Activate" activate_start)
string(FIND "${text_service_text}" "STDMETHODIMP TextService::Deactivate" deactivate_start)
if(activate_start LESS 0 OR deactivate_start LESS 0 OR deactivate_start LESS_EQUAL activate_start)
    message(FATAL_ERROR "Could not isolate the TSF Activate implementation")
endif()

if(NOT preview_text MATCHES "PiInput 输入测试台" OR
   NOT preview_text MATCHES "package_root = module_directory")
    message(FATAL_ERROR "Standalone PiInput test window must work directly from the package root")
endif()
if(NOT preview_text MATCHES "piinput/english_session\.h" OR
   NOT preview_text MATCHES "EnglishLexicon" OR
   NOT preview_text MATCHES "EnglishSession" OR
   NOT preview_text MATCHES "中文候选" OR
   NOT preview_text MATCHES "英文候选")
    message(FATAL_ERROR "Standalone PiInput test window must expose both Chinese and English candidate modes")
endif()
foreach(required_preview_english_resource IN ITEMS
        "english_lexicon.tsv"
        "english_supplement.tsv"
        "english_completion_preferences.tsv"
        "english_downloaded.tsv"
        "english_user.tsv"
        "english_learning.tsv")
    if(NOT preview_text MATCHES "${required_preview_english_resource}")
        message(FATAL_ERROR
            "Standalone PiInput test window must load ${required_preview_english_resource}")
    endif()
endforeach()
if(NOT preview_text MATCHES "ES_MULTILINE" OR
   NOT preview_text MATCHES "EM_REPLACESEL" OR
   NOT preview_text MATCHES "测试文本")
    message(FATAL_ERROR
        "Standalone PiInput test window must provide a freely editable multiline test document")
endif()
string(FIND "${preview_text}" "find_packaged_data_file(L\"symbols.tsv\")" preview_symbol_lookup)
if(preview_symbol_lookup LESS 0)
    message(FATAL_ERROR "Standalone PiInput test window must resolve symbols.tsv from either installed or package-root layout")
endif()
if(NOT package_text MATCHES "PiInput-Test.exe")
    message(FATAL_ERROR "Release package must provide a one-click standalone PiInput-Test.exe")
endif()
if(NOT package_text MATCHES "PiInput-Uninstall.exe")
    message(FATAL_ERROR "Release package must provide PiInput-Uninstall.exe at the package root")
endif()
if(NOT package_text MATCHES "PiInputHost.exe" OR
   NOT package_text MATCHES "PiInput-Settings.exe" OR
   NOT package_text MATCHES "piinput-diagnostics.exe" OR
   NOT package_text MATCHES "host_protocol.json" OR
   NOT package_text MATCHES "稳定入口与无重启升级说明.md")
    message(FATAL_ERROR "Release package must include the Host, Settings, diagnostics, protocol metadata, and stable-upgrade guide")
endif()
if(NOT stable_text_service_text MATCHES "event.resume = mirror_.resume_state\(\)" OR
   NOT stable_text_service_text MATCHES "HostKeyKind::select_digit" OR
   NOT stable_text_service_text MATCHES "HostKeyKind::space")
    message(FATAL_ERROR "Stable Shim must carry resume state and resolve Space/digits in the current Host")
endif()
foreach(required_caret_token IN ITEMS
        "GetSelection"
        "Collapse(edit_cookie, TF_ANCHOR_END)"
        "GetActiveView"
        "GetTextExt"
        "send_caret")
    string(FIND "${stable_text_service_text}" "${required_caret_token}" caret_token_position)
    if(caret_token_position LESS 0)
        message(FATAL_ERROR
            "Stable TSF Shim must query and send the text insertion caret; missing ${required_caret_token}")
    endif()
endforeach()
string(FIND "${stable_text_service_text}" "void TextService::capture_composition_caret" stable_caret_start)
string(FIND "${stable_text_service_text}" "HRESULT TextService::apply_composition_edit" stable_apply_start)
if(stable_caret_start LESS 0 OR stable_apply_start LESS 0 OR
   stable_apply_start LESS_EQUAL stable_caret_start)
    message(FATAL_ERROR "Could not isolate the stable TSF caret capture implementation")
endif()
math(EXPR stable_caret_length "${stable_apply_start} - ${stable_caret_start}")
string(SUBSTRING "${stable_text_service_text}" ${stable_caret_start}
    ${stable_caret_length} stable_caret_text)
if(NOT stable_caret_text MATCHES "context->GetSelection" OR
   NOT stable_caret_text MATCHES "composition_->GetRange\\(&composition_range\\)" OR
   NOT stable_caret_text MATCHES "choose_text_caret_geometry" OR
   NOT stable_caret_text MATCHES "GetWindowDpiAwarenessContext\\(view_window\\)" OR
   NOT stable_caret_text MATCHES "DPI_AWARENESS_PER_MONITOR_AWARE" OR
   NOT stable_caret_text MATCHES "normalized_text_caret_geometry" OR
   NOT stable_caret_text MATCHES "LogicalToPhysicalPointForPerMonitorDPI")
    message(FATAL_ERROR
        "Stable TSF caret capture must prefer the insertion selection, retain the Composition fallback, and use the target view window DPI context so a TSF callback thread cannot double-scale per-monitor-aware client coordinates")
endif()
# The authoritative anchor must still come from inside the successful
# composition edit session -- capturing it from an unrelated read-only session
# was the v0.4.2 bug. A read-only session is allowed for exactly one purpose:
# probing where the insertion point is BEFORE a word opens, so the bar does not
# appear at the previous word's position. That probe never replaces the
# in-session capture.
string(FIND "${stable_text_service_text}"
    "if (SUCCEEDED(result) && capture != nullptr && !commit_ && !cancel_) {" in_session_capture)
if(in_session_capture LESS 0)
    message(FATAL_ERROR
        "Stable TSF Shim must capture the caret inside the successful composition edit session")
endif()
# The probe is worthless unless the Host actually keeps it. A caret the staged
# snapshot cannot use must still be remembered as the next word's anchor.
# A composition range is only ever safe to overwrite when it still holds what
# this service wrote. Applications that rebuild their document on paste hand
# back a range spanning the user's own text, and SetText would replace it.
string(FIND "${stable_text_service_text}"
    "if (!range_text_equals(range, edit_cookie, composition_written_))" composition_write_guard)
if(composition_write_guard LESS 0)
    message(FATAL_ERROR
        "The composition range must be verified against composition_written_ before SetText overwrites it")
endif()

# The anchor a word opens on is a guess. Locking on it would stop the
# authoritative caret from ever correcting the candidate bar's position.
string(FIND "${candidate_presenter_text}"
    "anchor_locked_ = !current_.raw.empty() && !provisional" authoritative_anchor_lock)
string(FIND "${candidate_presenter_text}"
    "caret_ = remembered_caret_" guessed_anchor)
# The window keeps its geometry locked until something releases it. A word that
# opens must release it explicitly rather than depending on hide() having run.
string(FIND "${candidate_presenter_text}"
    "if (model_.word_just_opened()) window_.release_anchor()" explicit_anchor_release)
string(FIND "${candidate_window_text}" "void CandidateWindow::release_anchor()" release_anchor_impl)
if(explicit_anchor_release LESS 0 OR release_anchor_impl LESS 0)
    message(FATAL_ERROR
        "A word that opens must release the candidate window anchor explicitly")
endif()
if(authoritative_anchor_lock LESS 0)
    message(FATAL_ERROR
        "Only an authoritative caret may lock the candidate anchor")
endif()
if(NOT guessed_anchor LESS 0)
    message(FATAL_ERROR
        "A word must never open the candidate bar on a remembered caret; that is what put the bar a step behind the insertion point")
endif()

# Store-packaged applications run in an AppContainer and cannot load a Shim
# that does not grant read+execute to ALL APPLICATION PACKAGES. Without it the
# input method is simply unavailable in those applications.
string(FIND "${stable_runtime_text}" "S-1-15-2-1" app_container_sid)
string(FIND "${stable_runtime_text}" "grant_app_container_read(stable_shim)" app_container_grant)
if(app_container_sid LESS 0 OR app_container_grant LESS 0)
    message(FATAL_ERROR
        "The installer must grant ALL APPLICATION PACKAGES read+execute on the stable Shim so packaged applications can load it")
endif()

# Store-sandboxed applications run in an AppContainer. Their token carries the
# user SID like any other, but it is refused unless the DACL also names an
# application-package SID, so the pipe must grant AC or the input method is dead
# in those applications and the raw letters fall through.
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/host/pipe_security.cpp" pipe_security_text)
string(FIND "${pipe_security_text}" "(A;;GA;;;AC)" pipe_app_container_ace)
if(pipe_app_container_ace LESS 0)
    message(FATAL_ERROR
        "The Host pipe must grant ALL APPLICATION PACKAGES so sandboxed applications can reach the input method")
endif()

if(NOT host_pipe_server_text MATCHES "presenter_->remember_caret")
    message(FATAL_ERROR
        "Host must remember a caret update the staged snapshot rejects, so the pre-key probe anchors the next word")
endif()

string(FIND "${stable_text_service_text}" "TF_ES_SYNC | TF_ES_READ," readonly_caret_session)
if(NOT readonly_caret_session LESS 0)
    message(FATAL_ERROR
        "Stable TSF Shim must capture the caret inside the successful composition edit session, never from a separate read-only session on the key path")
endif()
math(EXPR activate_length "${deactivate_start} - ${activate_start}")
string(SUBSTRING "${text_service_text}" ${activate_start} ${activate_length} activate_text)

string(FIND "${text_service_text}" "STDMETHODIMP TextService::OnSetFocus" focus_start)
string(FIND "${text_service_text}" "STDMETHODIMP TextService::OnTestKeyDown" test_key_start)
if(focus_start LESS 0 OR test_key_start LESS 0 OR test_key_start LESS_EQUAL focus_start)
    message(FATAL_ERROR "Could not isolate the TSF focus implementation")
endif()
math(EXPR focus_length "${test_key_start} - ${focus_start}")
string(SUBSTRING "${text_service_text}" ${focus_start} ${focus_length} focus_text)

string(FIND "${text_service_text}" "STDMETHODIMP TextService::OnKeyDown" key_down_start)
string(FIND "${text_service_text}" "STDMETHODIMP TextService::OnTestKeyUp" test_key_up_start)
if(key_down_start LESS 0 OR test_key_up_start LESS 0 OR
   key_down_start LESS_EQUAL test_key_start OR test_key_up_start LESS_EQUAL key_down_start)
    message(FATAL_ERROR "Could not isolate the TSF key-down probe and formal callbacks")
endif()
math(EXPR test_key_down_length "${key_down_start} - ${test_key_start}")
string(SUBSTRING "${text_service_text}" ${test_key_start} ${test_key_down_length} test_key_down_text)
foreach(forbidden_probe_call IN ITEMS
        "shift_toggle_"
        "ensure_engine_loaded_for_key"
        "navigate_chinese_rows"
        "candidate_grid_.move_row"
        "handle_key")
    string(FIND "${test_key_down_text}" "${forbidden_probe_call}" forbidden_probe_position)
    if(NOT forbidden_probe_position LESS 0)
        message(FATAL_ERROR
            "OnTestKeyDown must be side-effect free; found ${forbidden_probe_call}")
    endif()
endforeach()

if(activate_text MATCHES "load_engine\\(" OR focus_text MATCHES "load_engine\\(")
    message(FATAL_ERROR "TSF activation and focus must never synchronously materialize the full dictionary")
endif()
if(NOT EXISTS "${PIINPUT_SOURCE_DIR}/platform/windows/installer/main.cpp")
    message(FATAL_ERROR "Native PiInput installer source is missing")
endif()
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/installer/main.cpp" installer_text)
file(READ "${PIINPUT_SOURCE_DIR}/platform/windows/installer/install_layout.cpp" installer_layout_text)

foreach(settings_writer IN ITEMS candidate_settings_script_text install_text installer_text)
    if("${${settings_writer}}" MATCHES
        "single_syllable_page_size=[0-9]|phrase_page_size=[0-9]")
        message(FATAL_ERROR "Runtime settings writers must not emit obsolete paging keys")
    endif()
endforeach()
if(NOT candidate_settings_script_text MATCHES "ItemsPerRow" OR
   NOT candidate_settings_script_text MATCHES "VisibleRows" OR
   NOT candidate_settings_script_text MATCHES "MaxItems" OR
   NOT candidate_settings_script_text MATCHES "\\[candidates\\]")
    message(FATAL_ERROR "Candidate settings script must write the modern candidate grid section")
endif()
if(candidate_settings_script_text MATCHES "Set-Content[^\r\n]*-Encoding ASCII" OR
   install_text MATCHES "Set-Content[^\r\n]*CandidateSettings[^\r\n]*-Encoding ASCII")
    message(FATAL_ERROR "Settings writers must preserve non-ASCII INI content with UTF-8")
endif()
if(NOT candidate_settings_script_text MATCHES "Get-Content[^\r\n]*-Encoding UTF8" OR
   NOT candidate_settings_script_text MATCHES "Set-Content[^\r\n]*-Encoding UTF8" OR
   NOT install_text MATCHES "Get-Content[^\r\n]*CandidateSettings[^\r\n]*-Encoding UTF8" OR
   NOT install_text MATCHES "Set-Content[^\r\n]*CandidateSettings[^\r\n]*-Encoding UTF8")
    message(FATAL_ERROR "Settings writers must explicitly read and write UTF-8")
endif()
if(NOT installer_text MATCHES "\\[general\\]" OR
   NOT installer_text MATCHES "\\[candidates\\]" OR
   NOT installer_text MATCHES "items_per_row=6" OR
   NOT installer_text MATCHES "visible_rows=5" OR
   NOT installer_text MATCHES "max_items=90")
    message(FATAL_ERROR "Native installer defaults must use the modern settings format")
endif()
if(NOT install_text MATCHES "\\[candidates\\]" OR
   NOT install_text MATCHES "items_per_row" OR
   NOT install_text MATCHES "visible_rows" OR
   NOT install_text MATCHES "max_items")
    message(FATAL_ERROR "Developer installer must ensure modern candidate defaults")
endif()
if(NOT profile_text MATCHES "\\[general\\]")
    message(FATAL_ERROR "Profile schema writer must use the modern [general] section")
endif()

if(cmake_text MATCHES "target_link_libraries\\(PiInputTSF[^\\)]*msctf")
    message(FATAL_ERROR "PiInputTSF must not link a non-existent msctf import library")
endif()

if(cmake_text MATCHES "target_link_libraries\\(piinput-profile[^\\)]*msctf")
    message(FATAL_ERROR "piinput-profile must not link a non-existent msctf import library")
endif()

if(profile_text MATCHES "CLSID_TF_InputProcessorProfileMgr")
    message(FATAL_ERROR "CLSID_TF_InputProcessorProfileMgr is not a Windows SDK COM class")
endif()

if(NOT registration_text MATCHES "CLSID_TF_InputProcessorProfiles")
    message(FATAL_ERROR "Profile manager creation must use CLSID_TF_InputProcessorProfiles")
endif()

if(NOT registration_text MATCHES "QueryInterface\\(IID_PPV_ARGS\\(manager\\)\\)")
    message(FATAL_ERROR "Profile manager should be obtained by querying CLSID_TF_InputProcessorProfiles")
endif()

if(NOT registration_text MATCHES "RegisterProfile\\(")
    message(FATAL_ERROR "Modern TSF registration must use ITfInputProcessorProfileMgr::RegisterProfile")
endif()

if(NOT EXISTS "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/piinput_icon.ico" OR
   NOT EXISTS "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/piinput_resources.rc" OR
   NOT EXISTS "${PIINPUT_SOURCE_DIR}/platform/windows/tsf/resource.h")
    message(FATAL_ERROR "PiInput TSF must ship its own pi icon resources")
endif()

if(NOT registration_text MATCHES "icon_file" OR
   NOT registration_text MATCHES "kPiInputIconIndex = 0U")
    message(FATAL_ERROR "RegisterProfile must receive the PiInput icon path and first icon index")
endif()

if(dllmain_text MATCHES "replace_filename\\(L\"piinput_icon\\.ico\"\\)" OR
   NOT dllmain_text MATCHES "register_profile\\([\r\n ]*std::wstring_view\\(module_path, module_path_length\\)\\)")
    message(FATAL_ERROR "DllRegisterServer must register the branding icon embedded in PiInputTSF.dll")
endif()

if(NOT profile_text MATCHES "return .*parent_path\\(\\) /[\r\n ]*L\"PiInputTSF\\.dll\"")
    message(FATAL_ERROR "The profile tool must refresh the profile with the embedded PiInputTSF.dll icon")
endif()

if(NOT cmake_text MATCHES "platform/windows/tsf/piinput_resources\\.rc" OR
   NOT cmake_text MATCHES "install\\(FILES platform/windows/tsf/piinput_icon\\.ico DESTINATION bin\\)")
    message(FATAL_ERROR "PiInputTSF must compile and install the pi icon resource")
endif()

if(NOT registration_text MATCHES "TRUE,[\r\n ]*0U\\);")
    message(FATAL_ERROR "PiInput profile must be enabled by default and visible in Settings")
endif()

if(NOT registration_text MATCHES "accept_existing_profile_registration")
    message(FATAL_ERROR "TSF profile registration must accept an already registered profile")
endif()

if(NOT guid_text MATCHES "0x13eb305f" OR NOT guid_text MATCHES "0x4ed27b7c")
    message(FATAL_ERROR "PiInput must use the permanent stable TSF CLSID and profile GUID")
endif()

if(NOT installer_main_text MATCHES "retire_previous_tsf_identities" OR
   NOT installer_main_text MATCHES "kRetiredTextService" OR
   NOT installer_main_text MATCHES "--disable-user" OR
   NOT installer_main_text MATCHES "DllUnregisterServer")
    message(FATAL_ERROR "Installer must retire the pre-PiInput TSF identity before registering the new profile")
endif()
string(FIND "${installer_main_text}" "register_first_install(new_dll)" installer_register_position)
string(FIND "${installer_main_text}" "retire_previous_tsf_identities();" installer_retire_position)
if(installer_register_position LESS 0 OR installer_retire_position LESS 0 OR
   installer_retire_position LESS installer_register_position)
    message(FATAL_ERROR "Installer must preserve the previous TSF identity until the new profile is registered")
endif()

if(NOT profile_text MATCHES "--status")
    message(FATAL_ERROR "Profile tool must expose a machine-readable status command")
endif()

if(NOT profile_text MATCHES "result == S_FALSE")
    message(FATAL_ERROR "Idempotent deactivate/unregister must accept an absent profile")
endif()

if(NOT dllmain_text MATCHES "register_profile\\(")
    message(FATAL_ERROR "DllRegisterServer must use the shared profile registration helper")
endif()

if(NOT dllmain_text MATCHES "GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT")
    message(FATAL_ERROR "PiInput must declare Windows system-tray compatibility")
endif()

if(NOT repair_text MATCHES "Invoke-NativeBestEffort")
    message(FATAL_ERROR "Repair script must tolerate an absent or inactive previous profile")
endif()

if(NOT repair_text MATCHES "--register")
    message(FATAL_ERROR "Repair script must explicitly register the profile before activation")
endif()

if(NOT repair_text MATCHES "--status")
    message(FATAL_ERROR "Repair script must verify profile registration state")
endif()

foreach(installed_script IN ITEMS repair_text refresh_text uninstall_text)
    if(NOT "${${installed_script}}" MATCHES "resolve-installed-dev\\.ps1" OR
       NOT "${${installed_script}}" MATCHES "Resolve-PiInputInstalledDev")
        message(FATAL_ERROR "Installed-development script does not use the shared active-version resolver")
    endif()
endforeach()
if(NOT resolver_text MATCHES "current\\.json" OR
   NOT resolver_text MATCHES "CurrentHostPath" OR
   NOT resolver_text MATCHES "InprocServer32" OR
   NOT resolver_text MATCHES "Runtime" OR
   NOT resolver_text MATCHES "versions")
    message(FATAL_ERROR "Active-version resolver must validate the stable Shim and current Host under Runtime/versions")
endif()
if(refresh_text MATCHES "Stop-Process" OR NOT refresh_text MATCHES "PiInput-Install\\.exe")
    message(FATAL_ERROR "Refresh must use the side-by-side installer without force-closing applications")
endif()
if(NOT uninstall_text MATCHES "Invoke-NativeRequired" OR
   uninstall_text MATCHES "Active PiInput version could not be resolved")
    message(FATAL_ERROR "Uninstall must preserve the runtime when resolution or unregistration fails")
endif()

if(NOT install_text MATCHES "Previous profile deactivation")
    message(FATAL_ERROR "Installer must treat previous profile cleanup as best effort")
endif()

if(text_service_text MATCHES "kPageSize = 5")
    message(FATAL_ERROR "TSF candidate paging must not return to the fixed five-item layout")
endif()

if(NOT text_service_text MATCHES "VK_OEM_MINUS" OR NOT text_service_text MATCHES "VK_OEM_PLUS")
    message(FATAL_ERROR "TSF must support minus/equal candidate paging")
endif()

if(NOT text_service_text MATCHES "toggle_input_mode")
    message(FATAL_ERROR "TSF must support standalone Shift input-mode switching")
endif()

if(NOT candidate_window_text MATCHES "const int column" OR NOT candidate_window_text MATCHES "item_width")
    message(FATAL_ERROR "Candidate window must use horizontal column layout")
endif()






if(text_service_header_text MATCHES "page_start_" OR
   text_service_text MATCHES "move_candidate_page" OR
   text_service_text MATCHES "current_page_size")
    message(FATAL_ERROR "TSF must not retain the legacy candidate page state")
endif()
if(NOT cmake_text MATCHES "data/english_supplement\\.tsv")
    message(FATAL_ERROR "The technical English supplement must be installed with PiInput")
endif()
# The loading half of this moved to the Host with the engine; what stays
# checkable here is that the file is still installed for it to load.
if(NOT cmake_text MATCHES "data/english_completion_preferences\\.tsv")
    message(FATAL_ERROR "Curated English prefix preferences must still be installed")
endif()





if(text_service_text MATCHES "if \\(session_ == nullptr \\|\\| !foreground_ \\|\\|")
    message(FATAL_ERROR "Candidate refresh must not hide an active English-only Composition")
endif()

# The shim assigns this to a differently named local; what matters is that the
# initial selection is collapsed and the result checked before proceeding.
if(NOT text_service_text MATCHES "selection\\.range->Collapse")
    message(FATAL_ERROR "TSF must stop when the initial selection cannot be collapsed safely")
endif()
if(NOT candidate_window_header_text MATCHES "items_per_row" OR
   NOT candidate_window_header_text MATCHES "visible_rows" OR
   NOT candidate_window_text MATCHES "actual_visible_rows")
    message(FATAL_ERROR "Candidate window must render the configured multi-row grid")
endif()
if(NOT candidate_window_text MATCHES "fit_candidate_column_widths" OR
   NOT candidate_window_text MATCHES "available_width = \\(std::max\\)\\(1L" OR
   NOT candidate_window_text MATCHES "available_height = \\(std::max\\)\\(1L")
    message(FATAL_ERROR "Candidate window must keep narrow-work-area geometry non-negative")
endif()
if(NOT candidate_window_text MATCHES "composition_ == composition && candidates_ == candidates" OR
   NOT candidate_window_text MATCHES "InvalidateRect\\(window_, nullptr, FALSE\\)")
    message(FATAL_ERROR
        "Candidate window must skip identical redraws and update changed content without background erase")
endif()
string(FIND "${candidate_window_text}" "WS_POPUP | WS_BORDER" candidate_system_border)
string(FIND "${candidate_window_text}" "RoundRect" candidate_rounded_selection)
string(FIND "${candidate_window_text}" "RGB(241, 235, 255)" candidate_purple_selection)
string(FIND "${candidate_window_text}" "expand_candidates" candidate_expand_button)
string(FIND "${candidate_window_text}" "CreateRoundRectRgn" candidate_rounded_window)
if(NOT candidate_system_border LESS 0 OR
   candidate_rounded_selection LESS 0 OR
   candidate_purple_selection LESS 0 OR
   candidate_expand_button LESS 0 OR
   candidate_rounded_window LESS 0)
    message(FATAL_ERROR
        "Candidate window must use the borderless purple rounded selection and split expand/tool controls")
endif()
if(candidate_window_text MATCHES "DT_END_ELLIPSIS" OR
   candidate_window_text MATCHES "candidate_header_text" OR
   candidate_window_text MATCHES "ETO_CLIPPED" OR
   NOT candidate_window_text MATCHES "limit_candidate_window_width" OR
   NOT candidate_window_text MATCHES "candidate_window_height")
    message(FATAL_ERROR
        "Candidate window must stay compact, omit the redundant Composition header, and never render ellipsis")
endif()
if(NOT candidate_window_text MATCHES "Microsoft YaHei UI" OR
   NOT candidate_window_text MATCHES "candidate_text_top" OR
   NOT candidate_window_text MATCHES "固定首位" OR
   NOT candidate_window_text MATCHES "删除该词" OR
   NOT candidate_window_text MATCHES "WM_RBUTTONUP")
    message(FATAL_ERROR
        "Candidate window must use centered Chinese UI metrics and expose local pin/suppress controls")
endif()
if(NOT profile_text MATCHES "--refresh-profile" OR
   NOT profile_text MATCHES "L\"Runtime\"" OR
   NOT profile_text MATCHES "piinput_icon\\.ico")
    message(FATAL_ERROR
        "Profile upgrades must refresh the stable purple Pi icon metadata")
endif()
if(NOT registration_text MATCHES "PiInput 中文输入法" OR
   registration_text MATCHES "PiInput 中文输入法（开发版）")
    message(FATAL_ERROR
        "The Windows input profile must use PiInput 中文输入法 without a developer suffix")
endif()
if(NOT stable_text_service_text MATCHES "wparam == VK_OEM_3" OR
   NOT stable_text_service_text MATCHES "event.character = '`'")
    message(FATAL_ERROR
        "Stable TSF shim must forward the grave command prefix to the resident Host")
endif()

if(NOT cmake_text MATCHES "add_executable\\(PiInput-Install")
    message(FATAL_ERROR "CMake must build PiInput-Install.exe")
endif()
foreach(settings_label IN ITEMS
        "输入方案"
        "默认输入语言"
        "单行候选框高度"
        "展开候选行数")
    string(FIND "${settings_main_text}" "${settings_label}" settings_label_position)
    if(settings_label_position LESS 0)
        message(FATAL_ERROR "PiInput Settings must expose ${settings_label}")
    endif()
endforeach()
# The window is laid out from a table of options rather than from hand-placed
# controls, so the range for expanded rows is stated in that table.
if(NOT settings_main_text MATCHES "20U" OR
   NOT settings_main_text MATCHES "DefaultInputLanguage::english" OR
   NOT settings_main_text MATCHES "InputSchema::full" OR
   NOT settings_main_text MATCHES "Field::visible_rows[^\n]*1U, 6U")
    message(FATAL_ERROR
        "PiInput Settings must support compact rows, default English, full pinyin, and 1-6 expanded rows")
endif()
# Every option the engine reads must be reachable from the window, not just the
# handful it used to show.
foreach(settings_field IN ITEMS
        "Field::simplified_pinyin"
        "Field::uv_compatibility"
        "Field::max_items"
        "Field::horizontal"
        "Field::punctuation_mode"
        "Field::bracket_style"
        "Field::command_hotkey"
        "Field::english_enabled"
        "Field::prefix_scan_limit")
    string(FIND "${settings_main_text}" "${settings_field}" settings_field_position)
    if(settings_field_position LESS 0)
        message(FATAL_ERROR "PiInput Settings must expose ${settings_field}")
    endif()
endforeach()
if(NOT settings_main_text MATCHES "SS_OWNERDRAW" OR
   NOT settings_main_text MATCHES "WM_DRAWITEM" OR
   NOT settings_main_text MATCHES "DT_VCENTER")
    message(FATAL_ERROR
        "PiInput Settings preview must owner-draw vertically centered text at every row height")
endif()
if(NOT settings_main_text MATCHES "WM_MOUSEWHEEL" OR
   NOT settings_main_text MATCHES "step_numeric_setting" OR
   NOT settings_main_text MATCHES "SetWindowSubclass")
    message(FATAL_ERROR
        "PiInput Settings numeric selectors must support one-step mouse-wheel adjustment")
endif()
if(NOT settings_main_text MATCHES "WM_CTLCOLORSTATIC" OR
   NOT settings_main_text MATCHES "GetSysColorBrush\\(COLOR_WINDOW\\)")
    message(FATAL_ERROR
        "PiInput Settings labels must use the plain window background without gray blocks")
endif()
string(FIND "${settings_main_text}" "RGB(241, 235, 255)" settings_purple_preview)
string(FIND "${settings_main_text}" "RoundRect" settings_rounded_preview)
string(FIND "${settings_main_text}" "piinput::default_settings()" settings_restore_defaults)
if(settings_purple_preview LESS 0 OR
   settings_rounded_preview LESS 0 OR
   settings_restore_defaults LESS 0)
    message(FATAL_ERROR
        "PiInput Settings preview and restore-default action must match the compact purple candidate style")
endif()
# Options are grouped into tabbed pages now; thirty of them do not fit in one
# flat list of group boxes.
if(NOT settings_main_text MATCHES "WC_TABCONTROLW" OR
   NOT settings_main_text MATCHES "TCM_INSERTITEMW" OR
   NOT settings_main_text MATCHES "候选窗" OR
   NOT settings_main_text MATCHES "标点符号")
    message(FATAL_ERROR
        "PiInput Settings must group its options into aligned tabbed pages")
endif()
if(NOT installer_text MATCHES "require_file\\(source_bin / L\"PiInput-Uninstall\\.exe\"\\)" OR
   NOT installer_text MATCHES "require_file\\(source_bin / L\"PiInput-Settings\\.exe\"\\)" OR
   NOT installer_text MATCHES "install_uninstaller" OR
   NOT installer_text MATCHES "quiet_uninstall_string")
    message(FATAL_ERROR "Installer must install and register the native PiInput uninstaller")
endif()
string(FIND "${installer_text}" "bracket_style=sogou" installer_sogou_bracket)
string(FIND "${installer_text}" "existing.find(\"bracket_style=\")" installer_bracket_migration)
if(installer_sogou_bracket LESS 0 OR installer_bracket_migration LESS 0)
    message(FATAL_ERROR
        "Installer must expose the default Sogou bracket style without overwriting user settings")
endif()
if(NOT cmake_text MATCHES "PIINPUT_BUNDLED_LEXICON" OR
   NOT cmake_text MATCHES "piinput-base\\.lex")
    message(FATAL_ERROR "CMake must stage the complete external base dictionary when available")
endif()
if(NOT installer_layout_text MATCHES "versions")
    message(FATAL_ERROR "Installer must use side-by-side version directories")
endif()
if(NOT installer_text MATCHES "current_marker_value\\(version_root\\)" OR
   installer_text MATCHES "output << version_root\\.wstring\\(\\)")
    message(FATAL_ERROR "Installer current.txt must contain only the active version directory name")
endif()
if(installer_text MATCHES "TerminateProcess" OR installer_text MATCHES "Stop-Process")
    message(FATAL_ERROR "Installer must not force-close user applications")
endif()
if(installer_text MATCHES "Dev[/\\\\]bin[/\\\\]PiInputTSF\\.dll")
    message(FATAL_ERROR "Installer must not overwrite the legacy fixed TSF DLL path")
endif()
if(NOT installer_text MATCHES "remove_or_schedule_legacy_runtime\\(item\\.path\\(\\)\\)")
    message(FATAL_ERROR "Installer must remove or reboot-schedule every inactive side-by-side runtime")
endif()
if(NOT installer_text MATCHES "command\\.max_attempts" OR
   NOT installer_text MATCHES "command\\.retry_delay_ms")
    message(FATAL_ERROR "Installer must tolerate delayed TSF profile visibility with bounded retries")
endif()
if(NOT installer_text MATCHES "install_or_refresh_stable_shim" OR
   NOT installer_text MATCHES "files_are_identical" OR
   NOT installer_text MATCHES "MOVEFILE_REPLACE_EXISTING")
    message(FATAL_ERROR
        "Installer must verify and atomically refresh a changed stable Shim instead of silently keeping old code")
endif()
if(NOT installer_text MATCHES "--silent")
    message(FATAL_ERROR "Installer must support silent integration verification")
endif()
if(NOT installer_text MATCHES "ERROR_PROC_NOT_FOUND")
    message(FATAL_ERROR "Missing DllRegisterServer must report ERROR_PROC_NOT_FOUND instead of a stale last-error value")
endif()
if(NOT installer_text MATCHES "DllUnregisterServer")
    message(FATAL_ERROR "A failed first installation must best-effort unregister a partially created TSF profile")
endif()
if(installer_text MATCHES "if \\(previous_status == 0U\\)" OR
   NOT installer_text MATCHES "register_first_install\\(new_dll\\)")
    message(FATAL_ERROR "Every installer upgrade must refresh TSF capability categories")
endif()
if(NOT setup_text MATCHES "PiInput-Install\\.exe" OR NOT setup_text MATCHES "--silent")
    message(FATAL_ERROR "setup-dev.ps1 must use the native side-by-side installer")
endif()

# An application-driven composition termination must never blank text the user
# owns. Web editors rebuild their document on paste and terminate the
# composition afterwards, at which point the composition range can map onto the
# freshly pasted content.
if(NOT stable_text_service_text MATCHES "range_holds_exactly" OR
   NOT stable_text_service_text MATCHES "if \\(range_holds_exactly\\(terminated_range")
    message(FATAL_ERROR
        "OnCompositionTerminated may only erase the range when it still holds exactly the text PiInput wrote")
endif()

# Direct English typing commits one character per key. Wrapping each of those
# in StartComposition/EndComposition turns every letter into a full IME state
# transition inside the host application, which is what made English typing
# after Shift feel slow.
if(NOT stable_text_service_text MATCHES "insert_text_at_selection" OR
   NOT stable_text_service_text MATCHES "ITfInsertAtSelection" OR
   NOT stable_text_service_text MATCHES "composition_ == nullptr && commit")
    message(FATAL_ERROR
        "A commit with no composition in flight must insert at the selection instead of creating and ending a TSF composition per character")
endif()

# Keystroke hot path. Allocating and zero-filling the 1 MiB protocol ceiling
# for every message cost more than the keystroke it carried, on both sides of
# the pipe. Both readers must reuse one owned, right-sized buffer.
if(NOT host_pipe_server_text MATCHES "class PipeMessageBuffer" OR
   NOT host_pipe_server_text MATCHES "ERROR_MORE_DATA")
    message(FATAL_ERROR
        "PiInput Host must reuse one growable pipe read buffer instead of allocating the 1 MiB protocol ceiling per message")
endif()
if(NOT shim_transport_text MATCHES "reply_buffer_" OR
   NOT shim_transport_text MATCHES "ERROR_MORE_DATA" OR
   shim_transport_text MATCHES "response\\(host_header_bytes \\+ host_max_payload_bytes\\)")
    message(FATAL_ERROR
        "Stable Shim must reuse one growable reply buffer instead of allocating the 1 MiB protocol ceiling per keystroke")
endif()

# The first key after a commit is a composition boundary. Testing it must not
# copy a whole candidate snapshot, and the Host must not decode the reply it
# just encoded in order to present it.
if(NOT host_pipe_server_text MATCHES "at_composition_boundary" OR
   host_pipe_server_text MATCHES "decode_host_reply")
    message(FATAL_ERROR
        "PiInput Host must detect composition boundaries without copying a snapshot and present the reply it already holds")
endif()

# Settings hot reload runs at every composition boundary. A missing settings
# file must be reported through an error code, not by throwing once per key.
file(READ "${PIINPUT_SOURCE_DIR}/src/settings_manager.cpp" settings_manager_text)
if(NOT settings_manager_text MATCHES "try_metadata" OR
   NOT settings_manager_text MATCHES "directory_entry")
    message(FATAL_ERROR
        "Settings polling must probe the file without throwing on the keystroke hot path")
endif()

# Candidate numbers belong only to the active row. Repeating 1..N on every
# visible row makes multi-row scanning ambiguous.
string(FIND "${candidate_window_text}"
    "const bool show_number = index / items_per_row_ == active_row_;"
    candidate_active_row_number_policy)
if(candidate_active_row_number_policy EQUAL -1)
    message(FATAL_ERROR "Candidate numbers must only be shown on the active row")
endif()

message(STATUS "Windows TSF source regression checks passed")
