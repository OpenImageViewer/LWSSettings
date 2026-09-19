#include "Internal.hpp"
#include "TextEditing.hpp"
#include <LWS/Platform.hpp>
#include <LWS/FileDialog.hpp>
#include <LWS/TextClipboard.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>
#ifdef _WIN32
    #include <windows.h>
#endif

namespace LWSSettings
{
    namespace
    {
        LLUtils::Color Color(uint32_t c)
        {
            return LLUtils::Color{c};
        }
        LWS::string_type Native(std::string_view text)
        {
#ifdef _WIN32
            int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
            std::wstring s(n, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), s.data(), n);
            return s;
#else
            return std::string(text);
#endif
        }
        std::string Utf8(const std::filesystem::path& path)
        {
            auto s = path.u8string();
            return {reinterpret_cast<const char*>(s.data()), s.size()};
        }
        size_t VisibleChoices(size_t total, float height)
        {
            return std::min(total, size_t(std::max(1.f, std::min(8.f, std::floor((height - 290) / 28)))));
        }
        bool Editable(Control type)
        {
            return type == Control::Text || type == Control::Integer || type == Control::Number ||
                   type == Control::File || type == Control::Color;
        }
    }  // namespace

    SettingsPage::Impl::~Impl()
    {
        lifetime.reset();
        spinTimer.reset();
        timer.reset();
        listener = {};
        EndScrollbarDrag();
        canvas.reset();
        window.reset();
    }
    bool SettingsPage::Impl::ControlPressed() const
    {
        return LWS::Platform::isKeyPressed(LWS::KeyCode::Control) ||
               LWS::Platform::isKeyPressed(LWS::KeyCode::LControl) ||
               LWS::Platform::isKeyPressed(LWS::KeyCode::RControl);
    }
    bool SettingsPage::Impl::ShiftPressed() const
    {
        return LWS::Platform::isKeyPressed(LWS::KeyCode::Shift) || LWS::Platform::isKeyPressed(LWS::KeyCode::LShift) ||
               LWS::Platform::isKeyPressed(LWS::KeyCode::RShift);
    }
    Status SettingsPage::Impl::Show()
    {
        if (!LWS::Platform::isInitialized())
            return Fail("window", "Initialize an LWS::Platform::Session first");
        if (!window)
        {
            try
            {
                window = std::make_unique<LWS::Window>();
                LWS::WindowConfig wc;
                wc.title = Native(config.title);
                dpiInfo = config.owner ? config.owner->GetDpiInfo() : window->GetDpiInfo();
                const auto scale = Scales();
                wc.size = {int(std::max(480.f * scale.input, config.size.x * scale.window)),
                           int(std::max(440.f * scale.input, config.size.y * scale.window))};
                wc.minSize = {int(480 * scale.input), int(440 * scale.input)};
                wc.styles = LWS::WindowStyle::Caption | LWS::WindowStyle::CloseButton |
                            LWS::WindowStyle::ResizableBorder;
                wc.eraseBackground = false;
                window->SetDestroyOnClose(false);
                if (config.owner)
                    window->SetParent(config.owner);
                if (window->Create(wc) != LWS::Result::Success)
                {
                    window.reset();
                    return Fail("window", "Could not create settings window");
                }
                listener = window->MakeListenerGuard(
                    window->AddEventListener([this](const auto& e) { return Event(e); }));
                canvas = std::make_unique<Canvas>();
                timer = std::make_unique<LWS::Timer>();
                timer->SetTargetWindow(window->GetHandle());
                timer->SetInterval(500);
                timer->SetCallback(
                    [this]
                    {
                        if (window->GetVisible() && (edit || searchFocus))
                        {
                            caretVisible = !caretVisible;
                            Paint();
                        }
                    });
                window->CenterOnScreen();
            }
            catch (const std::exception& e)
            {
                spinTimer.reset();
                timer.reset();
                listener = {};
                canvas.reset();
                window.reset();
                return Fail("window", e.what());
            }
        }
        window->SetVisible(true);
        RebuildRows();
        Paint();
        return {};
    }
    void SettingsPage::Impl::RebuildRows()
    {
        rows.clear();
        float y = 0;
        const std::string query = Lower(filter);
        auto matches = [&](auto&& self, const Node& n) -> bool
        {
            if (query.empty() || Lower(n.label + " " + n.id + " " + n.description).find(query) != std::string::npos)
                return true;
            for (const auto& child : n.children)
                if (self(self, child))
                    return true;
            return false;
        };
        auto walk = [&](auto&& self, const std::vector<Node>& list, int depth, bool ancestorMatches) -> void
        {
            for (const auto& n : list)
            {
                bool own = !query.empty() &&
                           Lower(n.label + " " + n.id + " " + n.description).find(query) != std::string::npos;
                if (!ancestorMatches && !matches(matches, n))
                    continue;
                float h = n.control == Control::Radio ? std::max(44.f, float(n.options.size()) * 27 + 12) : 44.f;
                rows.push_back({&n, depth, y, h});
                y += h;
                if (n.control == Control::Group && (!query.empty() || !collapsed.contains(n.id)))
                    self(self, n.children, depth + 1, ancestorMatches || own);
            }
        };
        walk(walk, roots, 0, false);
        contentHeight = y;
        scroll = std::clamp(scroll, 0.f, std::max(0.f, contentHeight - std::max(1.f, height - 290)));
    }
    std::string SettingsPage::Impl::Description() const
    {
        const Node* n = Find(hovered.empty() ? selected : hovered);
        return n ? n->description : std::string{};
    }
    void SettingsPage::Impl::SetFilter(std::string text)
    {
        EndScrollbarDrag();
        if (!FinishEdit())
            return;
        filter = std::move(text);
        dropdown = false;
        hovered.clear();
        scroll = 0;
        RebuildRows();
        Paint();
    }
    void SettingsPage::Impl::Select(std::string id, bool reveal)
    {
        if (selected != id && !FinishEdit())
            return;
        if (selected != id)
            descriptionScroll = 0;
        selected = std::move(id);
        searchFocus = false;
        dropdown = false;
        if (reveal)
            ScrollSelection();
        Paint();
    }
    void SettingsPage::Impl::ScrollSelection()
    {
        for (const auto& row : rows)
            if (row.node->id == selected)
            {
                float body = std::max(1.f, height - 290);
                if (row.y < scroll)
                    scroll = row.y;
                if (row.y + row.height > scroll + body)
                    scroll = row.y + row.height - body;
                scroll = std::max(0.f, scroll);
                break;
            }
    }
    void SettingsPage::Impl::BeginEdit(const Node& node)
    {
        if (edit && edit->id == node.id)
            return;
        if (!FinishEdit())
            return;
        Edit e;
        e.id = node.id;
        e.start = values.at(node.id);
        e.text = FormatValue(e.start);
        e.caret = e.text.size();
        e.anchor = e.caret;
        edit = std::move(e);
        selected = node.id;
        searchFocus = false;
        caretVisible = true;
    }
    void SettingsPage::Impl::EditText(std::string text)
    {
        if (!edit)
            return;
        auto* node = Find(edit->id);
        edit->text = std::move(text);
        nlohmann::json input;
        if (node->control == Control::Integer)
        {
            int64_t v = 0;
            auto [end, ec] = std::from_chars(edit->text.data(), edit->text.data() + edit->text.size(), v);
            if (ec != std::errc{} || end != edit->text.data() + edit->text.size())
            {
                edit->invalid = true;
                (void) Fail(node->id, "Enter a valid integer");
                Paint();
                return;
            }
            input = v;
        }
        else if (node->control == Control::Number)
        {
            double v = 0;
            auto [end, ec] = std::from_chars(edit->text.data(), edit->text.data() + edit->text.size(), v);
            if (ec != std::errc{} || end != edit->text.data() + edit->text.size() || !std::isfinite(v))
            {
                edit->invalid = true;
                (void) Fail(node->id, "Enter a finite number");
                Paint();
                return;
            }
            input = v;
        }
        else
            input = edit->text;
        auto value = ParseValue(*node, input);
        if (!value)
        {
            edit->invalid = true;
            (void) Fail(node->id, value.error().message);
            Paint();
            return;
        }
        edit->invalid = false;
        error.reset();
        status.clear();
        (void) Assign(node->id, *value, ChangeKind::Live, ChangeOrigin::User);
        Paint();
    }
    void SettingsPage::Impl::ReplaceSelection(std::string text)
    {
        if (!edit)
            return;
        text = SingleLine(std::move(text));
        size_t begin = std::min(edit->caret, edit->anchor), end = std::max(edit->caret, edit->anchor);
        auto next = edit->text;
        next.replace(begin, end - begin, text);
        edit->caret = edit->anchor = begin + text.size();
        caretVisible = true;
        EditText(std::move(next));
    }
    bool SettingsPage::Impl::FinishEdit(bool cancel)
    {
        AbortSpin();
        if (!edit)
            return true;
        if (!cancel && edit->invalid)
        {
            (void) Fail(edit->id, "Correct the invalid value or press Escape");
            return false;
        }
        auto previous = std::move(*edit);
        edit.reset();
        if (cancel)
        {
            (void) Assign(previous.id, previous.start, ChangeKind::Live, ChangeOrigin::Cancel);
            error.reset();
            status.clear();
        }
        else if (values.at(previous.id) != previous.start && config.workflow != Workflow::Staged)
        {
            Notify({previous.id, previous.start, values.at(previous.id), ChangeKind::Confirmed, ChangeOrigin::User,
                    ValueDomain::Effective});
            AutoSave();
        }
        return true;
    }
    Status SettingsPage::Impl::ResetNode(std::string_view id)
    {
        auto* n = Find(id);
        if (!n || n->control == Control::Group)
            return Fail(std::string(id), "Unknown setting");
        if (spin && spin->id == id)
            AbortSpin();
        if (edit && edit->id == id)
            edit.reset();
        if (colorId == id)
        {
            colorStart.reset();
            colorId.clear();
        }
        auto result = Assign(id, n->initial, ChangeKind::Confirmed, ChangeOrigin::Reset);
        Paint();
        return result;
    }
    Status SettingsPage::Impl::ResetAll()
    {
        AbortSpin();
        edit.reset();
        colorStart.reset();
        colorId.clear();
        const auto previous = values;
        struct BatchGuard
        {
            bool& flag;
            ~BatchGuard() { flag = false; }
        } guard{batch};
        batch = true;
        for (const auto& [id, n] : nodes)
            if (n->control != Control::Group)
                if (auto result = Assign(id, n->initial, ChangeKind::Confirmed, ChangeOrigin::Reset); !result)
                    return result;
        batch = false;
        if (previous != values)
            AutoSave();
        Paint();
        return {};
    }
    void SettingsPage::Impl::Browse(const Node& node)
    {
        EndScrollbarDrag();
        if (!FinishEdit())
            return;
        const auto id = node.id, title = node.label;
        const auto initial = std::get<std::string>(values.at(id));
        modal = true;
        struct ModalGuard
        {
            bool& flag;
            ~ModalGuard() { flag = false; }
        } modalGuard{modal};
        LWS::file_dialog_string_type filename;
        auto result = LWS::FileDialog::Show(LWS::FileDialogType::OpenFile, {}, Native(title), window->GetHandle(), {},
                                            0, Native(initial), filename);
        modal = false;
        if (result == LWS::FileDialogResult::Success)
            (void) Assign(id, Utf8(std::filesystem::path(filename)), ChangeKind::Confirmed, ChangeOrigin::User);
        else if (result != LWS::FileDialogResult::UserCanceled)
            (void) Fail(id, "File picker unavailable. Enter a path directly.");
        Paint();
    }
    void SettingsPage::Impl::OpenColor(const Node& node)
    {
        EndScrollbarDrag();
        if (!FinishEdit())
            return;
        colorId = node.id;
        colorStart = values.at(node.id);
        colorHslSource = std::get<LLUtils::Color>(*colorStart);
        colorHsl = RgbToHsl(colorHslSource);
        colorChannel = 0;
        dropdown = false;
        Paint();
    }
    void SettingsPage::Impl::CloseColor(bool cancel)
    {
        if (!colorStart)
            return;
        auto start = *colorStart;
        auto id = colorId;
        colorStart.reset();
        colorId.clear();
        if (cancel)
            (void) Assign(id, start, ChangeKind::Live, ChangeOrigin::Cancel);
        else if (start != values.at(id) && config.workflow != Workflow::Staged)
        {
            Notify({id, start, values.at(id), ChangeKind::Confirmed, ChangeOrigin::User, ValueDomain::Effective});
            AutoSave();
        }
        Paint();
    }

    void SettingsPage::Impl::Paint()
    {
        if (!window || !canvas || !window->GetVisible() || painting || batch)
            return;
        painting = true;
        try
        {
            dpiInfo = window->GetDpiInfo();
            auto size = window->GetFramebufferSize();
            if (size.x <= 0 || size.y <= 0)
            {
                painting = false;
                return;
            }
            float s = Scales().render;
            width = size.x / s;
            height = size.y / s;
            RebuildRows();
            if (dropdown)
                if (auto* n = Find(selected))
                    dropdownFirst = std::min(dropdownFirst,
                                             n->options.size() - VisibleChoices(n->options.size(), height));
            UpdateHover();
            canvas->Begin(size.x, size.y);
            auto fill = [&](float x, float y, float w, float h, LLUtils::Color c)
            { canvas->Fill(x * s, y * s, w * s, h * s, c); };
            auto text = [&](std::string_view t, float x, float y, float w, float h, LLUtils::Color c, float font = 14,
                            bool wrap = false) { canvas->Text(t, x * s, y * s, w * s, h * s, c, font * s, wrap); };
            auto fg = config.appearance.foreground, accent = config.appearance.accent;
            auto muted = Color(0x929fb4ff), line = Color(0x303b4dff), field = Color(0x111720ff);
            fill(0, 0, width, height, config.appearance.background);
            text(config.title, 22, 16, width - 150, 32, fg, 23);
            text(values != saved ? "Unsaved" : "Saved", width - 105, 24, 90, 22, values != saved ? accent : muted, 12);
            fill(20, 60, width - 40, 32, searchFocus ? line : field);
            float searchWidth = canvas->Measure(filter, 14 * s) / s;
            float searchOffset = std::max(0.f, searchWidth - (width - 70));
            canvas->Clip(28 * s, 62 * s, (width - 57) * s, 28 * s);
            if (searchFocus && searchAll && !filter.empty())
                fill(30 - searchOffset, 65, searchWidth, 24, Color(0x426389ff));
            text(filter.empty() ? "Search settings..." : filter, 30 - searchOffset, 67,
                 std::max(width - 65, searchWidth), 23, filter.empty() ? muted : fg);
            if (searchFocus && caretVisible)
                fill(30 + searchWidth - searchOffset, 65, 1, 24, accent);
            canvas->Unclip();
            const float bottom = height - 190;
            canvas->Clip(0, 100 * s, width * s, std::max(0.f, bottom - 100) * s);
            float controlX = ControlLeft(), controlW = width - controlX - 85;
            for (const auto& row : rows)
            {
                const auto& n = *row.node;
                float y = 100 + row.y - scroll;
                if (y + row.height < 100 || y > bottom)
                    continue;
                if (n.id == selected)
                    fill(12, y, width - 24, row.height, Color(0x26394eff));
                else if (n.id == hovered)
                    fill(12, y, width - 24, row.height, Color(0x202b3aff));
                fill(22, y + row.height - 1, width - 44, 1, line);
                float labelX = LabelLeft(row.depth);
                if (n.control == Control::Group)
                {
                    text(collapsed.contains(n.id) && filter.empty() ? ">" : "v", labelX, y + 13, 18, 22, accent);
                    text(n.label, labelX + 22, y + 12, width - labelX - 40, 26, accent, 15);
                    continue;
                }
                const auto rowFg = ValueForeground(n);
                text(n.label, labelX, y + 13, controlX - labelX - 14, 24, rowFg);
                text("Reset", width - 68, y + 14, 48, 24, muted, 12);
                const auto& v = values.at(n.id);
                if (n.control == Control::Checkbox)
                {
                    fill(controlX, y + 12, 20, 20, std::get<bool>(v) ? accent : line);
                    if (std::get<bool>(v))
                        text("✓", controlX + 3, y + 11, 18, 22, field, 16);
                    text(std::get<bool>(v) ? "Enabled" : "Disabled", controlX + 30, y + 13, controlW - 30, 22, rowFg);
                }
                else if (n.control == Control::Radio)
                {
                    for (size_t i = 0; i < n.options.size(); ++i)
                    {
                        bool active = std::get<std::string>(v) == n.options[i].value;
                        text(active ? "●" : "○", controlX, y + 9 + i * 27, 22, 24, active ? accent : muted, 17);
                        text(n.options[i].label, controlX + 27, y + 11 + i * 27, controlW - 28, 24,
                             active ? rowFg : fg);
                    }
                }
                else
                {
                    bool editing = edit && edit->id == n.id;
                    fill(controlX, y + 7, controlW, 30, editing ? (edit->invalid ? Color(0x69303aff) : line) : field);
                    std::string value = editing ? edit->text : FormatValue(v);
                    if (n.control == Control::Combo)
                    {
                        for (const auto& option : n.options)
                            if (option.value == std::get<std::string>(v))
                                value = option.label;
                    }
                    const bool numeric = n.control == Control::Integer || n.control == Control::Number;
                    float extra = n.control == Control::File || n.control == Control::Color ? 42 : numeric ? 28 : 0;
                    if (editing)
                    {
                        size_t a = std::min(edit->caret, edit->anchor), b = std::max(edit->caret, edit->anchor);
                        float caret = canvas->Measure(std::string_view(value).substr(0, edit->caret), 14 * s) / s;
                        float offset = std::max(0.f, caret - (controlW - extra - 18));
                        canvas->Clip((controlX + 6) * s, (y + 8) * s, std::max(1.f, controlW - extra - 12) * s, 28 * s);
                        float left = canvas->Measure(std::string_view(value).substr(0, a), 14 * s) / s;
                        float right = canvas->Measure(std::string_view(value).substr(0, b), 14 * s) / s;
                        if (a != b)
                            fill(controlX + 7 + left - offset, y + 9, right - left, 25, Color(0x426389ff));
                        text(value, controlX + 7 - offset, y + 13, 100000, 23,
                             edit->invalid ? Color(0xff9ba7ff) : rowFg);
                        if (caretVisible)
                            fill(controlX + 7 + caret - offset, y + 10, 1, 23, accent);
                        canvas->Unclip();
                    }
                    else
                        text(value, controlX + 7, y + 13, controlW - extra - 20, 23, rowFg);
                    if (numeric)
                    {
                        const float rockerX = controlX + controlW - 25;
                        fill(rockerX, y + 8, 24, 28, line);
                        fill(rockerX, y + 21, 24, 1, field);
                        text("▲", rockerX + 7, y + 8, 16, 13, StepValue(n, v, 1) ? accent : muted, 10);
                        text("▼", rockerX + 7, y + 22, 16, 13, StepValue(n, v, -1) ? accent : muted, 10);
                    }
                    if (n.control == Control::Combo)
                        text("v", controlX + controlW - 17, y + 12, 15, 20, accent);
                    if (n.control == Control::File)
                    {
                        fill(controlX + controlW - 36, y + 8, 35, 28, line);
                        text("...", controlX + controlW - 29, y + 11, 25, 23, accent);
                    }
                    if (n.control == Control::Color)
                    {
                        fill(controlX + controlW - 35, y + 10, 29, 24, Color(0xffffffff));
                        fill(controlX + controlW - 34, y + 11, 27, 22, std::get<LLUtils::Color>(v));
                    }
                }
            }
            canvas->Unclip();
            const auto track = ScrollbarTrack(), thumb = ScrollbarThumb();
            if (track.height > 0)
            {
                fill(track.x, track.y, track.width, track.height, field);
                const bool hot = pointer && track.Contains(pointer->x / Scales().input, pointer->y / Scales().input);
                fill(thumb.x, thumb.y, thumb.width, thumb.height, scrollbarDragging ? accent : hot ? fg : muted);
            }
            fill(0, bottom, width, 1, line);
            auto* described = Find(hovered.empty() ? selected : hovered);
            text(described ? described->label : "Setting details", 22, bottom + 15, width - 44, 26, accent, 15);
            const auto description = (described ? breadcrumbs.at(described->id) + "\n\n" : std::string{}) +
                                     Description();
            descriptionHeight = canvas->TextHeight(description, (width - 44) * s, 14 * s) / s;
            descriptionScroll = std::clamp(descriptionScroll, 0.f, std::max(0.f, descriptionHeight - 76));
            canvas->Clip(22 * s, (bottom + 44) * s, (width - 44) * s, 76 * s);
            text(description, 22, bottom + 44 - descriptionScroll, width - 44, std::max(76.f, descriptionHeight), muted,
                 14, true);
            canvas->Unclip();
            if (descriptionHeight > 76)
                text("Scroll for more", width - 130, bottom + 124, 110, 18, muted, 10);
            text(status, 22, height - 68, width - 44, 24, error ? Color(0xff9ba7ff) : muted, 12);
            fill(0, height - 44, width, 44, field);
            text("Reset all", 22, height - 30, 100, 24, muted);
            if (config.workflow == Workflow::Staged)
            {
                text("Cancel", width - 270, height - 30, 70, 24, muted);
                text("Apply", width - 185, height - 30, 70, 24, accent);
            }
            text(config.workflow == Workflow::ImmediateAutoSave ? "Save now" : "Save", width - 95, height - 30, 80, 24,
                 accent);
            if (dropdown)
            {
                for (const auto& row : rows)
                    if (row.node->id == selected)
                    {
                        const auto count = VisibleChoices(row.node->options.size(), height);
                        dropdownFirst = std::min(dropdownFirst, row.node->options.size() - count);
                        float y = DropdownBox().y;
                        fill(controlX, y, controlW, float(count) * 28, line);
                        for (size_t i = 0; i < count; ++i)
                        {
                            const auto& option = row.node->options[dropdownFirst + i];
                            bool active = option.value == std::get<std::string>(values.at(selected));
                            bool hot = dropdownHovered == dropdownFirst + i;
                            if (active || hot)
                                fill(controlX, y + i * 28, controlW, 28, Color(hot ? 0x426389ff : 0x2c4058ff));
                            if (active)
                                fill(controlX, y + i * 28, 3, 28, accent);
                            text(option.label, controlX + 8, y + i * 28 + 5, controlW - 16, 24, fg);
                        }
                    }
            }
            if (colorStart)
            {
                SyncColorHsl();
                const auto panel = ColorPanel();
                float x = panel.x, y = panel.y;
                fill(0, 0, width, height, Color(0x000000a0));
                fill(x, y, panel.width, panel.height, Color(0x263344ff));
                text("Color · " + FormatValue(values.at(colorId)), x + 20, y + 16, 400, 28, fg, 17);
                auto c = std::get<LLUtils::Color>(values.at(colorId));
                auto preview = [&](float left, LLUtils::Color color)
                {
                    for (int row = 0; row < 6; ++row)
                        for (int column = 0; column < 24; ++column)
                            fill(left + column * 8, y + 52 + row * 8, 8, 8,
                                 Color((row + column) % 2 ? 0x626873ff : 0xb9bdc5ff));
                    fill(left, y + 52, 192, 48, color);
                };
                preview(x + 20, std::get<LLUtils::Color>(*colorStart));
                preview(x + 228, c);
                text("Original", x + 20, y + 107, 160, 24, muted);
                text("Current", x + 228, y + 107, 192, 24, muted);
                text("RGB / Alpha", x + 20, y + 129, 192, 18, muted, 12);
                text("HSL", x + 230, y + 129, 192, 18, muted, 12);
                const char* labels[] = {"R", "G", "B", "A", "H", "S", "L"};
                const char* names[] = {"Red · 0–255",  "Green · 0–255",       "Blue · 0–255",      "Alpha · 0–255",
                                       "Hue · 0–360°", "Saturation · 0–100%", "Lightness · 0–100%"};
                for (int i = 0; i < 7; ++i)
                {
                    const auto slider = ColorSlider(i);
                    const double value = ColorChannelValue(i);
                    text(labels[i], slider.x - 28, slider.y, 24, 25, i == colorChannel ? accent : muted);
                    fill(slider.x, slider.y, slider.width, slider.height, field);
                    fill(slider.x, slider.y, float(slider.width * value / ColorChannelMaximum(i)), slider.height,
                         accent);
                    auto display = FormatValue(Value{std::round(value * 10) / 10});
                    if (i == 4)
                        display += "°";
                    else if (i > 4)
                        display += "%";
                    text(display, slider.x + slider.width + 8, slider.y + 3, 44, 22, fg, 12);
                }
                text(names[colorChannel], x + 20, y + 301, 400, 24, muted, 12);
                text("Cancel", x + 260, y + 337, 70, 28, muted);
                text("Accept", x + 350, y + 337, 70, 28, accent);
            }
            auto bitmap = canvas->End();
            const auto result = window->PresentBitmap(bitmap);
            if (result == LWS::Result::Success)
                presented = true;
            else if (result != LWS::Result::InvalidState)
                (void) Fail("render", "Bitmap presentation failed");
        }
        catch (const std::exception& e)
        {
            error = Error{"render", e.what()};
        }
        painting = false;
    }

    void SettingsPage::Impl::Click(float x, float y)
    {
        if (colorStart)
        {
            const auto panel = ColorPanel();
            float left = panel.x, top = panel.y;
            if (UiBox{left + 340, top + 328, 80, 36}.Contains(x, y))
            {
                CloseColor(false);
                return;
            }
            if (UiBox{left + 250, top + 328, 80, 36}.Contains(x, y))
            {
                CloseColor(true);
                return;
            }
            for (int i = 0; i < 7; ++i)
                if (ColorSlider(i).Contains(x, y))
                {
                    colorChannel = i;
                    colorDragging = true;
                    DragColorChannel(x);
                    break;
                }
            Paint();
            return;
        }
        float controlX = ControlLeft(), controlW = width - controlX - 85;
        if (dropdown)
        {
            for (const auto& row : rows)
                if (row.node->id == selected)
                {
                    const auto count = VisibleChoices(row.node->options.size(), height);
                    float top = DropdownBox().y;
                    int i = int((y - top) / 28);
                    if (x >= controlX && x <= controlX + controlW && y >= top && i >= 0 && i < int(count))
                        (void) Assign(selected, row.node->options[dropdownFirst + size_t(i)].value,
                                      ChangeKind::Confirmed, ChangeOrigin::User);
                    break;
                }
            dropdown = false;
            Paint();
            return;
        }
        const auto track = ScrollbarTrack();
        if (track.Contains(x, y))
        {
            StopSpin();
            textSelecting = false;
            colorDragging = false;
            const auto thumb = ScrollbarThumb();
            if (thumb.Contains(x, y))
            {
                scrollbarGrab = std::clamp((y - thumb.y) / thumb.height, 0.f, 1.f);
                scrollbarDragging = true;
                if (window)
                    (void) window->SetMouseCapture(true);
            }
            else
            {
                scroll = std::clamp(scroll + (y < thumb.y ? -track.height : track.height), 0.f,
                                    contentHeight - track.height);
            }
            hovered.clear();
            Paint();
            return;
        }
        if (y >= height - 44)
        {
            if (x < 140)
                ResetAll();
            else if (x >= width - 105)
                (void) Save();
            else if (config.workflow == Workflow::Staged && x >= width - 200)
                (void) Apply();
            else if (config.workflow == Workflow::Staged && x >= width - 280)
                Cancel();
            Paint();
            return;
        }
        if (y >= 60 && y <= 94)
        {
            if (FinishEdit())
            {
                searchFocus = true;
                selected.clear();
            }
            Paint();
            return;
        }
        if (y < 100 || y >= height - 190)
            return;
        for (const auto row : rows)
            if (y >= 100 + row.y - scroll && y < 100 + row.y - scroll + row.height)
            {
                const auto& n = *row.node;
                Select(n.id, false);
                if (selected != n.id)
                    return;
                if (n.control == Control::Group)
                {
                    if (collapsed.contains(n.id))
                        collapsed.erase(n.id);
                    else
                        collapsed.insert(n.id);
                    RebuildRows();
                    Paint();
                    return;
                }
                if (x >= width - 75)
                {
                    ResetNode(n.id);
                    return;
                }
                if (x < controlX)
                {
                    Paint();
                    return;
                }
                if ((n.control == Control::Integer || n.control == Control::Number) && x >= controlX + controlW - 25 &&
                    x < controlX + controlW)
                {
                    StartSpin(n, y - (100 + row.y - scroll) < 22 ? 1 : -1, false);
                    return;
                }
                if (n.control == Control::Checkbox)
                    (void) Assign(n.id, !std::get<bool>(values.at(n.id)), ChangeKind::Confirmed, ChangeOrigin::User);
                else if (n.control == Control::Radio)
                {
                    int i = int((y - (100 + row.y - scroll) - 6) / 27);
                    if (i >= 0 && i < int(n.options.size()))
                        (void) Assign(n.id, n.options[size_t(i)].value, ChangeKind::Confirmed, ChangeOrigin::User);
                }
                else if (n.control == Control::Combo)
                {
                    dropdownFirst = 0;
                    dropdown = true;
                }
                else if (n.control == Control::File && x >= controlX + controlW - 40)
                    Browse(n);
                else if (n.control == Control::Color && x >= controlX + controlW - 40)
                    OpenColor(n);
                else if (Editable(n.control))
                {
                    BeginEdit(n);
                    if (edit && edit->id == n.id && canvas)
                    {
                        PlaceCaret(x, ShiftPressed());
                        textSelecting = true;
                    }
                }
                Paint();
                return;
            }
    }

    void SettingsPage::Impl::PlaceCaret(float x, bool extend)
    {
        if (!edit || !canvas)
            return;
        float s = Scales().render;
        float left = ControlLeft(), available = width - left - 85;
        auto* node = Find(edit->id);
        if (node->control == Control::File || node->control == Control::Color)
            available -= 42;
        else if (node->control == Control::Integer || node->control == Control::Number)
            available -= 28;
        float caret = canvas->Measure(std::string_view(edit->text).substr(0, edit->caret), 14 * s) / s;
        float offset = std::max(0.f, caret - (available - 18));
        float target = x - left - 7 + offset;
        size_t at = 0;
        while (at < edit->text.size())
        {
            size_t next = NextCharacter(edit->text, at);
            float a = canvas->Measure(std::string_view(edit->text).substr(0, at), 14 * s) / s;
            float b = canvas->Measure(std::string_view(edit->text).substr(0, next), 14 * s) / s;
            if ((a + b) / 2 > target)
                break;
            at = next;
        }
        edit->caret = at;
        if (!extend)
            edit->anchor = at;
        caretVisible = true;
    }

    void SettingsPage::Impl::Key(LWS::KeyCode key)
    {
        using K = LWS::KeyCode;
        bool control = ControlPressed(), shift = ShiftPressed();
        if (scrollbarDragging)
        {
            EndScrollbarDrag();
            if (key == K::Escape)
            {
                Paint();
                return;
            }
        }
        if (colorStart)
        {
            if (key == K::Escape)
                CloseColor(true);
            else if (key == K::Enter)
                CloseColor(false);
            else if (key == K::Up)
                colorChannel = (colorChannel + 6) % 7;
            else if (key == K::Down || key == K::Tab)
                colorChannel = (colorChannel + (key == K::Tab && shift ? 6 : 1)) % 7;
            else if (key == K::Left || key == K::Right)
            {
                SyncColorHsl();
                int delta = (key == K::Left ? -1 : 1) * (shift ? 10 : 1);
                SetColorChannel(colorChannel, ColorChannelValue(colorChannel) + delta);
            }
            Paint();
            return;
        }
        if (control && key == K::S)
        {
            (void) Save();
            Paint();
            return;
        }
        if (control && key == K::Enter && config.workflow == Workflow::Staged)
        {
            (void) Apply();
            Paint();
            return;
        }
        if (control && key == K::R)
        {
            if (shift)
                (void) ResetAll();
            else
                (void) ResetNode(selected);
            Paint();
            return;
        }
        if (control && key == K::F)
        {
            if (FinishEdit())
                searchFocus = true;
            Paint();
            return;
        }
        if (searchFocus)
        {
            if (key == K::Escape)
            {
                SetFilter("");
                searchFocus = false;
            }
            else if (control && key == K::A)
                searchAll = true;
            else if (control && key == K::V)
            {
                std::weak_ptr<int> alive = lifetime;
                LWS::RequestClipboardText(
                    *window,
                    [this, alive](auto result, std::string text)
                    {
                        if (!alive.expired() && searchFocus && result == LWS::ClipboardResult::Success)
                        {
                            SetFilter((searchAll ? std::string{} : filter) + SingleLine(std::move(text)));
                            searchAll = false;
                        }
                    });
            }
            else if (key == K::Backspace && !filter.empty())
            {
                SetFilter(searchAll ? std::string{} : filter.substr(0, PreviousCharacter(filter, filter.size())));
                searchAll = false;
            }
            else if (key == K::Down || key == K::Tab)
            {
                searchFocus = false;
                if (!rows.empty())
                    Select(rows.front().node->id);
            }
            Paint();
            return;
        }
        if (key == K::F4)
        {
            if (auto* node = Find(edit ? edit->id : selected); node && node->control == Control::File)
            {
                Browse(*node);
                return;
            }
        }
        if (edit && (key == K::Up || key == K::Down))
        {
            if (auto* node = Find(edit->id);
                node && (node->control == Control::Integer || node->control == Control::Number))
            {
                StartSpin(*node, key == K::Up ? 1 : -1, true);
                return;
            }
        }
        if (edit)
        {
            if (key == K::Escape)
            {
                FinishEdit(true);
                Paint();
                return;
            }
            if (key == K::Enter || key == K::Tab)
            {
                if (!FinishEdit())
                {
                    Paint();
                    return;
                }
                if (key == K::Enter)
                {
                    Paint();
                    return;
                }
            }
            else
            {
                if (control && key == K::A)
                {
                    edit->anchor = 0;
                    edit->caret = edit->text.size();
                }
                else if (control && (key == K::C || key == K::X))
                {
                    size_t a = std::min(edit->caret, edit->anchor), b = std::max(edit->caret, edit->anchor);
                    if (a != b)
                    {
                        auto result = LWS::SetClipboardText(*window, std::string_view(edit->text).substr(a, b - a));
                        if (result == LWS::ClipboardResult::Success && key == K::X)
                            ReplaceSelection("");
                    }
                }
                else if (control && key == K::V)
                {
                    std::weak_ptr<int> alive = lifetime;
                    auto id = edit->id;
                    LWS::RequestClipboardText(*window,
                                              [this, alive, id](auto result, std::string text)
                                              {
                                                  if (!alive.expired() && edit && edit->id == id &&
                                                      result == LWS::ClipboardResult::Success)
                                                      ReplaceSelection(std::move(text));
                                              });
                }
                else if (key == K::Left || key == K::Right || key == K::Home || key == K::End)
                {
                    if (key == K::Left)
                        edit->caret = PreviousCharacter(edit->text, edit->caret);
                    if (key == K::Right)
                        edit->caret = NextCharacter(edit->text, edit->caret);
                    if (key == K::Home)
                        edit->caret = 0;
                    if (key == K::End)
                        edit->caret = edit->text.size();
                    if (!shift)
                        edit->anchor = edit->caret;
                }
                else if (key == K::Backspace || key == K::Delete)
                {
                    if (edit->caret == edit->anchor)
                    {
                        if (key == K::Backspace)
                            edit->anchor = PreviousCharacter(edit->text, edit->caret);
                        else
                            edit->anchor = NextCharacter(edit->text, edit->caret);
                    }
                    ReplaceSelection("");
                }
                Paint();
                return;
            }
        }
        auto* node = Find(selected);
        if (key == K::Escape)
        {
            dropdown = false;
            Paint();
            return;
        }
        if (node && node->control == Control::Group &&
            (key == K::Left || key == K::Right || key == K::Enter || key == K::Space))
        {
            if (key == K::Left)
                collapsed.insert(node->id);
            else if (key == K::Right)
                collapsed.erase(node->id);
            else if (collapsed.contains(node->id))
                collapsed.erase(node->id);
            else
                collapsed.insert(node->id);
            RebuildRows();
            Paint();
            return;
        }
        if (node && node->control == Control::File && key == K::F4)
        {
            Browse(*node);
            return;
        }
        if (node && (key == K::Enter || key == K::Space))
        {
            if (node->control == Control::Checkbox)
                (void) Assign(node->id, !std::get<bool>(values.at(node->id)), ChangeKind::Confirmed,
                              ChangeOrigin::User);
            else if (node->control == Control::Color)
                OpenColor(*node);
            else if (Editable(node->control))
                BeginEdit(*node);
            else if (node->control == Control::Combo)
            {
                dropdownFirst = 0;
                dropdown = !dropdown;
            }
            Paint();
            return;
        }
        if (node && (node->control == Control::Radio || node->control == Control::Combo) &&
            (key == K::Left || key == K::Right || (dropdown && (key == K::Up || key == K::Down))))
        {
            size_t index = 0;
            for (size_t i = 0; i < node->options.size(); ++i)
                if (node->options[i].value == std::get<std::string>(values.at(node->id)))
                    index = i;
            if (key == K::Left || key == K::Up)
                index = (index + node->options.size() - 1) % node->options.size();
            else
                index = (index + 1) % node->options.size();
            if (dropdown)
            {
                auto count = VisibleChoices(node->options.size(), height);
                if (index < dropdownFirst)
                    dropdownFirst = index;
                if (index >= dropdownFirst + count)
                    dropdownFirst = index - count + 1;
            }
            (void) Assign(node->id, node->options[index].value, ChangeKind::Confirmed, ChangeOrigin::User);
            Paint();
            return;
        }
        if (key == K::Up || key == K::Down || key == K::Tab || key == K::Home || key == K::End)
        {
            if (rows.empty())
                return;
            auto it = std::find_if(rows.begin(), rows.end(), [&](const auto& r) { return r.node->id == selected; });
            int index = it == rows.end() ? -1 : int(it - rows.begin());
            if (key == K::Home)
                index = 0;
            else if (key == K::End)
                index = int(rows.size()) - 1;
            else
                index = std::clamp(index + ((key == K::Up || (key == K::Tab && shift)) ? -1 : 1), 0,
                                   int(rows.size()) - 1);
            Select(rows[size_t(index)].node->id);
            if (key == K::Tab && Editable(rows[size_t(index)].node->control))
                BeginEdit(*rows[size_t(index)].node);
        }
        Paint();
    }

    bool SettingsPage::Impl::Event(const LWS::AnyEvent& event)
    {
        if (std::holds_alternative<LWS::EventMouseCaptureLost>(event))
        {
            EndScrollbarDrag();
            Paint();
            return true;
        }
        if (std::holds_alternative<LWS::EventWindowDestroyed>(event))
        {
            EndScrollbarDrag();
            return false;
        }
        if (auto* dpi = std::get_if<LWS::EventDpiChanged>(&event))
        {
            dpiInfo = dpi->dpi;
            if (window && pointer)
                pointer = window->GetMousePosition();
            if (window)
                window->SetMinMaxSize({int(480 * Scales().input), int(440 * Scales().input)}, {0, 0});
            Paint();
            if (spin && !spin->keyboard && !PointerOverSpin())
                StopSpin();
            return true;
        }
        if (std::holds_alternative<LWS::EventPaint>(event) || std::holds_alternative<LWS::EventResize>(event))
        {
            Paint();
            return true;
        }
        if (modal)
            return true;
        if (auto* e = std::get_if<LWS::EventMouseMove>(&event))
        {
            pointer = e->position;
            if (scrollbarDragging)
            {
                DragScrollbar(e->position.y / Scales().input);
                return true;
            }
            if (spin && !spin->keyboard)
            {
                bool inside = false;
                for (const auto& row : rows)
                    if (row.node->id == spin->id)
                    {
                        const UiBox button{width - 110, 100 + row.y - scroll + (spin->direction > 0 ? 7.f : 22.f), 25,
                                           15};
                        inside = button.Contains(pointer->x / Scales().input, pointer->y / Scales().input);
                        break;
                    }
                if (!inside)
                    StopSpin();
            }
            if (textSelecting && edit)
                PlaceCaret(e->position.x / Scales().input, true);
            if (colorStart && colorDragging)
                DragColorChannel(e->position.x / Scales().input);
            UpdateHover();
            Paint();
            return true;
        }
        if (std::holds_alternative<LWS::EventMouseLeave>(event))
        {
            pointer.reset();
            dropdownHovered.reset();
            if (spin && !spin->keyboard)
                StopSpin();
            hovered.clear();
            descriptionScroll = 0;
            textSelecting = false;
            colorDragging = false;
            Paint();
            return true;
        }
        if (auto* e = std::get_if<LWS::EventMouseButton>(&event))
        {
            pointer = e->position;
            if (e->button == LWS::MouseButton::Left)
            {
                if (e->pressed)
                    Click(e->position.x / Scales().input, e->position.y / Scales().input);
                else
                {
                    EndScrollbarDrag();
                    textSelecting = false;
                    colorDragging = false;
                    if (spin && !spin->keyboard)
                        StopSpin(false, true);
                }
            }
            return true;
        }
        if (auto* e = std::get_if<LWS::EventMouseWheel>(&event))
        {
            EndScrollbarDrag();
            pointer = e->position;
            if (!colorStart)
            {
                if (dropdown)
                {
                    if (auto* node = Find(selected))
                        dropdownFirst = size_t(
                            std::clamp(int(dropdownFirst) - int(e->steps()), 0,
                                       int(node->options.size() - VisibleChoices(node->options.size(), height))));
                    Paint();
                    return true;
                }
                if (e->position.y / Scales().input >= height - 190)
                    descriptionScroll -= float(e->steps()) * 28;
                else
                {
                    scroll -= float(e->steps()) * 44;
                    hovered.clear();
                    dropdown = false;
                    RebuildRows();
                }
                Paint();
            }
            return true;
        }
        if (auto* e = std::get_if<LWS::EventTextInput>(&event))
        {
            if (ControlPressed() && !LWS::Platform::isKeyPressed(LWS::KeyCode::RAlt))
                return true;
            if (searchFocus)
            {
                SetFilter((searchAll ? std::string{} : filter) + SingleLine(e->text));
                searchAll = false;
            }
            else if (edit)
                ReplaceSelection(e->text);
            return true;
        }
        if (auto* e = std::get_if<LWS::EventKeyUp>(&event))
        {
            if (spin && spin->keyboard &&
                ((spin->direction > 0 && e->key == LWS::KeyCode::Up) ||
                 (spin->direction < 0 && e->key == LWS::KeyCode::Down)))
                StopSpin(false, true);
            return true;
        }
        if (auto* e = std::get_if<LWS::EventKeyDown>(&event))
        {
            Key(e->key);
            return true;
        }
        if (std::holds_alternative<LWS::EventFocusLost>(event))
        {
            EndScrollbarDrag();
            StopSpin();
            textSelecting = false;
            colorDragging = false;
            if (!colorStart)
                FinishEdit();
            Paint();
            return true;
        }
        if (std::holds_alternative<LWS::EventClose>(event))
        {
            EndScrollbarDrag();
            if (FinishEdit())
            {
                if (colorStart)
                    CloseColor(true);
                window->SetVisible(false);
            }
            return true;
        }
        return false;
    }
}  // namespace LWSSettings
