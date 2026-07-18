# 技术参考

## Windows 输入法和 TSF

- Microsoft Learn: Text Service Registration  
  `https://learn.microsoft.com/en-us/windows/win32/tsf/text-service-registration`
- Microsoft Learn: ITfInputProcessorProfiles::Register  
  `https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinputprocessorprofiles-register`
- Microsoft Learn: ITfInputProcessorProfiles::AddLanguageProfile  
  `https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinputprocessorprofiles-addlanguageprofile`
- Microsoft Learn: ITfInputProcessorProfileMgr  
  `https://learn.microsoft.com/en-us/windows/win32/api/msctf/nn-msctf-itfinputprocessorprofilemgr`
- Microsoft Learn: msctf.h header  
  `https://learn.microsoft.com/en-us/windows/win32/api/msctf/`
- Microsoft Learn: ITfInputProcessorProfileMgr::ActivateProfile  
  `https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinputprocessorprofilemgr-activateprofile`
- Microsoft Learn: ITfContext::RequestEditSession  
  `https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-requesteditsession`
- Microsoft Learn: ITfContextComposition::StartComposition  
  `https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontextcomposition-startcomposition`
- Microsoft Windows classic samples / Sample IME 相关代码。

v0.1.4-dev 的 TSF 实现以官方 COM、语言配置文件、edit session 和 composition 接口为依据。当前只是最小开发基线，后续仍需与 Microsoft Sample IME 的生命周期、候选 UI、显示属性和兼容策略继续交叉审查。

## SCEL 社区解析实现

SCEL 没有作为 LiteIME 的稳定公开格式。当前解析结构通过多个公开解析器交叉核对，并使用用户真实文件验证：

- `lewangdev/scel2txt`
- `howl-anderson/scel2txt`
- `shonenada/scel2txt.py`

采用社区实现时只参考二进制结构事实，LiteIME 代码为独立 C++20 实现，并加入严格边界检查、UTF-16 代理项处理、错误偏移和回归测试。

## 开发方法

- 用户提供的 `superpowers-skills(4).zip`；
- 重点参考 brainstorming、writing-plans、test-driven-development、systematic-debugging、verification-before-completion 和 Git worktree/代码审查流程。

## 双拼映射

内置小鹤、自然码、微软和智能 ABC 映射，使用 Rime `rime-double-pinyin` 项目中的对应 schema 交叉核对。LiteIME 没有复制 Rime 引擎代码，而是将键位事实重新实现为独立 C++ 映射和标准拼音解码测试。

公开发布前必须再次审查：

- 映射事实与实现代码的版权边界；
- 第三方方案名称和说明；
- 是否需要在 NOTICE 中补充引用和许可证信息。
