#include "ThemeFields.hpp"
#include <LWSUI/ScopedFlag.hpp>
#include <LWSSettings/ThemeEditor.hpp>
#include <LWSSettings/SettingsStorage.hpp>
#include <LWSSettings/ThemeSelector.hpp>
namespace LWSSettings
{
    std::string ThemeEditor::OverrideId(std::string_view path)
    {
        return "@theme/override/" + std::string(path);
    }
    EditorConfig ThemeEditor::Config(const std::filesystem::path& file, const std::filesystem::path& iconPath)
    {
        EditorConfig config;
        config.iconPath = iconPath;
        config.title = "Theme editor";
        config.theme.tree.resetWidth = 128;  // Fits both Inherited and Use inherited without changing layout.
        config.showRevert = config.revertOnClose = config.collapseGroups = true;
        config.document.valuesFile = file;
        config.document.writeValues = [this](const std::filesystem::path&, const Values& values) -> Status
        {
            auto profile = ReadProfile(values);
            if (!profile) return std::unexpected(profile.error());
            LWSUI::ScopedFlag guard(synchronizing_, true);
            auto result = session_.SetProfile(std::move(*profile));
            if (result) result = session_.Save();
            return result;
        };
        return config;
    }
    ThemeEditor::ThemeEditor(LWS::PlatformContext& platform, std::filesystem::path file, const std::filesystem::path& iconPath)
        : ownedSession_(std::make_unique<ThemeSession>(std::move(file))), session_(*ownedSession_),
          editor_(platform, Config(session_.File(), iconPath))
    { Connect(); }
    ThemeEditor::ThemeEditor(LWS::PlatformContext& platform, ThemeSession& session, const std::filesystem::path& iconPath)
        : session_(session), editor_(platform, Config(session_.File(), iconPath))
    { Connect(); }
    void ThemeEditor::Connect()
    {
        errors_ = editor_.Document().OnError.Connect([this](const Error& error) { OnError.Raise(error); });
        sessionChanged_ = session_.OnChange.Connect([this]
        {
            OnPreview.Raise(session_.Theme());
            if (!synchronizing_) RequestPull();
        });
        hidden_ = editor_.OnHidden.Connect([this] { session_.Revert(); });
        resetFinished_ = editor_.Document().OnResetFinished.Connect([this] { Synchronize(); });
        changes_ = editor_.Document().OnChange.Connect([this](const Change& c) { Changed(c); });
        finished_ = editor_.Document().OnEditFinished.Connect([this](const auto& id, auto phase, auto origin)
                                                              { Finished(id, phase, origin); });
        reloaded_ = editor_.Document().OnValuesReload.Connect(
            [this]
            {
                if (synchronizing_) return;
                automaticOverrides_.clear();
                Synchronize();
            });
    }
    std::expected<ThemeOverrides, Error> ThemeEditor::ReadOverrides(const Values& values)
    {
        ThemeOverrides overrides;
        for (const auto& field : ThemeFields::All())
        {
            auto flag = values.find(OverrideId(field.path)), value = values.find(field.path);
            if (flag == values.end() || value == values.end() || !std::holds_alternative<bool>(flag->second))
                return std::unexpected(
                    Error{field.path, "Theme editing state is missing a property or its internal override flag"});
            if (std::get<bool>(flag->second))
                overrides.emplace(field.path, value->second);
        }
        return overrides;
    }
    std::expected<ThemeOverrides, Error> ThemeEditor::Overrides() const
    {
        return ReadOverrides(editor_.Document().WorkingValues());
    }
    Status ThemeEditor::Load(const std::filesystem::path& templateFile)
    {
        if (editor_.IsVisible())
            return std::unexpected(Error{"theme", "Hide the theme editor before loading another baseline"});
        SettingsDocument candidate;
        auto definitions = templateFile.empty() ? candidate.ReplaceDefinitions(ThemeFields::CreateDefinitions())
                                                : SettingsStorage::LoadTemplateFile(candidate, templateFile);
        if (!definitions)
            return definitions;
        // Insert known additions beside their prior catalog siblings, preserving custom grouping.
        // Other missing/unknown fields still fail the strict catalog validation below.
        auto upgraded = candidate.Definitions();
        bool changed = false;
        // Upgrade only known previous catalog defaults; explicit theme overrides stay intact.
        auto upgradeDefaults = [&](auto&& self, std::vector<Definition>& nodes) -> void
        {
            for (auto& node : nodes)
                if (auto* group = std::get_if<GroupDefinition>(&node.content))
                    self(self, group->children);
                else if (auto* setting = std::get_if<SettingDefinition>(&node.content))
                {
                    if (const auto* field = ThemeFields::Find(setting->id); field && !field->definition.visibleInEditor)
                    {
                        changed |= setting->visibleInEditor;
                        setting->visibleInEditor = false;
                    }
                    if (auto* color = std::get_if<ColorDefinition>(&setting->specification);
                        color && setting->id.ends_with("/colors/changed") &&
                        color->defaultValue == LLUtils::Color{uint32_t{0xf2c66dff}})
                        if (const auto* field = ThemeFields::Find(setting->id))
                        {
                            color->defaultValue = std::get<LLUtils::Color>(field->definition.DefaultValue());
                            changed = true;
                        }
                    std::optional<double> oldDefault;
                    if (setting->id.ends_with("/metrics/resetRightMargin")) oldDefault = 8;
                    else if (setting->id == "sections/search/metrics/spacing") oldDefault = 0;
                    else if (setting->id.ends_with("/metrics/popupWidth")) oldDefault = double(3520.f / 7);
                    else if (setting->id.ends_with("/metrics/popupHeight")) oldDefault = double(3040.f / 7);
                    if (auto* number = std::get_if<DecimalDefinition>(&setting->specification);
                        number && oldDefault && number->defaultValue == *oldDefault)
                        if (const auto* field = ThemeFields::Find(setting->id))
                        {
                            number->defaultValue = std::get<double>(field->definition.DefaultValue());
                            changed = true;
                        }
                }
        };
        upgradeDefaults(upgradeDefaults, upgraded);
        const std::map<std::string, std::string> additions{{"closeHoverBackground", "hoverSurface"},
                                                           {"closePressedBackground", "closeHoverBackground"},
                                                           {"closeActiveForeground", "closePressedBackground"},
                                                           {"resetRightMargin", "resetWidth"},
                                                           {"menuBarHeight", "menuPadding"},
                                                           {"menuBarPadding", "menuBarHeight"},
                                                           {"menuArrowWidth", "menuBarPadding"},
                                                           {"menuHoverDelayMs", "accelerationMs"}};
        for (const auto& field : ThemeFields::All())
        {
            const auto name = field.path.substr(field.path.rfind('/') + 1);
            if (auto known = additions.find(name); known != additions.end() && !candidate.Find(field.path))
            {
                const auto sibling = field.path.substr(0, field.path.rfind('/') + 1) + known->second;
                auto insert = [&](auto&& self, std::vector<Definition>& nodes) -> bool
                {
                    for (auto it = nodes.begin(); it != nodes.end(); ++it)
                    {
                        if (it->Info().id == sibling && std::holds_alternative<SettingDefinition>(it->content))
                        {
                            nodes.insert(it + 1, Definition{field.definition});
                            return true;
                        }
                        if (auto* group = std::get_if<GroupDefinition>(&it->content); group && self(self, group->children))
                            return true;
                    }
                    return false;
                };
                if (!insert(insert, upgraded))
                    return std::unexpected(
                        Error{"template", "Theme template is missing migration sibling: " + sibling});
                changed = true;
            }
        }
        if (changed)
        {
            auto valid = candidate.ReplaceDefinitions(std::move(upgraded));
            if (!valid) return valid;
        }
        if (candidate.WorkingValues().size() != ThemeFields::All().size())
            return std::unexpected(Error{"template", "Theme template does not match the theme field catalog"});
        auto combinedDefinitions = candidate.Definitions();
        for (const auto& field : ThemeFields::All())
        {
            const auto* value = candidate.Find(field.path);
            if (!value || value->visibleInEditor != field.definition.visibleInEditor ||
                value->specification.index() != field.definition.specification.index() ||
                value->DefaultValue() != field.definition.DefaultValue())
                return std::unexpected(
                    Error{field.path, "Theme template field type or default differs from the catalog"});
            const bool sameBounds = std::visit(
                [&](const auto& specification)
                {
                    using T = std::decay_t<decltype(specification)>;
                    if constexpr (std::is_same_v<T, IntegerDefinition> || std::is_same_v<T, DecimalDefinition>)
                    {
                        const auto& expected = std::get<T>(field.definition.specification);
                        return specification.minimum == expected.minimum && specification.maximum == expected.maximum &&
                               specification.step == expected.step;
                    }
                    else
                        return true;
                },
                value->specification);
            if (!sameBounds)
                return std::unexpected(Error{field.path, "Theme template bounds or step differ from the catalog"});
            SettingDefinition flag;
            flag.id = OverrideId(field.path);
            flag.label = "Internal theme override";
            flag.visibleInEditor = false;
            flag.specification = BooleanDefinition{false};
            combinedDefinitions.push_back({std::move(flag)});
        }
        SettingDefinition preset;
        preset.id = "@theme/preset"; preset.label = "Theme preset"; preset.visibleInEditor = false;
        ChoiceDefinition choices;
        choices.defaultValue = LWSUI::ThemePresetName(LWSUI::DefaultThemePreset);
        for (auto value : LWSUI::ThemePresets) choices.options.push_back({LWSUI::ThemePresetLabel(value), LWSUI::ThemePresetName(value)});
        preset.specification = std::move(choices);
        combinedDefinitions.push_back({std::move(preset)});
        if (ownedSession_)
        {
            LWSUI::ScopedFlag guard(synchronizing_, true);
            auto result = session_.Load();
            if (!result) return result;
        }
        definitions_ = std::move(combinedDefinitions);
        definitionPreset_.reset(); loaded_ = true;
        PullSession();
        return {};
    }
    std::expected<ThemeProfile, Error> ThemeEditor::ReadProfile(const Values& values) const
    {
        auto overrides = ReadOverrides(values);
        if (!overrides) return std::unexpected(overrides.error());
        auto found = values.find("@theme/preset");
        if (found == values.end() || !std::holds_alternative<std::string>(found->second))
            return std::unexpected(Error{"preset", "Missing theme preset"});
        const auto& name = std::get<std::string>(found->second);
        auto preset = LWSUI::ParseThemePreset(name);
        if (!preset) return std::unexpected(Error{"preset", "Unknown theme preset"});
        return ThemeProfile{*preset, std::move(*overrides)};
    }
    Values ThemeEditor::EditingValues(const ThemeProfile& profile) const
    {
        Values values{{"@theme/preset", std::string(LWSUI::ThemePresetName(profile.preset))}};
        const auto resolved = ThemeFields::Resolve(profile.overrides, profile.preset).value();
        for (const auto& field : ThemeFields::All())
        {
            values[field.path] = field.read(resolved);
            values[OverrideId(field.path)] = profile.overrides.contains(field.path);
        }
        return values;
    }
    void ThemeEditor::RequestPull()
    {
        if (!loaded_) return;
        if (auto* view = editor_.View())
        {
            if (pullPending_) return;
            pullPending_ = true;
            view->Host()->Post(view->Handle(), [this](auto&) { if (pullPending_) PullSession(); });
        }
        else PullSession();
    }
    void ThemeEditor::PullSession()
    {
        if (!loaded_) return;
        pullPending_ = false;
        LWSUI::ScopedFlag guard(synchronizing_, true);
        if (auto* view = editor_.View()) view->Finish(LWSUI::EditPhase::Cancel);
        automaticOverrides_.clear();
        auto& document = editor_.Document();
        if (definitionPreset_ != session_.Current().preset)
        {
            auto definitions = definitions_;
            const SettingsTheme defaults(session_.Current().preset);
            auto update = [&](auto&& self, std::vector<Definition>& nodes) -> void
            {
                for (auto& node : nodes)
                    if (auto* group = std::get_if<GroupDefinition>(&node.content)) self(self, group->children);
                    else
                    {
                        auto& setting = std::get<SettingDefinition>(node.content);
                        if (const auto* field = ThemeFields::Find(setting.id))
                        {
                            auto value = field->read(defaults);
                            std::visit([&](auto& spec) { spec.defaultValue = std::get<std::decay_t<decltype(spec.defaultValue)>>(value); }, setting.specification);
                        }
                        else if (setting.id == "@theme/preset")
                            std::get<ChoiceDefinition>(setting.specification).defaultValue = LWSUI::ThemePresetName(session_.Current().preset);
                    }
            };
            update(update, definitions);
            (void) document.ReplaceDefinitions(std::move(definitions));
            definitionPreset_ = session_.Current().preset;
        }
        (void) document.ReplaceValues(EditingValues(session_.Saved()));
        if (session_.IsDirty())
            for (auto& [path, value] : EditingValues(session_.Current()))
                if (document.WorkingValue(path) != value)
                    (void) document.SetValue(path, std::move(value), EditPhase::Commit, ChangeOrigin::Load);
        UpdatePresentation(session_.Current().overrides);
    }
    void ThemeEditor::Changed(const Change& change)
    {
        // Load is reserved for the adapter's derived refreshes. Such notifications
        // still refresh controls, but must not promote inherited values to overrides.
        if (synchronizing_ || change.origin == ChangeOrigin::Load)
            return;
        auto& document = editor_.Document();
        if (ThemeFields::Find(change.id))
        {
            const auto& path = change.id;
            auto flag = OverrideId(path);
            const auto flagValue = document.WorkingValue(flag);
            if (!flagValue || !std::holds_alternative<bool>(*flagValue))
            {
                document.OnError.Raise(Error{path, "Theme editing state is missing its internal override flag"});
                return;
            }
            if (change.origin == ChangeOrigin::Reset)
            {
                automaticOverrides_.erase(path);
                (void) document.SetValue(flag, false, EditPhase::Commit, ChangeOrigin::Load);
            }
            else if (change.phase != EditPhase::Cancel && change.origin != ChangeOrigin::Cancel &&
                     !std::get<bool>(*flagValue))
            {
                if (change.phase == EditPhase::Preview)
                    automaticOverrides_.insert(path);
                (void) document.SetValue(flag, true, change.phase, ChangeOrigin::Load);
            }
        }
        Synchronize();
    }
    void ThemeEditor::Finished(const std::string& id, EditPhase phase, ChangeOrigin origin)
    {
        if (synchronizing_ || origin == ChangeOrigin::Load || !ThemeFields::Find(id))
            return;
        if (origin == ChangeOrigin::Reset)
        {
            // Reset must remove an explicit override even when the effective value
            // equals the default and therefore emits no value-change notification.
            automaticOverrides_.erase(id);
            (void) editor_.Document().SetValue(OverrideId(id), false, EditPhase::Commit, ChangeOrigin::Load);
            Synchronize();
            return;
        }
        if (!automaticOverrides_.erase(id))
            return;
        // A no-op confirmation still finishes the companion internal edit. Escape
        // restores inheritance only when this interaction created the override.
        (void) editor_.Document().SetValue(OverrideId(id), phase != EditPhase::Cancel, phase, ChangeOrigin::Load);
        Synchronize();
    }
    Status ThemeEditor::Show()
    {
        if (!loaded_)
        {
            auto initialized = Load();
            if (!initialized)
                return initialized;
        }
        if (!editor_.IsVisible())
        {
            PullSession();
            auto snapshot = session_.Theme();
            snapshot.tree.resetWidth = std::max(snapshot.tree.resetWidth, 128.f);
            auto styled = editor_.SetTheme(snapshot);
            if (!styled) return styled;
        }
        auto result = editor_.Show();
        if (result)
        {
            auto& actions = editor_.View()->HeaderActions();
            if (actions.Children().empty()) actions.Emplace<ThemeSelector>(session_, [this] { return Save(); }, [this] { Revert(); });
            auto overrides = Overrides();
            if (overrides)
                UpdatePresentation(*overrides);
            std::ignore = editor_.View()->Host()->Window().RequestActivation();
        }
        return result;
    }
    void ThemeEditor::UpdatePresentation(const ThemeOverrides& overrides)
    {
        if (auto* view = editor_.View())
            view->SetRowResetPresentation(
                [&](std::string_view id)
                {
                    const bool explicitValue = overrides.contains(id);
                    return RowResetPresentation{explicitValue ? "Use inherited" : "Inherited", explicitValue,
                                                LWSUI::EditPhase::Cancel};
                });
    }
    void ThemeEditor::Synchronize()
    {
        if (synchronizing_ || editor_.Document().resetting_) return;
        auto profile = ReadProfile(editor_.Document().WorkingValues());
        if (!profile) { editor_.Document().OnError.Raise(profile.error()); return; }
        const auto& overrides = profile->overrides;
        auto resolved = ThemeFields::Resolve(overrides, profile->preset);
        if (!resolved)
        {
            editor_.Document().OnError.Raise(resolved.error());
            return;
        }
        auto& document = editor_.Document();
        for (const auto& field : ThemeFields::All())
            if (!overrides.contains(field.path))
            {
                auto effective = field.read(*resolved);
                if (document.WorkingValue(field.path) != effective)
                    (void) document.SetValue(field.path, std::move(effective), EditPhase::Commit, ChangeOrigin::Load);
            }
        UpdatePresentation(overrides);
        {
            LWSUI::ScopedFlag guard(synchronizing_, true);
            auto result = session_.SetProfile(std::move(*profile));
            if (!result) editor_.Document().OnError.Raise(result.error());
        }
        if (definitionPreset_ != session_.Current().preset) RequestPull();
    }
    void ThemeEditor::Revert()
    {
        {
            LWSUI::ScopedFlag guard(synchronizing_, true);
            if (auto* view = editor_.View()) view->Finish(LWSUI::EditPhase::Cancel);
            session_.Revert();
        }
        PullSession();
    }
    Status ThemeEditor::Save()
    {
        if (!loaded_)
            return std::unexpected(Error{"theme", "Load the theme before saving"});
        if (pullPending_) PullSession();
        if (auto* view = editor_.View(); view && !view->Finish(LWSUI::EditPhase::Commit))
            return std::unexpected(Error{"theme", "Correct the invalid draft before saving"});
        return editor_.Document().Save();
    }
}  // namespace LWSSettings
