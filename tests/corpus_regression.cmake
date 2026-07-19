set(CORPUS_ROOT "${SOURCE_DIR}/tests/corpus/v0.2.0")
set(VALIDATOR "${CORPUS_ROOT}/tools/validate_test_corpus.py")

if(NOT EXISTS "${CORPUS_ROOT}/MANIFEST.json")
    message(FATAL_ERROR "PiInput v0.2.0 corpus manifest is missing")
endif()
if(NOT EXISTS "${VALIDATOR}")
    message(FATAL_ERROR "PiInput v0.2.0 corpus validator is missing")
endif()

find_program(PYTHON_EXECUTABLE NAMES python3 python REQUIRED)
execute_process(
    COMMAND "${PYTHON_EXECUTABLE}" "${VALIDATOR}"
    WORKING_DIRECTORY "${CORPUS_ROOT}"
    RESULT_VARIABLE VALIDATION_RESULT
    OUTPUT_VARIABLE VALIDATION_OUTPUT
    ERROR_VARIABLE VALIDATION_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT VALIDATION_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Corpus validation failed with ${VALIDATION_RESULT}\n"
        "stdout:\n${VALIDATION_OUTPUT}\n"
        "stderr:\n${VALIDATION_ERROR}")
endif()

foreach(EXPECTED_LINE IN ITEMS
    "standard pinyin syllables: 407"
    "structured test cases: 786"
    "language-model cases: 84"
    "professional vocabulary cases: 59"
    "correction cases: 160"
    "fuzzy-pinyin cases: 20"
    "user-learning cases: 12")
    string(FIND "${VALIDATION_OUTPUT}" "${EXPECTED_LINE}" FOUND_AT)
    if(FOUND_AT EQUAL -1)
        message(FATAL_ERROR
            "Corpus validator did not report '${EXPECTED_LINE}'\n${VALIDATION_OUTPUT}")
    endif()
endforeach()

file(READ "${CORPUS_ROOT}/keyboard/printable_keys.json" PRINTABLE_KEYS)
string(FIND "${PRINTABLE_KEYS}" "ascii_printable" ASCII_FIELD)
if(ASCII_FIELD EQUAL -1)
    message(FATAL_ERROR "Printable-key corpus is missing ascii_printable")
endif()

message(STATUS "${VALIDATION_OUTPUT}")
