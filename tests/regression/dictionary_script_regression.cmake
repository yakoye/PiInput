file(READ "${SOURCE_DIR}/scripts/update-dictionaries.ps1" update_script)
file(READ "${SOURCE_DIR}/scripts/build-dictionaries.ps1" build_script)
file(READ "${SOURCE_DIR}/build.ps1" main_build_script)
file(READ "${SOURCE_DIR}/scripts/dev/update-dictionaries.cmd" command_script)
file(READ "${SOURCE_DIR}/dictionary_sources.json" source_manifest)
file(READ "${SOURCE_DIR}/docs/TSF_DEVELOPER_TEST.md" english_runtime_doc)
file(READ "${SOURCE_DIR}/docs/词库更新说明.md" dictionary_doc)

foreach(required_text
    "Join-Path (Split-Path -Parent $RepoRoot) \"dicts\""
    "existing cache was preserved"
    "dictionary-build-manifest.json")
    string(FIND "${update_script}${build_script}" "${required_text}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Dictionary update safety marker is missing: ${required_text}")
    endif()
endforeach()

foreach(required_bootstrap_text
    "[switch]$SkipTests"
    "if (-not $SkipTests)"
    "-SkipTests"
    "piinput-character-coverage-tests.exe")
    string(FIND "${main_build_script}${build_script}" "${required_bootstrap_text}" bootstrap_position)
    if(bootstrap_position EQUAL -1)
        message(FATAL_ERROR "Dictionary bootstrap/test-order marker is missing: ${required_bootstrap_text}")
    endif()
endforeach()

foreach(required_rime_text
    "[string]$RimeIceRoot"
    "[switch]$SkipInstall"
    "rime-ice-with-large-character-table-v2"
    "rime_ice.dict.yaml"
    "cn_dicts/8105.dict.yaml"
    "cn_dicts/base.dict.yaml"
    "cn_dicts/ext.dict.yaml"
    "cn_dicts/tencent.dict.yaml"
    "cn_dicts/others.dict.yaml"
    # The large character table is what makes anything past the 8105 standard
    # set typable at all. Rime ships it commented out of the master import
    # list, so it is loaded as its own source and has to stay listed here.
    "cn_dicts/41448.dict.yaml"
    "$arguments.Add(\"--rime-dictionary\")"
    "$arguments.Add(\"--rime-report\")"
    "if (-not $SkipInstall)")
    string(FIND "${build_script}" "${required_rime_text}" rime_position)
    if(rime_position EQUAL -1)
        message(FATAL_ERROR "Rime Ice dictionary marker is missing: ${required_rime_text}")
    endif()
endforeach()

foreach(required_professional_text
    "data/professional_lexicon.tsv"
    "piinput_professional"
    "$arguments.Add(\"--source\"); $arguments.Add(\"tsv\")"
    "事务层数据包"
    "链接器")
    string(FIND "${build_script}" "${required_professional_text}" professional_position)
    if(professional_position EQUAL -1)
        message(FATAL_ERROR "PiInput professional lexicon build marker is missing: ${required_professional_text}")
    endif()
endforeach()

file(STRINGS "${SOURCE_DIR}/data/professional_lexicon.tsv"
    professional_lexicon_lines ENCODING UTF-8)
list(FILTER professional_lexicon_lines EXCLUDE REGEX "^#")
list(FILTER professional_lexicon_lines EXCLUDE REGEX "^[	 ]*$")
list(FILTER professional_lexicon_lines EXCLUDE REGEX "^word[	]pinyin[	]weight$")
list(LENGTH professional_lexicon_lines professional_lexicon_count)
if(NOT professional_lexicon_count EQUAL 13)
    message(FATAL_ERROR
        "PiInput professional terminology patch must contain exactly 13 entries, found ${professional_lexicon_count}")
endif()
file(READ
    "${SOURCE_DIR}/tests/corpus/v0.2.0/tests/professional_vocabulary_test_cases.json"
    professional_corpus_text)
foreach(required_professional_term IN ITEMS
        "事务层"
        "事务层数据包"
        "配置读请求"
        "配置写请求"
        "内存读请求"
        "内存写请求"
        "完成报文"
        "完成超时"
        "可调整基地址寄存器"
        "输入输出内存管理单元"
        "进程地址空间标识符"
        "单根输入输出虚拟化"
        "链接器")
    string(REGEX MATCH
        "(^|;)${required_professional_term}[	][^;]+[	]20000($|;)"
        professional_lexicon_match "${professional_lexicon_lines}")
    if(NOT professional_lexicon_match)
        message(FATAL_ERROR
            "Required professional term is missing or malformed: ${required_professional_term}")
    endif()
    string(FIND "${professional_corpus_text}"
        "\"target_text\": \"${required_professional_term}\""
        professional_corpus_position)
    if(professional_corpus_position LESS 0)
        message(FATAL_ERROR
            "Required professional term has no structured corpus case: ${required_professional_term}")
    endif()
endforeach()

# The release dictionary comes from Rime Ice plus the small project-owned
# professional terminology table. Keep banning the superseded bulk sources;
# naming them individually lets the intentional TSV and rare-character sources
# remain explicit and provenance-auditable.
foreach(forbidden_release_source
    "$arguments.Add(\"pinyin-data\")"
    "$arguments.Add(\"phrase-pinyin-data\")"
    "$arguments.Add(\"thuocl\")"
    "data/base_lexicon.tsv"
    "rime-pinyin-simp/pinyin_simp.dict.yaml"
    "THUOCL_poem.txt"
    "phrase-pinyin-data/large_pinyin.txt")
    string(FIND "${build_script}" "${forbidden_release_source}" forbidden_position)
    if(NOT forbidden_position EQUAL -1)
        message(FATAL_ERROR "Old Chinese candidate source remains in release build: ${forbidden_release_source}")
    endif()
endforeach()

foreach(required_doc_text
    "word<TAB>positive_weight"
    "uint32"
    "builtin=1"
    "downloaded=2"
    "user=4"
    "proper=8"
    "ASCII-only"
    "english_user.tsv"
    "english_downloaded.tsv"
    "english_learning.tsv"
    "程序管理"
    "builtin_dictionary"
    "user_dictionary"
    "user_learning")
    string(FIND "${english_runtime_doc}${dictionary_doc}" "${required_doc_text}" doc_position)
    if(doc_position EQUAL -1)
        message(FATAL_ERROR "English dictionary documentation marker is missing: ${required_doc_text}")
    endif()
endforeach()

foreach(required_english_text
    "IncludeEnglish"
    "Get-FileHash"
    "convert-english-wordfreq.ps1"
    "generated"
    "english_lexicon.tsv"
    "LOCALAPPDATA"
    "english_downloaded.tsv"
    "RuntimeTsv"
    "wordfreq-en-25000"
    "existing cache was preserved"
    "9650fa612e121beb6126b9b4d7344da287013c6e"
    "51cc5521d1ff8cf4353f72199fd4d3ce3cbff9ca9e61858414b94314c9df35a6")
    string(FIND "${update_script}${source_manifest}" "${required_english_text}" english_position)
    if(english_position EQUAL -1)
        message(FATAL_ERROR "Optional English dictionary source marker is missing: ${required_english_text}")
    endif()
endforeach()

string(FIND "${command_script}" "ExecutionPolicy Bypass" command_position)
if(command_position EQUAL -1)
    message(FATAL_ERROR "Double-click dictionary command does not invoke PowerShell safely")
endif()
