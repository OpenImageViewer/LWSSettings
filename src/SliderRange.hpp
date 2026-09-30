#pragma once
#include <LWSSettings/SettingsDocument.hpp>
#include <LWSUI/NumericSpec.hpp>
namespace LWSSettings::internal
{
    template <class T>
    const char* SliderRangeError(const NumericDefinition<T>& spec)
    {
        return LWSUI::SliderRangeError(LWSUI::NumericSpec<T>{spec.minimum, spec.maximum, spec.step});
    }
}  // namespace LWSSettings::internal
