#include "piinput/datetime_candidates.h"

#include <array>
#include <cstdio>

namespace piinput {
namespace {

// One entry per year from 1900. Bits 4..15 are the twelve ordinary months, set
// meaning 30 days; bits 0..3 are the leap month number, zero for none; bit 16
// is the leap month length. This is the compact encoding the Chinese calendar
// is conventionally published in -- the lunar year has no formula, only a
// table, because it follows observed new moons.
constexpr std::array<std::uint32_t, 201U> kLunarYears{
    0x04bd8U, 0x04ae0U, 0x0a570U, 0x054d5U, 0x0d260U, 0x0d950U, 0x16554U, 0x056a0U,
    0x09ad0U, 0x055d2U, 0x04ae0U, 0x0a5b6U, 0x0a4d0U, 0x0d250U, 0x1d255U, 0x0b540U,
    0x0d6a0U, 0x0ada2U, 0x095b0U, 0x14977U, 0x04970U, 0x0a4b0U, 0x0b4b5U, 0x06a50U,
    0x06d40U, 0x1ab54U, 0x02b60U, 0x09570U, 0x052f2U, 0x04970U, 0x06566U, 0x0d4a0U,
    0x0ea50U, 0x06e95U, 0x05ad0U, 0x02b60U, 0x186e3U, 0x092e0U, 0x1c8d7U, 0x0c950U,
    0x0d4a0U, 0x1d8a6U, 0x0b550U, 0x056a0U, 0x1a5b4U, 0x025d0U, 0x092d0U, 0x0d2b2U,
    0x0a950U, 0x0b557U, 0x06ca0U, 0x0b550U, 0x15355U, 0x04da0U, 0x0a5b0U, 0x14573U,
    0x052b0U, 0x0a9a8U, 0x0e950U, 0x06aa0U, 0x0aea6U, 0x0ab50U, 0x04b60U, 0x0aae4U,
    0x0a570U, 0x05260U, 0x0f263U, 0x0d950U, 0x05b57U, 0x056a0U, 0x096d0U, 0x04dd5U,
    0x04ad0U, 0x0a4d0U, 0x0d4d4U, 0x0d250U, 0x0d558U, 0x0b540U, 0x0b6a0U, 0x195a6U,
    0x095b0U, 0x049b0U, 0x0a974U, 0x0a4b0U, 0x0b27aU, 0x06a50U, 0x06d40U, 0x0af46U,
    0x0ab60U, 0x09570U, 0x04af5U, 0x04970U, 0x064b0U, 0x074a3U, 0x0ea50U, 0x06b58U,
    0x05ac0U, 0x0ab60U, 0x096d5U, 0x092e0U, 0x0c960U, 0x0d954U, 0x0d4a0U, 0x0da50U,
    0x07552U, 0x056a0U, 0x0abb7U, 0x025d0U, 0x092d0U, 0x0cab5U, 0x0a950U, 0x0b4a0U,
    0x0baa4U, 0x0ad50U, 0x055d9U, 0x04ba0U, 0x0a5b0U, 0x15176U, 0x052b0U, 0x0a930U,
    0x07954U, 0x06aa0U, 0x0ad50U, 0x05b52U, 0x04b60U, 0x0a6e6U, 0x0a4e0U, 0x0d260U,
    0x0ea65U, 0x0d530U, 0x05aa0U, 0x076a3U, 0x096d0U, 0x04afbU, 0x04ad0U, 0x0a4d0U,
    0x1d0b6U, 0x0d250U, 0x0d520U, 0x0dd45U, 0x0b5a0U, 0x056d0U, 0x055b2U, 0x049b0U,
    0x0a577U, 0x0a4b0U, 0x0aa50U, 0x1b255U, 0x06d20U, 0x0ada0U, 0x14b63U, 0x09370U,
    0x049f8U, 0x04970U, 0x064b0U, 0x168a6U, 0x0ea50U, 0x06b20U, 0x1a6c4U, 0x0aae0U,
    0x0a2e0U, 0x0d2e3U, 0x0c960U, 0x0d557U, 0x0d4a0U, 0x0da50U, 0x05d55U, 0x056a0U,
    0x0a6d0U, 0x055d4U, 0x052d0U, 0x0a9b8U, 0x0a950U, 0x0b4a0U, 0x0b6a6U, 0x0ad50U,
    0x055a0U, 0x0aba4U, 0x0a5b0U, 0x052b0U, 0x0b273U, 0x06930U, 0x07337U, 0x06aa0U,
    0x0ad50U, 0x14b55U, 0x04b60U, 0x0a570U, 0x054e4U, 0x0d160U, 0x0e968U, 0x0d520U,
    0x0daa0U, 0x16aa6U, 0x056d0U, 0x04ae0U, 0x0a9d4U, 0x0a2d0U, 0x0d150U, 0x0f252U,
    0x0d520U,
};

[[nodiscard]] int leap_month_of(const int year) noexcept {
    return static_cast<int>(kLunarYears[static_cast<std::size_t>(year - lunar_first_year)] & 0xfU);
}

[[nodiscard]] int leap_month_days(const int year) noexcept {
    if (leap_month_of(year) == 0) return 0;
    const auto entry = kLunarYears[static_cast<std::size_t>(year - lunar_first_year)];
    return (entry & 0x10000U) != 0U ? 30 : 29;
}

[[nodiscard]] int month_days(const int year, const int month) noexcept {
    const auto entry = kLunarYears[static_cast<std::size_t>(year - lunar_first_year)];
    return (entry & (0x10000U >> static_cast<unsigned>(month))) != 0U ? 30 : 29;
}

[[nodiscard]] int lunar_year_days(const int year) noexcept {
    int days = 0;
    for (int month = 1; month <= 12; ++month) days += month_days(year, month);
    return days + leap_month_days(year);
}

// Days since 1900-01-31, which is 正月初一 of the 1900 lunar year and the anchor
// the table is built from.
[[nodiscard]] long days_since_epoch(const int year, const int month, const int day) noexcept {
    // Days from the civil epoch, by the standard days-from-civil algorithm.
    const long y = year - (month <= 2 ? 1 : 0);
    const long era = (y >= 0 ? y : y - 399) / 400;
    const long year_of_era = y - era * 400;
    const long day_of_year =
        (153L * (month + (month > 2 ? -3 : 9)) + 2L) / 5L + day - 1L;
    const long day_of_era =
        year_of_era * 365L + year_of_era / 4L - year_of_era / 100L + day_of_year;
    const long days_from_civil = era * 146097L + day_of_era - 719468L;
    constexpr long anchor = -25537L;  // 1900-01-31 in the same terms.
    return days_from_civil - anchor;
}

constexpr std::array<const char*, 10U> kHeavenlyStems{
    "甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸"};
constexpr std::array<const char*, 12U> kEarthlyBranches{
    "子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥"};
constexpr std::array<const char*, 12U> kZodiac{
    "鼠", "牛", "虎", "兔", "龙", "蛇", "马", "羊", "猴", "鸡", "狗", "猪"};
constexpr std::array<const char*, 12U> kLunarMonths{
    "正", "二", "三", "四", "五", "六", "七", "八", "九", "十", "冬", "腊"};
constexpr std::array<const char*, 10U> kDigits{
    "〇", "一", "二", "三", "四", "五", "六", "七", "八", "九"};

[[nodiscard]] std::string lunar_day_name(const int day) {
    constexpr std::array<const char*, 10U> ones{
        "十", "一", "二", "三", "四", "五", "六", "七", "八", "九"};
    if (day <= 10) return std::string("初") + ones[static_cast<std::size_t>(day % 10)];
    if (day < 20) return std::string("十") + ones[static_cast<std::size_t>(day % 10)];
    if (day == 20) return "二十";
    if (day < 30) return std::string("廿") + ones[static_cast<std::size_t>(day % 10)];
    return "三十";
}

// 21 -> 二十一, the way a date is written out rather than spelled digit by digit.
[[nodiscard]] std::string chinese_count(const int value) {
    if (value <= 10) {
        return value == 10 ? "十" : kDigits[static_cast<std::size_t>(value)];
    }
    if (value < 20) return std::string("十") + kDigits[static_cast<std::size_t>(value % 10)];
    std::string text = kDigits[static_cast<std::size_t>(value / 10)];
    text += "十";
    if (value % 10 != 0) text += kDigits[static_cast<std::size_t>(value % 10)];
    return text;
}

[[nodiscard]] std::string formatted(const char* const pattern, const int a, const int b, const int c) {
    std::array<char, 64U> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(), pattern, a, b, c);
    return written > 0 ? std::string(buffer.data(), static_cast<std::size_t>(written))
                       : std::string{};
}

}  // namespace

