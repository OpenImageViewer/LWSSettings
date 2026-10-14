#include "ThemeFields.hpp"
#include <cmath>
#include <cctype>
namespace LWSSettings
{
    namespace
    {
        using UI = LWSUI::Theme;
        const std::pair<const char*, LLUtils::Color UI::*> colors[] = {
            {"background", &LWSUI::Theme::background},
            {"foreground", &LWSUI::Theme::foreground},
            {"surface", &LWSUI::Theme::surface},
            {"accent", &LWSUI::Theme::accent},
            {"changed", &LWSUI::Theme::changed},
            {"error", &LWSUI::Theme::error},
            {"hoverSurface", &LWSUI::Theme::hoverSurface},
            {"closeHoverBackground", &LWSUI::Theme::closeHoverBackground},
            {"closePressedBackground", &LWSUI::Theme::closePressedBackground},
            {"closeActiveForeground", &LWSUI::Theme::closeActiveForeground},
            {"line", &LWSUI::Theme::line},
            {"muted", &LWSUI::Theme::muted},
            {"selectedRow", &LWSUI::Theme::selectedRow},
            {"hoveredRow", &LWSUI::Theme::hoveredRow},
            {"selection", &LWSUI::Theme::selection},
            {"selectedOption", &LWSUI::Theme::selectedOption},
            {"popupSurface", &LWSUI::Theme::popupSurface},
            {"checkerDark", &LWSUI::Theme::checkerDark},
            {"checkerLight", &LWSUI::Theme::checkerLight},
            {"modalOverlay", &LWSUI::Theme::modalOverlay},
            {"searchForeground", &LWSUI::Theme::searchForeground},
            {"searchBackground", &LWSUI::Theme::searchBackground},
            {"selectionForeground", &LWSUI::Theme::selectionForeground},
        };
        const std::pair<const char*, float UI::*> metrics[] = {
            {"spacing", &LWSUI::Theme::spacing},
            {"rowHeight", &LWSUI::Theme::rowHeight},
            {"padding", &LWSUI::Theme::padding},
            {"labelPadding", &LWSUI::Theme::labelPadding},
            {"textPadding", &LWSUI::Theme::textPadding},
            {"borderWidth", &LWSUI::Theme::borderWidth},
            {"focusWidth", &LWSUI::Theme::focusWidth},
            {"checkboxSize", &LWSUI::Theme::checkboxSize},
            {"choiceGap", &LWSUI::Theme::choiceGap},
            {"radioHeight", &LWSUI::Theme::radioHeight},
            {"rockerWidth", &LWSUI::Theme::rockerWidth},
            {"browseWidth", &LWSUI::Theme::browseWidth},
            {"swatchWidth", &LWSUI::Theme::swatchWidth},
            {"swatchGap", &LWSUI::Theme::swatchGap},
            {"checkerSize", &LWSUI::Theme::checkerSize},
            {"sliderThickness", &LWSUI::Theme::sliderThickness},
            {"sliderThumbWidth", &LWSUI::Theme::sliderThumbWidth},
            {"scrollbarWidth", &LWSUI::Theme::scrollbarWidth},
            {"scrollbarMargin", &LWSUI::Theme::scrollbarMargin},
            {"scrollbarInset", &LWSUI::Theme::scrollbarInset},
            {"scrollbarMinThumb", &LWSUI::Theme::scrollbarMinThumb},
            {"scrollStep", &LWSUI::Theme::scrollStep},
            {"treeRowHeight", &LWSUI::Theme::treeRowHeight},
            {"treeIndent", &LWSUI::Theme::treeIndent},
            {"treeLabelInset", &LWSUI::Theme::treeLabelInset},
            {"treeRowInset", &LWSUI::Theme::treeRowInset},
            {"treeLineInset", &LWSUI::Theme::treeLineInset},
            {"treeRowPadding", &LWSUI::Theme::treeRowPadding},
            {"editorMinLeft", &LWSUI::Theme::editorMinLeft},
            {"editorFraction", &LWSUI::Theme::editorFraction},
            {"resetWidth", &LWSUI::Theme::resetWidth},
            {"resetRightMargin", &LWSUI::Theme::resetRightMargin},
            {"menuRowHeight", &LWSUI::Theme::menuRowHeight},
            {"menuMaxRows", &LWSUI::Theme::menuMaxRows},
            {"menuPadding", &LWSUI::Theme::menuPadding},
            {"menuBarHeight", &LWSUI::Theme::menuBarHeight},
            {"menuBarPadding", &LWSUI::Theme::menuBarPadding},
            {"menuArrowWidth", &LWSUI::Theme::menuArrowWidth},
            {"popupWidth", &LWSUI::Theme::popupWidth},
            {"popupHeight", &LWSUI::Theme::popupHeight},
            {"popupPadding", &LWSUI::Theme::popupPadding},
            {"popupGap", &LWSUI::Theme::popupGap},
            {"popupPreviewHeight", &LWSUI::Theme::popupPreviewHeight},
            {"popupChannelHeight", &LWSUI::Theme::popupChannelHeight},
            {"popupButtonWidth", &LWSUI::Theme::popupButtonWidth},
        };
        const std::pair<const char*, LWSUI::FontSpec UI::*> fonts[] = {
            {"font", &LWSUI::Theme::font},
            {"smallFont", &LWSUI::Theme::smallFont},
            {"headingFont", &LWSUI::Theme::headingFont},
            {"titleFont", &LWSUI::Theme::titleFont},
            {"glyphFont", &LWSUI::Theme::glyphFont},
        };
        const std::pair<const char*, float SettingsLayout::*> layoutFields[] = {
            {"horizontalPadding", &SettingsLayout::horizontalPadding},
            {"sectionGap", &SettingsLayout::sectionGap},
            {"headerHeight", &SettingsLayout::headerHeight},
            {"searchHeight", &SettingsLayout::searchHeight},
            {"descriptionHeight", &SettingsLayout::descriptionHeight},
            {"footerHeight", &SettingsLayout::footerHeight},
            {"statusHeight", &SettingsLayout::statusHeight},
            {"actionWidth", &SettingsLayout::actionWidth},
            {"actionGap", &SettingsLayout::actionGap},
            {"descriptionTitleGap", &SettingsLayout::descriptionTitleGap},
            {"descriptionMaxFraction", &SettingsLayout::descriptionMaxFraction},
        };
        const std::pair<const char*, UI SettingsTheme::*> sections[] = {
            {"header", &SettingsTheme::header}, {"search", &SettingsTheme::search},
            {"tree", &SettingsTheme::tree},     {"description", &SettingsTheme::description},
            {"footer", &SettingsTheme::footer}, {"controls", &SettingsTheme::controls},
            {"popups", &SettingsTheme::popups}};

