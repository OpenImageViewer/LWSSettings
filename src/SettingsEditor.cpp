#include <LWSSettings/SettingsEditor.hpp>
#include <LWS/Platform.hpp>
#include "ThemeFields.hpp"
#include "SettingsIcon.hpp"
#include <cmath>
namespace LWSSettings
{
    SettingsEditor::SettingsEditor(LWS::PlatformContext& platform, EditorConfig config)
        : platform_(platform), config_(std::move(config)), document_(config_.document) {}
    SettingsEditor::~SettingsEditor() = default;
    void SettingsEditor::SetRedrawOnResize(bool enabled)
    {
        config_.redrawOnResize = enabled;
        if (host_)
            host_->SetRedrawOnResize(enabled);
    }
    void SettingsEditor::SetInvalidEditPolicy(LWSUI::InvalidEditPolicy policy)
    {
        config_.invalidEditPolicy = policy;
        if (host_) host_->SetInvalidEditPolicy(policy);
    }
    Status SettingsEditor::Show()
    {
        if (!platform_.IsUsable())
            return std::unexpected(Error{"window", "Initialize the editor platform context first"});
        auto validTheme = ThemeFields::Validate(config_.theme);
        if (!validTheme) return validTheme;
        try
        {
            // A native Window lifetime is terminal upstream. Rebuild its UI only outside dispatch.
            if (window_ && (!window_->IsCreated() || !host_ || !view_))
            {
                errorConnection_ = {};
                minimumConnection_ = {};
                unhandledKeyConnection_ = {};
                minimumPending_ = false;
                if (view_) { initialValueFilter_ = view_->GetValueFilter(); filter_ = view_->GetFilter(); }
                host_.reset();
                listener_ = {};
                window_.reset();
                view_ = nullptr;
                themePending_ = false;
            }
            if (!window_)
            {
                auto window = std::make_unique<LWS::Window>(platform_);
                LWS::WindowConfig wc;
                wc.title = LWSUI::NativeText(config_.title);
                wc.clientSize = config_.size;
                wc.clientSizeLimits.minimum = {480, 440};
                wc.styles = LWS::WindowStyle::ResizableBorder;
                wc.eraseBackground = false;
                wc.backgroundColor = config_.theme.base.background;
                if (window->Create(wc) != LWS::Result::Success)
                    return std::unexpected(Error{"window", "Cannot create settings window"});
                auto loadedIcon = internal::LoadSettingsIcon(config_.iconPath);
                auto icon = loadedIcon ? *loadedIcon : internal::DefaultSettingsIcon();
                auto nativeResource = LWS::WindowIcon::FromBitmap(icon->GetBuffer());
                const auto nativeIcon = nativeResource ? window->SetWindowIcon(std::move(*nativeResource)) : nativeResource.error();
                window_ = std::move(window);
                // Observe application shortcuts before modal controls consume keys.
                auto listener = window_->Listen(
                    [this](const LWS::AnyEvent& event)
                    {
                        if (auto* key = std::get_if<LWS::EventKeyDown>(&event); key && !key->repeat)
                            OnKeyDown.Raise(key->key);
                        if (std::holds_alternative<LWS::EventCloseRequested>(event))
                        {
                            Hide();
                            return LWS::EventResponse::Handled;
                        }
                        return LWS::EventResponse::Unhandled;
                    });
                if (!listener) throw std::runtime_error("Cannot register editor listener");
                listener_ = std::move(*listener);
                host_ = std::make_unique<LWSUI::UIHost>(*window_, config_.theme.base);
                unhandledKeyConnection_ = host_->OnUnhandledKeyDown.Connect(
                    [this](const LWSUI::Input& input, bool& handled) { OnUnhandledKeyDown.Raise(input, handled); });
                host_->SetRedrawOnResize(config_.redrawOnResize);
                host_->SetInvalidEditPolicy(config_.invalidEditPolicy);
                host_->SetPopupStyle(config_.theme.popups);
                auto bar = std::make_unique<LWSUI::MenuBar>();
                bar->SetWindowControls({.minimize = true, .draggable = true, .requestClose = [this] { Hide(); }});
                host_->SetMainMenu(std::move(bar));
                auto view = std::make_unique<SettingsView>(document_, config_.title, config_.theme, config_.showRevert,
                                                           config_.collapseGroups);
                auto* createdView = view.get();
                view->SetIcon(icon);
                view->SetSaveValidationPolicy(config_.saveValidationPolicy);
                view->SetSaveDialogOptions(config_.saveDialogOptions);
                host_->SetRoot(std::move(view));
                view_ = createdView;
                view_->MoveHeaderTo(*host_->MainMenu());
                minimumConnection_ = host_->MainMenu()->OnMinimumWidthChanged.Connect(
                    [this, handle = host_->MainMenu()->Handle()](float width)
                    {
                        minimumWidth_ = std::max(480, int32_t(std::ceil(width)));
                        if (minimumPending_) return;
                        minimumPending_ = true;
                        host_->Post(handle, [this](LWSUI::Control&)
                        {
                            minimumPending_ = false;
                            if (!window_ || !window_->IsCreated()) return;
                            auto limits = window_->GetClientSizeLimits();
                            auto result = LWS::Result::Success;
                            if (limits.minimum.x != minimumWidth_) { limits.minimum.x = minimumWidth_; result = window_->SetClientSizeLimits(limits); }
                            const auto size = window_->GetClientAreaMetrics().logical;
                            if (result == LWS::Result::Success && size.x < minimumWidth_)
                                result = window_->RequestPlacement({.clientSize = LWS::LogicalSize{minimumWidth_, std::max(440, size.y)}});
                            if (result != LWS::Result::Success) document_.OnError.Raise({"window", "Cannot apply header minimum width"});
                        });
                    });
                view_->SetFilter(filter_);
                view_->SetValueFilter(initialValueFilter_);
                errorConnection_ = host_->OnError.Connect(
                    [this](const std::string& message)
                    {
                        Error error{"ui", message};
                        document_.OnError.Raise(error);
                    });
                std::ignore = window_->Center(LWS::CenterTarget::PrimaryMonitor);
                if (!loadedIcon) document_.OnError.Raise(loadedIcon.error());
                if (nativeIcon != LWS::Result::Success && nativeIcon != LWS::Result::NotSupported)
                    document_.OnError.Raise(Error{"icon", "Cannot apply native window icon; the header icon remains available"});
            }
            if (window_->SetVisible(true) != LWS::Result::Success)
                return std::unexpected(Error{"window", "Cannot show settings window"});
            host_->Invalidate(true);
            host_->Update();
            return {};
        }
        catch (const std::exception& e)
        {
            return std::unexpected(Error{"window", e.what()});
        }
    }
    LWS::Result SettingsEditor::RequestActivation()
    {
        if (!window_ || !window_->IsCreated()) return LWS::Result::InvalidState;
        if (window_->GetShowState() == LWS::WindowShowState::Minimized)
        {
            const auto restored = window_->RequestShowState(LWS::WindowShowState::Restored);
            if (restored != LWS::Result::Success) return restored;
        }
        return window_->RequestActivation();
    }
    bool SettingsEditor::Hide()
    {
        const bool wasVisible = IsVisible();
        if (host_ && wasVisible)
        {
            if (config_.revertOnClose)
            {
                // Explicit revert remains cancellation rather than a save request.
                if (view_ && !view_->PrepareClose()) return false;
                if (!host_->Finish(LWSUI::EditPhase::Cancel)) return false;
                document_.RevertSaved();
            }
            else if (view_ && !view_->FinishForClose(config_.closeValidationPolicy, config_.closeDialogOptions,
                                                    [this] { HideWindow(); })) return false;
        }
        return HideWindow();
    }
    bool SettingsEditor::HideWindow()
    {
        const bool wasVisible = IsVisible();
        if (window_ && window_->IsCreated() && window_->SetVisible(false) != LWS::Result::Success)
            return false;
        if (wasVisible) OnHidden.Raise();
        return true;
    }
    bool SettingsEditor::IsVisible() const
    {
        return window_ && window_->IsVisible();
    }
    Status SettingsEditor::SetTheme(const SettingsTheme& theme)
    {
        auto valid = ThemeFields::Validate(theme);
        if (!valid)
            return valid;
        config_.theme = theme;
        if (host_ && !themePending_)
        {
            themePending_ = true;
            host_->Post(view_->Handle(),
                        [this](LWSUI::Control& control)
                        {
                            themePending_ = false;
                            host_->ClosePopup(LWSUI::EditPhase::Cancel);
                            host_->SetTheme(config_.theme.base);
                            host_->SetPopupStyle(config_.theme.popups);
                            static_cast<SettingsView&>(control).SetTheme(config_.theme);
                        });
        }
        return {};
    }
    void SettingsEditor::SetValueFilter(SettingValueFilter filter)
    {
        if (view_) { view_->SetValueFilter(filter); initialValueFilter_ = view_->GetValueFilter(); }
        else initialValueFilter_ = filter;
    }
    SettingValueFilter SettingsEditor::GetValueFilter() const
    {
        return view_ ? view_->GetValueFilter() : initialValueFilter_;
    }
    void SettingsEditor::SetFilter(std::string text)
    {
        filter_ = std::move(text);
        if (view_)
            view_->SetFilter(filter_);
    }
}  // namespace LWSSettings
