#include "SettingsHeader.hpp"
#include <LWSUI/UIHost.hpp>
namespace LWSSettings::internal
{
    SettingsHeader::SettingsHeader(std::shared_ptr<SettingsHeaderState> state) : state_(std::move(state))
    {
        hitTestBackground = false;
        auto row = std::make_unique<LWSUI::DockPanel>();
        row->gap = 8;
        row->hitTestBackground = false;
        auto icon = std::make_unique<LWSUI::Image>();
        image_ = icon.get();
        row->AddDocked(std::move(icon), LWSUI::Dock::Left);
        auto saved = std::make_unique<LWSUI::Label>();
        saved_ = saved.get();
        saved_->minimumText = "Unsaved";
        row->AddDocked(std::move(saved), LWSUI::Dock::Right);
        auto actions = std::make_unique<LWSUI::StackPanel>();
        actions_ = actions.get();
        actions_->orientation = LWSUI::Orientation::Horizontal;
        actions_->sizeToContent = true;
        actions_->clipPartialChildren = true;
        actions_->hitTestBackground = false;
        row->AddDocked(std::move(actions), LWSUI::Dock::Right);
        auto title = std::make_unique<LWSUI::Label>();
        title_ = title.get();
        title_->trimming = LWSUI::TextTrimming::Ellipsis;
        row->AddDocked(std::move(title), LWSUI::Dock::Fill);
        SetContent(std::move(row));
        changed_ = state_->changed.Connect([this] { Refresh(); });
        Refresh();
    }
    void SettingsHeader::UseMenuBar(LWSUI::MenuBar& bar)
    {
        compact_ = true;
        bar_ = bar.Handle();
        Refresh();
    }
    void SettingsHeader::Refresh()
    {
        auto style = state_->theme.header;
        if (compact_)
        {
            const float height = appliedHeight_ > 0 ? appliedHeight_ : style.menuBarHeight;
            const float available = std::max(1.f, height - 8);
            style.font.size = std::min(style.font.size, available / 1.4f);
            style.smallFont.size = std::min(style.smallFont.size, available / 1.4f);
            style.rowHeight = std::min(style.rowHeight, available);
            style.padding = std::min(style.padding, 4.f);
            style.labelPadding = 0;
        }
        SetStyle(style);
        title_->SetStyle(style);
        saved_->SetStyle(style);
        actions_->SetStyle(style);
        title_->fontRole = compact_ ? FontRole::Normal : FontRole::Title;
        saved_->fontRole = FontRole::Small;
        title_->verticalAlignment = saved_->verticalAlignment = LWSUI::Alignment::Center;
        title_->hitTestVisible = saved_->hitTestVisible = !compact_;
        title_->SetText(state_->title);
        saved_->SetText(state_->dirty ? "Unsaved" : "Saved");
        saved_->SetForeground(state_->dirty ? style.accent : style.muted);
        image_->SetVisible(!compact_ && bool(state_->icon));
        image_->SetBitmap(state_->icon);
        image_->size = style.titleFont.size * 1.4f;
        actions_->fallbackItemWidth = state_->theme.layout.actionWidth;
        padding = compact_ ? LWSUI::Insets{6, 0, 6, 0}
                           : LWSUI::Insets{state_->theme.layout.horizontalPadding, style.padding,
                                           state_->theme.layout.horizontalPadding, style.padding};
        minimumHeight = compact_ ? 0 : state_->theme.layout.headerHeight;
        paintBackground = !compact_;
        if (auto* bar = static_cast<LWSUI::MenuBar*>(bar_.Get()); bar && bar->Content() == this)
        {
            auto chrome = style;
            chrome.surface = chrome.background;
            bar->SetStyle(chrome);
            bar->SetIcon(state_->icon);
        }
        Invalidate(true);
    }
    LWSUI::Size SettingsHeader::OnMeasure(LWSUI::Size available)
    {
        if (compact_ && appliedHeight_ != available.height)
        {
            appliedHeight_ = available.height;
            Refresh();
        }
        const auto measured = Panel::OnMeasure(available);
        if (!compact_)
            return measured;
        const float minimum = saved_->DesiredSize().width + actions_->DesiredSize().width + 28;
        if (auto* bar = static_cast<LWSUI::MenuBar*>(bar_.Get()); bar && bar->Content() == this)
            bar->SetContentMinimumWidth(minimum);
        return {minimum + (Host() ? Host()->MeasureText(state_->title, 10000, false, title_->Font()).width : 160),
                available.height};
    }
}  // namespace LWSSettings::internal