        std::string Label(std::string_view name)
        {
            std::string result;
            for (size_t i = 0; i < name.size(); ++i)
            {
                if (i && name[i] >= 'A' && name[i] <= 'Z')
                    result += ' ';
                result += i == 0 ? char(std::toupper(static_cast<unsigned char>(name[i]))) : name[i];
            }
            return result;
        }
        void Add(std::vector<ThemeField>& fields, std::string path, std::function<Value(const SettingsTheme&)> read,
                 std::function<void(SettingsTheme&, const Value&)> assign, double minimum = 0, double maximum = 10000,
                 double step = 1)
        {
            ThemeField field;
            field.path = std::move(path);
            field.read = std::move(read);
            field.assign = std::move(assign);
            auto& d = field.definition;
            d.id = field.path;
            d.visibleInEditor = d.id != "layout/searchHeight" && d.id != "layout/statusHeight";
            d.label = Label(field.path.substr(field.path.rfind('/') + 1));
            d.description = "Theme property: " + field.path +
                            ". Editing creates an override; Use inherited restores the base or default value.";
            if (field.path.ends_with("/resetRightMargin"))
                d.description = "Space to the right of Reset in logical pixels. Increasing it moves the whole button left without resizing it. Column padding is separate.";
            else if (field.path.ends_with("/resetWidth"))
                d.description = "Reset button width in logical pixels, expanded when needed to fit its caption. Does not include the right margin or column padding.";
            const auto value = field.read(SettingsTheme{});
            std::visit(
                [&](const auto& v)
                {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, bool>)
                        d.specification = BooleanDefinition{v};
                    else if constexpr (std::is_same_v<T, std::string>)
                        d.specification = TextDefinition{v};
                    else if constexpr (std::is_same_v<T, LLUtils::Color>)
                        d.specification = ColorDefinition{v};
                    else if constexpr (std::is_same_v<T, int64_t>)
                        d.specification = IntegerDefinition{v, int64_t(minimum), int64_t(maximum), int64_t(step)};
                    else
                        d.specification = DecimalDefinition{v, minimum, maximum, step};
                },
                value);
            fields.push_back(std::move(field));
        }
        std::vector<ThemeField> Build()
        {
            std::vector<ThemeField> fields;
            const double positive = std::nextafter(0.f, 1.f);
            auto addUI = [&](std::string prefix, UI SettingsTheme::* scope)
            {
                for (auto [name, member] : colors)
                    Add(
                        fields, prefix + "/colors/" + name,
                        [=](const SettingsTheme& t) -> Value { return (t.*scope).*member; },
                        [=](SettingsTheme& t, const Value& v) { (t.*scope).*member = std::get<LLUtils::Color>(v); });
                for (auto [name, member] : metrics)
                {
                    std::string_view n = name;
                    bool zero = n == "spacing" || n.find("Padding") != n.npos || n == "padding" ||
                                n.find("Gap") != n.npos || n.find("Inset") != n.npos || n == "scrollbarMargin" || n == "resetRightMargin";
                    Add(
                        fields, prefix + "/metrics/" + name,
                        [=](const SettingsTheme& t) -> Value { return double((t.*scope).*member); },
                        [=](SettingsTheme& t, const Value& v) { (t.*scope).*member = float(std::get<double>(v)); },
                        n == "checkerSize" ? 1 : zero ? 0 : positive, n == "editorFraction" ? double(std::nextafter(1.f, 0.f)) : 10000,
                        n == "editorFraction" ? .01 : 1);
                }
                for (auto [name, member] : fonts)
                {
                    std::string path = prefix + "/typography/" + name;
                    Add(
                        fields, path + "/family", [=](const SettingsTheme& t) -> Value
                        { return ((t.*scope).*member).family; }, [=](SettingsTheme& t, const Value& v)
                        { ((t.*scope).*member).family = std::get<std::string>(v); });
                    Add(
                        fields, path + "/size", [=](const SettingsTheme& t) -> Value
                        { return double(((t.*scope).*member).size); }, [=](SettingsTheme& t, const Value& v)
                        { ((t.*scope).*member).size = float(std::get<double>(v)); }, positive);
                    Add(
                        fields, path + "/weight", [=](const SettingsTheme& t) -> Value
                        { return int64_t(((t.*scope).*member).weight); }, [=](SettingsTheme& t, const Value& v)
                        { ((t.*scope).*member).weight = int(std::get<int64_t>(v)); }, 1, 999, 100);
                    Add(
                        fields, path + "/italic",
                        [=](const SettingsTheme& t) -> Value { return ((t.*scope).*member).italic; },
                        [=](SettingsTheme& t, const Value& v) { ((t.*scope).*member).italic = std::get<bool>(v); });
                }
                for (auto [name, member] : std::initializer_list<std::pair<const char*, int UI::*>>{
                         {"repeatDelayMs", &UI::repeatDelayMs},
                         {"repeatIntervalMs", &UI::repeatIntervalMs},
                         {"accelerationMs", &UI::accelerationMs},
                         {"menuHoverDelayMs", &UI::menuHoverDelayMs}})
                    Add(
                        fields, prefix + "/interaction/" + name,
                        [=](const SettingsTheme& t) -> Value { return int64_t((t.*scope).*member); },
                        [=](SettingsTheme& t, const Value& v) { (t.*scope).*member = int(std::get<int64_t>(v)); },
                        std::string_view(name) == "menuHoverDelayMs" ? 0 : 1, 60000, 1);
            };
            addUI("base", &SettingsTheme::base);
            for (auto [name, scope] : sections)
                addUI(std::string("sections/") + name, scope);
            for (auto [name, member] : layoutFields)
                Add(
                    fields, std::string("layout/") + name,
                    [=](const SettingsTheme& t) -> Value { return double(t.layout.*member); },
                    [=](SettingsTheme& t, const Value& v) { t.layout.*member = float(std::get<double>(v)); },
                    std::string_view(name) == "descriptionMaxFraction" ? positive : 0,
                    std::string_view(name) == "descriptionMaxFraction" ? 1 : 10000,
                    std::string_view(name) == "descriptionMaxFraction" ? .01 : 1);
            return fields;
        }
    }  // namespace
    const std::vector<ThemeField>& ThemeFields::All()
    {
        static const auto fields = Build();
        return fields;
    }
    const ThemeField* ThemeFields::Find(std::string_view path)
    {
        static const auto index = []
        {
            std::map<std::string, const ThemeField*, std::less<>> result;
            for (const auto& field : All())
                result.emplace(field.path, &field);
            return result;
        }();
        auto it = index.find(path);
        return it == index.end() ? nullptr : it->second;
    }
    std::expected<SettingsTheme, Error> ThemeFields::Resolve(const ThemeOverrides& overrides, LWSUI::ThemePreset preset)
    {
        if (preset != LWSUI::ThemePreset::Dark && preset != LWSUI::ThemePreset::Light && preset != LWSUI::ThemePreset::Warm)
            return std::unexpected(Error{"preset", "Unknown theme preset"});
        SettingsTheme theme(preset);
        // Base overrides precede section overrides. The candidate stays local until
        // every value is valid, so no second owning map is needed for atomicity.
        for (bool base : {true, false})
            for (const auto& [path, value] : overrides)
            {
                if (path.starts_with("base/") != base) continue;
                const auto* field = Find(path);
                if (!field) return std::unexpected(Error{path, "Unknown theme field"});
                auto valid = LWSSettings::Validate(field->definition, value);
                if (!valid) return std::unexpected(valid.error());
                field->assign(theme, *valid);
                if (base)
                    for (auto [name, scope] : sections)
                        Find(std::string("sections/") + name + path.substr(4))->assign(theme, *valid);
            }
        return theme;
    }
    Status ThemeFields::Validate(const SettingsTheme& theme)
    {
        for (const auto& field : All())
        {
            auto valid = LWSSettings::Validate(field.definition, field.read(theme));
            if (!valid)
                return std::unexpected(valid.error());
        }
        return {};
    }
    std::vector<Definition> ThemeFields::CreateDefinitions()
    {
        std::vector<Definition> roots;
        for (const auto& field : All())
        {
            auto* children = &roots;
            std::string prefix;
            size_t begin = 0;
            while (begin < field.path.size())
            {
                auto end = field.path.find('/', begin);
                if (end == std::string::npos)
                    break;  // The last component is the property row, not another group.
                auto name = field.path.substr(begin, end - begin);
                prefix += (prefix.empty() ? "" : "/") + name;
                GroupDefinition* group = nullptr;
                for (auto& node : *children)
                    if (node.Info().id == "group/" + prefix)
                        group = &std::get<GroupDefinition>(node.content);
                if (!group)
                {
                    GroupDefinition created;
                    created.id = "group/" + prefix;
                    created.label = Label(name);
                    created.description = "Theme " + prefix;
                    children->push_back({std::move(created)});
                    group = &std::get<GroupDefinition>(children->back().content);
                }
                children = &group->children;
                begin = end + 1;
            }
            children->push_back({field.definition});
        }
        return roots;
    }
}  // namespace LWSSettings
