# PiInput 免安装测试结果（2026-08-15）

## 结论

本轮只构建和测试免安装包，没有安装输入法、没有触发 UAC、没有修改 Windows 输入法注册，也没有关闭用户正在使用的软件。

生成文件：

```text
artifacts\PiInput-0.4.9-dev-portable-test-windows-x64.zip
```

这个包用于先验证输入核心。它不能代替 Notepad4、Notepad++、Word 中的 TSF 系统集成验收。

## 已确认的安装根因

当前系统曾同时出现旧版和新版 DLL，不是 Host 自动升级机制本身失效，而是旧安装器在发现“旧版本目录中的 DLL 与新 DLL 内容相同”时，继续复用了旧版本目录路径：

```text
Runtime\versions\0.4.8-...\bin\PiInputTSF.dll
```

正确注册路径必须始终是：

```text
Runtime\Shim\PiInputTSF.dll
```

源码已经增加回归测试并修正：只有注册路径本身就是固定 Shim 路径，且文件内容一致时，安装器才允许复用。版本目录中的旧 DLL 即使字节相同也不得继续作为 COM/TSF 稳定入口。

本轮没有执行修正后的安装器，因此当前 Windows 注册状态没有被更改。

## 免安装包自动结果

从最终 ZIP 重新解压并运行包内自测，得到：

```text
system_registration=NOT_TOUCHED
command_self_test=PASS
full_pinyin_ganjue=PASS
xiaohe_gjjt=PASS
symbol_celsius=PASS
dictionary_benchmark=PASS
dictionary_benchmark_wall_ms=369
symbol_command_;;f=PASS
symbol_command_double_grave_f=PASS
markdown_single_grave=PASS
markdown_inline_code=PASS
markdown_triple_grave=PASS
overall=PASS_WITH_DOCUMENTED_DEFERRED_ITEMS
```

其中词库性能项目使用包内真实二进制词库执行 50 次预热和 1000 次查询，并设置 P95 5 ms、P99 10 ms 的发布门禁。

## 命令状态

| 输入 | 当前结果 | 说明 |
|---|---|---|
| `;;f` | 通过 | 打开符号中心候选 |
| `；；f` | 通过同一物理键路由 | 系统输入法层按原始分号键送入，与 `;;f` 共用 Host 行为 |
| 双反引号 + `f` | 通过 | 打开同一符号中心候选 |
| 单反引号 | 通过 | 保持 ASCII 原样 |
| Markdown 行内代码 | 通过 | 不误触发命令 |
| 三反引号代码块 | 通过 | 不误触发命令 |
| `;;h` / 双反引号 + `h` | 未实现 | 中文帮助目录和数据尚未接入生产代码 |
| `;;u` / 双反引号 + `u` | 未实现 | 拆字数据、匹配算法和许可方案尚未实现 |

帮助和拆字项目没有伪报成功，也没有用硬编码少量示例代替完整实现。

## 自动化测试

- Release 全目标构建完成，包括核心、Host、TSF、安装器、卸载器、诊断工具、词库工具和全部测试目标。
- 首次完整 CTest 为 50/51；唯一失败是本轮重新生成 `SHA256SUMS.txt` 时误用了 ASCII 编码，中文文件名被写成问号。
- 校验清单改为 UTF-8 后，SHA 门禁单独复测通过。
- 修正清单编码并完成本文件后，最终 fresh CTest 为 51/51 通过、0 失败。

## 暂缓的真实应用测试

以下项目需要下一次使用修正后的安装器做一次干净安装后才能给出有效结论：

| 应用 | 状态 | 原因 |
|---|---|---|
| Notepad4 | 暂缓 | 当前系统注册仍可能指向旧版本目录 Shim |
| Notepad++ | 暂缓 | 同上；混合版本会让测试失真 |
| Microsoft Word | 暂缓 | 同上 |

下一次只需安装一次修正后的系统包，然后在三个全新进程中验证 `;;f`、全角分号物理键别名、双反引号命令、单/行内/三反引号、Shift、空格上屏、候选位置和一行展开规则。

## 使用免安装包

1. 完整解压 ZIP。
2. 双击 `Run-Portable-Tests.cmd`。
3. 查看同目录生成的 `Portable-Test-Result.txt`。
4. 自动测试完成后使用 `PiInput-Test.exe` 继续手工验证全拼、小鹤双拼、英文候选和自由文本框。

免安装包不包含安装器、卸载器、TSF DLL、Host 或注册工具，因此不会改变系统输入法，也不会要求管理员确认。
