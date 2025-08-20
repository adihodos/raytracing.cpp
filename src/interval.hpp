#pragma once

#include <limits>

struct Interval {
    double Min{std::numeric_limits<double>::max()};
    double Max{std::numeric_limits<double>::min()};

    Interval() noexcept = default;
    constexpr Interval(const double min_, const double max_) noexcept : Min{min_}, Max{max_} {}

    double size() const noexcept { return Max - Min; }
    bool contains(const double x) const noexcept { return Min <= x && x <= Max; }
    bool surrounds(const double x) const noexcept { return Min < x && x < Max; }

    struct Stdc;
};

struct Interval::Stdc {
    static constexpr Interval Empty{};
    static constexpr Interval Universe{std::numeric_limits<double>::min(), std::numeric_limits<double>::max()};
};