bool gregorian_to_lunar(
    const int year, const int month, const int day, LunarDate& out) noexcept {
    if (year < lunar_first_year || year > lunar_last_year) return false;
    long remaining = days_since_epoch(year, month, day);
    if (remaining < 0) return false;

    int lunar_year = lunar_first_year;
    while (lunar_year <= lunar_last_year) {
        const long span = lunar_year_days(lunar_year);
        if (remaining < span) break;
        remaining -= span;
        ++lunar_year;
    }
    if (lunar_year > lunar_last_year) return false;

    const int leap = leap_month_of(lunar_year);
    for (int month_index = 1; month_index <= 12; ++month_index) {
        const long ordinary = month_days(lunar_year, month_index);
        if (remaining < ordinary) {
            out = LunarDate{lunar_year, month_index, static_cast<int>(remaining) + 1, false};
            return true;
        }
        remaining -= ordinary;
        // The leap month follows the month it repeats, so it is consumed after
        // that month rather than in place of it.
        if (month_index == leap) {
            const long extra = leap_month_days(lunar_year);
            if (remaining < extra) {
                out = LunarDate{lunar_year, month_index, static_cast<int>(remaining) + 1, true};
                return true;
            }
            remaining -= extra;
        }
    }
    return false;
}

std::string format_lunar_date(const LunarDate& lunar) {
    // 1984 is 甲子, the start of a sexagenary cycle, and the usual anchor.
    const int offset = ((lunar.year - 1984) % 60 + 60) % 60;
    std::string text = kHeavenlyStems[static_cast<std::size_t>(offset % 10)];
    text += kEarthlyBranches[static_cast<std::size_t>(offset % 12)];
    text += "[";
    text += kZodiac[static_cast<std::size_t>(offset % 12)];
    text += "]年";
    if (lunar.leap_month) text += "闰";
    text += kLunarMonths[static_cast<std::size_t>(lunar.month - 1)];
    text += "月";
    text += lunar_day_name(lunar.day);
    return text;
}

