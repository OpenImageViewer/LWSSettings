#include <LWSSettings/SettingsView.hpp>
#include <LWSUI/UIHost.hpp>
#include "SettingsIcon.hpp"
#include "SliderValueEditor.hpp"
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
    namespace
    {
        class GroupActionButton final : public LWSUI::Button
        {
          public:
            explicit GroupActionButton(bool expand)
                : Button(expand ? "Expand all" : "Collapse all"), expand_(expand) { flat = true; }
            bool Focusable() const override { return false; }

          protected:
            bool OnInput(const LWSUI::Input& input) override
            {
                if (input.kind == LWSUI::InputKind::KeyDown || input.kind == LWSUI::InputKind::KeyUp) return false;
                return Button::OnInput(input);
            }
            void OnRender(LWSUI::Canvas& canvas) override
            {
                const auto b = Bounds();
                if (Enabled() && (pressed_ || Hovered()))
                    canvas.Fill(b.x, b.y, b.width, b.height, pressed_ ? Style().selectedRow : Style().hoverSurface);
                const auto color = Enabled() ? Foreground() : Style().muted;
                const float side = std::min(b.width, b.height) * .45f;
                const float left = b.x + (b.width - side) / 2;
                const float direction = expand_ ? 1.f : -1.f;
                const float stroke = std::max(1.f, Style().borderWidth);
                for (float offset : {-.23f, .23f})
                {
                    const float y = b.y + b.height / 2 + offset * side;
                    canvas.Line(left, y - direction * .18f * side,
                                left + side / 2, y + direction * .18f * side, stroke, color);
                    canvas.Line(left + side / 2, y + direction * .18f * side,
                                left + side, y - direction * .18f * side, stroke, color);
                }
            }

          private:
            bool expand_;
        };
        class ActionsBar : public LWSUI::Container
        {
          public:

            explicit ActionsBar(SettingsLayout layout) : layout_(layout) {}
            void SetLayout(SettingsLayout layout)
            {
                layout_ = layout;
                Invalidate(true);
            }

          protected:

            LWSUI::Size OnMeasure(LWSUI::Size available) override
            {
                float h = 0;
                for (auto& child : Children())
                    h = std::max(h, child->Measure(available).height);
                return {available.width, std::max(layout_.footerHeight, h + 2 * Style().padding)};
            }
            void OnArrange() override
            {
                auto b = Bounds();
                const float p = layout_.horizontalPadding;
                const float width = std::max(layout_.actionWidth, Font().size * 6);
                for (size_t i = 0; i < Children().size(); ++i)
                {
                    float x = i == 0 ? p
                                     : b.width - p - width - (width + layout_.actionGap) * (Children().size() - 1 - i);
                    Children()[i]->Arrange(
                        {b.x + x, b.y + Style().padding, width, std::max(0.f, b.height - 2 * Style().padding)});
                }
            }
            void OnRender(LWSUI::Canvas& canvas) override
            {
                auto b = Bounds();
                canvas.Fill(b.x, b.y, b.width, b.height, Style().background);
                Container::OnRender(canvas);
            }

          private:

            SettingsLayout layout_;
        };
    }  // namespace
    struct SettingsView::Binding
    {
        std::string id, search;
        LWSUI::TreeView::Row* row;
        LWSUI::Label* label;
        LWSUI::Control* editor;
        LWSUI::Button* reset;
        std::function<void(const Value&)> refresh;
        // Type-erased connection ownership, not type-erased control state.
        std::vector<std::shared_ptr<void>> connections;
        bool updating = false;
        std::optional<bool> editVisibility;
        std::unique_ptr<LWSUI::Control> CreateEditor(const SettingDefinition&, std::function<Status(Value, LWSUI::EditPhase)> edit);
        template <class E, class F>
        void Connect(E& event, F callback)
        {
            using C = typename E::Connection;
            connections.push_back(std::make_shared<C>(event.Connect(std::move(callback))));
        }
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
                    Connect(control.OnEdit,
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
                    Connect(control.OnEdit, [edit](const auto& value, auto phase) { edit(value, phase); });
                }
                else if constexpr (std::is_same_v<S, BooleanDefinition>)
                {
                    auto& control = own(std::make_unique<LWSUI::CheckBox>());
                    editor = &control;
                    refresh = [&control](const Value& v) { control.SetValue(std::get<bool>(v)); };
                    Connect(control.OnChange, [edit](bool value) { edit(value, LWSUI::EditPhase::Commit); });
                }
                else if constexpr (std::is_same_v<S, ColorDefinition>)
                {
                    auto& control = own(std::make_unique<LWSUI::ColorPicker>());
                    editor = &control;
                    refresh = [&control](const Value& v) { control.SetValue(std::get<LLUtils::Color>(v)); };
                    Connect(control.OnEdit, [edit](auto value, auto phase) { edit(value, phase); });
                }
                else if constexpr (std::is_same_v<S, IntegerDefinition> || std::is_same_v<S, DecimalDefinition>)
                {
                    using T = decltype(spec.defaultValue);
                    auto setup = [&](auto& control)
                    {
                        editor = &control;
                        refresh = [&control](const Value& v) { control.SetValue(std::get<T>(v)); };
                        Connect(control.OnEdit, [edit](T value, auto phase) { edit(value, phase); });
                    };
                    if (spec.presentation == NumericPresentation::Slider)
                        setup(own(std::make_unique<internal::SliderValueEditor<T>>(spec)));
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
                        Connect(control.OnChange,
                                [edit](const std::string& value) { edit(value, LWSUI::EditPhase::Commit); });
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
        icon_ = internal::DefaultSettingsIcon();
        SetStyle(theme_.base);
        headerActions_ = &Emplace<LWSUI::StackPanel>();
        headerActions_->orientation = LWSUI::Orientation::Horizontal;
        headerActions_->SetStyle(theme_.header);
        title_ = &Emplace<LWSUI::Label>(std::move(title));
        title_->SetStyle(theme_.header);
        title_->fontRole = FontRole::Title;
        saved_ = &Emplace<LWSUI::Label>();
        saved_->SetStyle(theme_.header);
        saved_->fontRole = FontRole::Small;
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
            clicks_.push_back(filterButtons_[i]->OnClick.Connect([this, i] { SetValueFilter(SettingValueFilter(i)); }));
        }
        StyleFilterButtons();
        for (size_t i = 0; i < groupButtons_.size(); ++i)
        {
            groupButtons_[i] = &Emplace<GroupActionButton>(i == 0);
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
        actions_ = &Emplace<ActionsBar>(theme_.layout);
        actions_->SetStyle(theme_.footer);
        actions_->SetHighlightPattern("");
        auto action =
            [&](std::string name, std::function<void()> callback, LWSUI::EditPhase phase = LWSUI::EditPhase::Commit)
        {
            auto& button = actions_->Emplace<LWSUI::Button>(name);
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
    }
    SettingsView::~SettingsView() = default;
    void SettingsView::SetTheme(const SettingsTheme& theme)
    {
        theme_ = theme;
        SetStyle(theme_.base);
        title_->SetStyle(theme_.header);
        saved_->SetStyle(theme_.header);
        headerActions_->SetStyle(theme_.header);
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
        static_cast<ActionsBar&>(*actions_).SetLayout(theme_.layout);
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
        saved_->SetText(dirty ? "Unsaved" : "Saved");
        saved_->SetForeground(dirty ? theme_.header.accent : theme_.header.muted);
        status_->SetText(!validation_.empty()       ? validation_
                         : !operationError_.empty() ? operationError_
                                                    : information_);
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
                binding->Connect(reset.OnClick, [this, id = setting.id] { Report(document_.Reset(id)); });
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
            if (Finish(LWSUI::EditPhase::Commit))
            {
                Report(SaveChanges());
            }
            return true;
        }
        if (input.control && input.key == Key::Enter)
        {
            if (Finish(LWSUI::EditPhase::Commit))
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
        const auto& l = theme_.layout;
        for (auto* c : std::array<LWSUI::Control*, 8>{title_, saved_, headerActions_, search_, clearSearch_, descriptionTitle_, status_, actions_})
            c->Measure(available);
        for (auto* button : filterButtons_) button->Measure(available);
        for (auto* button : groupButtons_) button->Measure(available);
        descriptionScroll_->Measure({std::max(1.f, available.width - 2 * l.horizontalPadding), l.descriptionHeight});
        locationScroll_->Measure({std::max(1.f, available.width - 2 * l.horizontalPadding), l.descriptionHeight});
        treeScroll_->Measure({available.width, available.height});
        return available;
    }
    void SettingsView::OnArrange()
    {
        UpdateGroupActions();
        const auto& l = theme_.layout;
        auto b = Bounds();
        UpdateStatus();
        const float p = l.horizontalPadding, inner = std::max(0.f, b.width - 2 * p);
        const float savedWidth = std::min(inner, std::max(l.actionWidth, theme_.header.smallFont.size * 8));
        const float iconWidth = icon_ ? title_->DesiredSize().height + theme_.header.padding : 0;
        const auto headerActions = headerActions_->DesiredSize();
        const float headerGap = headerActions.width > 0 ? l.sectionGap : 0;
        const float titleWidth = Host() ? Host()->MeasureText(title_->Text(), 10000, false, title_->Font()).width : 200.f;
        const bool wrapHeader = headerActions.width > 0 && inner < titleWidth + iconWidth + savedWidth + headerActions.width + headerGap;
        const float firstHeader = std::max(l.headerHeight, std::max(title_->DesiredSize().height,
            wrapHeader ? 0.f : headerActions.height) + 2 * theme_.header.padding);
        const float header = firstHeader + (wrapHeader ? headerActions.height + headerGap : 0);
        const auto buttonWidth = [&](const auto& buttons)
        {
            float width = 0;
            for (auto* button : buttons)
                width = std::max(width, Host() ? Host()->MeasureText(button->Text(), 10000, false, button->Font()).width
                                               : button->Text().size() * button->Font().size);
            return width + 2 * theme_.search.padding;
        };
        float searchRow = std::max({l.searchHeight, search_->DesiredSize().height,
                                    clearSearch_->DesiredSize().height});
        for (auto* button : filterButtons_) searchRow = std::max(searchRow, button->DesiredSize().height);
        const float filterWidth = buttonWidth(filterButtons_), groupWidth = searchRow;
        const float selectorWidth = 3 * filterWidth;
        const float gap = theme_.search.spacing;
        const float groupGap = std::min(gap, inner);
        const float actionsWidth = 2 * groupWidth + groupGap;
        const float controlsWidth = selectorWidth + gap + actionsWidth;
        const bool stacked = inner < controlsWidth + gap + 12 * search_->Font().size;
        const bool separateActions = inner < controlsWidth;
        const int rows = separateActions ? 3 : stacked ? 2 : 1;
        const float search = rows * searchRow + (rows - 1) * gap;
        const float footer = actions_->DesiredSize().height;
        const float status = status_->Text().empty() ? 0.f : std::max(l.statusHeight, status_->DesiredSize().height);
        const float title = descriptionTitle_->DesiredSize().height;
        const float locationWanted = locationScroll_->Visible() ? locationScroll_->DesiredSize().height : 0.f;
        const float locationMinimum = std::min(locationWanted,
            location_->Font().size * 1.4f + 2 * location_->Style().labelPadding);
        const float detailsMinimum = title + theme_.description.font.size * 1.4f + status + locationMinimum +
                                     2 * theme_.description.padding + l.descriptionTitleGap;
        const float availableBottom = std::max(0.f, b.height - header - search - l.sectionGap - footer);
        const float details = std::min(std::max(l.descriptionHeight, detailsMinimum),
                                       availableBottom * l.descriptionMaxFraction);
        const float treeTop = b.y + header + search + l.sectionGap;
        const float footerTop = std::max(treeTop, b.y + b.height - footer);
        const float descriptionTop = std::max(treeTop, footerTop - details);
        headerBounds_ = {b.x, b.y, b.width, header};
        searchBounds_ = {b.x, b.y + header, b.width, search + l.sectionGap};
        descriptionBounds_ = {b.x, descriptionTop, b.width, footerTop - descriptionTop};
        footerBounds_ = {b.x, footerTop, b.width, footer};
        title_->Arrange({b.x + p + iconWidth - theme_.header.labelPadding, b.y + theme_.header.padding, std::max(0.f, inner - savedWidth - iconWidth - (wrapHeader ? 0.f : headerActions.width + headerGap)),
                         title_->DesiredSize().height});
        saved_->Arrange({b.x + b.width - p - savedWidth, b.y + (firstHeader - saved_->DesiredSize().height) / 2, savedWidth,
                         saved_->DesiredSize().height});
        headerActions_->Arrange({wrapHeader ? b.x + p : b.x + b.width - p - savedWidth - headerActions.width - headerGap,
            wrapHeader ? b.y + firstHeader : b.y + (firstHeader - headerActions.height) / 2,
            std::min(inner, headerActions.width), headerActions.height});
        const float searchWidth = stacked ? inner : std::max(0.f, inner - controlsWidth - gap);
        const float clearWidth = clearSearch_->Visible() ? std::min(searchRow, searchWidth) : 0.f;
        // Reserve the trailing square so long queries cannot paint beneath the clear button.
        search_->Arrange({b.x + p, b.y + header, searchWidth - clearWidth, searchRow});
        clearSearch_->Arrange({b.x + p + searchWidth - clearWidth, b.y + header, clearWidth, searchRow});
        const float selectorX = stacked ? b.x + p : b.x + b.width - p - controlsWidth;
        const float selectorY = b.y + header + (stacked ? searchRow + gap : 0);
        const float segment = std::min(filterWidth, inner / 3);
        for (size_t i = 0; i < filterButtons_.size(); ++i)
            filterButtons_[i]->Arrange({selectorX + i * segment, selectorY, segment, searchRow});
        const float groupX = separateActions ? b.x + p : b.x + b.width - p - actionsWidth;
        const float groupY = selectorY + (separateActions ? searchRow + gap : 0);
        const float actionWidth = std::min(groupWidth, std::max(0.f, (inner - groupGap) / 2));
        for (size_t i = 0; i < groupButtons_.size(); ++i)
            groupButtons_[i]->Arrange({groupX + i * (actionWidth + groupGap), groupY, actionWidth, searchRow});
        treeScroll_->Arrange({b.x, treeTop, b.width, std::max(0.f, descriptionTop - treeTop)});
        const float headingHeight = std::min(title, std::max(0.f, details - 2 * theme_.description.padding));
        descriptionTitle_->Arrange({b.x + p - theme_.description.labelPadding,
                                     descriptionTop + theme_.description.padding, inner, headingHeight});
        const float bodyTop = std::min(footerTop, descriptionTop + theme_.description.padding +
                                                    headingHeight + l.descriptionTitleGap);
        const float contentHeight = std::max(0.f, footerTop - theme_.description.padding - bodyTop);
        const float statusHeight = std::min(status, contentHeight);
        // Keep the breadcrumb pinned below status. Deep paths scroll independently
        // and consume at most half the space otherwise available for the description.
        const float locationHeight = std::min(locationWanted, (contentHeight - statusHeight) / 2);
        const float locationTop = bodyTop + contentHeight - locationHeight;
        const float statusTop = locationTop - statusHeight;
        descriptionScroll_->Arrange({b.x + p - theme_.description.labelPadding, bodyTop, inner,
                                     std::max(0.f, statusTop - bodyTop)});
        status_->Arrange({b.x + p - theme_.description.labelPadding, statusTop, inner, statusHeight});
        locationScroll_->Arrange({b.x + p - theme_.description.labelPadding, locationTop, inner, locationHeight});
        actions_->Arrange(footerBounds_);
    }
    void SettingsView::OnRender(LWSUI::Canvas& canvas)
    {
        auto fill = [&](LWSUI::Rect r, LLUtils::Color color) { canvas.Fill(r.x, r.y, r.width, r.height, color); };
        fill(Bounds(), theme_.tree.background);
        fill(headerBounds_, theme_.header.background);
        fill(searchBounds_, theme_.search.background);
        fill(descriptionBounds_, theme_.description.background);
        fill(footerBounds_, theme_.footer.background);
        canvas.Fill(descriptionBounds_.x, descriptionBounds_.y, descriptionBounds_.width,
                    theme_.description.borderWidth, theme_.description.line);
        if (icon_)
        {
            const float side = title_->DesiredSize().height;
            canvas.Image(icon_->GetBuffer(), headerBounds_.x + theme_.layout.horizontalPadding,
                         headerBounds_.y + theme_.header.padding, side, side);
        }
        Container::OnRender(canvas);
        if (!Host() || Host()->HasPopup()) return;
        size_t hint = groupButtons_.size();
        for (size_t i = 0; i < groupButtons_.size(); ++i)
            if (groupButtons_[i]->Enabled() && groupButtons_[i]->Hovered()) { hint = i; break; }
        if (hint == groupButtons_.size()) return;
        // These local hints follow pointer hover only; actions never transfer focus.
        const std::string_view text = groupButtons_[hint]->Text();
        const auto bounds = Bounds(), button = groupButtons_[hint]->Bounds();
        const auto& style = theme_.popups;
        const float padding = style.padding;
        const float width = std::min(bounds.width, canvas.Measure(text, style.smallFont) + 2 * padding);
        const float height = canvas.TextHeight(text, std::max(1.f, width - 2 * padding), style.smallFont, false) + 2 * padding;
        const float x = std::clamp(button.x, bounds.x, bounds.x + std::max(0.f, bounds.width - width));
        float y = button.y + button.height + theme_.search.spacing;
        if (y + height > bounds.y + bounds.height) y = std::max(bounds.y, button.y - height - theme_.search.spacing);
        canvas.Fill(x, y, width, height, style.background);
        canvas.CenteredText(text, x + padding, y, std::max(0.f, width - 2 * padding), height,
                            style.foreground, style.smallFont);
    }
}  // namespace LWSSettings
