# PiInput v0.1.6-dev 版本说明

## 版本目标

修复 Windows 开发版注册修复脚本在旧配置文件不存在或未激活时被 `DeactivateProfile failed: 0x80004005` 提前终止的问题，并将 TSF 配置文件注册流程切换为 Windows Vista 以后推荐的 `ITfInputProcessorProfileMgr` 接口。

## 用户真实问题

执行：

```powershell
.\repair-registration.ps1
```

出现：

```text
DeactivateProfile failed: 0x80004005
```

该错误发生在“清理旧注册”阶段。旧配置文件本来就没有成功注册，因此无法停用是可接受状态，不应阻止后续重新注册。

## 本版修改

### 1. 修复 PowerShell 错误处理

以下操作改为幂等、尽力执行：

- 停用旧配置文件；
- 注销旧 DLL；
- 卸载不存在的旧配置文件。

即使旧版本不存在、未激活或注册不完整，也会继续执行新版注册。

### 2. 使用现代 TSF 配置文件管理接口

新增共享注册实现：

```text
platform/windows/tsf/profile_registration.h
```

统一使用：

```text
ITfInputProcessorProfileMgr::RegisterProfile
ITfInputProcessorProfileMgr::UnregisterProfile
ITfInputProcessorProfileMgr::GetProfile
ITfInputProcessorProfileMgr::ActivateProfile
ITfInputProcessorProfileMgr::DeactivateProfile
```

注册时设置：

```text
bEnabledByDefault = TRUE
dwFlags = 0
```

确保配置文件默认启用，并且不使用隐藏于设置界面的标志。

### 3. 增加配置文件状态检查

`piinput-profile.exe` 新增：

```text
--register
--unregister
--status
```

`--status` 输出：

```text
registered=yes/no
enabled=yes/no
active=yes/no
flags=0x...
```

安装和修复脚本会在完成后检查配置文件确实已注册并启用。

### 4. 修复脚本刷新文本输入服务

`repair-registration.ps1` 完成后会重启当前用户的 `ctfmon.exe`，减少必须注销 Windows 才能看到新输入法的情况。

## Windows 验收条件

执行：

```powershell
.\setup-dev.cmd
```

或：

```powershell
.\repair-registration.ps1
```

最终应显示：

```text
registered=yes
enabled=yes
```

然后在 Windows 设置的“添加键盘”或 `Win + Space` 中看到：

```text
PiInput 中文输入法（开发版）
```

## 已知限制

当前发布环境没有 Windows SDK/MSVC，无法代替用户完成 Windows 真机编译、注册及语言列表刷新验证。本版已完成跨平台构建测试和 Windows 源码回归检查，最终 Windows 状态以用户机器输出为准。
