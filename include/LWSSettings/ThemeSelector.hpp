#pragma once
#include <LWSSettings/ThemeSession.hpp>
#include <LWSUI/Composites.hpp>
namespace LWSSettings
{
    // Optional UI adapter; persistence and previews remain in the shared session.
    class ThemeSelector : public LWSUI::Button
    {
      public:
        ThemeSelector(ThemeSession&, std::function<Status()> save = {}, std::function<void()> revert = {},
                      std::function<void()> edit = {});
      protected:
        LWSUI::Size OnMeasure(LWSUI::Size) override;
        void Activate() override;
      private:
        void Refresh();
        std::vector<LWSUI::ContextMenuItem> Menu();
        ThemeSession& session_;
        std::function<Status()> save_;
        std::function<void()> revert_, edit_;
        LWSUI::Event<void()>::Connection changed_;
    };
}
