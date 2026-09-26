#pragma once
#include <LWSSettings/SettingsEditor.hpp>
#include <LWSSettings/ThemeSession.hpp>
#include <set>
namespace LWSSettings
{
    namespace internal { struct ThemeEditorTestAccess; }
    // A stable-style settings editor for a partial theme. The destination is fixed
    // for the session. Subscribers may apply previews to any number of other editors.
    class ThemeEditor
    {
      public:

        ThemeEditor(LWS::PlatformContext& platform, std::filesystem::path file, const std::filesystem::path& iconPath = {});
        ThemeEditor(LWS::PlatformContext& platform, ThemeSession& session, const std::filesystem::path& iconPath = {});
        Status Load(const std::filesystem::path& templateFile = {});
        Status Show();
        void Hide() { if (editor_.IsVisible()) editor_.Hide(); }
        void Revert();
        Status Save();
        // Inspection only: the document also contains private editing metadata.
        ThemeSession& Session() { return session_; }
        const ThemeSession& Session() const { return session_; }
        const SettingsEditor& Editor() const { return editor_; }
        std::expected<ThemeOverrides, Error> Overrides() const;
        LWSUI::Event<void(const SettingsTheme&)> OnPreview;
        LWSUI::Event<void(const Error&)> OnError;

      private:

        friend struct internal::ThemeEditorTestAccess;
        static std::string OverrideId(std::string_view path);
        void UpdatePresentation(const ThemeOverrides&);
        EditorConfig Config(const std::filesystem::path&, const std::filesystem::path& iconPath);
        static std::expected<ThemeOverrides, Error> ReadOverrides(const Values&);
        void Changed(const Change&);
        void Finished(const std::string&, EditPhase, ChangeOrigin);
        void Synchronize();
        void Connect();
        void PullSession();
        void RequestPull();
        Values EditingValues(const ThemeProfile&) const;
        std::expected<ThemeProfile, Error> ReadProfile(const Values&) const;
        std::unique_ptr<ThemeSession> ownedSession_;
        ThemeSession& session_;
        std::vector<Definition> definitions_;
        std::optional<LWSUI::ThemePreset> definitionPreset_;
        bool synchronizing_ = false, pullPending_ = false;
        SettingsEditor editor_;
        std::set<std::string> automaticOverrides_;
        bool loaded_ = false;
        LWSUI::Event<void(const Change&)>::Connection changes_;
        LWSUI::Event<void(const std::string&, EditPhase, ChangeOrigin)>::Connection finished_;
        LWSUI::Event<void()>::Connection reloaded_, sessionChanged_, hidden_, resetFinished_;
        LWSUI::Event<void(const Error&)>::Connection errors_;
    };
}  // namespace LWSSettings
