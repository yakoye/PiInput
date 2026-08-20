# PiInput v0.4.4-dev 验证记录

验证环境：Windows x64、MSVC 19.51、Visual Studio 18 Build Tools、Windows SDK 10.0.26100.0。

## RED / GREEN

- RED：`fwihkk` 具有多于一行自动组合时，第一次 `=` 仍进入第二行乱词；
- GREEN：弱组合不再扩展可信分页，第一次 `=` 进入 `非` 开始的单字/分段选择；
- RED：Presenter 首次 stage 后不可见，每个新 generation 都执行 `window_.hide()`；
- GREEN：首个快照立即使用降级 caret 显示，新 generation 复用上一文本 caret，不再隐藏；
- GREEN：候选内容完全相同时跳过无效重画，普通更新使用无背景擦除失效区域。

## 最终验证

- Windows Release 全目标单并发构建：通过；
- 除发布文件哈希门禁外的 CTest：`50/50` 通过，`0` 失败；
- 进程级 Host、Host 重启交接、真实外部词库、SCEL、3500/7000 汉字覆盖：通过；
- `piinput-performance-smoke` 与真实外部增量性能门禁：通过；
- 完整 CTest：`51/51` 通过，`0` 失败，实际用时 `106.22 s`；
- CMake 安装布局：通过，安装器、卸载器、测试台、Host、稳定 TSF DLL 和数据文件齐全；
- 发布 ZIP 展开检查：通过，Host `--build-id` 为 `0.4.4`；
- 随包二进制基础词库：`4,004,169` 字节；
- ZIP SHA-256 写入同名 `.sha256.txt`，最终值以发布目录中的伴随文件为准。
