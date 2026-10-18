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
        LWSUI::InvalidEditPolicy invalidEditPolicy = LWSUI::InvalidEditPolicy::KeepInvalid;
        FormValidationPolicy saveValidationPolicy = FormValidationPolicy::ShowDecisionDialog;
        FormValidationPolicy closeValidationPolicy = FormValidationPolicy::ShowDecisionDialog;
        ValidationDialogOptions saveDialogOptions, closeDialogOptions;
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
        // Returns false while a decision is pending or closing fails.
        // OnHidden also covers a successful asynchronous dialog decision.
        bool Hide();
        bool IsVisible() const;
        void SetRedrawOnResize(bool enabled);
        void SetInvalidEditPolicy(LWSUI::InvalidEditPolicy policy);
        LWSUI::InvalidEditPolicy GetInvalidEditPolicy() const { return config_.invalidEditPolicy; }
        void SetCloseValidationPolicy(FormValidationPolicy policy) { config_.closeValidationPolicy = policy; }
        FormValidationPolicy GetCloseValidationPolicy() const { return config_.closeValidationPolicy; }
        void SetSaveValidationPolicy(FormValidationPolicy policy)
        {
            config_.saveValidationPolicy = policy;
            if (view_) view_->SetSaveValidationPolicy(policy);
        }
        FormValidationPolicy GetSaveValidationPolicy() const { return config_.saveValidationPolicy; }
        void SetSaveDialogOptions(ValidationDialogOptions options)
        {
            config_.saveDialogOptions = options;
            if (view_) view_->SetSaveDialogOptions(options);
        }
        void SetCloseDialogOptions(ValidationDialogOptions options) { config_.closeDialogOptions = options; }
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

        bool HideWindow();
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
