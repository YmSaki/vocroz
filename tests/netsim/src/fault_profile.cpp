// SPDX-License-Identifier: AGPL-3.0-only
//
// FaultProfile の文字列表現のパースと書き出し。

#include "vocroz/netsim/fault_profile.h"

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace vocroz::netsim {
namespace {

using common::make_unexpected;

std::optional<std::int64_t> parse_integer(std::string_view text) {
    std::int64_t value = 0;
    const auto* const end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

// 「数値 + 単位」をマイクロ秒にする。小数は受け付けない（設定の再現性を単純に保つため）。
std::optional<std::chrono::microseconds> parse_duration(std::string_view text) {
    struct Unit {
        std::string_view suffix;
        std::int64_t micros;
    };
    // us を ms や s より先に調べないと、"10us" の末尾の "s" を秒と読んでしまう。
    constexpr Unit kUnits[] = {{"us", 1}, {"ms", 1'000}, {"s", 1'000'000}};
    for (const auto& unit : kUnits) {
        if (text.size() > unit.suffix.size() && text.ends_with(unit.suffix)) {
            const auto value = parse_integer(text.substr(0, text.size() - unit.suffix.size()));
            if (!value || *value < 0) {
                return std::nullopt;
            }
            return std::chrono::microseconds{*value * unit.micros};
        }
    }
    return std::nullopt;
}

std::optional<double> parse_probability(std::string_view text) {
    double value = 0.0;
    const auto* const end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, value);
    if (ec != std::errc{} || ptr != end || !(value >= 0.0 && value <= 1.0)) {
        return std::nullopt;
    }
    return value;
}

// 1 つの key=value を profile に反映する。失敗したら原因を返す。
std::optional<std::string> apply_entry(std::string_view key, std::string_view value,
                                       FaultProfile& profile) {
    const auto duration = [&](std::chrono::microseconds& field) -> std::optional<std::string> {
        const auto parsed = parse_duration(value);
        if (!parsed) {
            return "invalid duration for " + std::string(key) + ": " + std::string(value) +
                   " (use us, ms or s)";
        }
        field = *parsed;
        return std::nullopt;
    };
    const auto probability = [&](double& field) -> std::optional<std::string> {
        const auto parsed = parse_probability(value);
        if (!parsed) {
            return "invalid probability for " + std::string(key) + ": " + std::string(value) +
                   " (use 0..1)";
        }
        field = *parsed;
        return std::nullopt;
    };
    if (key == "delay") {
        return duration(profile.delay);
    }
    if (key == "jitter") {
        return duration(profile.jitter);
    }
    if (key == "reorder_delay") {
        return duration(profile.reorder_delay);
    }
    if (key == "loss") {
        return probability(profile.loss);
    }
    if (key == "duplicate") {
        return probability(profile.duplicate);
    }
    if (key == "reorder") {
        return probability(profile.reorder);
    }
    if (key == "corrupt") {
        return probability(profile.corrupt);
    }
    if (key == "seed") {
        std::uint64_t seed = 0;
        const auto* const end = value.data() + value.size();
        const auto [ptr, ec] = std::from_chars(value.data(), end, seed);
        if (ec != std::errc{} || ptr != end) {
            return "invalid seed: " + std::string(value);
        }
        profile.seed = seed;
        return std::nullopt;
    }
    return "unknown key: " + std::string(key);
}

std::string format_probability(double value) {
    std::string out(32, '\0');
    const auto [ptr, ec] = std::to_chars(out.data(), out.data() + out.size(), value);
    out.resize(ec == std::errc{} ? static_cast<std::size_t>(ptr - out.data()) : 0);
    return out;
}

}  // namespace

common::Result<FaultProfile, std::string> parse_fault_profile(std::string_view text) {
    FaultProfile profile;
    while (!text.empty()) {
        const auto comma = text.find(',');
        const std::string_view entry = text.substr(0, comma);
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
        if (entry.empty()) {
            continue;
        }
        const auto equals = entry.find('=');
        if (equals == std::string_view::npos) {
            return make_unexpected("expected key=value: " + std::string(entry));
        }
        if (auto error = apply_entry(entry.substr(0, equals), entry.substr(equals + 1), profile)) {
            return make_unexpected(std::move(*error));
        }
    }
    return profile;
}

std::string format_fault_profile(const FaultProfile& profile) {
    return "delay=" + std::to_string(profile.delay.count()) + "us" +
           ",jitter=" + std::to_string(profile.jitter.count()) + "us" +
           ",loss=" + format_probability(profile.loss) +
           ",duplicate=" + format_probability(profile.duplicate) +
           ",reorder=" + format_probability(profile.reorder) +
           ",reorder_delay=" + std::to_string(profile.reorder_delay.count()) + "us" +
           ",corrupt=" + format_probability(profile.corrupt) +
           ",seed=" + std::to_string(profile.seed);
}

}  // namespace vocroz::netsim
