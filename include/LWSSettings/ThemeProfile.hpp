#pragma once
#include <LWSSettings/SettingsTheme.hpp>
#include <LWSSettings/SettingsDocument.hpp>
namespace LWSSettings
{
    using ThemeOverrides = Values;
    struct ThemeProfile
    {
        LWSUI::ThemePreset preset = LWSUI::ThemePreset::Light;
        ThemeOverrides overrides;
        bool operator==(const ThemeProfile&) const = default;
    };
}
