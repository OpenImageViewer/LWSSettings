#include <LWSSettings/SettingsView.hpp>
#include <LWSUI/UIHost.hpp>
#include "SettingsIcon.hpp"
#include "SettingsHeader.hpp"
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace LWSSettings
{
    static std::string Lower(std::string text)
    {
        return LWSUI::FoldSearchText(std::move(text));
    }
    static EditPhase ModelPhase(LWSUI::EditPhase phase)
    {
        return phase == LWSUI::EditPhase::Preview  ? EditPhase::Preview
               : phase == LWSUI::EditPhase::Cancel ? EditPhase::Cancel
                                                   : EditPhase::Commit;
    }
    struct SettingsView::Binding
    {
        std::string id, search;
        LWSUI::TreeView::Row* row;
        LWSUI::Label* label;
        LWSUI::Control* editor;
        LWSUI::Button* reset;
        std::function<void(const Value&)> refresh;
        LWSUI::ConnectionSet connections;
        bool updating = false;
        std::optional<bool> editVisibility;
        std::unique_ptr<LWSUI::Control> CreateEditor(const SettingDefinition&, std::function<Status(Value, LWSUI::EditPhase)> edit);
    };
    std::unique_ptr<LWSUI::Control> SettingsView::Binding::CreateEditor(const SettingDefinition& setting,
                                             std::function<Status(Value, LWSUI::EditPhase)> edit)
    {
        std::unique_ptr<LWSUI::Control> result;
        auto own = [&](auto control) -> auto&
        { auto& reference = *control; result = std::move(control); return reference; };
        std::visit(
            [&](const auto& spec)
            {
                using S = std::decay_t<decltype(spec)>;
                if constexpr (std::is_same_v<S, TextDefinition>)
                {
                    auto& control = own(std::make_unique<LWSUI::TextBox>());
                    editor = &control;
                    refresh = [&control](const Value& v) { control.SetText(std::get<std::string>(v)); };
                    connections.Connect(control.OnEdit,
                                        [edit, &control](const std::string& value, LWSUI::EditPhase phase)
                                        {
                                            auto result = edit(value, phase);
                                            if (!result)
                                                control.SetValidation(result.error().message);
                                        });
                }
                else if constexpr (std::is_same_v<S, FileDefinition>)
                {
                    auto& control = own(std::make_unique<LWSUI::FilePicker>());
                    editor = &control;
                    refresh = [&control](const Value& v) { control.SetValue(std::get<std::string>(v)); };
                    connections.Connect(control.OnEdit, [edit](const auto& value, auto phase) { (void) edit(value, phase); });
                }
                else if constexpr (std::is_same_v<S, BooleanDefinition>)
                {
                    auto& control = own(std::make_unique<LWSUI::CheckBox>());
                    editor = &control;
                    refresh = [&control](const Value& v) { control.SetValue(std::get<bool>(v)); };
                    connections.Connect(control.OnChange, [edit](bool value) { (void) edit(value, LWSUI::EditPhase::Commit); });
                }
                else if constexpr (std::is_same_v<S, ColorDefinition>)
                {
                    auto& control = own(std::make_unique<LWSUI::ColorPicker>());
                    editor = &control;
                    refresh = [&control](const Value& v) { control.SetValue(std::get<LLUtils::Color>(v)); };
                    connections.Connect(control.OnEdit, [edit](auto value, auto phase) { (void) edit(value, phase); });
                }
                else if constexpr (std::is_same_v<S, IntegerDefinition> || std::is_same_v<S, DecimalDefinition>)
                {
                    using T = decltype(spec.defaultValue);
                    auto setup = [&](auto& control)
                    {
                        editor = &control;
                        refresh = [&control](const Value& v) { control.SetValue(std::get<T>(v)); };
                        connections.Connect(control.OnEdit, [edit](T value, auto phase) { (void) edit(value, phase); });
                    };
                    if (spec.presentation == NumericPresentation::Slider)
                    {
                        auto& slider = own(std::make_unique<LWSUI::Slider>());
                        auto& value = slider.template ConfigureValue<T>({spec.minimum, spec.maximum, spec.step},
                                                                        spec.defaultValue);
                        editor = &slider;
                        refresh = [&value](const Value& v) { value.SetValue(std::get<T>(v)); };
                        connections.Connect(value.OnEdit, [edit](T value, auto phase) { (void) edit(value, phase); });
                    }
                    else
                        setup(own(std::make_unique<LWSUI::NumericEdit<T>>(
                            LWSUI::NumericSpec<T>{spec.minimum, spec.maximum, spec.step})));
                }
                else if constexpr (std::is_same_v<S, ChoiceDefinition>)
                {
                    std::vector<LWSUI::Choice> choices;
                    for (auto& o : spec.options)
                        choices.push_back({o.label, o.value});
                    auto setup = [&](auto& control)
                    {
                        editor = &control;
                        refresh = [&control](const Value& v) { control.SetValue(std::get<std::string>(v)); };
                        connections.Connect(control.OnChange,
                                            [edit](const std::string& value) { (void) edit(value, LWSUI::EditPhase::Commit); });
                    };
                    if (spec.presentation == ChoicePresentation::Radio)
                        setup(own(std::make_unique<LWSUI::RadioGroup>(choices)));
                    else
                        setup(own(std::make_unique<LWSUI::ComboBox>(choices)));
                }
            },
            setting.specification);
        return result;
    }
    SettingsView::SettingsView(SettingsDocument& document, std::string title, SettingsTheme theme, bool showRevert,
                               bool collapseGroups)
        : document_(document), theme_(std::move(theme)), collapseGroups_(collapseGroups)
    {
        SetStyle(theme_.base);
        headerState_ = std::make_shared<internal::SettingsHeaderState>();
        headerState_->title = std::move(title);
        headerState_->icon = internal::DefaultSettingsIcon();
        headerState_->theme = theme_;
        ownedHeader_ = &Emplace<internal::SettingsHeader>(headerState_);
        search_ = &Emplace<LWSUI::TextBox>();
        search_->SetStyle(theme_.search);
        StyleFilterButtons();
        search_->SetHighlightPattern("");
        search_->SetPlaceholder("Search settings...");
        clearSearch_ = &Emplace<LWSUI::Button>("×");
        clearSearch_->SetStyle(theme_.search);
        clearSearch_->SetHighlightPattern("");
        clearSearch_->SetVisible(false);
        clicks_.push_back(clearSearch_->OnClick.Connect([this] { ClearSearch(); }));
        const std::array names{"All", "Changed", "Default"};
        for (size_t i = 0; i < names.size(); ++i)
        {
            filterButtons_[i] = &Emplace<LWSUI::Button>(names[i]);
            filterButtons_[i]->SetHighlightPattern("");
            filterButtons_[i]->sizeToContent = true;
            clicks_.push_back(filterButtons_[i]->OnClick.Connect([this, i] { SetValueFilter(SettingValueFilter(i)); }));
        }
        StyleFilterButtons();
        for (size_t i = 0; i < groupButtons_.size(); ++i)
        {
            groupButtons_[i] = &Emplace<LWSUI::GlyphButton>(i == 0 ? LWSUI::Glyph::DoubleChevronDown
                                                                   : LWSUI::Glyph::DoubleChevronUp);
            groupButtons_[i]->SetText(i == 0 ? "Expand all" : "Collapse all");
            groupButtons_[i]->focusable = false;
            groupButtons_[i]->SetTooltip(i == 0 ? "Expand all" : "Collapse all");
            groupButtons_[i]->SetStyle(theme_.search);
            groupButtons_[i]->SetHighlightPattern("");
            clicks_.push_back(groupButtons_[i]->OnClick.Connect([this, i] { SetGroupsExpanded(i == 0); }));
        }
        treeScroll_ = &Emplace<LWSUI::ScrollView>();
        treeScroll_->overlayScrollBar = true;
        treeScroll_->SetStyle(theme_.tree);
        descriptionScroll_ = &Emplace<LWSUI::ScrollView>();
        descriptionScroll_->SetStyle(theme_.description);
        descriptionTitle_ = &Emplace<LWSUI::Label>("Setting details");
        descriptionTitle_->fontRole = FontRole::Heading;
        descriptionTitle_->SetStyle(theme_.description);
        descriptionTitle_->SetForeground(theme_.description.accent);
        auto description = std::make_unique<LWSUI::Label>();
        description->SetStyle(theme_.description);
        description->SetForeground(theme_.description.muted);
        description->wrap = true;
        description_ = description.get();
        descriptionScroll_->SetContent(std::move(description));
        status_ = &Emplace<LWSUI::Label>();
        status_->SetStyle(theme_.description);
        status_->fontRole = FontRole::Small;
        locationScroll_ = &Emplace<LWSUI::ScrollView>();
        auto location = std::make_unique<LWSUI::Label>();
        location_ = location.get();
        location_->fontRole = FontRole::Small;
        location_->wrap = true;
        locationScroll_->SetContent(std::move(location));
        StyleLocation();
        locationScroll_->SetVisible(false);
        actions_ = &Emplace<LWSUI::FlowPanel>();
        actions_->alignEnd = true;
        actions_->firstLeading = true;
        actions_->SetStyle(theme_.footer);
        actions_->SetHighlightPattern("");
        auto action =
            [&](std::string name, std::function<void()> callback, LWSUI::EditPhase phase = LWSUI::EditPhase::Commit)
        {
            auto ownedButton = std::make_unique<LWSUI::Button>(name);
            auto& button = *ownedButton;
            button.sizeToContent = true;
            actions_->AddItem(std::move(ownedButton));
            button.focusLossPhase = phase;
            button.flat = true;
            button.SetForeground(name == "Reset all" || name == "Cancel" || name == "Revert" ? theme_.footer.muted
                                                                                             : theme_.footer.accent);
            clicks_.push_back(button.OnClick.Connect(std::move(callback)));
        };
        action("Reset all",
               [this]
               {
                   if (Finish(LWSUI::EditPhase::Commit))
                       Report(document_.ResetAll());
               });
        if (document_.Config().applyPolicy == ApplyPolicy::OnApply)
        {
            action(
                "Cancel",
                [this]
                {
                    Finish(LWSUI::EditPhase::Cancel);
                    document_.Cancel();
                    information_ = "Changes cancelled";
                    UpdateStatus();
                },
                LWSUI::EditPhase::Cancel);
            action("Apply",
                   [this]
                   {
                       if (Finish(LWSUI::EditPhase::Commit))
                       {
                           auto result = document_.Apply();
                           if (!result)
                               Report(result);
                           else if (document_.Persistence().error)
                               Report(std::unexpected(*document_.Persistence().error));
                           else
                               Report({});
                           if (document_.Persistence().state != PersistenceState::Failed)
                           {
                               information_ = "Applied";
                               UpdateStatus();
                           }
                       }
                   });
        }
        if (showRevert)
            action(
                "Revert",
                [this]
                {
                    Finish(LWSUI::EditPhase::Cancel);
                    document_.RevertSaved();
                    information_ = "Restored saved values";
                    UpdateStatus();
                },
                LWSUI::EditPhase::Cancel);
        action(document_.Config().savePolicy == SavePolicy::OnCommit ? "Save now" : "Save",
               [this]
               {
                   if (Finish(LWSUI::EditPhase::Commit))
                   {
                       auto result = SaveChanges();
                       Report(result);
                       if (result)
                       {
                           information_ = "Saved";
                           UpdateStatus();
                       }
                   }
               });
        layout_ = &Emplace<LWSUI::DockPanel>();
        const auto take = [&](LWSUI::Control* control) { return Remove(*control); };
        layout_->AddDocked(take(ownedHeader_), LWSUI::Dock::Top);
        auto entry = std::make_unique<LWSUI::DockPanel>();
        clearSearch_->sizeToContent = true;
        entry->AddDocked(take(clearSearch_), LWSUI::Dock::Right);
        entry->AddDocked(take(search_), LWSUI::Dock::Fill);
        auto controls = std::make_unique<LWSUI::StackPanel>();
        controls->orientation = LWSUI::Orientation::Horizontal;
        controls->sizeToContent = true;
        auto filters = std::make_unique<LWSUI::StackPanel>();
        filters->orientation = LWSUI::Orientation::Horizontal;
        filters->sizeToContent = true;
        filters->spacing = 0;
        for (auto* button : filterButtons_)
            filters->Add(take(button));
        for (auto* button : groupButtons_)
            controls->Add(take(button));
        auto flow = std::make_unique<LWSUI::FlowPanel>();
        searchFlow_ = flow.get();
        flow->AddItem(std::move(entry), true, 180);
        flow->AddItem(std::move(filters));
        flow->AddItem(std::move(controls));
        auto searchPanel = std::make_unique<LWSUI::Panel>(std::move(flow));
        searchPanel_ = searchPanel.get();
        layout_->AddDocked(std::move(searchPanel), LWSUI::Dock::Top);
        auto footer = std::make_unique<LWSUI::Panel>(take(actions_));
        footerPanel_ = footer.get();
        auto treePanel = std::make_unique<LWSUI::Panel>(take(treeScroll_));
        treePanel_ = treePanel.get();
        auto details = std::make_unique<LWSUI::DockPanel>();
        details->AddDocked(take(descriptionTitle_), LWSUI::Dock::Top);
        auto metadata = std::make_unique<LWSUI::DockPanel>();
        metadata->AddDocked(take(status_), LWSUI::Dock::Top);
        metadata->AddDocked(take(locationScroll_), LWSUI::Dock::Fill);
        details->AddDocked(std::move(metadata), LWSUI::Dock::Bottom, 0, .5f);
        details->AddDocked(take(descriptionScroll_), LWSUI::Dock::Fill);
        auto descriptionPanel = std::make_unique<LWSUI::Panel>(std::move(details));
        descriptionPanel_ = descriptionPanel.get();
        auto split = std::make_unique<LWSUI::SplitPanel>(std::move(treePanel), std::move(descriptionPanel));
        split_ = split.get();
        layout_->AddDocked(std::move(split), LWSUI::Dock::Fill);
        layout_->AddDocked(std::move(footer), LWSUI::Dock::Bottom);
        surface_ = &Emplace<LWSUI::Panel>(Remove(*layout_));
        searchConnection_ = search_->OnEdit.Connect([this](const auto& text, LWSUI::EditPhase) { SetFilter(text); });
        changeConnection_ = document_.OnChange.Connect([this](const Change& change) { Refresh(change); });
        finishedConnection_ = document_.OnEditFinished.Connect([this](const std::string& id, EditPhase, ChangeOrigin)
        {
            if (auto* binding = FindBinding(id)) binding->editVisibility.reset();
            if (valueFilter_ != SettingValueFilter::All) RequestFilterUpdate();
        });
        errorConnection_ = document_.OnError.Connect([this](const Error& error) { Report(std::unexpected(error)); });
        refreshConnection_ = document_.OnRefresh.Connect(
            [this](const std::string& id)
            {
                if (auto* binding = FindBinding(id); binding && !binding->updating)
                    binding->refresh(*document_.WorkingValue(id));
            });
        reloadConnection_ = document_.OnReload.Connect([this] { Rebuild(); });
        valuesReloadConnection_ = document_.OnValuesReload.Connect([this] { ReloadValues(); });
        persistenceConnection_ = document_.OnPersistenceChanged.Connect([this] { UpdateStatus(); });
        Rebuild();
        SetTheme(theme_);
    }
    SettingsView::~SettingsView() = default;
    void SettingsView::OnDetach()
    {
        filterPending_ = false;
        filterDirty_ = true;
    }
    void SettingsView::OnAttach()
    {
        if (filterDirty_)
            RequestFilterUpdate();
    }
    void SettingsView::SetIcon(LWS::BitmapSharedPtr icon)
    {
        headerState_->icon = std::move(icon);
        headerState_->changed.Raise();
        Invalidate(true);
    }
    LWSUI::StackPanel& SettingsView::HeaderActions()
    {
        auto* header = ownedHeader_ ? ownedHeader_ : static_cast<internal::SettingsHeader*>(externalHeader_.Get());
        if (!header) throw std::logic_error("Settings header content is no longer available");
        return header->Actions();
    }
    void SettingsView::MoveHeaderTo(LWSUI::MenuBar& bar)
    {
        if (!ownedHeader_) throw std::logic_error("Settings header has already been moved");
        auto header = layout_->Take(*ownedHeader_);
        ownedHeader_ = nullptr;
        bar.SetContent(std::move(header));
        auto* presenter = static_cast<internal::SettingsHeader*>(bar.Content());
        presenter->UseMenuBar(bar);
        externalHeader_ = presenter->Handle();
        Invalidate(true);
    }
    void SettingsView::SetTheme(const SettingsTheme& theme)
    {
        theme_ = theme;
        SetStyle(theme_.base);
        headerState_->theme = theme_;
        headerState_->changed.Raise();
        search_->SetStyle(theme_.search);
        clearSearch_->SetStyle(theme_.search);
        for (auto* button : groupButtons_) button->SetStyle(theme_.search);
        StyleFilterButtons();
        treeScroll_->SetStyle(theme_.tree);
        tree_->SetStyle(theme_.tree);
        tree_->spacing = theme_.tree.spacing;
        descriptionScroll_->SetStyle(theme_.description);
        descriptionTitle_->SetStyle(theme_.description);
        descriptionTitle_->SetForeground(theme_.description.accent);
        description_->SetStyle(theme_.description);
        description_->SetForeground(theme_.description.muted);
        status_->SetStyle(theme_.description);
        StyleLocation();
        actions_->SetStyle(theme_.footer);
        const auto& l = theme_.layout;
        searchPanel_->SetStyle(theme_.search);
        searchPanel_->padding = LWSUI::Insets{l.horizontalPadding, 0, l.horizontalPadding, l.sectionGap};
        searchFlow_->gap = searchFlow_->rowGap = theme_.search.spacing;
        groupButtons_[0]->Parent()->SetStyle(theme_.search);
        static_cast<LWSUI::StackPanel*>(groupButtons_[0]->Parent())->spacing = theme_.search.spacing;
        treePanel_->SetStyle(theme_.tree);
        descriptionPanel_->SetStyle(theme_.description);
        descriptionPanel_->padding = LWSUI::Insets{l.horizontalPadding - theme_.description.labelPadding,
                                                   theme_.description.padding,
                                                   l.horizontalPadding - theme_.description.labelPadding,
                                                   theme_.description.padding};
        static_cast<LWSUI::DockPanel*>(descriptionPanel_->Content())->gap = l.descriptionTitleGap;
        footerPanel_->SetStyle(theme_.footer);
        footerPanel_->padding = LWSUI::Insets{l.horizontalPadding, theme_.footer.padding, l.horizontalPadding,
                                              theme_.footer.padding};
        footerPanel_->minimumHeight = l.footerHeight;
        actions_->gap = actions_->rowGap = l.actionGap;
        actions_->minimumItemWidth = l.actionWidth;
        split_->SetStyle(theme_.description);
        split_->SetDefaultTrailingSize(l.descriptionHeight, l.descriptionMaxFraction);
        split_->SetMinimumSizes(theme_.tree.treeRowHeight, theme_.description.headingFont.size * 1.4f +
                                                               theme_.description.font.size * 1.4f +
                                                               2 * theme_.description.padding);
        for (auto& child : actions_->Children())
        {
            auto& button = static_cast<LWSUI::Button&>(*child);
            button.SetForeground(button.Text() == "Save" || button.Text() == "Save now" || button.Text() == "Apply"
                                     ? theme_.footer.accent
                                     : theme_.footer.muted);
        }
        for (auto& binding : bindings_)
        {
            binding->label->SetStyle(theme_.tree);
            binding->editor->SetStyle(theme_.controls);
            StyleChanged(*binding);
            binding->reset->SetStyle(theme_.footer);
            binding->reset->SetForeground(theme_.footer.muted);
        }
        UpdateStatus();
        Invalidate(true);
    }
    void SettingsView::SetRowResetPresentation(const std::function<RowResetPresentation(std::string_view)>& present)
    {
        for (auto& binding : bindings_)
        {
            auto state = present(binding->id);
            if (binding->reset->Text() != state.caption)
                binding->reset->SetText(std::move(state.caption));
            if (!state.enabled && Host() && Host()->IsFocused(*binding->reset))
                Host()->Focus(binding->row);
            if (binding->reset->Enabled() != state.enabled)
                binding->reset->SetEnabled(state.enabled);
            binding->reset->focusLossPhase = state.focusLossPhase;
        }
    }
    Status SettingsView::SaveChanges()
    {
        auto result = document_.Apply();
        if (!result)
            return result;
        // Apply already saves in OnCommit mode. An explicit second write adds no value
        // and could turn a successful save into an unrelated second-write failure.
        if (document_.Config().savePolicy == SavePolicy::OnCommit)
            return document_.Persistence().error ? Status{std::unexpected(*document_.Persistence().error)} : Status{};
        return document_.Save();
    }
    void SettingsView::ReloadValues()
    {
        // Cancel the old popup session without publishing its old value over the reload.
        // Keep the controls, focus, filter, scroll offset and expansion state intact.
        reloadingValues_ = true;
        try
        {
            if (Host())
                Host()->ClosePopup(LWSUI::EditPhase::Cancel);
            for (auto& binding : bindings_)
            {
                binding->editVisibility.reset();
                binding->refresh(*document_.WorkingValue(binding->id));
                StyleChanged(*binding);
            }
        }
        catch (...)
        {
            reloadingValues_ = false;
            throw;
        }
        reloadingValues_ = false;
        UpdateFilter();
        ClearStatus();
        Invalidate(true);
    }
    void SettingsView::Report(const Status& status)
    {
        operationError_ = status ? std::string{} : status.error().location + ": " + status.error().message;
        UpdateStatus();
    }
    void SettingsView::ClearStatus()
    {
        validation_.clear(); operationError_.clear(); information_.clear();
        UpdateStatus();
    }
    void SettingsView::UpdateStatus()
    {
        const bool dirty = document_.IsDirty();
        if (headerState_->dirty != dirty) { headerState_->dirty = dirty; headerState_->changed.Raise(); }
        status_->SetText(!validation_.empty()       ? validation_
                         : !operationError_.empty() ? operationError_
                                                    : information_);
        status_->SetVisible(!status_->Text().empty());
        status_->SetForeground(
            !validation_.empty() || !operationError_.empty() ? std::optional{theme_.description.error} : std::nullopt);
    }
    void SettingsView::StyleLocation()
    {
        auto style = theme_.description;
        // Metadata remains smaller even when a custom small font exceeds the body font.
        style.smallFont.size = std::min(style.smallFont.size, style.font.size * .85f);
        style.smallFont.weight = 400;
        style.smallFont.italic = false;
        style.rowHeight = 0;
        style.accent = style.muted;
        locationScroll_->SetStyle(style);
        location_->SetStyle(style);
        location_->SetForeground(style.muted);
    }
    void SettingsView::Describe(const std::string& title, const std::string& path, const std::string& text)
    {
        descriptionTitle_->SetText(title);
        description_->SetText(text);
        location_->SetText(path.empty() ? std::string{} : "Location: " + path);
        locationScroll_->SetVisible(!path.empty());
        descriptionScroll_->SetOffset(0);
        locationScroll_->SetOffset(0);
    }
    void SettingsView::Rebuild()
    {
        if (Host())
            Host()->ClosePopup(LWSUI::EditPhase::Cancel);
        descriptionTitle_->SetText("Setting details");
        description_->SetText("");
        location_->SetText("");
        locationScroll_->SetVisible(false);
        bindings_.clear();
        groups_.clear();
        hovers_.clear();
        auto tree = std::make_unique<LWSUI::TreeView>();
        tree->SetStyle(theme_.tree);
        tree->spacing = theme_.tree.spacing;
        tree->SetHighlightPattern(filter_);
        tree_ = tree.get();
        auto walk = [&](auto&& self, LWSUI::StackPanel& parent, const std::vector<Definition>& definitions,
                        const std::string& path, const std::string& ancestorSearch) -> void
        {
            for (const auto& definition : definitions)
            {
                const auto& info = definition.Info();
                std::string breadcrumb = path.empty() ? info.label : path + " / " + info.label;
                if (auto* group = std::get_if<GroupDefinition>(&definition.content))
                {
                    auto& branch = parent.Emplace<LWSUI::TreeView::Branch>(group->label);
                    branch.SetExpanded(!collapseGroups_);
                    groups_.push_back({&branch, Lower(breadcrumb + " " + group->description + " " + group->id)});
                    hovers_.push_back(branch.OnHover.Connect([this, title = group->label, path, text = group->description]
                                                             { Describe(title, path, text); }));
                    self(self, branch.Content(), group->children, breadcrumb, ancestorSearch + " " + groups_.back().search);
                    continue;
                }
                const auto& setting = std::get<SettingDefinition>(definition.content);
                if (!setting.visibleInEditor)
                    continue;
                auto ownedBinding = std::make_unique<Binding>();
                auto* binding = ownedBinding.get();
                binding->id = setting.id;
                binding->search = Lower(ancestorSearch + " " + breadcrumb + " " + setting.description + " " + setting.id);
                auto label = std::make_unique<LWSUI::Label>(setting.label);
                binding->label = label.get();
                auto edit = [this, binding](Value value, LWSUI::EditPhase phase)
                {
                    if (reloadingValues_)
                        return Status{};
                    if (phase == LWSUI::EditPhase::Preview && !binding->editVisibility)
                        binding->editVisibility = binding->row->Visible();
                    Status result;
                    try
                    {
                        struct Updating
                        {
                            bool& flag;
                            bool previous;
                            explicit Updating(bool& value) : flag(value), previous(std::exchange(value, true)) {}
                            ~Updating() { flag = previous; }
                        } guard(binding->updating);
                        result = document_.SetValue(binding->id, value, ModelPhase(phase), ChangeOrigin::User);
                    }
                    catch (...)
                    {
                        binding->refresh(*document_.WorkingValue(binding->id));
                        StyleChanged(*binding);
                        UpdateStatus();
                        throw;
                    }
                    // Preserve typed drafts unless a subscriber changed the accepted value.
                    if (result && document_.WorkingValue(binding->id) != value)
                        binding->refresh(*document_.WorkingValue(binding->id));
                    if (!result)
                    {
                        validation_ = result.error().message;
                        UpdateStatus();
                    }
                    else
                    {
                        validation_.clear();
                        UpdateStatus();
                    }
                    return result;
                };
                auto editor = binding->CreateEditor(setting, edit);
                binding->editor->SetStyle(theme_.controls);
                auto action = std::make_unique<LWSUI::Button>("Reset");
                auto& reset = *action;
                auto& row = parent.Emplace<LWSUI::TreeView::Row>(std::move(label), std::move(editor), std::move(action));
                binding->row = &row;
                binding->reset = &reset;
                reset.flat = true;
                reset.fontRole = FontRole::Small;
                reset.SetStyle(theme_.footer);
                reset.SetForeground(theme_.footer.muted);
                reset.SetHighlightPattern("");
                binding->connections.Connect(reset.OnClick, [this, id = setting.id] { Report(document_.Reset(id)); });
                hovers_.push_back(
                    row.OnHover.Connect([this, title = setting.label, path, text = setting.description]
                                         { Describe(title, path, text); }));
                binding->refresh(*document_.WorkingValue(setting.id));
                StyleChanged(*binding);
                bindings_.push_back(std::move(ownedBinding));
            }
        };
        walk(walk, *tree, document_.Definitions(), {}, {});
        std::sort(bindings_.begin(), bindings_.end(), [](const auto& a, const auto& b) { return a->id < b->id; });
        empty_ = &tree->Emplace<LWSUI::Label>("No matching settings");
        empty_->SetHighlightPattern("");
        treeScroll_->SetContent(std::move(tree));
        ClearStatus();
        UpdateFilter();
        Invalidate(true);
    }
    SettingsView::Binding* SettingsView::FindBinding(std::string_view id) const
    {
        auto found = std::lower_bound(bindings_.begin(), bindings_.end(), id,
                                      [](const auto& binding, std::string_view key) { return binding->id < key; });
        return found != bindings_.end() && (*found)->id == id ? found->get() : nullptr;
    }
    void SettingsView::Refresh(const Change& change)
    {
        auto* binding = FindBinding(change.id);
        if (binding)
        {
            if (change.phase == EditPhase::Preview && !binding->editVisibility)
                binding->editVisibility = binding->row->Visible();
            if (change.phase != EditPhase::Preview) binding->editVisibility.reset();
        }
        if (valueFilter_ != SettingValueFilter::All && change.phase != EditPhase::Preview)
            RequestFilterUpdate();
        UpdateStatus();
        if (binding)
        {
            if (!binding->updating) binding->refresh(*document_.WorkingValue(binding->id));
            StyleChanged(*binding);
        }
    }
    void SettingsView::StyleChanged(Binding& binding)
    {
        const bool changed = document_.WorkingValue(binding.id) != document_.Find(binding.id)->DefaultValue();
        auto font = theme_.tree.font;
        if (changed) font.weight = std::max(font.weight, 700);
        // Only a weight transition needs layout; ordinary value edits keep the same font.
        if (binding.label->Font() != font)
        {
            auto style = theme_.tree;
            style.font = std::move(font);
            binding.label->SetStyle(std::move(style));
        }
        binding.label->SetForeground(changed ? std::optional{theme_.tree.changed} : std::nullopt);
        binding.editor->SetForeground(changed ? std::optional{theme_.controls.changed} : std::nullopt);
    }
    void SettingsView::UpdateGroupActions()
    {
        bool canExpand = false, canCollapse = false;
        if (!filterPending_ && filter_.empty() && valueFilter_ == SettingValueFilter::All)
            for (const auto& group : groups_)
                // Visible() is the branch's own flag, so nested groups beneath collapsed
                // parents still participate; empty groups hidden by UpdateFilter do not.
                if (group.branch->Visible())
                {
                    canExpand |= !group.branch->Expanded();
                    canCollapse |= group.branch->Expanded();
                }
        const std::array enabled{canExpand, canCollapse};
        for (size_t i = 0; i < groupButtons_.size(); ++i)
        {
            auto* button = groupButtons_[i];
            if (button->Enabled() != enabled[i]) button->SetEnabled(enabled[i]);
        }
    }
    void SettingsView::SetGroupsExpanded(bool expanded)
    {
        auto apply = [expanded](LWSUI::Control& control)
        {
            auto& view = static_cast<SettingsView&>(control);
            auto* action = view.groupButtons_[expanded ? 0 : 1];
            if (view.filterPending_ || !view.filter_.empty() ||
                view.valueFilter_ != SettingValueFilter::All || !action->Enabled()) return;
            if (view.Host() ? !view.Host()->Finish(LWSUI::EditPhase::Commit)
                            : !view.Finish(LWSUI::EditPhase::Commit)) return;
            // Finishing an edit can synchronously change the filter through a subscriber.
            if (view.filterPending_ || !view.filter_.empty() ||
                view.valueFilter_ != SettingValueFilter::All || !action->Enabled()) return;
            for (auto& group : view.groups_)
                if (group.branch->Visible() && group.branch->Expanded() != expanded)
                    group.branch->SetExpanded(expanded);
            view.UpdateGroupActions();
            view.Invalidate(true);
        };
        if (Host()) Host()->Post(Handle(), std::move(apply));
        else apply(*this);
    }
    void SettingsView::ClearSearch()
    {
        SetFilter("");
        if (Host())
            Host()->Focus(search_);
    }
    void SettingsView::SetFilter(std::string text)
    {
        if (search_->Text() != text)
            search_->SetText(text);
        if (Host())
            Host()->ClosePopup(LWSUI::EditPhase::Cancel);
        if (text.empty() && Host() && Host()->IsFocused(*clearSearch_))
            Host()->Focus(search_);
        clearSearch_->SetVisible(!text.empty());
        auto next = Lower(std::move(text));
        if (next == filter_)
            return;
        filter_ = std::move(next);
        tree_->SetHighlightPattern(filter_);
        descriptionTitle_->SetHighlightPattern(filter_);
        description_->SetHighlightPattern(filter_);
        location_->SetHighlightPattern(filter_);
        RequestFilterUpdate();
        treeScroll_->SetOffset(0);
    }
    void SettingsView::StyleFilterButtons()
    {
        for (size_t i = 0; i < filterButtons_.size(); ++i)
            if (auto* button = filterButtons_[i])
            {
                auto style = theme_.search;
                const bool active = i == size_t(valueFilter_);
                style.line = active ? style.selectedRow : style.surface;
                button->SetStyle(style);
                button->SetForeground(active ? style.accent : style.foreground);
            }
    }
    void SettingsView::SetValueFilter(SettingValueFilter filter)
    {
        if (filter == valueFilter_ || size_t(filter) >= filterButtons_.size()) return;
        if (Host() && !Host()->Finish(LWSUI::EditPhase::Commit)) return;
        valueFilter_ = filter;
        StyleFilterButtons();
        RequestFilterUpdate();
        treeScroll_->SetOffset(0);
    }
    void SettingsView::RequestFilterUpdate()
    {
        filterDirty_ = true;
        if (filterPending_) return;
        if (!Host()) { UpdateFilter(); return; }
        filterPending_ = true;
        UpdateGroupActions();
        // One pass after a burst of document notifications, outside control dispatch.
        Host()->Post(Handle(), [](LWSUI::Control& control)
        {
            auto& view = static_cast<SettingsView&>(control);
            view.filterPending_ = false;
            view.UpdateFilter();
        });
    }
    void SettingsView::UpdateFilter()
    {
        filterDirty_ = false;
        bool any = false;
        for (auto& binding : bindings_)
        {
            bool visible = true;
            if (!filter_.empty() || valueFilter_ != SettingValueFilter::All)
            {
                const auto value = *document_.WorkingValue(binding->id);
                const auto* definition = document_.Find(binding->id);
                if (valueFilter_ != SettingValueFilter::All)
                {
                    const bool changed = value != definition->DefaultValue();
                    visible = valueFilter_ == SettingValueFilter::Changed ? changed : !changed;
                }
                if (visible && !filter_.empty())
                {
                    std::string text = binding->search + " " + FormatValue(value);
                    if (auto* choice = std::get_if<ChoiceDefinition>(&definition->specification))
                        for (const auto& option : choice->options) text += " " + option.label;
                    if (std::holds_alternative<BooleanDefinition>(definition->specification))
                        text += std::get<bool>(value) ? " Enabled" : " Disabled";
                    visible = Lower(std::move(text)).find(filter_) != std::string::npos;
                }
            }
            if (!document_.IsEditing(binding->id)) binding->editVisibility.reset();
            if (valueFilter_ != SettingValueFilter::All && binding->editVisibility)
                visible = *binding->editVisibility;
            if (!visible && binding->row->Visible() && Host() &&
                (Host()->HasFocusWithin(*binding->row) || Host()->HasPopupWithin(*binding->row)))
            {
                // A reset/reload has already replaced the old popup's value. Do not
                // publish a stale preview or cancellation over that replacement.
                {
                    struct RestoreFlag { bool& flag; bool previous; ~RestoreFlag() { flag = previous; } };
                    RestoreFlag restore{reloadingValues_, reloadingValues_};
                    reloadingValues_ = true;
                    Host()->ClosePopup(LWSUI::EditPhase::Cancel);
                    binding->refresh(*document_.WorkingValue(binding->id));
                }
                if (!Host()->Focus(filterButtons_[size_t(valueFilter_)])) visible = true;
            }
            binding->row->SetVisible(visible);
            any |= visible;
        }
        const bool filtering = !filter_.empty() || valueFilter_ != SettingValueFilter::All;
        for (auto it = groups_.rbegin(); it != groups_.rend(); ++it)
        {
            bool visible = false;
            for (auto& child : it->branch->Content().Children()) visible |= child->Visible();
            if (!visible && Host() && Host()->HasFocusWithin(*it->branch))
                Host()->Focus(filterButtons_[size_t(valueFilter_)]);
            it->branch->SetVisible(visible);
            it->branch->RevealForSearch(filtering);
        }
        if (empty_) empty_->SetVisible(!any);
        UpdateGroupActions();
        Invalidate(true);
    }
    bool SettingsView::Finish(LWSUI::EditPhase phase)
    {
        if (!Container::Finish(phase))
        {
            validation_ = "Correct the invalid draft or press Escape to cancel it.";
            UpdateStatus();
            return false;
        }
        validation_.clear();
        UpdateStatus();
        return true;
    }
    bool SettingsView::OnPreviewInput(const LWSUI::Input& input)
    {
        if (Container::OnPreviewInput(input))
            return true;
        using Key = LWS::KeyCode;
        if (input.kind != LWSUI::InputKind::KeyDown)
            return false;
        if (input.key == Key::Escape && Host() && Host()->IsFocused(*search_))
        {
            ClearSearch();
            return true;
        }
        if (input.key == Key::Down && Host() && Host()->IsFocused(*search_))
        {
            for (auto& child : tree_->Children())
                if (child->Visible())
                {
                    if (auto* branch = dynamic_cast<LWSUI::TreeView::Branch*>(child.get()))
                        Host()->Focus(&branch->Header());
                    else
                        Host()->Focus(child.get());
                    break;
                }
            return true;
        }
        if (input.control && input.key == Key::F && Host())
        {
            Host()->Focus(search_);
            return true;
        }
        if (input.control && input.key == Key::S)
        {
            if (Host() ? Host()->Finish(LWSUI::EditPhase::Commit) : Finish(LWSUI::EditPhase::Commit))
            {
                Report(SaveChanges());
            }
            return true;
        }
        if (input.control && input.key == Key::Enter)
        {
            if (Host() ? Host()->Finish(LWSUI::EditPhase::Commit) : Finish(LWSUI::EditPhase::Commit))
                Report(document_.Apply());
            return true;
        }
        if (input.control && input.key == Key::R)
        {
            if (input.shift)
                Report(document_.ResetAll());
            else if (Host())
                for (auto& binding : bindings_)
                    if (Host()->HasFocusWithin(*binding->row))
                    {
                        Report(document_.Reset(binding->id));
                        break;
                    }
            return true;
        }
        return false;
    }
    LWSUI::Size SettingsView::OnMeasure(LWSUI::Size available)
    {
        UpdateGroupActions();
        UpdateStatus();
        surface_->Measure(available);
        return available;
    }
    void SettingsView::OnArrange()
    {
        surface_->Arrange(Bounds());
    }
}  // namespace LWSSettings
