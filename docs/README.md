# docs 文档目录

根目录只保留当前开发、测试和 v0.7.15 发布仍会直接使用的文档。

## 当前必需文档

- 产品与工程基线：`260719_0854_01产品定义.md`、`02总体架构.md`、`04开发约束.md`、`05Windows技术栈.md`、`06词库与SCEL.md`、`07标点与符号.md`、`构建排错.md`。
- 用户与运维指南：`安装与使用指南.md`、`稳定入口与无重启升级说明.md`、`词库更新说明.md`、`词库查询与分段取字.md`。
- 当前发布：`release_notes_v0.7.15.md`、`VERIFICATION_v0.7.15.md`、`v0.7.15安装、使用与测试.md`、`三个长文本打字测试_v0.7.15.md`。
- QA 体系：六份 `PiInput_*_v1.1/v1.2.md` 是当前可维护版本。
- 发布辅助：`PORTABLE_TEST_GUIDE.md`、`PORTABLE_TEST_RESULTS_2026-08-15.md`、`TSF_DEVELOPER_TEST.md`、`WORKSPACE_LAYOUT.md`。

发布脚本会直接读取当前版本的版本说明、验证记录、安装测试和长文本测试，因此这些文件在当前版本完成前不能移动。

## archive

- `archive/releases/vX.Y.Z`：旧版本说明、验证记录、安装测试、打字测试和开发计划。
- `archive/history`：不属于某个版本的旧同步计划、旧审计、旧待办和历史对照记录。
- `archive/original-docx`：六份 QA 初版 Word 文档；当前维护以根目录 Markdown 新版为准。
- `archive/plans/superpowers`：开发过程中产生的旧规格和执行计划，不是当前规范。

归档是移动，不是删除。历史文档中的路径和状态反映当时版本，不应当作为当前发布结论。

