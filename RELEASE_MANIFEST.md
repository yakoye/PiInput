# Release Manifest — PiInput v0.1.6-dev

## Package

```text
piinput-v0.1.6-dev.zip
└── piinput-dev/
```

## Core deliverables

- C++20 cross-platform input core;
- full-pinyin segmentation;
- Flypy, Natural, Microsoft and Intelligent ABC shuangpin;
- SCEL converter;
- binary lexicon compiler;
- sentence candidate decoder;
- candidate snapshot/session model;
- local user learning;
- punctuation transformer;
- symbol search index;
- built-in starter base lexicon;
- deterministic base + SCEL merge workflow;
- performance benchmark;
- improved native Windows preview;
- Windows TSF DLL source and build target;
- idempotent TSF install/repair/uninstall workflow;
- modern `ITfInputProcessorProfileMgr::RegisterProfile` registration;
- profile status and explicit register/unregister commands;
- root build/setup/install workflow.

## Required documentation

- `README.md`;
- `PROJECT_CONTEXT.md`;
- `docs/03_development_tasks.md`;
- `docs/04_development_constraints.md`;
- `docs/RELEASE_WORKFLOW.md`;
- `docs/TSF_DEVELOPER_TEST.md`;
- `docs/release_notes_v0.1.6-dev.md`;
- `docs/VERIFICATION_v0.1.6-dev.md`;
- `docs/next_develop_plan_v0.1.7.md`;
- `docs/10_continuation_guide.md`;
- `docs/GIT_COMMANDS_v0.1.6-dev.md`.

## Excluded from the source package

- `build/`;
- `dist/`;
- `.vs/`;
- `.git/`;
- CMake caches;
- generated EXE/DLL/LIB/PDB/OBJ files;
- user SCEL dictionaries;
- user learning data.

## Known boundary

`v0.1.6-dev` fixes the repair/install control flow that stopped on an absent or inactive previous profile and migrates profile registration to `ITfInputProcessorProfileMgr::RegisterProfile`. Core code, starter dictionary, SCEL regressions and Windows source regressions are verified in the available environment. Windows TSF DLL compilation, registration, Settings visibility and real application input must still be verified on the user's Visual Studio 2026 machine.
