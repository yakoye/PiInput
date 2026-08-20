# PiInput 版本更新与本地开发工作流

当前 Windows 运行时采用永久稳定 Shim + 版本化 Host。首次安装注册一次 `Runtime\Shim\PiInputTSF.dll`；普通升级只发布 `Runtime\versions\<版本>` 并切换 Host，不得要求用户关闭所有应用。

当前开发版本：`v0.6.1-dev`。默认源码包名由根目录 `VERSION` 生成。

## 1. 版本包命名

```text
piinput-vX.Y.Z-dev.zip
```

压缩包内部固定为：

```text
piinput-dev/
```

本地不使用带版本号的源码目录，版本号由 `VERSION` 和文档记录。

## 2. 推荐本地结构

```text
piinput/
├── dicts/
├── packages/
├── releases/
└── piinput-dev/
```

`dicts` 放用户自己的 SCEL，`packages` 存放历史 ZIP，`piinput-dev` 始终是当前源码。

## 3. 更新步骤

1. 关闭 `piinput-preview.exe`；
2. 将旧 `piinput-dev` 改名为备份，或确认 Git 已提交后删除；
3. 解压新版 ZIP 到 `piinput` 目录；
4. 确认得到新的 `piinput-dev`；
5. 从根目录运行 `setup-dev.cmd`；
6. 构建、测试和安装成功后再删除旧备份。

不要把新版直接覆盖到旧目录，因为旧文件和 CMake 缓存可能残留。

## 4. 一键入口

```powershell
.\setup-dev.cmd
```

或：

```powershell
.\setup-dev.ps1 -Configuration Release
```

只构建：

```powershell
.\build.cmd -Configuration Release -Clean
```

独立预览：

```powershell
.\start-preview.cmd
```

TSF 注册检查：

```powershell
.\verify-windows.ps1
```

修复开发版注册：

```powershell
.\repair-registration.ps1
```

卸载开发预览并保留用户数据：

```powershell
.\uninstall-dev.ps1
```

同时删除用户词库和学习数据：

```powershell
.\uninstall-dev.ps1 -RemoveUserData
```

## 5. 发布物类型

### 完整源码开发包

```text
piinput-vX.Y.Z-dev.zip
```

包含源码、文档、测试和构建脚本，不包含 build/dist。

### Windows 可执行测试包

后续 Windows CI 可产出：

```text
piinput-vX.Y.Z-dev-windows-x64.zip
```

### 正式安装包

TSF 真机链路和兼容性稳定后产出：

```text
PiInput-Setup-vX.Y.Z-dev-x64.exe
```

正式版：

```text
PiInput-Setup-v1.0.0-x64.exe
```

## 6. 用户数据升级约束

开发包更新和程序升级默认必须保留：

- 导入词库；
- 用户学习；
- 收藏和自定义符号；
- 设置；
- 同步设备信息。

任何清除用户数据的行为必须由用户显式选择。
