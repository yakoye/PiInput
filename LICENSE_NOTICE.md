# License Notice

LiteIME 尚未确定公开发布许可证。除非后续版本明确附带许可证，否则源代码和文档仅用于当前项目开发、测试和评审。

用户提供的第三方 SCEL 文件及其转换结果不因进入测试流程而改变原有权利状态。公开发布时不得默认打包没有明确再分发授权的第三方完整词库。

`update-dictionaries.cmd` 下载的数据保存在外部本地 `dicts/sources`，不随本仓库重新分发：

- `mozillazg/pinyin-data`：MIT；
- `mozillazg/phrase-pinyin-data`：MIT；
- `rime/rime-pinyin-simp`：Apache-2.0；
- `thunlp/THUOCL`：MIT，仅作为可选领域来源下载，默认不合并。

LiteIME 不提取或重新分发已安装商业输入法的内部基础词库。
