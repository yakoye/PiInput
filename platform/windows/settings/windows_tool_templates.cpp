#include "windows_tool_templates.h"

#include <array>

namespace piinput::windows {
namespace {

// Adapted from YeTool's MIT-licensed Windows tool template library. PiInput
// stores only category/name/launch target; imported items become ordinary,
// editable shortcut rows. The attribution is also recorded in LICENSE_NOTICE.md.
constexpr std::array<WindowsToolTemplate, 96U> kTemplates{{
    {L"第三方工具", L"Everything 文件搜索", L"system:everything"},

    {L"系统属性", L"高级系统属性", L"%SystemRoot%\\System32\\SystemPropertiesAdvanced.exe"},
    {L"系统属性", L"性能选项", L"%SystemRoot%\\System32\\SystemPropertiesPerformance.exe"},
    {L"系统属性", L"远程系统属性", L"%SystemRoot%\\System32\\SystemPropertiesRemote.exe"},
    {L"系统属性", L"计算机名", L"%SystemRoot%\\System32\\SystemPropertiesComputerName.exe"},
    {L"系统属性", L"系统保护", L"%SystemRoot%\\System32\\SystemPropertiesProtection.exe"},
    {L"系统属性", L"系统环境变量", L"shell:%SystemRoot%\\System32\\rundll32.exe|sysdm.cpl,EditEnvironmentVariables"},

    {L"系统管理", L"注册表编辑器", L"%SystemRoot%\\regedit.exe"},
    {L"系统管理", L"计算机管理", L"shell:%SystemRoot%\\System32\\mmc.exe|%SystemRoot%\\System32\\compmgmt.msc"},
    {L"系统管理", L"服务", L"shell:%SystemRoot%\\System32\\mmc.exe|%SystemRoot%\\System32\\services.msc"},
    {L"系统管理", L"设备管理器", L"shell:%SystemRoot%\\System32\\mmc.exe|%SystemRoot%\\System32\\devmgmt.msc"},
    {L"系统管理", L"磁盘管理", L"shell:%SystemRoot%\\System32\\mmc.exe|%SystemRoot%\\System32\\diskmgmt.msc"},
    {L"系统管理", L"事件查看器", L"shell:%SystemRoot%\\System32\\mmc.exe|%SystemRoot%\\System32\\eventvwr.msc"},
    {L"系统管理", L"任务计划程序", L"shell:%SystemRoot%\\System32\\mmc.exe|%SystemRoot%\\System32\\taskschd.msc"},
    {L"系统管理", L"高级防火墙", L"shell:%SystemRoot%\\System32\\mmc.exe|%SystemRoot%\\System32\\wf.msc"},
    {L"系统管理", L"系统信息", L"%SystemRoot%\\System32\\msinfo32.exe"},
    {L"系统管理", L"性能监视器", L"%SystemRoot%\\System32\\perfmon.exe"},
    {L"系统管理", L"资源监视器", L"%SystemRoot%\\System32\\resmon.exe"},
    {L"系统管理", L"任务管理器", L"%SystemRoot%\\System32\\taskmgr.exe"},
    {L"系统管理", L"系统配置", L"%SystemRoot%\\System32\\msconfig.exe"},
    {L"系统管理", L"DirectX 诊断", L"%SystemRoot%\\System32\\dxdiag.exe"},
    {L"系统管理", L"字符映射表", L"%SystemRoot%\\System32\\charmap.exe"},
    {L"系统管理", L"远程桌面连接", L"%SystemRoot%\\System32\\mstsc.exe"},
    {L"系统管理", L"组策略编辑器", L"shell:%SystemRoot%\\System32\\mmc.exe|%SystemRoot%\\System32\\gpedit.msc"},
    {L"系统管理", L"本地安全策略", L"shell:%SystemRoot%\\System32\\mmc.exe|%SystemRoot%\\System32\\secpol.msc"},
    {L"系统管理", L"本地用户和组", L"shell:%SystemRoot%\\System32\\mmc.exe|%SystemRoot%\\System32\\lusrmgr.msc"},
    {L"系统管理", L"当前用户证书", L"shell:%SystemRoot%\\System32\\mmc.exe|%SystemRoot%\\System32\\certmgr.msc"},
    {L"系统管理", L"共享文件夹", L"shell:%SystemRoot%\\System32\\mmc.exe|%SystemRoot%\\System32\\fsmgmt.msc"},

    {L"经典控制面板", L"Windows 功能", L"%SystemRoot%\\System32\\OptionalFeatures.exe"},
    {L"经典控制面板", L"程序和功能", L"shell:%SystemRoot%\\System32\\control.exe|/name Microsoft.ProgramsAndFeatures"},
    {L"经典控制面板", L"电源选项", L"shell:%SystemRoot%\\System32\\control.exe|/name Microsoft.PowerOptions"},
    {L"经典控制面板", L"电源高级设置", L"shell:%SystemRoot%\\System32\\control.exe|powercfg.cpl,,3"},
    {L"经典控制面板", L"文件夹选项", L"shell:%SystemRoot%\\System32\\control.exe|folders"},
    {L"经典控制面板", L"网络和共享中心", L"shell:%SystemRoot%\\System32\\control.exe|/name Microsoft.NetworkAndSharingCenter"},
    {L"经典控制面板", L"网络连接", L"shell:%SystemRoot%\\System32\\control.exe|netconnections"},
    {L"经典控制面板", L"Internet 选项", L"shell:%SystemRoot%\\System32\\control.exe|inetcpl.cpl"},
    {L"经典控制面板", L"声音", L"shell:%SystemRoot%\\System32\\control.exe|mmsys.cpl"},
    {L"经典控制面板", L"鼠标属性", L"shell:%SystemRoot%\\System32\\control.exe|mouse"},
    {L"经典控制面板", L"键盘属性", L"shell:%SystemRoot%\\System32\\control.exe|keyboard"},
    {L"经典控制面板", L"用户账户", L"shell:%SystemRoot%\\System32\\control.exe|/name Microsoft.UserAccounts"},

    {L"Windows 设置·系统", L"设置主页", L"ms-settings:"},
    {L"Windows 设置·系统", L"显示", L"ms-settings:display"},
    {L"Windows 设置·系统", L"高级显示", L"ms-settings:display-advanced"},
    {L"Windows 设置·系统", L"图形", L"ms-settings:display-advancedgraphics"},
    {L"Windows 设置·系统", L"声音", L"ms-settings:sound"},
    {L"Windows 设置·系统", L"音量混合器", L"ms-settings:apps-volume"},
    {L"Windows 设置·系统", L"通知", L"ms-settings:notifications"},
    {L"Windows 设置·系统", L"电源和睡眠", L"ms-settings:powersleep"},
    {L"Windows 设置·系统", L"存储", L"ms-settings:storagesense"},
    {L"Windows 设置·系统", L"磁盘和卷", L"ms-settings:disksandvolumes"},
    {L"Windows 设置·系统", L"剪贴板", L"ms-settings:clipboard"},
    {L"Windows 设置·系统", L"多任务处理", L"ms-settings:multitasking"},
    {L"Windows 设置·系统", L"夜间模式", L"ms-settings:nightlight"},
    {L"Windows 设置·系统", L"远程桌面", L"ms-settings:remotedesktop"},
    {L"Windows 设置·系统", L"关于", L"ms-settings:about"},

    {L"Windows 设置·个性化", L"个性化", L"ms-settings:personalization"},
    {L"Windows 设置·个性化", L"背景", L"ms-settings:personalization-background"},
    {L"Windows 设置·个性化", L"颜色", L"ms-settings:personalization-colors"},
    {L"Windows 设置·个性化", L"主题", L"ms-settings:themes"},
    {L"Windows 设置·个性化", L"锁屏", L"ms-settings:lockscreen"},
    {L"Windows 设置·个性化", L"开始菜单", L"ms-settings:personalization-start"},
    {L"Windows 设置·个性化", L"任务栏", L"ms-settings:taskbar"},
    {L"Windows 设置·个性化", L"字体", L"ms-settings:fonts"},

    {L"Windows 设置·设备", L"蓝牙", L"ms-settings:bluetooth"},
    {L"Windows 设置·设备", L"打印机和扫描仪", L"ms-settings:printers"},
    {L"Windows 设置·设备", L"鼠标和触摸板", L"ms-settings:mousetouchpad"},
    {L"Windows 设置·设备", L"触摸板", L"ms-settings:devices-touchpad"},
    {L"Windows 设置·设备", L"USB", L"ms-settings:usb"},
    {L"Windows 设置·设备", L"相机", L"ms-settings:camera"},

    {L"Windows 设置·网络", L"网络和 Internet", L"ms-settings:network-status"},
    {L"Windows 设置·网络", L"高级网络设置", L"ms-settings:network-advancedsettings"},
    {L"Windows 设置·网络", L"Wi-Fi", L"ms-settings:network-wifi"},
    {L"Windows 设置·网络", L"以太网", L"ms-settings:network-ethernet"},
    {L"Windows 设置·网络", L"VPN", L"ms-settings:network-vpn"},
    {L"Windows 设置·网络", L"代理", L"ms-settings:network-proxy"},
    {L"Windows 设置·网络", L"移动热点", L"ms-settings:network-mobilehotspot"},

    {L"Windows 设置·应用", L"已安装的应用", L"ms-settings:appsfeatures"},
    {L"Windows 设置·应用", L"默认应用", L"ms-settings:defaultapps"},
    {L"Windows 设置·应用", L"可选功能", L"ms-settings:optionalfeatures"},
    {L"Windows 设置·应用", L"启动应用", L"ms-settings:startupapps"},

    {L"Windows 设置·时间语言", L"日期和时间", L"ms-settings:dateandtime"},
    {L"Windows 设置·时间语言", L"语言和区域", L"ms-settings:regionlanguage"},
    {L"Windows 设置·时间语言", L"微软拼音", L"ms-settings:regionlanguage-chsime-pinyin"},
    {L"Windows 设置·时间语言", L"语音", L"ms-settings:speech"},

    {L"Windows 设置·隐私安全", L"隐私", L"ms-settings:privacy"},
    {L"Windows 设置·隐私安全", L"位置", L"ms-settings:privacy-location"},
    {L"Windows 设置·隐私安全", L"相机权限", L"ms-settings:privacy-webcam"},
    {L"Windows 设置·隐私安全", L"麦克风权限", L"ms-settings:privacy-microphone"},
    {L"Windows 设置·隐私安全", L"Windows 安全", L"ms-settings:windowsdefender"},
    {L"Windows 设置·隐私安全", L"Windows Update", L"ms-settings:windowsupdate"},
    {L"Windows 设置·隐私安全", L"更新历史", L"ms-settings:windowsupdate-history"},
    {L"Windows 设置·隐私安全", L"可选更新", L"ms-settings:windowsupdate-optionalupdates"},
    {L"Windows 设置·隐私安全", L"Windows Update 高级选项", L"ms-settings:windowsupdate-options"},
    {L"Windows 设置·隐私安全", L"恢复", L"ms-settings:recovery"},
    {L"Windows 设置·隐私安全", L"开发者选项", L"ms-settings:developers"},
    {L"Windows 设置·隐私安全", L"激活", L"ms-settings:activation"},
}};

}  // namespace

std::span<const WindowsToolTemplate> windows_tool_templates() noexcept {
    return kTemplates;
}

}  // namespace piinput::windows
