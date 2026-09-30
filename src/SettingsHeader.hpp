#pragma once
#include <LWSSettings/SettingsTheme.hpp>
#include <LWSUI/Menu.hpp>
#include <LWSUI/Containers.hpp>

namespace LWSSettings::internal
{
    struct SettingsHeaderState
    {
        std::string title;
        LWS::BitmapSharedPtr icon;
        SettingsTheme theme;
        bool dirty = false;
        LWSUI::Event<void()> changed;
    };
    class SettingsHeader final : public LWSUI::Panel
    {
      public:

        explicit SettingsHeader(std::shared_ptr<SettingsHeaderState> state);
        LWSUI::StackPanel& Actions() { return *actions_; }
        void UseMenuBar(LWSUI::MenuBar& bar);

      protected:

        LWSUI::Size OnMeasure(LWSUI::Size available) override;

      private:

        void Refresh();
        std::shared_ptr<SettingsHeaderState> state_;
        LWSUI::Label *title_, *saved_;
        LWSUI::StackPanel* actions_;
        LWSUI::ControlHandle bar_;
        LWSUI::Event<void()>::Connection changed_;
        LWSUI::Image* image_ = nullptr;
        bool compact_ = false;
        float appliedHeight_ = -1;
    };
}  // namespace LWSSettings::internal
