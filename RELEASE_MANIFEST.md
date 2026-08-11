# PiInput v0.3.0-dev 发布清单

## Windows x64 用户包

```text
PiInput-v0.3.0-dev-windows-x64.zip
└── PiInput-v0.3.0-dev-windows-x64/
    ├── PiInput-Install.exe
    ├── 安装与使用指南.md
    ├── LICENSE_NOTICE.md
    ├── bin/
    │   ├── PiInputTSF.dll
    │   ├── PiInput-Install.exe
    │   ├── piinput-profile.exe
    │   ├── piinput-preview.exe
    │   └── 其他开发工具
    └── data/
        ├── base_lexicon.tsv
        ├── english_lexicon.tsv
        └── symbols.tsv
```

用户完整解压后双击最外层 `PiInput-Install.exe`。安装器自动完成：

- Windows 管理员权限确认；
- TSF DLL 和语言 Profile 注册；
- 版本并存安装，不覆盖已加载 DLL；
- 自动发现唯一的旧运行目录；
- 用户设置、词库和学习数据事务迁移；
- 冲突文件保留为 `.legacy-import`；
- 失败回滚；
- 锁定旧文件安排在重启后删除。

## 发布门禁

- Windows Release 全目标构建；
- 全量 CTest；
- 407 个全拼音节和 786 条结构化语料校验；
- 全拼、小鹤双拼、增量前缀、长句和专业词汇回归；
- 全部内置符号和键盘标点回归；
- 英文候选、事务下载、学习合并和排序稳定性回归；
- 安装布局、迁移、回滚和路径边界回归；
- 品牌、发布元数据和 SHA-256 完整性门禁；
- 外部大词库性能与真实 SCEL 回归。

## 不进入用户包

- `build/`、`dist/`、`.git/`、`.vs/`；
- OBJ、PDB、LIB 等中间文件；
- 用户自行下载的 SCEL；
- 用户设置和学习数据；
- 测试源码与开发历史文档。

## 已知边界

- 当前仅发布 Windows x64 开发测试版；
- 尚未提供正式代码签名；
- 32 位宿主和 ARM64 尚未验证；
- 候选 UI、高 DPI、多显示器和完整设置界面仍需继续完善；
- 英文候选默认关闭，所有输入功能均可离线使用。
