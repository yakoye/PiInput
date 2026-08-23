# PiInput v0.7.13 版本说明

v0.7.13 是词库覆盖、发布工程和长时间运行质量的收口版本。

## 候选质量

- 增加项目自有的专业词 TSV，补齐结构化语料中 13 个明确缺失的 PCIe、IOMMU、虚拟化和工具链术语。
- 59 个专业词用例全部从“已知缺失”改为真实候选排名断言；结构化语料仍覆盖 313 个用例。
- 修复超长完整拼音只检查主分段、忽略正确备选分段的问题，长专业词可以按完整拼音命中。

## 大词库内存

- `.lex` 改为操作系统只读内存映射，不再把 92 万条记录和字符串池复制成近百万个 C++ 字符串对象。
- 精确、前缀、简拼和反向查词直接读取映射记录；反向与简拼索引按需建立并可在线程间共享。
- Host 健康信息新增 `lexicon_storage` 与 `lexicon_mapped_bytes`，性能工具也输出相同证据。

## Windows 发布闭环

- 新增 Windows GitHub Actions：构建固定来源的大词库、全量 CTest/JUnit、隔离 Authenticode 签名、前版 Release 覆盖升级、安装×2、受控 TSF 实际 DLL smoke、卸载、公开资产回下载及失败证据归档。
- CI 成功或失败都会聚合 schema v1 `result.json` 和 SHA-256 artifact manifest，严格区分 `PASS/FAIL/BLOCKED/NOT_RUN/N/A`，拒绝重复 Case ID、非法状态和缺失证据。
- 版本标签强制要求代码签名证书；无证书时不会把未签名产物当作正式发布。
- 发布包精确校验 tag commit build ID、SHA-256、安装后 Host/TSF 哈希、注册路径、实际加载模块、签名者和 RFC3161 时间戳，并拒绝源码、调试文件、源码映射和 PowerShell 脚本泄漏。
- 正式 tag 在任何构建和签名前要求 Host-only 8h、TSF/App 8h 与 P0 真实宿主矩阵全部有 PASS 证据；未完成时流水线直接阻断。

正式发布仍需使用可信代码签名证书跑标签流水线，并完成真实应用输入框验收。
