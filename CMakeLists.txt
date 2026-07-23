cmake_minimum_required(VERSION 3.24)

project(PiInput VERSION 0.3.0 LANGUAGES CXX)

option(PIINPUT_BUILD_TESTS "Build PiInput tests" ON)
option(PIINPUT_STATIC_MSVC_RUNTIME "Use the static MSVC runtime" ON)
set(PIINPUT_TESTDATA_DIR "" CACHE PATH "Directory containing the two SCEL regression files")

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

add_library(piinput_core STATIC
    src/scel_parser.cpp
    src/utf.cpp
    src/lexicon.cpp
    src/binary_lexicon.cpp
    src/dictionary_builder.cpp
    src/candidate_paging.cpp
    src/candidate_grid.cpp
    src/candidate_layout.cpp
    src/composition_caret.cpp
    src/input_mode.cpp
    src/english_lexicon.cpp
    src/english_key_policy.cpp
    src/english_session.cpp
    src/incremental_decoder.cpp
    src/full_pinyin_variants.cpp
    src/pinyin.cpp
    src/pinyin_prefix.cpp
    src/shuangpin.cpp
    src/engine.cpp
    src/symbols.cpp
    src/punctuation.cpp
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

function(piinput_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

piinput_enable_warnings(piinput_core)

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
    add_executable(piinput-preview WIN32
        platform/windows/preview/main.cpp
    )
    target_link_libraries(piinput-preview PRIVATE piinput_core comctl32 shell32 ole32)
    piinput_enable_warnings(piinput-preview)

    add_library(PiInputTSF SHARED
        platform/windows/tsf/dllmain.cpp
        platform/windows/tsf/text_service.cpp
        platform/windows/tsf/candidate_window.cpp
        platform/windows/tsf/PiInputTSF.def
    )
    target_include_directories(PiInputTSF PRIVATE platform/windows/tsf)
    target_link_libraries(PiInputTSF PRIVATE
        piinput_core
        ole32
        shell32
        user32
        gdi32
        advapi32
        uuid
    )
    set_target_properties(PiInputTSF PROPERTIES PREFIX "" OUTPUT_NAME "PiInputTSF")
    piinput_enable_warnings(PiInputTSF)

    add_executable(piinput-profile
        platform/windows/tsf/profile_tool.cpp
    )
    target_include_directories(piinput-profile PRIVATE platform/windows/tsf)
    target_link_libraries(piinput-profile PRIVATE piinput_core ole32 shell32 uuid)
    piinput_enable_warnings(piinput-profile)

    add_executable(PiInput-Install WIN32
        platform/windows/installer/main.cpp
        platform/windows/installer/install_layout.cpp
    )
    target_include_directories(PiInput-Install PRIVATE
        include
        platform/windows/installer
        platform/windows/tsf
    )
    target_compile_definitions(PiInput-Install PRIVATE PIINPUT_VERSION=L"${PROJECT_VERSION}")
    target_link_libraries(PiInput-Install PRIVATE ole32 shell32 advapi32 user32)
    piinput_enable_warnings(PiInput-Install)

    install(TARGETS
        piinput-preview
        piinput-profile
        PiInputTSF
        PiInput-Install
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION bin
    )
endif()
install(FILES
    data/symbols.tsv
    data/base_lexicon.tsv
    data/english_lexicon.tsv
    DESTINATION data
)
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

    if(WIN32)
        add_executable(piinput-installer-layout-tests
            tests/installer_layout_tests.cpp
            platform/windows/installer/install_layout.cpp
        )
        target_include_directories(piinput-installer-layout-tests PRIVATE platform/windows/installer)
        piinput_enable_warnings(piinput-installer-layout-tests)
    endif()

    add_test(NAME piinput-core-tests COMMAND piinput-core-tests)
    add_test(NAME piinput-settings-tests COMMAND piinput-settings-tests)
    add_test(NAME piinput-candidate-grid COMMAND piinput-candidate-grid-tests)
    add_test(NAME piinput-full-pinyin-variants COMMAND piinput-full-pinyin-variants-tests)
    add_test(NAME piinput-incremental-decoder COMMAND piinput-incremental-decoder-tests)
    add_test(NAME piinput-english-lexicon COMMAND piinput-english-lexicon-tests)

    if(WIN32)
        add_test(NAME piinput-installer-layout-tests COMMAND piinput-installer-layout-tests)
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
        find_program(PIINPUT_POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)
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
            --min-entries 450000
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

    if(PIINPUT_TESTDATA_DIR)
        set(EXTERNAL_LEXICON "${PIINPUT_TESTDATA_DIR}/cache/piinput-base.lex")
        if(EXISTS "${EXTERNAL_LEXICON}")
            add_test(
                NAME piinput-external-dictionary-regression
                COMMAND piinput-core-tests --lexicon "${EXTERNAL_LEXICON}"
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
