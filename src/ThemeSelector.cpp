#include <LWSSettings/ThemeSelector.hpp>
namespace LWSSettings
{
    ThemeSelector::ThemeSelector(ThemeSession& session, std::function<Status()> save, std::function<void()> revert,
                                 std::function<void()> edit)
        : session_(session), save_(std::move(save)), revert_(std::move(revert)), edit_(std::move(edit))
    {
        sizeToContent = true;
        flat = true; SetHighlightPattern(""); Refresh();
        changed_ = session_.OnChange.Connect([this] { Refresh(); });
        SetContextMenuProvider([](auto& owner, const auto&) { return static_cast<ThemeSelector&>(owner).Menu(); });
    }
    void ThemeSelector::Refresh()
    {
        SetText(std::string("Theme: ") + LWSUI::ThemePresetLabel(session_.Current().preset) + (session_.IsDirty() ? " *" : ""));
        Invalidate(true);
    }
    std::vector<LWSUI::ContextMenuItem> ThemeSelector::Menu()
    {
        std::vector<LWSUI::ContextMenuItem> items;
        for (auto preset : LWSUI::ThemePresets)
            items.push_back({LWSUI::ThemePresetLabel(preset), "", [preset](auto& owner)
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
