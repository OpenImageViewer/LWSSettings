#pragma once
#include <LWSSettings/SettingsDocument.hpp>
#include <cmath>
#include <type_traits>
namespace LWSSettings::internal
{
    template <class T>
    const char* SliderRangeError(const NumericDefinition<T>& spec)
    {
        if (!spec.minimum || !spec.maximum || !std::isfinite(double(*spec.minimum)) ||
            !std::isfinite(double(*spec.maximum)) || *spec.minimum >= *spec.maximum ||
            !std::isfinite(double(spec.step)) || spec.step <= 0)
            return "Slider presentation requires finite min < max and a positive step";
        if constexpr (std::is_integral_v<T>)
        {
            constexpr int64_t exact = 9007199254740991LL;
            if (*spec.minimum < -exact || *spec.maximum > exact || *spec.maximum - *spec.minimum > exact)
                return "Integer slider bounds and span must fit within +/- (2^53 - 1); use numeric presentation for "
                       "larger ranges";
        }
        const double span = double(*spec.maximum) - double(*spec.minimum);
        if (!std::isfinite(span) || !std::isfinite(span / double(spec.step)))
            return "Slider range or step count is too large";
        return nullptr;
    }
}  // namespace LWSSettings::internal