std::string format_chinese_numeral_date(const int year, const int month, const int day) {
    std::string text;
    // The year is read digit by digit -- 二〇二六, never 两千零二十六.
    for (int value = year, divisor = 1000; divisor > 0; divisor /= 10) {
        text += kDigits[static_cast<std::size_t>((value / divisor) % 10)];
    }
    text += "年";
    text += chinese_count(month);
    text += "月";
    text += chinese_count(day);
    text += "日";
    return text;
}

std::string datetime_group_label(const bool date_rather_than_time) {
    // A clock and a calendar. These are emoji, so they come out in colour and
    // sized by the emoji font rather than the candidate font -- which is the
    // point: the entry should not look like the words around it, because it
    // does something different when chosen.
    return date_rather_than_time ? "📅日期" : "🕗时间";
}

std::vector<std::string> date_candidates(const std::tm& local) {
    const int year = local.tm_year + 1900;
    const int month = local.tm_mon + 1;
    const int day = local.tm_mday;

    std::vector<std::string> candidates{
        formatted("%d年%d月%d日", year, month, day),
        formatted("%04d-%02d-%02d", year, month, day),
        formatted("%04d.%02d.%02d", year, month, day),
        formatted("%04d/%02d/%02d", year, month, day),
        formatted("%04d%02d%02d", year, month, day),
        format_chinese_numeral_date(year, month, day),
    };
    LunarDate lunar{};
    if (gregorian_to_lunar(year, month, day, lunar)) {
        candidates.push_back(format_lunar_date(lunar));
    }
    return candidates;
}

std::vector<std::string> time_candidates(const std::tm& local) {
    const int year = local.tm_year + 1900;
    const int month = local.tm_mon + 1;
    const int day = local.tm_mday;
    const int hour = local.tm_hour;
    const int minute = local.tm_min;
    const int second = local.tm_sec;

    const std::string clock = formatted("%02d:%02d:%02d", hour, minute, second);
    const std::string stamp = formatted("%02d%02d%02d", hour, minute, second);
    return {
        clock,
        formatted("%d年%d月%d日", year, month, day) + " " + clock,
        formatted("%04d-%02d-%02d", year, month, day) + " " + clock,
        formatted("%04d.%02d.%02d", year, month, day) + " " + clock,
        formatted("%04d%02d%02d", year, month, day) + stamp,
        formatted("%04d%02d%02d", year, month, day) + "_" + stamp,
    };
}

}  // namespace piinput
