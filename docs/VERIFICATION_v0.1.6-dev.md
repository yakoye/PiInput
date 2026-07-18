# LiteIME v0.1.6-dev 验证记录

验证日期：2026-07-18

## 1. 修复对象

用户在 Windows 真机执行 `repair-registration.ps1` 时，旧配置文件停用返回：

```text
DeactivateProfile failed: 0x80004005
```

旧配置文件不存在或未激活时，清理操作应视为幂等状态，而不是致命错误。

## 2. 源码检查

已确认：

- `repair-registration.ps1` 对旧 profile 停用和旧 DLL 注销采用 best-effort；
- 安装和卸载脚本同样不会因旧状态缺失而提前退出；
- 新增 `ITfInputProcessorProfileMgr::RegisterProfile` 注册路径；
- `bEnabledByDefault` 设置为 `TRUE`；
- 注册标志为 `0`，不隐藏于 Windows 设置界面；
- profile tool 增加 `--register`、`--unregister`、`--status`；
- 安装、修复和验证流程都会检查 `registered=yes` 与 `enabled=yes`。

## 3. 自动测试

已运行 Linux Release 构建、核心测试、Windows 源码回归测试和真实 SCEL 回归测试。

结果：

```text
3/3 tests passed
```

测试项：

```text
liteime-core-tests
liteime-windows-source-regression
liteime-scel-regression
```

## 4. Sanitizer

已运行 AddressSanitizer 和 UndefinedBehaviorSanitizer Debug 构建。

结果：

```text
3/3 tests passed
未报告 AddressSanitizer 错误
未报告 UndefinedBehaviorSanitizer 错误
```

## 5. Windows 待验证

用户需要执行：

```powershell
.\setup-dev.cmd
```

若已安装本版，也可执行：

```powershell
.\repair-registration.ps1
```

必须继续确认：

1. `LiteImeTSF.dll` 和 `liteime-profile.exe` 编译成功；
2. `--status` 输出 `registered=yes`；
3. `--status` 输出 `enabled=yes`；
4. Windows“添加键盘”列表出现 LiteIME；
5. `Win + Space` 出现 LiteIME；
6. 记事本中小鹤输入 `jisrji` 后可上屏“计算机”。
