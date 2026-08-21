# PiInput v0.7.4 验证记录

## 修复依据

现场取证自一台真实安装了 v0.7.3 的机器：

```text
%LOCALAPPDATA%\PiInput\
├── bin\                      所有 exe 和 dll 直接在此，无版本子目录
│   └── *.retired.*         升级退休的旧文件，9 个
├── current.json            {"version_id":"0.7.3-20260819-151735-25768"}
├── data\
├── Uninstall\PiInput-Uninstall.exe
└── UserData\
```

- **根因一**：`resolve_active_version` 读出 `version_id` 后要求 `versions_root/<version_id>` 是目录。`versions_root` 就是 `bin`，而 `bin` 下没有任何版本子目录——统一布局本来就不建。于是抛出「PiInput active-version directory is missing」，每一次卸载都停在这里。
- **根因二**：`unregister_tsf` 找 `active/bin/piinput-profile.exe`，统一布局下等于 `bin/bin/piinput-profile.exe`；找 `developer_root/Shim/PiInputTSF.dll`，实际文件在 `bin/PiInputTSF.dll`。两条路径都停留在旧布局，即使绕过根因一也会失败。
- **根因三**：`run_worker` 是一条「全或无」的链。`--disable-user` 返回非零、`LoadLibraryExW` 失败、`DllUnregisterServer` 失败、`remove_or_schedule_legacy_runtime` 删不掉、注册表项删不掉，任何一项都抛异常中止整个卸载。而这些失败的原因往往正是「东西已经不在了」。
- **清理缺口**：`current.json` 不在 `uninstall_roots` 里。它留下来，下一次卸载又会去解析一个不存在的版本目录——根因一的触发条件由上一次卸载自己制造。`Runtime/`、`Dev/` 两棵历史目录树同样从未被清理，COM 类注册在 DLL 被删后也没人再去注销。

## 修复方式

- `resolve_active_version` 找不到版本目录时回退到统一布局；marker 不可读或含路径穿越时同样回退。它永远不会变成一个目录名，但也永远不该挡住卸载。
- 新增 `locate_uninstall_tools`，在所有历史布局目录里按顺序查找 `piinput-profile.exe`、`PiInputHost.exe`、`PiInputTSF.dll`：活动版本自己的 `bin` 最优先，然后是 `bin`、`bin/Shim`、`Runtime/Shim`、`Runtime/bin`、`Dev`、`Dev/bin`。找不到就报告为缺失，不当作损坏的安装。
- `unregister_tsf` 返回失败项列表而不是抛异常。反注册在删文件之前的顺序保留——不能让系统里留下指向已删文件的输入法。
- `run_worker` 收集所有失败并继续，返回残留清单。
- `uninstall_roots` 加入 `current.json`、`Runtime/`、`Dev/`；新增 `delete_com_registration` 清理 `HKCU/Software/Classes/CLSID/{13EB305F-2DA3-4CF7-8C45-16B016B801B5}` 和 `HKCU/Software/PiInput`。
- 结果提示：清干净就说清干净，有残留就逐条列出，不再把整个卸载叫做失败。
- 唯一保留的硬失败是 `validate_uninstall_layout`。不描述 PiInput 自己目录的布局绝不能交给递归删除。

## 契约变更

`tests/uninstall_layout_tests.cpp` 中两条断言的语义调整：

- 原「路径穿越 marker 必须被拒绝」「绝对路径 marker 必须被拒绝」用 `require_throws` 表达。现改为断言这类 marker **回退到统一布局**，并且断言返回值不是 marker 里的路径。安全边界不变——恶意 marker 依然永远不会变成目录名——但它不再让用户失去卸载能力。
- `uninstall_roots` 的断言从「精确元素个数与顺序」改为「必须包含哪些」，因为清理范围扩大了。「保留用户数据的卸载不得删除 UserData 和产品根目录」这条防御原样保留。

## 自动验证

- Windows x64 Release 全目标构建：通过；
- 完整 CTest：59/59 通过，0 失败；
- `piinput-uninstall-layout-tests`：通过，新增用例覆盖——marker 指向已删除的版本目录不再失败、不可读与路径穿越 marker 回退且不被当作路径、统一布局在 `bin` 下定位到三个程序、旧版布局在 `<版本>/bin` 与 `Shim` 下定位、Runtime 时代布局可定位、空安装报告无程序而不是失败、`current.json` 与 `Runtime`/`Dev` 进入清理范围；
- `piinput-uninstaller-source-regression`、`piinput-installer-layout-tests`、`piinput-uninstall-layout-tests`、`piinput-stable-runtime`：通过；
- 发布版本、文件清单与源码 SHA-256 门禁：通过。

## 人工验证要点

自动测试覆盖布局与查找逻辑，不覆盖真实注册表、真实 TSF 反注册和真实文件占用。以下必须在真机执行，步骤见 `v0.7.4安装、使用与测试.md`：

1. 在一台卸载失败的机器上运行本版卸载器，确认不再报 active-version 错误；
2. 卸载后检查 `%LOCALAPPDATA%\PiInput` 只剩 `UserData`，`current.json` 已消失；
3. 检查三处注册表键均已消失，`Win + Space` 中不再出现 PiInput；
4. 勾选删除用户数据再卸载一次，确认整个产品目录消失；
5. 安装后确认自动打开 `UserData` 目录，`--silent` 时不弹窗不开目录。
