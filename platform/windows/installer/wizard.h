#pragma once

#include "piinput/windows_compat.h"

#include <functional>
#include <string>

namespace piinput::windows {

// 安装向导。替代原来那三个 TaskDialog：确认、进度、完成。
//
// 换成自绘窗口不是为了好看，是因为那三个对话框说不完必须说的话。装完之后用户
// 不知道怎么用——Win+Space 切换、Shift 切中英、候选数字选词、fh/bq/sz 这些入口，
// 全都只写在随包的 markdown 里，而没有人会去读它。没有签名这件事也必须在动手
// 之前讲清楚，否则被 Defender 或智能应用控制拦下来时，用户会以为程序有毒。
//
// 不引入 NSIS / Inno Setup：它们会多带一个未签名的可执行壳，而这正是我们最怕的
// 东西。现在 Defender 已经会把 PiInputHost.exe 当木马隔离，再套一层第三方 stub
// 只会让启发式判定更难过。
struct WizardContext final {
    std::wstring program_root;
    std::wstring user_data;
    std::wstring version;
    bool upgrade{};
};

// 工作线程上执行真正的安装。`report` 回报 0–100 的百分比与当前步骤文字；返回
// false 表示失败，并把原因写进 `error`。
using WizardProgressReport = std::function<void(int percent, std::wstring text)>;
using WizardInstallAction =
    std::function<bool(const WizardProgressReport& report, std::wstring& error)>;

struct WizardOutcome final {
    // 用户是否走到了「开始安装」。取消时为 false，且什么都没写过。
    bool started{};
    bool succeeded{};
    std::wstring error;
    // 完成页上的三个勾选项。
    bool open_settings{};
    bool open_guide{};
    bool activate_profile{};
};

[[nodiscard]] WizardOutcome run_install_wizard(
    HINSTANCE instance,
    const WizardContext& context,
    const WizardInstallAction& run_install);

}  // namespace piinput::windows
