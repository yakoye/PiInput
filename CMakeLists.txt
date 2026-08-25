cmake_minimum_required(VERSION 3.24)

project(PiInput VERSION 0.7.13 LANGUAGES CXX)

option(PIINPUT_BUILD_TESTS "Build PiInput tests" ON)
option(PIINPUT_STATIC_MSVC_RUNTIME "Use the static MSVC runtime" ON)
option(PIINPUT_ENABLE_INTERACTIVE_TESTS
    "Register focus-changing SendInput/TSF tests; use only on an isolated desktop" OFF)
set(PIINPUT_TEST_REGISTERED_TSF "" CACHE FILEPATH
    "Registered PiInputTSF.dll expected by the optional real-profile smoke test")
set(PIINPUT_TESTDATA_DIR "" CACHE PATH "Directory containing the two SCEL regression files")
set(PIINPUT_BUILD_REVISION "" CACHE STRING
    "Exact source revision embedded in runtime diagnostics; auto-detected when empty")
if(NOT PIINPUT_BUILD_REVISION)
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}"
                rev-parse --short=12 HEAD
            OUTPUT_VARIABLE PIINPUT_DETECTED_REVISION
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE PIINPUT_REVISION_RESULT
        )
        if(PIINPUT_REVISION_RESULT EQUAL 0 AND PIINPUT_DETECTED_REVISION)
            set(PIINPUT_BUILD_REVISION "${PIINPUT_DETECTED_REVISION}")
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}"
                    status --porcelain
                OUTPUT_VARIABLE PIINPUT_GIT_STATUS
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(PIINPUT_GIT_STATUS)
                string(APPEND PIINPUT_BUILD_REVISION "-dirty")
            endif()
        endif()
    endif()
endif()
if(NOT PIINPUT_BUILD_REVISION)
    set(PIINPUT_BUILD_REVISION "source")
endif()
set(PIINPUT_BUILD_ID "${PROJECT_VERSION}+${PIINPUT_BUILD_REVISION}")
message(STATUS "PiInput exact build id: ${PIINPUT_BUILD_ID}")

set(PIINPUT_BUNDLED_LEXICON "")
if(PIINPUT_TESTDATA_DIR AND EXISTS "${PIINPUT_TESTDATA_DIR}/cache/piinput-base.lex")
    set(PIINPUT_BUNDLED_LEXICON "${PIINPUT_TESTDATA_DIR}/cache/piinput-base.lex")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../dicts/cache/piinput-base.lex")
    set(PIINPUT_BUNDLED_LEXICON "${CMAKE_CURRENT_SOURCE_DIR}/../dicts/cache/piinput-base.lex")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_compile_definitions(
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
    )
endif()

