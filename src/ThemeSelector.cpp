#include <LWSSettings/ThemeSelector.hpp>
namespace LWSSettings
{
    namespace
    {
        const char* Label(LWSUI::ThemePreset preset)
        {
            switch (preset) { case LWSUI::ThemePreset::Light: return "Light"; case LWSUI::ThemePreset::Warm: return "Warm"; default: return "Dark"; }
        }
    }
    ThemeSelector::ThemeSelector(ThemeSession& session, std::function<Status()> save, std::function<void()> revert,
                                 std::function<void()> edit)
        : session_(session), save_(std::move(save)), revert_(std::move(revert)), edit_(std::move(edit))
    {
        flat = true; SetHighlightPattern(""); Refresh();
        changed_ = session_.OnChange.Connect([this] { Refresh(); });
        SetContextMenuProvider([](auto& owner, const auto&) { return static_cast<ThemeSelector&>(owner).Menu(); });
    }
    void ThemeSelector::Refresh()
    {
        SetText(std::string("Theme: ") + Label(session_.Current().preset) + (session_.IsDirty() ? " *" : ""));
        Invalidate(true);
    }
    LWSUI::Size ThemeSelector::OnMeasure(LWSUI::Size available)
    {
        auto size = Button::OnMeasure(available);
        size.width = std::min(available.width, (Host() ? Host()->MeasureText(Text(), 10000, false, Font()).width : Text().size()*Font().size) + 2*Style().padding);
        return size;
    }
    std::vector<LWSUI::ContextMenuItem> ThemeSelector::Menu()
    {
        std::vector<LWSUI::ContextMenuItem> items;
        for (auto preset : {LWSUI::ThemePreset::Dark, LWSUI::ThemePreset::Light, LWSUI::ThemePreset::Warm})
            items.push_back({Label(preset), "", [preset](auto& owner)
            { auto& self = static_cast<ThemeSelector&>(owner); (void) self.session_.SelectPreset(preset); }, true, preset == session_.Current().preset});
        items.push_back(LWSUI::ContextMenuItem::Separator());
        items.push_back({"Save theme", "", [](auto& owner)
        {
            auto& self = static_cast<ThemeSelector&>(owner);
            auto result = self.save_ ? self.save_() : self.session_.Save();
            if (!result && self.Host()) self.Host()->ReportError(result.error().message);
        }, session_.IsDirty()});
        items.push_back({"Revert theme", "", [](auto& owner)
        { auto& self = static_cast<ThemeSelector&>(owner); if (self.revert_) self.revert_(); else self.session_.Revert(); }, session_.IsDirty()});
        if (edit_) items.push_back({"Edit theme", "F6", [](auto& owner) { static_cast<ThemeSelector&>(owner).edit_(); }});
        return items;
    }
    void ThemeSelector::Activate()
    {
        if (Host()) { const auto b = Bounds(); Host()->ShowContextMenu(*this, {Handle(), b.x, b.y+b.height, true}, Menu()); }
    }
}
