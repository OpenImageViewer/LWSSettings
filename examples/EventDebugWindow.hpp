#pragma once
#include "EventLogging.hpp"
#include <LWS/Platform.hpp>
#include <LWSUI/UIHost.hpp>
#include <LWSUI/Containers.hpp>
#include <deque>
#include <stdexcept>
namespace LWSSettings::Example
{
    class EventDebugWindow
    {
      public:
        static constexpr size_t HistoryLimit = 200;
        EventDebugWindow(LWS::PlatformContext& platform, LWSUI::Theme theme, std::function<void(const Error&)> report)
            : platform_(platform), theme_(std::move(theme)), report_(std::move(report)) {}
        ~EventDebugWindow() { lifetime_.reset(); }
        void Append(const Change& event)
        {
            records_.push_back(EventJson(event).dump());
            if (records_.size() > HistoryLimit) records_.pop_front();
            if (window_ && window_->IsVisible()) Refresh();
        }
        void Clear()
        {
            records_.clear();
            Refresh();
        }
        const std::deque<std::string>& Records() const { return records_; }
        void RequestToggle()
        {
            if (pending_) return;
            pending_ = true;
            if (platform_.PostTask([this, weak = std::weak_ptr(lifetime_)]
            {
                if (weak.expired()) return;
                pending_ = false;
                try { Toggle(); }
                catch (const std::exception& error) { report_({"events", error.what()}); }
            }) != LWS::Result::Success)
            {
                pending_ = false;
                report_({"events", "Cannot schedule event window"});
            }
        }

      private:
        friend struct EventDebugWindowTestAccess;
        void Refresh()
        {
            if (!output_) return;
            std::string text;
            for (const auto& record : records_) { text += record; text += '\n'; }
            output_->SetText(std::move(text));
        }
        void Toggle()
        {
            if (!window_)
            {
                auto window = std::make_unique<LWS::Window>(platform_);
                LWS::WindowConfig config;
                config.title = LWSUI::NativeText("LWSSettings Events");
                config.styles = LWS::WindowStyle::Caption | LWS::WindowStyle::CloseButton |
                                LWS::WindowStyle::ResizableBorder | LWS::WindowStyle::MinimizeButton |
                                LWS::WindowStyle::MaximizeButton;
                config.clientSize = {780, 420};
                config.clientSizeLimits.minimum = {400, 240};
                config.visible = false;
                config.eraseBackground = false;
                config.backgroundColor = theme_.background;
                if (window->Create(config) != LWS::Result::Success)
                    throw std::runtime_error("Cannot create event window");
                auto listener = window->Listen([this](const LWS::AnyEvent& event)
                {
                    if (std::holds_alternative<LWS::EventCloseRequested>(event))
                    {
                        if (window_->SetVisible(false) != LWS::Result::Success)
                            report_({"events", "Cannot hide event window"});
                        return LWS::EventResponse::Handled;
                    }
                    if (auto* key = std::get_if<LWS::EventKeyDown>(&event); key && key->key == LWS::KeyCode::F8)
                    {
                        if (!key->repeat) RequestToggle();
                        return LWS::EventResponse::Handled;
                    }
                    return LWS::EventResponse::Unhandled;
                });
                if (!listener) throw std::runtime_error("Cannot register event window listener");
                auto host = std::make_unique<LWSUI::UIHost>(*window, theme_);
                host->SetRedrawOnResize(true);
                hostErrors_ = host->OnError.Connect([this](const auto& message) { report_({"events", message}); });
                auto content = std::make_unique<LWSUI::DockPanel>();
                content->gap = 8;
                auto toolbar = std::make_unique<LWSUI::DockPanel>();
                auto clear = std::make_unique<LWSUI::Button>("Clear");
                clear->sizeToContent = true;
                clear_ = clear->OnClick.Connect([this] { Clear(); });
                toolbar->AddDocked(std::move(clear), LWSUI::Dock::Right);
                toolbar->AddDocked(std::make_unique<LWSUI::Label>("Settings events / latest " + std::to_string(HistoryLimit)), LWSUI::Dock::Fill);
                content->AddDocked(std::move(toolbar), LWSUI::Dock::Top);
                auto output = std::make_unique<LWSUI::TextBox>("", LWSUI::TextBoxMode::Multiline);
                auto* text = output.get();
                output->SetReadOnly(true);
                output->fontRole = LWSUI::Control::FontRole::Small;
                output->SetPlaceholder("Settings events appear here. Select and copy text or scroll through the history.");
                content->AddDocked(std::move(output), LWSUI::Dock::Fill);
                auto frame = std::make_unique<LWSUI::Panel>(std::move(content));
                frame->padding = LWSUI::Insets{12};
                host->SetRoot(std::move(frame));
                window_ = std::move(window);
                host_ = std::move(host);
                listener_ = std::move(*listener);
                output_ = text;
                std::ignore = window_->Center(LWS::CenterTarget::PrimaryMonitor);
            }
            const bool show = !window_->IsVisible();
            if (show) Refresh();
            if (window_->SetVisible(show) != LWS::Result::Success)
                throw std::runtime_error("Cannot change event window visibility");
            if (show)
            {
                host_->Update();
                std::ignore = window_->RequestActivation();
            }
        }
        LWS::PlatformContext& platform_;
        LWSUI::Theme theme_;
        std::function<void(const Error&)> report_;
        std::deque<std::string> records_;
        std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
        bool pending_ = false;
        std::unique_ptr<LWS::Window> window_;
        std::unique_ptr<LWSUI::UIHost> host_;
        LWSUI::TextBox* output_ = nullptr;
        LWS::EventConnection listener_;
        LWSUI::Event<void()>::Connection clear_;
        LWSUI::Event<void(const std::string&)>::Connection hostErrors_;
    };
}
