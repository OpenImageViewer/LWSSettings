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
    class SettingsHeader final : public LWSUI::Container
    {
      public:

        explicit SettingsHeader(std::shared_ptr<SettingsHeaderState> state);
        LWSUI::StackPanel& Actions() { return *actions_; }
        void UseMenuBar(LWSUI::MenuBar& bar);
        LWSUI::Control* HitTest(float x, float y) override;

      protected:

        LWSUI::Size OnMeasure(LWSUI::Size available) override;
        void OnArrange() override;
        void OnRender(LWSUI::Canvas& canvas) override;

      private:

        void Refresh();
        float Width(std::string_view text, const LWSUI::FontSpec& font) const;
        std::shared_ptr<SettingsHeaderState> state_;
        LWSUI::Label *title_, *saved_;
        LWSUI::StackPanel* actions_;
        LWSUI::ControlHandle bar_;
        LWSUI::Event<void()>::Connection changed_;
        bool compact_ = false, wrap_ = false;
        float firstHeight_ = 0, statusWidth_ = 0, actionWidth_ = 0, appliedHeight_ = -1;
    };
}  // namespace LWSSettings::internal
