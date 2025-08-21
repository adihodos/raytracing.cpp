#pragma once

#include <concepts>

template <typename T>
    requires std::is_arithmetic_v<T>
T clamp(const T value, const T low, const T high) noexcept {
    return value < low ? low : (value > high ? high : value);
}
