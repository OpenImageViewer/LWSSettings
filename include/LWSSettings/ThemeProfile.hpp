#pragma once
#include <LWSSettings/SettingsTheme.hpp>
#include <LWSSettings/SettingsDocument.hpp>
namespace LWSSettings
{
    using ThemeOverrides = Values;
    struct ThemeProfile
    {
        LWSUI::ThemePreset preset = LWSUI::ThemePreset::Dark;
        ThemeOverrides overrides;
        bool operator==(const ThemeProfile&) const = default;
    };
}
