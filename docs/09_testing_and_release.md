# 测试与发布规范

## 测试层次

### 单元测试

- UTF-16LE 转 UTF-8；
- SCEL 边界检查；
- 拼音音节；
- 全拼切分；
- 各双拼映射；
- 词库索引；
- 排序评分；
- 候选快照；
- 用户学习；
- 符号别名；
- 同步冲突规则。

### 回归测试

- 用户提供的两个 SCEL；
- 解析条目数和首末词；
- 过去出现过的候选错误；
- 候选跳变；
- 拼音中间编辑；
- 不同应用标点状态。

### 性能测试

- 冷启动；
- 热按键；
- 首候选延迟；
- 长拼音句子；
- 候选翻页；
- 大词库内存；
- 用户数据提交；
- IPC 往返；
- 符号搜索。

性能报告至少包含 P50、P95、P99，不只报告平均值。


### TSF 开发基线测试

TSF 源码进入版本后，必须分别记录：

- 当前非 Windows 环境可执行的核心、词库和 sanitizer 测试；
- 受限的 Windows 源码语法审查；
- 用户 Windows/MSVC 实际构建；
- COM 注册；
- 语言配置文件可见性；
- 记事本 Composition、候选和上屏；
- 卸载。

没有 Windows 真机输出时，不得把“源码已加入”写成“系统输入法已验证完成”。

### Windows 兼容测试

- 记事本；
- Visual Studio；
- VS Code；
- Chrome/Edge；
- Word/Excel/PowerPoint；
- 微信；
- Windows Terminal；
- 文件资源管理器；
- 系统搜索；
- 远程桌面；
- 高 DPI；
- 多显示器；
- 深色模式；
- 睡眠恢复。

## TDD 规则

解析器、排序算法、状态机、同步冲突和 bug 修复优先遵循：

1. 先写失败测试；
2. 验证测试确实因目标问题失败；
3. 写最小实现；
4. 运行测试通过；
5. 重构；
6. 再运行完整测试。

## 完成声明规则

在声称“完成、修复、通过”之前必须：

1. 找到能证明该声明的命令；
2. 重新执行完整命令；
3. 检查退出码和全部输出；
4. 对照需求清单；
5. 记录实际结果。

## 发布包结构

```text
lite-ime-vX.Y.Z-dev.zip
└─ lite-ime-dev/
  README.md
  PROJECT_CONTEXT.md
  VERSION
  docs/
  include/
  src/
  tools/
  platform/
  tests/
  scripts/
```

源码发布 ZIP 不包含 `build/`、`dist/`、`.vs/`、CMake 缓存、EXE、DLL、LIB 或 PDB。Windows 二进制应使用独立测试包交付。

每版必须有：

- `docs/release_notes_vX.Y.Z.md`；
- `docs/next_develop_plan_vA.B.C.md`；
- 验证结果；
- 可执行文件或明确构建说明；
- Git 命令。

## Git 命令格式

每次交付提供类似：

```powershell
git add .; git commit -m "feat: add SCEL parser and dictionary conversion baseline"; git push
```

提交信息必须准确描述改动，禁止只写 `update`。

## 完整构建成功判定

完整构建只有在以下条件全部满足时才能标记为成功：

1. CMake 配置成功；
2. 所有目标编译和链接成功；
3. 自动测试全部通过；
4. 安装步骤成功；
5. 发布清单中的每个预期产物都存在；
6. 产物来自当前构建，不是旧输出目录残留。

某个测试程序通过时，其他独立工具仍可能编译失败，因此不得只依据 `100% tests passed` 宣称完整构建成功。
