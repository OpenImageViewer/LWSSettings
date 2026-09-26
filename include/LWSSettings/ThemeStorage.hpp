#pragma once
#include <LWSSettings/ThemeProfile.hpp>
#include <nlohmann/json_fwd.hpp>
namespace LWSSettings
{
    class ThemeStorage
    {
      public:

        static std::expected<ThemeProfile, Error> DecodeProfile(const nlohmann::json&);
        static nlohmann::json EncodeProfile(const ThemeProfile&);
        static std::expected<ThemeProfile, Error> LoadProfileFile(const std::filesystem::path&);
        static Status SaveProfileFile(const std::filesystem::path&, const ThemeProfile&);
        static std::expected<SettingsTheme, Error> Decode(const nlohmann::json&);
        static nlohmann::json Encode(const SettingsTheme&);
        static std::expected<SettingsTheme, Error> LoadFile(const std::filesystem::path&);
        static Status SaveFile(const std::filesystem::path&, const SettingsTheme&);
        static std::expected<ThemeOverrides, Error> DecodeOverrides(const nlohmann::json&);
        static nlohmann::json EncodeOverrides(const ThemeOverrides&);
        static std::expected<ThemeOverrides, Error> LoadOverridesFile(const std::filesystem::path&);
        static Status SaveOverridesFile(const std::filesystem::path&, const ThemeOverrides&);
        static nlohmann::json Template();
        static Status SaveTemplateFile(const std::filesystem::path&);
    };
}  // namespace LWSSettings
