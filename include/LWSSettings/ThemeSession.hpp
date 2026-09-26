#pragma once
#include <LWSSettings/ThemeProfile.hpp>
namespace LWSSettings
{
    // Lightweight shared preview/persistence state; never loads the editing template.
    class ThemeSession
    {
      public:
        explicit ThemeSession(std::filesystem::path file) : file_(std::move(file)) {}
        // Optional shipped defaults are read only when the writable profile is absent.
        Status Load(const std::filesystem::path& defaults = {});
        Status SetProfile(ThemeProfile profile);
        Status SelectPreset(LWSUI::ThemePreset preset) { return SetProfile({preset, {}}); }
        Status Save();
        void Revert();
        const ThemeProfile& Current() const { return current_; }
        const ThemeProfile& Saved() const { return saved_; }
        const SettingsTheme& Theme() const { return theme_; }
        const std::filesystem::path& File() const { return file_; }
        bool IsDirty() const { return current_ != saved_; }
        LWSUI::Event<void()> OnChange;
      private:
        std::filesystem::path file_;
        ThemeProfile current_, saved_;
        SettingsTheme theme_;
    };
}
