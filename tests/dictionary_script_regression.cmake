file(READ "${SOURCE_DIR}/scripts/update-dictionaries.ps1" update_script)
file(READ "${SOURCE_DIR}/scripts/build-dictionaries.ps1" build_script)
file(READ "${SOURCE_DIR}/update-dictionaries.cmd" command_script)
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

foreach(required_doc_text
    "word<TAB>positive_weight"
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
