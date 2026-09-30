#include "SettingsHeader.hpp"
#include <LWSUI/UIHost.hpp>
#include <LWSUI/TextEditing.hpp>
#include <algorithm>

namespace LWSSettings::internal
{
    namespace
    {
        class HeaderLabel : public LWSUI::Label
        {
          public:

            bool compact = false;

          protected:

            void OnRender(LWSUI::Canvas& canvas) override
            {
                if (!compact)
                {
                    Label::OnRender(canvas);
                    return;
                }
                const auto b = Bounds();
                const auto h = canvas.TextHeight(Text(), std::max(1.f, b.width), Font(), false);
                DrawText(canvas, Text(), b.x, b.y + (b.height - h) / 2, b.width, h, Foreground(), false);
            }
        };
        class HeaderActions : public LWSUI::StackPanel
        {
          public:

            bool compact = false;
            float fallbackWidth = 100;
            LWSUI::Control* HitTest(float x, float y) override
            {
                if (!Visible() || !Bounds().Contains(x, y))
                    return nullptr;
                for (auto& child : Children())
                {
                    if (auto* hit = child->HitTest(x, y))
                        return hit;
                    if (child->Visible() && !child->Enabled() && child->Bounds().Contains(x, y))
                        return child.get();
                }
                return nullptr;
            }

          protected:

            LWSUI::Size OnMeasure(LWSUI::Size available) override
            {
                if (!compact)
                    return StackPanel::OnMeasure(available);
                LWSUI::Size size{};
                size_t count = 0;
                for (auto& child : Children())
                    if (child->Visible())
                    {
                        float width = fallbackWidth;
                        if (auto* button = dynamic_cast<LWSUI::Button*>(child.get()); button && Host())
                            width = Host()->MeasureText(button->Text(), 10000, false, button->Font()).width +
                                    2 * button->Style().padding;
                        const auto measured = child->Measure({std::max(1.f, width), available.height});
                        size.width += measured.width;
                        size.height = std::max(size.height, measured.height);
                        ++count;
                    }
                if (count > 1)
                    size.width += (count - 1) * spacing;
                return size;
            }
            void OnArrange() override
            {
                if (!compact)
                {
                    StackPanel::OnArrange();
                    return;
                }
                const auto b = Bounds();
                float x = b.x;
                for (auto& child : Children())
                    if (child->Visible())
                    {
                        const float width = child->DesiredSize().width;
                        // A compositor may disregard a minimum size. Never expose a partial click target.
                        child->Arrange(x + width <= b.x + b.width + .01f ? LWSUI::Rect{x, b.y, width, b.height}
                                                                         : LWSUI::Rect{});
                        x += width + spacing;
                    }
            }
        };
    }  // namespace
    SettingsHeader::SettingsHeader(std::shared_ptr<SettingsHeaderState> state) : state_(std::move(state))
    {
        title_ = &Emplace<HeaderLabel>();
        saved_ = &Emplace<HeaderLabel>();
        actions_ = &Emplace<HeaderActions>();
        actions_->orientation = LWSUI::Orientation::Horizontal;
        changed_ = state_->changed.Connect([this] { Refresh(); });
        Refresh();
    }
    float SettingsHeader::Width(std::string_view text, const LWSUI::FontSpec& font) const
    {
        return Host() ? Host()->MeasureText(text, 10000, false, font).width : text.size() * font.size * .6f;
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
        }
        SetStyle(style);
        title_->SetStyle(style);
        saved_->SetStyle(style);
        actions_->SetStyle(style);
        title_->fontRole = compact_ ? FontRole::Normal : FontRole::Title;
        saved_->fontRole = FontRole::Small;
        static_cast<HeaderLabel*>(title_)->compact = compact_;
        static_cast<HeaderLabel*>(saved_)->compact = compact_;
        static_cast<HeaderActions*>(actions_)->compact = compact_;
        static_cast<HeaderActions*>(actions_)->fallbackWidth = state_->theme.layout.actionWidth;
        if (!compact_)
            title_->SetText(state_->title);
        saved_->SetText(state_->dirty ? "Unsaved" : "Saved");
        saved_->SetForeground(state_->dirty ? style.accent : style.muted);
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
        title_->Measure(available);
        saved_->Measure(available);
        const auto actions = actions_->Measure(available);
        actionWidth_ = actions.width;
        if (compact_)
        {
            statusWidth_ = std::max(Width("Saved", saved_->Font()), Width("Unsaved", saved_->Font())) + 8;
            const float minimum = statusWidth_ + actionWidth_ + (actionWidth_ > 0 ? 8.f : 0.f) + 12;
            if (auto* bar = static_cast<LWSUI::MenuBar*>(bar_.Get()); bar && bar->Content() == this)
                bar->SetContentMinimumWidth(minimum);
            return {minimum + Width(state_->title, title_->Font()) + 8, available.height};
        }
        const auto& theme = state_->theme;
        const float inner = std::max(0.f, available.width - 2 * theme.layout.horizontalPadding);
        statusWidth_ = std::min(inner, std::max(theme.layout.actionWidth, theme.header.smallFont.size * 8));
        const float icon = state_->icon ? title_->DesiredSize().height + theme.header.padding : 0;
        const float gap = actions.width > 0 ? theme.layout.sectionGap : 0;
        wrap_ = actions.width > 0 &&
                inner < Width(state_->title, title_->Font()) + icon + statusWidth_ + actions.width + gap;
        firstHeight_ = std::max(theme.layout.headerHeight,
                                std::max(title_->DesiredSize().height, wrap_ ? 0.f : actions.height) +
                                    2 * theme.header.padding);
        return {available.width, firstHeight_ + (wrap_ ? actions.height + gap : 0)};
    }
    void SettingsHeader::OnArrange()
    {
        const auto b = Bounds();
        if (compact_)
        {
            const float right = b.x + std::max(0.f, b.width - 6);
            saved_->Arrange({std::max(b.x, right - statusWidth_), b.y, std::min(statusWidth_, b.width), b.height});
            const float actionsRight = std::max(b.x, right - statusWidth_ - 8);
            const float actionsWidth = std::min(actionWidth_, std::max(0.f, actionsRight - b.x - 6));
            actions_->Arrange({actionsRight - actionsWidth, b.y, actionsWidth, b.height});
            const float remaining = std::max(0.f, actionsRight - actionsWidth - b.x - 14);
            std::string title = state_->title;
            if (Width(title, title_->Font()) > remaining)
            {
                while (!title.empty() && Width(title + "…", title_->Font()) > remaining)
                    title.resize(LWSUI::PreviousCharacter(title, title.size()));
                title = Width("…", title_->Font()) <= remaining ? title + "…" : "";
            }
            title_->SetText(title);
            title_->Arrange({b.x + 6, b.y, std::min(remaining, Width(title, title_->Font())), b.height});
            return;
        }
        const auto& theme = state_->theme;
        const float p = theme.layout.horizontalPadding, inner = std::max(0.f, b.width - 2 * p);
        const float icon = state_->icon ? title_->DesiredSize().height + theme.header.padding : 0;
        const float gap = actionWidth_ > 0 ? theme.layout.sectionGap : 0;
        title_->Arrange({b.x + p + icon - theme.header.labelPadding, b.y + theme.header.padding,
                         std::max(0.f, inner - statusWidth_ - icon - (wrap_ ? 0 : actionWidth_ + gap)),
                         title_->DesiredSize().height});
        saved_->Arrange({b.x + b.width - p - statusWidth_, b.y + (firstHeight_ - saved_->DesiredSize().height) / 2,
                         statusWidth_, saved_->DesiredSize().height});
        actions_->Arrange({wrap_ ? b.x + p : b.x + b.width - p - statusWidth_ - actionWidth_ - gap,
                           wrap_ ? b.y + firstHeight_ : b.y + (firstHeight_ - actions_->DesiredSize().height) / 2,
                           std::min(inner, actionWidth_), actions_->DesiredSize().height});
    }
    LWSUI::Control* SettingsHeader::HitTest(float x, float y)
    {
        if (!Visible() || !Bounds().Contains(x, y))
            return nullptr;
        // The compact title and status are passive chrome; their pixels can start a window move.
        if (compact_)
            return actions_->HitTest(x, y);
        for (auto& child : Children())
            if (auto* hit = child->HitTest(x, y))
                return hit;
        return nullptr;  // Layout gaps are genuinely unoccupied, even inside the content slot.
    }
    void SettingsHeader::OnRender(LWSUI::Canvas& canvas)
    {
        if (!compact_)
        {
            const auto b = Bounds();
            canvas.Fill(b.x, b.y, b.width, b.height, state_->theme.header.background);
            if (state_->icon)
            {
                const float side = title_->DesiredSize().height;
                canvas.Image(state_->icon->GetBuffer(), b.x + state_->theme.layout.horizontalPadding,
                             b.y + state_->theme.header.padding, side, side);
            }
        }
        Container::OnRender(canvas);
    }
}  // namespace LWSSettings::internal
