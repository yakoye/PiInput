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

# The release dictionary comes from Rime Ice and nothing else. Naming the old
# sources individually rather than banning --source outright: a Rime character
# table loaded through --source has the same provenance as the ones the master
# file imports, and blocking it blocked the rare characters with it.
foreach(forbidden_release_source
    "$arguments.Add(\"tsv\")"
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
