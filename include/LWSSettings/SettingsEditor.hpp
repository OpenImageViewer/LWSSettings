#pragma once
#include <LWSSettings/SettingsView.hpp>
#include <LWSUI/UIHost.hpp>
namespace LWSSettings
{
    struct EditorConfig
    {
        DocumentConfig document;
        std::string title = "Settings";
        LWS::LogicalSize size{820, 680};
        SettingsTheme theme;
        bool showRevert = false, revertOnClose = false, collapseGroups = false;
        std::filesystem::path iconPath; // PNG; empty selects the embedded cog wheels.
        bool redrawOnResize = true;     // Callers can opt out on expensive views or slower machines.
    };
    // Owns one document, window and view; the caller context must outlive the editor.
    // Data operations are available before Show. Reopening after native destruction
    // replaces the Window and must be scheduled outside its active event dispatch.
    class SettingsEditor
    {
      public:

        explicit SettingsEditor(LWS::PlatformContext& platform, EditorConfig config = {});
        ~SettingsEditor();
        SettingsDocument& Document() { return document_; }
        const SettingsDocument& Document() const { return document_; }
        Status Show();
        bool Hide();
        bool IsVisible() const;
        void SetRedrawOnResize(bool enabled);
        bool RedrawOnResize() const { return config_.redrawOnResize; }
        SettingsView* View() const { return view_; }
        void SetFilter(std::string text);
        void SetValueFilter(SettingValueFilter filter);
        SettingValueFilter GetValueFilter() const;
        // Validated immediately, applied on the next host update. Repeated calls coalesce.
        Status SetTheme(const SettingsTheme& theme);
        LWSUI::Event<void(LWS::KeyCode)> OnKeyDown;
        // Raised after a visible editor is successfully hidden. The application
        // decides whether hiding this editor should end its message loop.
        LWSUI::Event<void()> OnHidden;

      private:

        LWS::PlatformContext& platform_;
        EditorConfig config_;
        SettingsDocument document_;
        std::unique_ptr<LWS::Window> window_;
        std::unique_ptr<LWSUI::UIHost> host_;
        SettingsView* view_ = nullptr;
        std::string filter_;
        SettingValueFilter initialValueFilter_ = SettingValueFilter::All;
        bool themePending_ = false, minimumPending_ = false;
        int32_t minimumWidth_ = 480;
        LWS::EventConnection listener_;
        LWSUI::Event<void(const std::string&)>::Connection errorConnection_;
        LWSUI::Event<void(float)>::Connection minimumConnection_;
    };
}  // namespace LWSSettings
