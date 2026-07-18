cmake_minimum_required(VERSION 3.24)

project(LiteIME VERSION 0.2.0 LANGUAGES CXX)

option(LITEIME_BUILD_TESTS "Build LiteIME tests" ON)
option(LITEIME_STATIC_MSVC_RUNTIME "Use the static MSVC runtime" ON)
set(LITEIME_TESTDATA_DIR "" CACHE PATH "Directory containing the two SCEL regression files")

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

if(MSVC AND LITEIME_STATIC_MSVC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(liteime_core STATIC
    src/scel_parser.cpp
    src/utf.cpp
    src/lexicon.cpp
    src/binary_lexicon.cpp
    src/dictionary_builder.cpp
    src/candidate_paging.cpp
    src/input_mode.cpp
    src/pinyin.cpp
    src/shuangpin.cpp
    src/engine.cpp
    src/symbols.cpp
    src/punctuation.cpp
    src/session.cpp
    src/user_model.cpp
)

target_include_directories(liteime_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(liteime_core
    PUBLIC
        LITEIME_VERSION="${PROJECT_VERSION}"
)

function(liteime_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

liteime_enable_warnings(liteime_core)

add_executable(liteime-scel-converter
    tools/scel_converter/main.cpp
)
target_link_libraries(liteime-scel-converter PRIVATE liteime_core)
liteime_enable_warnings(liteime-scel-converter)

add_executable(liteime-lexicon-compiler
    tools/lexicon_compiler/main.cpp
)
target_link_libraries(liteime-lexicon-compiler PRIVATE liteime_core)
liteime_enable_warnings(liteime-lexicon-compiler)

add_executable(liteime-dictionary-builder
    tools/dictionary_builder/main.cpp
)
target_link_libraries(liteime-dictionary-builder PRIVATE liteime_core)
liteime_enable_warnings(liteime-dictionary-builder)

add_executable(liteime-cli
    tools/ime_cli/main.cpp
)
target_link_libraries(liteime-cli PRIVATE liteime_core)
liteime_enable_warnings(liteime-cli)

add_executable(liteime-benchmark
    tools/benchmark/main.cpp
)
target_link_libraries(liteime-benchmark PRIVATE liteime_core)
liteime_enable_warnings(liteime-benchmark)

install(TARGETS
    liteime-scel-converter
    liteime-lexicon-compiler
    liteime-dictionary-builder
    liteime-cli
    liteime-benchmark
    RUNTIME DESTINATION bin
)

if(WIN32)
    add_executable(liteime-preview WIN32
        platform/windows/preview/main.cpp
    )
    target_link_libraries(liteime-preview PRIVATE liteime_core comctl32 shell32 ole32)
    liteime_enable_warnings(liteime-preview)

    add_library(LiteImeTSF SHARED
        platform/windows/tsf/dllmain.cpp
        platform/windows/tsf/text_service.cpp
        platform/windows/tsf/candidate_window.cpp
        platform/windows/tsf/LiteImeTSF.def
    )
    target_include_directories(LiteImeTSF PRIVATE platform/windows/tsf)
    target_link_libraries(LiteImeTSF PRIVATE
        liteime_core
        ole32
        shell32
        user32
        gdi32
        advapi32
        uuid
    )
    set_target_properties(LiteImeTSF PROPERTIES PREFIX "" OUTPUT_NAME "LiteImeTSF")
    liteime_enable_warnings(LiteImeTSF)

    add_executable(liteime-profile
        platform/windows/tsf/profile_tool.cpp
    )
    target_include_directories(liteime-profile PRIVATE platform/windows/tsf)
    target_link_libraries(liteime-profile PRIVATE liteime_core ole32 shell32 uuid)
    liteime_enable_warnings(liteime-profile)

    install(TARGETS
        liteime-preview
        liteime-profile
        LiteImeTSF
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION bin
    )
endif()
install(FILES data/symbols.tsv data/base_lexicon.tsv DESTINATION data)
install(FILES examples/sample_lexicon.tsv DESTINATION data)

if(LITEIME_BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(liteime-core-tests
        tests/test_main.cpp
    )
    target_link_libraries(liteime-core-tests PRIVATE liteime_core)
    target_compile_definitions(liteime-core-tests PRIVATE
        LITEIME_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    liteime_enable_warnings(liteime-core-tests)

    add_test(NAME liteime-core-tests COMMAND liteime-core-tests)

    add_test(
        NAME liteime-windows-source-regression
        COMMAND ${CMAKE_COMMAND}
            -DLITEIME_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/windows_source_regression.cmake
    )
    add_test(
        NAME liteime-dictionary-script-regression
        COMMAND "${CMAKE_COMMAND}"
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/tests/dictionary_script_regression.cmake
    )
    add_test(
        NAME liteime-performance-smoke
        COMMAND liteime-benchmark
            --lexicon ${CMAKE_SOURCE_DIR}/data/base_lexicon.tsv
            --schema full
            --query wo
            --iterations 2000
            --warmup 200
            --max-p95-us 2000
            --max-p99-us 5000
    )

    if(LITEIME_TESTDATA_DIR)
        set(EXTERNAL_LEXICON "${LITEIME_TESTDATA_DIR}/cache/liteime-base.lex")
        if(EXISTS "${EXTERNAL_LEXICON}")
            add_test(
                NAME liteime-external-dictionary-regression
                COMMAND liteime-core-tests --lexicon "${EXTERNAL_LEXICON}"
            )
        endif()
        set(ELECTRONICS_SCEL "${LITEIME_TESTDATA_DIR}/电子词汇大全【官方推荐】.scel")
        set(COMPUTER_SCEL "${LITEIME_TESTDATA_DIR}/计算机词汇大全【官方推荐】.scel")
        if(EXISTS "${ELECTRONICS_SCEL}" AND EXISTS "${COMPUTER_SCEL}")
            add_test(
                NAME liteime-scel-regression
                COMMAND liteime-core-tests "${ELECTRONICS_SCEL}" "${COMPUTER_SCEL}"
            )
        endif()
    endif()
endif()
