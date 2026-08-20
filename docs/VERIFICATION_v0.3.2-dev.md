# PiInput v0.3.2-dev 验证记录

验证环境：Windows x64、Visual Studio 18 2026、MSVC 19.51、Windows SDK 10.0.26100.0。

## 根因证据

改名前可用版本和 PiInput 版本的注册流程、COM 范围和 TSF 键盘类别基本相同；关键差异是改名时同时更换了 CLSID 和语言 Profile GUID。Windows 将其视为一个全新的第三方 IME。

真机验证结果：

1. 新身份的 Profile 可返回 `registered=yes / enabled=yes / active=yes`，但不进入 `Get-WinUserLanguageList`，设置中短暂出现后消失。
2. 补充机器级 COM 注册不能解决问题。
3. 直接调用 Windows 用户语言列表接口仍会过滤新身份。
4. 仅恢复稳定内部身份仍不充分：Windows 设置可短暂显示 PiInput，但退出页面后仍会消失。
5. 根据 Windows TSF 类别要求补充 `GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT`，并让安装器每次升级都重新执行 DLL 注册，以刷新 Profile 和能力类别。
6. 同时确认旧安装只携带 349 条演示词库，`xnhe`、`xnheul` 空候选并非小鹤码错误；完整 459,505 条词库可以命中，并且核心解码器能够用单字候选组成完整路径。

## TDD 记录

修复过程保留了真实 RED→GREEN 证据：稳定身份断言最初 3 项失败；系统托盘类别源码门禁最初失败；安装器升级刷新类别门禁最初失败；完整词库安装与 TSF 优先加载门禁最初失败。对应实现完成后全部通过。`xnhe`、`xnheul` 及对应全拼的单字组合路径也加入增量解码测试。

## 安装状态

当前真机安装目录：

```text
%LOCALAPPDATA%\PiInput\Dev\versions\0.3.2-20260811-033304-5368
```

当前注册 DLL 位于该版本目录，Profile 状态：

```text
registered=yes
enabled=yes
active=no
flags=0x2
```

`active=no` 只表示验证时尚未在新文本程序中切换到 PiInput，不是注册失败。`Get-WinUserLanguageList` 已包含稳定 PiInput TIP 标识，安装目录中的完整词库为 15,211,009 字节、459,505 条。

## Release 验证

- Release 全目标构建：成功；
- CTest：24/24 通过（包括完整外部词库、SCEL、增量性能、安装器和 TSF 源码门禁）；
- `xnhe`：返回“小河”等候选；
- `xnheul`：返回完整三音节组合候选，不为空；
- 安装器静默真机安装：退出码 0；
- 系统 Profile：`registered=yes / enabled=yes`；
- 当前用户语言列表：包含 PiInput TIP 标识。
