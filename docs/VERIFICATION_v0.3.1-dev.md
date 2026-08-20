# PiInput v0.3.1-dev 验证记录

> 后续更正：v0.3.1 将“缺少 `InstallLayoutOrTip`”误判为唯一根因。Windows 真机验证表明，该接口即使返回成功，也会过滤改名时新生成的 TSF 身份。v0.3.2 已恢复稳定内部身份；详见 `VERIFICATION_v0.3.2-dev.md`。

验证环境：Windows x64、Visual Studio 18 2026、MSVC 19.51、Windows SDK 10.0.26100.0。

## 根因证据

失败安装的运行目录、COM 注册、TSF Profile 均已存在，`piinput-profile.exe --status` 返回：

```text
registered=yes
enabled=yes
active=yes
flags=0x3
```

但当前用户的 `Get-WinUserLanguageList` 中没有 PiInput 的 TIP 标识：

```text
0x0804:{D73AABA7-BE3E-4E53-8DE2-652D352743F3}{13D6EB0B-023B-4AA8-ADE1-2A360820EC49};
```

因此根因是安装器只注册并激活 TSF Profile，没有调用 `InstallLayoutOrTip` 将 PiInput 加入当前用户 Windows 键盘列表。

## TDD 记录

修复前新增测试分别因缺少用户键盘注册模块、缺少安装命令序列而编译失败。修复后：

```text
piinput-user-keyboard-registration：passed
piinput-installer-layout-tests：passed
```

测试覆盖：

- 完整 TIP 标识；
- 安装标志 `0`；
- 卸载标志 `ILOT_UNINSTALL (0x00000001)`；
- Windows API 失败向安装器传播；
- 安装顺序固定为 `--enable-user`、`--activate`、`--status`。

## Windows Release 构建

```text
配置：Release
架构：x64
所有 C++ 目标：构建成功
PiInputTSF.dll：构建成功
PiInput-Install.exe：构建成功
```

## 全量测试

```text
CTest：23/23 passed
失败：0
总测试时间：27.20 秒
```

其中包括核心输入、全拼、小鹤双拼、增量候选、英文候选、候选网格、安装器、用户键盘列表、旧数据迁移、符号、发布元数据、完整性、真实 SCEL 和外部大词库性能测试。

## 安装包

```text
PiInput-v0.3.1-dev-windows-x64.zip
大小：2,325,991 字节
SHA-256：b407d99958409a2ee8daf4d754af23978b70b5ff7f6365f9dc9e7f6b929e2b32
```

压缩包已检查，最外层包含可直接双击的 `PiInput-Install.exe`、中文安装指南、`bin` 和 `data`。包内 `piinput-profile.exe --help` 已确认包含 `--enable-user` 和 `--disable-user`。

## 人工测试边界

按用户要求，本轮没有自动运行 v0.3.1 安装器。最终 `Win + Space` 可见性由用户重新安装该包后验证。
