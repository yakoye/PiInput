#pragma once

#include <ctime>
#include <string>
#include <vector>

namespace piinput {

// The Chinese lunar date behind a Gregorian one: 2026-08-21 is 丙午[马]年七月初九.
struct LunarDate final {
    int year{};        // The lunar year, which turns over at Chinese New Year.
    int month{};       // 1-12.
    int day{};         // 1-30.
    bool leap_month{};
};

// Only the years the table covers. Outside that range the lunar candidate is
// left out rather than guessed at -- a wrong date is worse than a missing one.
inline constexpr int lunar_first_year = 1900;
inline constexpr int lunar_last_year = 2100;

[[nodiscard]] bool gregorian_to_lunar(int year, int month, int day, LunarDate& out) noexcept;

// 丙午[马]年七月初九
[[nodiscard]] std::string format_lunar_date(const LunarDate& lunar);

// 二〇二六年八月二十一日 -- the Gregorian date written in Chinese numerals, which
// is not the same thing as the lunar date and is what documents usually want.
[[nodiscard]] std::string format_chinese_numeral_date(int year, int month, int day);

// What the candidate row shows for the entry that opens the formats.
//
// Deliberately not one of the formats. Showing a bare 17:36:00 there told
// nobody that six more spellings were behind it -- it read as the answer
// rather than as a way in. The mark plus the word says both what it is and
// that there is something to open, which is how other input methods label the
// same entry.
[[nodiscard]] std::string datetime_group_label(bool date_rather_than_time);

// Every spelling of the current date, in the order they are offered.
[[nodiscard]] std::vector<std::string> date_candidates(const std::tm& local);

// Every spelling of the current time, likewise.
[[nodiscard]] std::vector<std::string> time_candidates(const std::tm& local);

}  // namespace piinput
