#pragma once
#include <LWSSettings/ThemeProfile.hpp>
namespace LWSSettings
{
    struct ThemeField
    {
        std::string path;
        SettingDefinition definition;  // Label, description, type, bounds, step and built-in default.
        std::function<Value(const SettingsTheme&)> read;
        std::function<void(SettingsTheme&, const Value&)> assign;
    };
    // One field catalog drives the codec, template and live editor. No JSON round trip
    // is needed to validate or resolve a preview.
    class ThemeFields
    {
      public:

        static const std::vector<ThemeField>& All();
        static const ThemeField* Find(std::string_view path);
        static std::expected<SettingsTheme, Error> Resolve(const ThemeOverrides&, LWSUI::ThemePreset preset = LWSUI::ThemePreset::Dark);
        static Status Validate(const SettingsTheme&);
        static std::vector<Definition> CreateDefinitions();
    };
}  // namespace LWSSettings