if(MSVC AND PIINPUT_STATIC_MSVC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(piinput_host_protocol STATIC
    src/host_protocol.cpp
    src/host_messages.cpp
)
target_include_directories(piinput_host_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(piinput_core STATIC
    src/scel_parser.cpp
    src/utf.cpp
    src/lexicon.cpp
    src/binary_lexicon.cpp
    src/dictionary_builder.cpp
    src/candidate_paging.cpp
    src/candidate_grid.cpp
    src/key_event_gate.cpp
    src/host_session.cpp
    src/candidate_layout.cpp
    src/composition_caret.cpp
    src/input_mode.cpp
    src/lazy_load_gate.cpp
    src/english_lexicon.cpp
    src/english_key_policy.cpp
    src/english_session.cpp
    src/segment_selection.cpp
    src/incremental_decoder.cpp
    src/full_pinyin_variants.cpp
    src/pinyin.cpp
    src/pinyin_prefix.cpp
    src/shuangpin.cpp
    src/engine.cpp
    src/datetime_candidates.cpp
    src/symbols.cpp
    src/punctuation.cpp
    src/smart_punctuation.cpp
    src/settings.cpp
    src/settings_manager.cpp
    src/session.cpp
    src/user_model.cpp
)

target_include_directories(piinput_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(piinput_core
    PUBLIC
        PIINPUT_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(piinput_core PUBLIC piinput_host_protocol)

function(piinput_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

piinput_enable_warnings(piinput_core)
piinput_enable_warnings(piinput_host_protocol)

add_executable(piinput-scel-converter
    tools/scel_converter/main.cpp
)
target_link_libraries(piinput-scel-converter PRIVATE piinput_core)
piinput_enable_warnings(piinput-scel-converter)

add_executable(piinput-lexicon-compiler
    tools/lexicon_compiler/main.cpp
)
target_link_libraries(piinput-lexicon-compiler PRIVATE piinput_core)
piinput_enable_warnings(piinput-lexicon-compiler)

add_executable(piinput-dictionary-builder
    tools/dictionary_builder/main.cpp
)
target_link_libraries(piinput-dictionary-builder PRIVATE piinput_core)
piinput_enable_warnings(piinput-dictionary-builder)

add_executable(piinput-cli
    tools/ime_cli/main.cpp
)
target_link_libraries(piinput-cli PRIVATE piinput_core)
piinput_enable_warnings(piinput-cli)

add_executable(piinput-benchmark
    tools/benchmark/main.cpp
)
target_link_libraries(piinput-benchmark PRIVATE piinput_core)
piinput_enable_warnings(piinput-benchmark)

install(TARGETS
    piinput-scel-converter
    piinput-lexicon-compiler
    piinput-dictionary-builder
    piinput-cli
    piinput-benchmark
    RUNTIME DESTINATION bin
)

if(WIN32)
    add_executable(PiInputHost
        platform/windows/host/candidate_presenter.cpp
        platform/windows/host/host_instance.cpp
        platform/windows/host/host_runtime.cpp
        platform/windows/host/main.cpp
        platform/windows/host/pipe_endpoint.cpp
        platform/windows/host/pipe_security.cpp
        platform/windows/host/pipe_server.cpp
        platform/windows/host/session_manager.cpp
        platform/windows/host/user_model_persistence.cpp
        platform/windows/settings/settings_file.cpp
        platform/windows/tsf/candidate_window.cpp
    )
    target_include_directories(PiInputHost PRIVATE
        platform/windows/host platform/windows/settings platform/windows/tsf)
    target_link_libraries(PiInputHost PRIVATE
        piinput_core advapi32 user32 gdi32 shell32 comctl32)
    target_compile_definitions(PiInputHost PRIVATE
        PIINPUT_BUILD_ID="${PIINPUT_BUILD_ID}")
    piinput_enable_warnings(PiInputHost)

    add_executable(piinput-host-client-fixture
        platform/windows/host/candidate_presenter.cpp
        tests/host_client_fixture.cpp
        platform/windows/host/pipe_endpoint.cpp
        platform/windows/host/pipe_security.cpp
        platform/windows/host/pipe_server.cpp
        platform/windows/host/session_manager.cpp
        platform/windows/tsf/candidate_window.cpp
        platform/windows/tsf/client_identity.cpp
    )
    target_include_directories(piinput-host-client-fixture PRIVATE platform/windows/host platform/windows/tsf)
    target_link_libraries(piinput-host-client-fixture PRIVATE piinput_core advapi32 user32 gdi32)
    piinput_enable_warnings(piinput-host-client-fixture)

    add_executable(piinput-caret-demo WIN32
        tools/caret_demo/main.cpp
        platform/windows/tsf/candidate_window.cpp
    )
    target_include_directories(piinput-caret-demo PRIVATE platform/windows/tsf)
    target_link_libraries(piinput-caret-demo PRIVATE piinput_core user32 gdi32)
    piinput_enable_warnings(piinput-caret-demo)

    add_executable(piinput-preview WIN32
        platform/windows/preview/main.cpp
    )
    target_link_libraries(piinput-preview PRIVATE piinput_core comctl32 shell32 ole32)
    piinput_enable_warnings(piinput-preview)

    add_executable(PiInput-Settings WIN32
        platform/windows/settings/main.cpp
        platform/windows/settings/settings_file.cpp
    )
    target_include_directories(PiInput-Settings PRIVATE platform/windows/settings)
    target_link_libraries(PiInput-Settings PRIVATE piinput_core comctl32 shell32 user32)
    piinput_enable_warnings(PiInput-Settings)

    add_library(PiInputTSF SHARED
        src/input_mode.cpp
        platform/windows/tsf/dllmain.cpp
        platform/windows/tsf/lang_bar_item.cpp
        platform/windows/tsf/mode_indicator.cpp
        platform/windows/tsf/client_identity.cpp
        platform/windows/tsf/candidate_ui_element.cpp
        platform/windows/tsf/stable_text_service.cpp
        src/smart_punctuation.cpp
        platform/windows/tsf/composition_mirror.cpp
        platform/windows/tsf/pipe_client.cpp
        platform/windows/tsf/shim_pipe_transport.cpp
        platform/windows/installer/stable_runtime.cpp
        platform/windows/tsf/piinput_resources.rc
        platform/windows/tsf/PiInputTSF.def
    )
    target_include_directories(PiInputTSF PRIVATE
        platform/windows/tsf
        platform/windows/installer
    )
    target_compile_definitions(PiInputTSF PRIVATE
        PIINPUT_VERSION="${PROJECT_VERSION}")
    target_link_libraries(PiInputTSF PRIVATE
        piinput_host_protocol
        ole32
        shell32
        user32
        advapi32
        oleaut32
        uuid
    )
    set_target_properties(PiInputTSF PROPERTIES PREFIX "" OUTPUT_NAME "PiInputTSF")
    piinput_enable_warnings(PiInputTSF)

    add_executable(piinput-profile
        platform/windows/tsf/profile_tool.cpp
        platform/windows/tsf/user_keyboard_registration.cpp
    )
    target_include_directories(piinput-profile PRIVATE platform/windows/tsf)
    target_link_libraries(piinput-profile PRIVATE piinput_core ole32 shell32 uuid)
    piinput_enable_warnings(piinput-profile)

    add_executable(piinput-diagnostics
        platform/windows/diagnostics/main.cpp
    )
    target_include_directories(piinput-diagnostics PRIVATE platform/windows/tsf)
    target_link_libraries(piinput-diagnostics PRIVATE
        piinput_host_protocol ole32 advapi32 bcrypt uuid)
    piinput_enable_warnings(piinput-diagnostics)

    add_executable(PiInput-Install WIN32
        platform/windows/installer/main.cpp
        platform/windows/installer/install_layout.cpp
        platform/windows/installer/migration.cpp
        platform/windows/installer/stable_runtime.cpp
        platform/windows/installer/uninstall_layout.cpp
        platform/windows/tsf/user_keyboard_registration.cpp
    )
    target_include_directories(PiInput-Install PRIVATE
        include
        platform/windows/installer
        platform/windows/tsf
    )
    target_compile_definitions(PiInput-Install PRIVATE
        PIINPUT_INSTALLER_VERSION=L"${PROJECT_VERSION}"
        PIINPUT_INSTALLER_BUILD_ID=L"${PIINPUT_BUILD_ID}")
    target_link_libraries(PiInput-Install PRIVATE piinput_core ole32 shell32 advapi32 user32)
    if(MSVC)
        target_link_options(PiInput-Install PRIVATE
            "/MANIFESTUAC:level='asInvoker' uiAccess='false'")
    endif()
    piinput_enable_warnings(PiInput-Install)

    add_executable(PiInput-Uninstall WIN32
        platform/windows/uninstaller/main.cpp
        platform/windows/host/pipe_endpoint.cpp
        platform/windows/installer/stable_runtime.cpp
        platform/windows/installer/uninstall_layout.cpp
        platform/windows/installer/migration.cpp
        platform/windows/tsf/user_keyboard_registration.cpp
    )
    target_include_directories(PiInput-Uninstall PRIVATE
        include
        platform/windows/host
        platform/windows/installer
        platform/windows/tsf
    )
    target_link_libraries(PiInput-Uninstall PRIVATE
        piinput_core ole32 shell32 advapi32 user32 comctl32)
    if(MSVC)
        target_link_options(PiInput-Uninstall PRIVATE
            "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
            "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'")
    endif()
    piinput_enable_warnings(PiInput-Uninstall)

    install(TARGETS
        PiInputHost
        PiInput-Settings
        piinput-preview
        piinput-profile
        piinput-diagnostics
        PiInputTSF
        PiInput-Install
        PiInput-Uninstall
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION bin
    )
endif()
install(FILES
    data/symbols.tsv
    data/english_lexicon.tsv
    data/english_supplement.tsv
    data/english_completion_preferences.tsv
    data/host_protocol.json
    DESTINATION data
)
if(WIN32)
    install(FILES platform/windows/tsf/piinput_icon.ico DESTINATION bin)
    # The symbol tool the language bar's 符号 entry launches. It ships beside the
    # other programs so a fresh install has one without any configuration.
    if(EXISTS ${CMAKE_SOURCE_DIR}/yesymbol/yesymbol.exe)
        install(PROGRAMS ${CMAKE_SOURCE_DIR}/yesymbol/yesymbol.exe DESTINATION bin)
    endif()
endif()
if(PIINPUT_BUNDLED_LEXICON)
    install(FILES "${PIINPUT_BUNDLED_LEXICON}" DESTINATION data)
endif()
install(FILES examples/sample_lexicon.tsv DESTINATION data)

if(PIINPUT_BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(piinput-core-tests
        tests/test_main.cpp
    )
    target_link_libraries(piinput-core-tests PRIVATE piinput_core)
    target_compile_definitions(piinput-core-tests PRIVATE
        PIINPUT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    piinput_enable_warnings(piinput-core-tests)

    add_executable(piinput-settings-tests
        tests/settings_tests.cpp
    )
    target_link_libraries(piinput-settings-tests PRIVATE piinput_core)
    piinput_enable_warnings(piinput-settings-tests)

    add_executable(piinput-candidate-grid-tests
        tests/candidate_grid_tests.cpp
    )
    target_link_libraries(piinput-candidate-grid-tests PRIVATE piinput_core)
    piinput_enable_warnings(piinput-candidate-grid-tests)

    add_executable(piinput-user-model-tests
        tests/user_model_tests.cpp
    )
    target_link_libraries(piinput-user-model-tests PRIVATE piinput_core)
    piinput_enable_warnings(piinput-user-model-tests)

    add_executable(piinput-user-model-persistence-tests
        tests/user_model_persistence_tests.cpp
        platform/windows/host/user_model_persistence.cpp
    )
    target_include_directories(piinput-user-model-persistence-tests PRIVATE
        platform/windows/host
    )
    piinput_enable_warnings(piinput-user-model-persistence-tests)

    add_executable(piinput-user-phrase-performance-tests
        tests/user_phrase_performance_tests.cpp
    )
    target_link_libraries(piinput-user-phrase-performance-tests PRIVATE piinput_core)
    piinput_enable_warnings(piinput-user-phrase-performance-tests)

    add_executable(piinput-key-event-gate-tests
        tests/key_event_gate_tests.cpp
    )
    target_link_libraries(piinput-key-event-gate-tests PRIVATE piinput_core)
    piinput_enable_warnings(piinput-key-event-gate-tests)

    if(WIN32)
        add_executable(piinput-input-scope-policy-tests
            tests/input_scope_policy_tests.cpp
        )
        target_include_directories(piinput-input-scope-policy-tests PRIVATE
            platform/windows/tsf
        )
        target_link_libraries(piinput-input-scope-policy-tests PRIVATE ole32)
        piinput_enable_warnings(piinput-input-scope-policy-tests)

        add_executable(piinput-controlled-tsf-host
            tests/controlled_tsf_host.cpp
        )
        target_link_libraries(piinput-controlled-tsf-host PRIVATE user32)
        piinput_enable_warnings(piinput-controlled-tsf-host)

        add_executable(piinput-controlled-tsf-controller
            tests/controlled_tsf_controller.cpp
        )
        target_include_directories(piinput-controlled-tsf-controller PRIVATE
            platform/windows/tsf
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        target_link_libraries(piinput-controlled-tsf-controller PRIVATE
            user32 ole32 uuid
        )
        piinput_enable_warnings(piinput-controlled-tsf-controller)
    endif()

    add_executable(piinput-host-protocol-tests
        tests/host_protocol_tests.cpp
    )
    target_link_libraries(piinput-host-protocol-tests PRIVATE piinput_core)
    piinput_enable_warnings(piinput-host-protocol-tests)

    add_executable(piinput-host-session-tests
        tests/host_session_tests.cpp
    )
    target_link_libraries(piinput-host-session-tests PRIVATE piinput_core)
    piinput_enable_warnings(piinput-host-session-tests)

    add_executable(piinput-host-messages-tests
        tests/host_messages_tests.cpp
    )
    target_link_libraries(piinput-host-messages-tests PRIVATE piinput_core)
    piinput_enable_warnings(piinput-host-messages-tests)

    add_executable(piinput-lazy-load-gate-tests
        tests/lazy_load_gate_tests.cpp
    )
    target_link_libraries(piinput-lazy-load-gate-tests PRIVATE piinput_core)
    piinput_enable_warnings(piinput-lazy-load-gate-tests)

    add_executable(piinput-full-pinyin-variants-tests
        tests/full_pinyin_variants_tests.cpp
    )
    target_link_libraries(piinput-full-pinyin-variants-tests PRIVATE piinput_core)
    piinput_enable_warnings(piinput-full-pinyin-variants-tests)

    add_executable(piinput-incremental-decoder-tests
        tests/incremental_decoder_tests.cpp
    )
    target_link_libraries(piinput-incremental-decoder-tests PRIVATE piinput_core)
    target_compile_definitions(piinput-incremental-decoder-tests PRIVATE
        PIINPUT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    piinput_enable_warnings(piinput-incremental-decoder-tests)

    add_executable(piinput-english-lexicon-tests
        tests/english_lexicon_tests.cpp
    )
    target_link_libraries(piinput-english-lexicon-tests PRIVATE piinput_core)
    target_compile_definitions(piinput-english-lexicon-tests PRIVATE
        PIINPUT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    piinput_enable_warnings(piinput-english-lexicon-tests)

    add_executable(piinput-dictionary-builder-tests
        tests/dictionary_builder_tests.cpp
    )
    target_link_libraries(piinput-dictionary-builder-tests PRIVATE piinput_core)
    target_compile_definitions(piinput-dictionary-builder-tests PRIVATE
        PIINPUT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    piinput_enable_warnings(piinput-dictionary-builder-tests)

    add_executable(piinput-lexicon-query-tests
        tests/lexicon_query_tests.cpp
    )
    target_link_libraries(piinput-lexicon-query-tests PRIVATE piinput_core)
    target_compile_definitions(piinput-lexicon-query-tests PRIVATE
        PIINPUT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    piinput_enable_warnings(piinput-lexicon-query-tests)

    add_executable(piinput-segment-selection-tests
        tests/segment_selection_tests.cpp
    )
    target_link_libraries(piinput-segment-selection-tests PRIVATE piinput_core)
    piinput_enable_warnings(piinput-segment-selection-tests)

    add_executable(piinput-character-coverage-tests
        tests/character_coverage_tests.cpp
    )
    target_link_libraries(piinput-character-coverage-tests PRIVATE piinput_core)
    piinput_enable_warnings(piinput-character-coverage-tests)

    add_executable(piinput-paragraph-input-tests
        tests/paragraph_input_tests.cpp
    )
    target_link_libraries(piinput-paragraph-input-tests PRIVATE piinput_core)
    piinput_enable_warnings(piinput-paragraph-input-tests)

    add_executable(piinput-corpus-candidate-tests
        tests/corpus_candidate_runner.cpp
    )
    target_link_libraries(piinput-corpus-candidate-tests PRIVATE piinput_core)
    piinput_enable_warnings(piinput-corpus-candidate-tests)

    add_executable(piinput-keystroke-latency-tests
        tests/keystroke_latency_tests.cpp
    )
    target_link_libraries(piinput-keystroke-latency-tests PRIVATE piinput_core)
    piinput_enable_warnings(piinput-keystroke-latency-tests)

    if(WIN32)
        add_executable(piinput-pipe-client-tests
            tests/pipe_client_tests.cpp
            platform/windows/tsf/pipe_client.cpp
        )
        target_include_directories(piinput-pipe-client-tests PRIVATE
            platform/windows/tsf
        )
        target_link_libraries(piinput-pipe-client-tests PRIVATE piinput_core)
        piinput_enable_warnings(piinput-pipe-client-tests)

        add_executable(piinput-composition-mirror-tests
            tests/composition_mirror_tests.cpp
            platform/windows/tsf/composition_mirror.cpp
        )
        target_include_directories(piinput-composition-mirror-tests PRIVATE
            platform/windows/tsf
        )
        target_link_libraries(piinput-composition-mirror-tests PRIVATE piinput_core)
        piinput_enable_warnings(piinput-composition-mirror-tests)

        add_executable(piinput-session-manager-tests
            tests/session_manager_tests.cpp
            platform/windows/host/session_manager.cpp
        )
        target_include_directories(piinput-session-manager-tests PRIVATE
            platform/windows/host
        )
        target_link_libraries(piinput-session-manager-tests PRIVATE piinput_core)
        piinput_enable_warnings(piinput-session-manager-tests)

        add_executable(piinput-host-runtime-tests
            tests/host_runtime_tests.cpp
            platform/windows/host/host_runtime.cpp
        )
        target_include_directories(piinput-host-runtime-tests PRIVATE
            platform/windows/host
        )
        target_compile_definitions(piinput-host-runtime-tests PRIVATE
            PIINPUT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        )
        target_link_libraries(piinput-host-runtime-tests PRIVATE piinput_core shell32 ole32)
        piinput_enable_warnings(piinput-host-runtime-tests)

        add_executable(piinput-candidate-presenter-tests
            tests/candidate_presenter_tests.cpp
            platform/windows/host/candidate_presenter.cpp
            platform/windows/tsf/candidate_window.cpp
        )
        target_include_directories(piinput-candidate-presenter-tests PRIVATE
            platform/windows/host
            platform/windows/tsf
        )
        target_link_libraries(piinput-candidate-presenter-tests PRIVATE piinput_core user32 gdi32)
        piinput_enable_warnings(piinput-candidate-presenter-tests)

        add_executable(piinput-candidate-ui-element-tests
            tests/candidate_ui_element_tests.cpp
            platform/windows/tsf/candidate_ui_element.cpp
        )
        target_include_directories(piinput-candidate-ui-element-tests PRIVATE
            platform/windows/tsf
        )
        target_link_libraries(piinput-candidate-ui-element-tests PRIVATE
            piinput_core ole32 oleaut32 uuid)
        piinput_enable_warnings(piinput-candidate-ui-element-tests)

        add_executable(piinput-mode-indicator-tests
            tests/mode_indicator_tests.cpp
            platform/windows/tsf/mode_indicator.cpp
            platform/windows/tsf/client_identity.cpp
            platform/windows/tsf/lang_bar_item.cpp
        )
        target_include_directories(piinput-mode-indicator-tests PRIVATE
            platform/windows/tsf
        )
        target_link_libraries(piinput-mode-indicator-tests PRIVATE
            piinput_core ole32 shell32 user32 gdi32 advapi32 uuid)
        piinput_enable_warnings(piinput-mode-indicator-tests)

        add_executable(piinput-settings-file-tests
            tests/settings_file_tests.cpp
            platform/windows/settings/settings_file.cpp
        )
        target_include_directories(piinput-settings-file-tests PRIVATE
            platform/windows/settings
        )
        target_link_libraries(piinput-settings-file-tests PRIVATE piinput_core)
        piinput_enable_warnings(piinput-settings-file-tests)

        add_executable(piinput-pipe-security-tests
            tests/pipe_security_tests.cpp
            platform/windows/host/pipe_security.cpp
        )
        target_include_directories(piinput-pipe-security-tests PRIVATE
            platform/windows/host
        )
        target_link_libraries(piinput-pipe-security-tests PRIVATE advapi32)
        piinput_enable_warnings(piinput-pipe-security-tests)

        add_executable(piinput-host-endpoint-tests
            tests/host_endpoint_tests.cpp
            platform/windows/host/pipe_endpoint.cpp
        )
        target_include_directories(piinput-host-endpoint-tests PRIVATE platform/windows/host)
        piinput_enable_warnings(piinput-host-endpoint-tests)

        add_executable(piinput-host-instance-tests
            tests/host_instance_tests.cpp
            platform/windows/host/host_instance.cpp
            platform/windows/host/pipe_endpoint.cpp
        )
        target_include_directories(piinput-host-instance-tests PRIVATE platform/windows/host)
        piinput_enable_warnings(piinput-host-instance-tests)

        add_executable(piinput-user-keyboard-tests
            tests/user_keyboard_registration_tests.cpp
            platform/windows/tsf/user_keyboard_registration.cpp
        )
        target_include_directories(piinput-user-keyboard-tests PRIVATE
            include
            platform/windows/tsf
        )
        target_link_libraries(piinput-user-keyboard-tests PRIVATE ole32)
        piinput_enable_warnings(piinput-user-keyboard-tests)

        add_executable(piinput-installer-layout-tests
            tests/installer_layout_tests.cpp
            platform/windows/installer/install_layout.cpp
        )
        target_include_directories(piinput-installer-layout-tests PRIVATE platform/windows/installer)
        piinput_enable_warnings(piinput-installer-layout-tests)

        add_executable(piinput-stable-runtime-tests
            tests/stable_runtime_tests.cpp
            platform/windows/installer/stable_runtime.cpp
        )
        target_include_directories(piinput-stable-runtime-tests PRIVATE
            include
            platform/windows/installer
        )
        piinput_enable_warnings(piinput-stable-runtime-tests)

        add_executable(piinput-uninstall-layout-tests
            tests/uninstall_layout_tests.cpp
            platform/windows/installer/stable_runtime.cpp
            platform/windows/installer/uninstall_layout.cpp
        )
        target_include_directories(piinput-uninstall-layout-tests PRIVATE include platform/windows/installer)
        piinput_enable_warnings(piinput-uninstall-layout-tests)

        add_executable(piinput-migration-tests
            tests/migration_tests.cpp
            platform/windows/installer/migration.cpp
        )
        target_include_directories(piinput-migration-tests PRIVATE
            include
            platform/windows/installer
        )
        target_link_libraries(piinput-migration-tests PRIVATE piinput_core)
        piinput_enable_warnings(piinput-migration-tests)
    endif()

    add_test(NAME piinput-core-tests COMMAND piinput-core-tests)
    add_test(NAME piinput-settings-tests COMMAND piinput-settings-tests)
    add_test(NAME piinput-candidate-grid COMMAND piinput-candidate-grid-tests)
    add_test(NAME piinput-user-model COMMAND piinput-user-model-tests)
    add_test(NAME piinput-user-model-persistence COMMAND piinput-user-model-persistence-tests)
    add_test(NAME piinput-user-phrase-performance COMMAND piinput-user-phrase-performance-tests)
    add_test(NAME piinput-key-event-gate COMMAND piinput-key-event-gate-tests)
    if(WIN32)
        add_test(NAME piinput-input-scope-policy COMMAND piinput-input-scope-policy-tests)
    endif()
    add_test(NAME piinput-host-protocol COMMAND piinput-host-protocol-tests)
    add_test(NAME piinput-host-session COMMAND piinput-host-session-tests)
    add_test(NAME piinput-host-messages COMMAND piinput-host-messages-tests)
    add_test(NAME piinput-lazy-load-gate COMMAND piinput-lazy-load-gate-tests)
    add_test(NAME piinput-full-pinyin-variants COMMAND piinput-full-pinyin-variants-tests)
    add_test(NAME piinput-incremental-decoder COMMAND piinput-incremental-decoder-tests)
    add_test(NAME piinput-english-lexicon COMMAND piinput-english-lexicon-tests)
    add_test(NAME piinput-dictionary-builder COMMAND piinput-dictionary-builder-tests)
    add_test(NAME piinput-lexicon-query COMMAND piinput-lexicon-query-tests)
    add_test(NAME piinput-segment-selection COMMAND piinput-segment-selection-tests)

    if(WIN32)
        add_test(
            NAME piinput-controlled-tsf-host-self-test
            COMMAND piinput-controlled-tsf-host --self-test
        )
        if(PIINPUT_ENABLE_INTERACTIVE_TESTS)
            add_test(
                NAME piinput-controlled-tsf-physical-input
                COMMAND piinput-controlled-tsf-controller
            )
            set_tests_properties(piinput-controlled-tsf-physical-input PROPERTIES
                LABELS "interactive;controlled-tsf"
                RUN_SERIAL TRUE
            )
            if(PIINPUT_TEST_REGISTERED_TSF)
                add_test(
                    NAME piinput-controlled-tsf-piinput-smoke
                    COMMAND piinput-controlled-tsf-controller
                        --piinput-smoke
                        --expected-tsf "${PIINPUT_TEST_REGISTERED_TSF}"
                )
                set_tests_properties(piinput-controlled-tsf-piinput-smoke PROPERTIES
                    LABELS "interactive;controlled-tsf;piinput-profile"
                    RUN_SERIAL TRUE
                )
                add_test(
                    NAME piinput-controlled-tsf-soak-smoke
                    COMMAND pwsh -NoProfile -ExecutionPolicy Bypass
                        -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/tsf_app_soak_tests.ps1"
                        -ControllerExe "$<TARGET_FILE:piinput-controlled-tsf-controller>"
                        -ExpectedTsf "${PIINPUT_TEST_REGISTERED_TSF}"
                        -DurationHours 0.003
                        -SampleSeconds 1
                        -OutputDirectory "${CMAKE_CURRENT_BINARY_DIR}/tsf-app-soak-smoke"
                )
                set_tests_properties(piinput-controlled-tsf-soak-smoke PROPERTIES
                    LABELS "interactive;controlled-tsf;piinput-profile;soak-smoke"
                    RUN_SERIAL TRUE
                    TIMEOUT 120
                )
            endif()
        endif()
        add_test(NAME piinput-pipe-client COMMAND piinput-pipe-client-tests)
        add_test(NAME piinput-composition-mirror COMMAND piinput-composition-mirror-tests)
        add_test(NAME piinput-session-manager COMMAND piinput-session-manager-tests)
        add_test(NAME piinput-host-runtime COMMAND piinput-host-runtime-tests)
        add_test(NAME piinput-candidate-presenter COMMAND piinput-candidate-presenter-tests)
        add_test(NAME piinput-candidate-ui-element COMMAND piinput-candidate-ui-element-tests)
        add_test(NAME piinput-mode-indicator COMMAND piinput-mode-indicator-tests)
        add_test(NAME piinput-settings-file COMMAND piinput-settings-file-tests)
        add_test(NAME piinput-pipe-security COMMAND piinput-pipe-security-tests)
        add_test(NAME piinput-host-endpoint COMMAND piinput-host-endpoint-tests)
        add_test(NAME piinput-host-instance COMMAND piinput-host-instance-tests)
        add_test(NAME piinput-user-keyboard-registration COMMAND piinput-user-keyboard-tests)
        add_test(NAME piinput-installer-layout-tests COMMAND piinput-installer-layout-tests)
        add_test(NAME piinput-stable-runtime COMMAND piinput-stable-runtime-tests)
        add_test(NAME piinput-uninstall-layout-tests COMMAND piinput-uninstall-layout-tests)
        add_test(NAME piinput-migration COMMAND piinput-migration-tests)
    endif()

    add_test(
        NAME piinput-brand-regression
        COMMAND "${CMAKE_COMMAND}"
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/tests/brand_regression.cmake
    )
    add_test(
        NAME piinput-brand-filter-regression
        COMMAND "${CMAKE_COMMAND}"
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DTEST_BINARY_DIR=${CMAKE_BINARY_DIR}/brand-filter-regression
            -P ${CMAKE_SOURCE_DIR}/tests/brand_filter_regression.cmake
    )
    add_test(
        NAME piinput-sha256-regression
        COMMAND "${CMAKE_COMMAND}"
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/tests/sha256_regression.cmake
    )
    add_test(
        NAME piinput-release-metadata-regression
        COMMAND "${CMAKE_COMMAND}"
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/tests/release_metadata_regression.cmake
    )
    if(WIN32)
        set(
            PIINPUT_SYSTEM_POWERSHELL
            "$ENV{SystemRoot}/System32/WindowsPowerShell/v1.0/powershell.exe"
        )
        if(EXISTS "${PIINPUT_SYSTEM_POWERSHELL}")
            set(PIINPUT_POWERSHELL_EXECUTABLE "${PIINPUT_SYSTEM_POWERSHELL}")
        else()
            find_program(PIINPUT_POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)
        endif()
        set(PIINPUT_HOST_PERFORMANCE_ARGS)
        if(EXISTS "${PIINPUT_TESTDATA_DIR}/cache/piinput-base.lex")
            list(APPEND PIINPUT_HOST_PERFORMANCE_ARGS
                -LexiconPath "${PIINPUT_TESTDATA_DIR}/cache/piinput-base.lex"
                -DataDir "${CMAKE_SOURCE_DIR}/data"
            )
        endif()
        add_test(
            NAME piinput-host-process
            COMMAND "${PIINPUT_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tests/host_process_tests.ps1
                -HostExe $<TARGET_FILE:PiInputHost>
                -ClientExe $<TARGET_FILE:piinput-host-client-fixture>
                ${PIINPUT_HOST_PERFORMANCE_ARGS}
        )
        set_tests_properties(piinput-host-process PROPERTIES
            ENVIRONMENT
                "PIINPUT_PACKAGE_DATA_DIR=${CMAKE_SOURCE_DIR}/data;PIINPUT_USER_DATA_DIR=${CMAKE_BINARY_DIR}/host-process-user"
        )
        # Long-form typing: the three real-world texts replayed key by key
        # through a live Host, including the row paging and digit keys used when
        # the wanted word is not candidate 1.
        add_test(
            NAME piinput-typing-latency
            COMMAND "${PIINPUT_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tests/typing_latency_tests.ps1
                -HostExe $<TARGET_FILE:PiInputHost>
                -ClientExe $<TARGET_FILE:piinput-host-client-fixture>
                -ScriptDirectory ${CMAKE_SOURCE_DIR}/tests/data/typing
                ${PIINPUT_HOST_PERFORMANCE_ARGS}
        )
        set_tests_properties(piinput-typing-latency PROPERTIES
            SKIP_RETURN_CODE 77
            CONFIGURATIONS Release
            DISABLED "$<NOT:$<CONFIG:Release>>"
        )
        add_test(
            NAME piinput-host-restart-handoff
            COMMAND "${PIINPUT_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tests/host_restart_handoff_tests.ps1
                -HostExe $<TARGET_FILE:PiInputHost>
                -ClientExe $<TARGET_FILE:piinput-host-client-fixture>
        )
        set_tests_properties(piinput-host-restart-handoff PROPERTIES
            ENVIRONMENT
                "PIINPUT_PACKAGE_DATA_DIR=${CMAKE_SOURCE_DIR}/data;PIINPUT_USER_DATA_DIR=${CMAKE_BINARY_DIR}/host-process-user"
        )
        add_test(
            NAME piinput-diagnostics
            COMMAND "${PIINPUT_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/tests/diagnostics_tests.ps1
                -DiagnosticsExe $<TARGET_FILE:piinput-diagnostics>
        )
        add_test(
            NAME piinput-installed-dev-resolver-regression
            COMMAND "${PIINPUT_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -File ${CMAKE_SOURCE_DIR}/tests/installed_dev_resolver_regression.ps1
        )
        add_test(
            NAME piinput-uninstall-fail-closed-regression
            COMMAND "${PIINPUT_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -File ${CMAKE_SOURCE_DIR}/tests/uninstall_fail_closed_regression.ps1
        )
        add_test(
            NAME piinput-settings-window-regression
            COMMAND "${PIINPUT_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -File ${CMAKE_SOURCE_DIR}/tests/settings_window_regression.ps1
                -SettingsExe $<TARGET_FILE:PiInput-Settings>
        )
        add_test(
            NAME piinput-candidate-settings-script-regression
            COMMAND "${PIINPUT_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -File ${CMAKE_SOURCE_DIR}/tests/candidate_settings_script_regression.ps1
        )
        add_test(
            NAME piinput-english-dictionary-conversion-regression
            COMMAND "${PIINPUT_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -File ${CMAKE_SOURCE_DIR}/tests/english_dictionary_conversion_regression.ps1
        )
        add_test(
            NAME piinput-dictionary-builder-cli-regression
            COMMAND "${PIINPUT_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -File ${CMAKE_SOURCE_DIR}/tests/dictionary_builder_cli_regression.ps1
                -Builder $<TARGET_FILE:piinput-dictionary-builder>
                -SourceDir ${CMAKE_SOURCE_DIR}
        )
        add_test(
            NAME piinput-dictionary-query-cli-regression
            COMMAND "${PIINPUT_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -File ${CMAKE_SOURCE_DIR}/tests/dictionary_query_cli_regression.ps1
                -Cli $<TARGET_FILE:piinput-cli>
                -SourceDir ${CMAKE_SOURCE_DIR}
        )
        add_test(
            NAME piinput-query-dictionary-cmd-regression
            COMMAND "${PIINPUT_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -File ${CMAKE_SOURCE_DIR}/tests/query_dictionary_cmd_regression.ps1
                -SourceDir ${CMAKE_SOURCE_DIR}
        )
        add_test(
            NAME piinput-native-command-capture-regression
            COMMAND "${PIINPUT_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -File ${CMAKE_SOURCE_DIR}/tests/native_command_capture_regression.ps1
                -SourceDir ${CMAKE_SOURCE_DIR}
        )
        add_test(
            NAME piinput-human-input-scenario-regression
            COMMAND "${PIINPUT_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -File ${CMAKE_SOURCE_DIR}/tests/human_input_scenario_regression.ps1
                -SourceDir ${CMAKE_SOURCE_DIR}
        )
        add_test(
            NAME piinput-uninstaller-source-regression
            COMMAND ${CMAKE_COMMAND}
                -DPIINPUT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/uninstaller_source_regression.cmake
        )
        add_test(
            NAME piinput-uac-manifest-regression
            COMMAND "${PIINPUT_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -File ${CMAKE_SOURCE_DIR}/tests/uac_manifest_regression.ps1
                -InstallerExe $<TARGET_FILE:PiInput-Install>
                -UninstallerExe $<TARGET_FILE:PiInput-Uninstall>
        )
    endif()
    add_test(
        NAME piinput-windows-source-regression
        COMMAND ${CMAKE_COMMAND}
            -DPIINPUT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/windows_source_regression.cmake
    )
    add_test(
        NAME piinput-dictionary-script-regression
        COMMAND "${CMAKE_COMMAND}"
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/tests/dictionary_script_regression.cmake
    )
    add_test(
        NAME piinput-corpus-regression
        COMMAND "${CMAKE_COMMAND}"
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/tests/corpus_regression.cmake
    )
    add_test(
        NAME piinput-performance-smoke
        COMMAND piinput-benchmark
            --lexicon ${CMAKE_SOURCE_DIR}/data/base_lexicon.tsv
            --schema full
            --query wo
            --iterations 2000
            --warmup 200
            --max-p95-us 2000
            --max-p99-us 5000
    )

    if(PIINPUT_TESTDATA_DIR)
        set(
            PIINPUT_INCREMENTAL_BENCHMARK_LEXICON_DEFAULT
            "${PIINPUT_TESTDATA_DIR}/generated/piinput-combined.tsv"
        )
    else()
        set(
            PIINPUT_INCREMENTAL_BENCHMARK_LEXICON_DEFAULT
            "${CMAKE_SOURCE_DIR}/../dicts/generated/piinput-combined.tsv"
        )
    endif()
    set(
        PIINPUT_INCREMENTAL_BENCHMARK_LEXICON
        "${PIINPUT_INCREMENTAL_BENCHMARK_LEXICON_DEFAULT}"
        CACHE FILEPATH
        "External large lexicon used by the incremental Release performance gate"
    )
    add_test(
        NAME piinput-external-incremental-performance
        COMMAND piinput-benchmark
            --lexicon "${PIINPUT_INCREMENTAL_BENCHMARK_LEXICON}"
            --schema flypy
            --query biruwoycuibuv
            --iterations 2000
            --warmup 200
            --rounds 3
            --skip-if-missing
            # resident-hot-v1 deliberately excludes the 411k pronunciation-only
            # fallback table. Coverage and candidate regressions below are the
            # quality gates; this threshold only catches an empty/partial build.
            --min-entries 100000
            --require-results
            --max-p95-us 2000
            --max-p99-us 5000
        CONFIGURATIONS Release
    )
    set_tests_properties(
        piinput-external-incremental-performance
        PROPERTIES
            DISABLED "$<NOT:$<CONFIG:Release>>"
            SKIP_RETURN_CODE 77
    )

    add_test(
        NAME piinput-external-long-full-performance
        COMMAND piinput-benchmark
            --lexicon "${PIINPUT_TESTDATA_DIR}/cache/piinput-base.lex"
            --schema full
            --query wojintianxiawuyaoquchaoshimaidianshuiguo
            --iterations 100
            --warmup 10
            --rounds 3
            --skip-if-missing
            --min-entries 100000
            --require-results
            --max-p95-us 5000
            --max-p99-us 10000
        CONFIGURATIONS Release
    )
    set_tests_properties(
        piinput-external-long-full-performance
        PROPERTIES
            DISABLED "$<NOT:$<CONFIG:Release>>"
            SKIP_RETURN_CODE 77
    )

    if(PIINPUT_TESTDATA_DIR)
        set(EXTERNAL_LEXICON "${PIINPUT_TESTDATA_DIR}/cache/piinput-base.lex")
        set(EXTERNAL_DICTIONARY_TSV "${PIINPUT_TESTDATA_DIR}/generated/piinput-combined.tsv")
        set(COMMON_CHARACTER_LIST "${PIINPUT_TESTDATA_DIR}/tests/3500常用汉字.txt")
        set(GENERAL_CHARACTER_LIST "${PIINPUT_TESTDATA_DIR}/tests/7000通用汉字.txt")
        if(EXISTS "${EXTERNAL_LEXICON}")
            add_test(
                NAME piinput-external-dictionary-regression
                COMMAND piinput-core-tests --lexicon "${EXTERNAL_LEXICON}"
            )
            add_test(
                NAME piinput-paragraph-input-regression
                COMMAND piinput-paragraph-input-tests
                    "${EXTERNAL_LEXICON}"
                    "${CMAKE_SOURCE_DIR}/tests/data/paragraph_input_cases.tsv"
            )
            add_test(
                NAME piinput-structured-corpus-regression
                COMMAND "${PIINPUT_POWERSHELL_EXECUTABLE}"
                    -NoProfile
                    -File ${CMAKE_SOURCE_DIR}/tests/structured_corpus_regression.ps1
                    -Runner $<TARGET_FILE:piinput-corpus-candidate-tests>
                    -Lexicon "${EXTERNAL_LEXICON}"
                    -SourceDir ${CMAKE_SOURCE_DIR}
                    -TempRoot ${CMAKE_BINARY_DIR}/structured-corpus-temp
            )
            # Per-key budget for the reported "我，他们。我们明天，……" sentence,
            # including the first letter after every punctuation mark.
            add_test(
                NAME piinput-keystroke-latency
                COMMAND piinput-keystroke-latency-tests
                    "${EXTERNAL_LEXICON}"
                    2000
                    6000
                CONFIGURATIONS Release
            )
            set_tests_properties(
                piinput-keystroke-latency
                PROPERTIES
                    DISABLED "$<NOT:$<CONFIG:Release>>"
                    SKIP_RETURN_CODE 77
            )
        endif()
        if(EXISTS "${EXTERNAL_LEXICON}" AND
           EXISTS "${EXTERNAL_DICTIONARY_TSV}" AND
           EXISTS "${COMMON_CHARACTER_LIST}" AND
           EXISTS "${GENERAL_CHARACTER_LIST}")
            add_test(
                NAME piinput-character-coverage
                COMMAND piinput-character-coverage-tests
                    "${EXTERNAL_DICTIONARY_TSV}"
                    "${EXTERNAL_LEXICON}"
                    "${COMMON_CHARACTER_LIST}"
                    "${GENERAL_CHARACTER_LIST}"
                    "${CMAKE_SOURCE_DIR}/tests/data/xiaohe_legal_codes.tsv"
            )
        endif()
        set(ELECTRONICS_SCEL "${PIINPUT_TESTDATA_DIR}/电子词汇大全【官方推荐】.scel")
        set(COMPUTER_SCEL "${PIINPUT_TESTDATA_DIR}/计算机词汇大全【官方推荐】.scel")
        if(EXISTS "${ELECTRONICS_SCEL}" AND EXISTS "${COMPUTER_SCEL}")
            add_test(
                NAME piinput-scel-regression
                COMMAND piinput-core-tests "${ELECTRONICS_SCEL}" "${COMPUTER_SCEL}"
            )
        endif()
    endif()
endif()
