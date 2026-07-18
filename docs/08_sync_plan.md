# 跨设备同步规划

## 定位

同步不是基础输入热路径的一部分。它最后开发，但本地数据从第一版起必须可同步。

## 同步内容分级

### A. 输入法数据

- 用户词条；
- 选词频率；
- 置顶词；
- 屏蔽词；
- 词库启用状态；
- 双拼方案和设置；
- 标点设置；
- 收藏符号；
- 自定义符号。

### B. 常用短语

- 地址；
- 邮箱；
- 常用回复；
- 设备型号；
- 代码片段；
- 用户主动添加的模板。

### C. 文本剪贴板

- Windows 与手机之间同步文本；
- 默认有大小限制和过期时间；
- 敏感应用可禁用；
- 历史记录可关闭；
- 不默认同步图片和文件。

## 数据标识

每个可同步对象至少包含：

```text
object_id
object_type
device_id
created_at
modified_at
deleted_at / tombstone
schema_version
logical_clock / revision
```

## 变更日志

```text
SyncEvent
  event_id
  device_id
  object_id
  operation
  encrypted_payload
  timestamp
  schema_version
```

删除必须同步 tombstone，不能简单物理删除后让旧设备再次上传恢复。

## 安全模型

- 二维码配对；
- 每台设备独立密钥；
- 端到端加密；
- 中继只保存密文；
- 支持撤销设备；
- 支持密钥轮换；
- 支持完全关闭；
- 不同步按键流和完整输入历史；
- 同步进程与 TSF DLL 隔离。

## 网络路径

```text
同一局域网：优先直连
不同网络：加密中继
```

## 平台限制

Android 与 iOS 对后台、剪贴板和键盘扩展的能力不同。移动端同步架构需要“主应用 + 输入法扩展”，尤其 iOS 必须在真机验证后才能承诺无感同步程度。
