#include <LWSSettings/SettingsEditor.hpp>
#include <LWS/Platform.hpp>
#include "ThemeFields.hpp"
#include "SettingsIcon.hpp"
namespace LWSSettings
{
    SettingsEditor::SettingsEditor(LWS::PlatformContext& platform, EditorConfig config)
        : platform_(platform), config_(std::move(config)), document_(config_.document) {}
    SettingsEditor::~SettingsEditor() = default;
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
                wc.styles = LWS::WindowStyle::Caption | LWS::WindowStyle::CloseButton |
                            LWS::WindowStyle::ResizableBorder;
                wc.eraseBackground = false;
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
                host_->SetPopupStyle(config_.theme.popups);
                auto view = std::make_unique<SettingsView>(document_, config_.title, config_.theme, config_.showRevert,
                                                           config_.collapseGroups);
                auto* createdView = view.get();
                view->SetIcon(icon);
                host_->SetRoot(std::move(view));
                view_ = createdView;
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
    bool SettingsEditor::Hide()
    {
        const bool wasVisible = IsVisible();
        if (host_ && !host_->Finish(config_.revertOnClose ? LWSUI::EditPhase::Cancel : LWSUI::EditPhase::Commit))
            return false;
        if (config_.revertOnClose)
            document_.RevertSaved();
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
        if (view_) view_->SetValueFilter(filter);
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
