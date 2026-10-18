#include <LWSSettings/SettingsDocument.hpp>
#include "SliderRange.hpp"
#include <LWSUI/ScopedFlag.hpp>
#include <LWSSettings/SettingsStorage.hpp>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <set>
namespace LWSSettings
{
    Value SettingDefinition::DefaultValue() const
    {
        return std::visit([](const auto& s) -> Value { return s.defaultValue; }, specification);
    }
    const Metadata& Definition::Info() const
    {
        return std::visit([](const auto& d) -> const Metadata& { return d; }, content);
    }
    std::string FormatValue(const Value& value)
    {
        return std::visit(
            [](const auto& v) -> std::string
            {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::string>)
                    return v;
                else if constexpr (std::is_same_v<T, bool>)
                    return v ? "true" : "false";
                else if constexpr (std::is_same_v<T, LLUtils::Color>)
                {
                    std::ostringstream s;
                    s << '#' << std::hex << std::setfill('0');
                    for (auto c : v.channels)
                        s << std::setw(2) << int(c);
                    return s.str();
                }
                else
                {
                    char buffer[80];
                    auto [end, ec] = std::to_chars(buffer, buffer + sizeof(buffer), v);
                    return ec == std::errc{} ? std::string(buffer, end) : std::string{};
                }
            },
            value);
    }
    static bool Utf8(std::string_view text)
    {
        for (size_t i = 0; i < text.size();)
        {
            unsigned char c = text[i++];
            if (c < 128)
                continue;
            int count = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : -1;
            if (count < 0 || i + count > text.size())
                return false;
            unsigned char first = text[i];
            if ((c == 0xe0 && first < 0xa0) || (c == 0xed && first >= 0xa0) || (c == 0xf0 && first < 0x90) ||
                (c == 0xf4 && first >= 0x90))
                return false;
            while (count--)
                if ((static_cast<unsigned char>(text[i++]) & 0xc0) != 0x80)
                    return false;
        }
        return true;
    }
    std::expected<Value, Error> Validate(const SettingDefinition& definition, Value value)
    {
        auto bad = [&](std::string message) -> std::expected<Value, Error>
        { return std::unexpected(Error{definition.id, std::move(message)}); };
        if (std::holds_alternative<DecimalDefinition>(definition.specification) &&
            std::holds_alternative<int64_t>(value))
            value = double(std::get<int64_t>(value));
        return std::visit(
            [&](const auto& spec) -> std::expected<Value, Error>
            {
                using S = std::decay_t<decltype(spec)>;
                if (!std::holds_alternative<decltype(spec.defaultValue)>(value))
                    return bad("Wrong value type");
                if (auto* text = std::get_if<std::string>(&value); text && !Utf8(*text))
                    return bad("Expected valid UTF-8 text");
                if constexpr (std::is_same_v<S, IntegerDefinition> || std::is_same_v<S, DecimalDefinition>)
                {
                    auto number = std::get<decltype(spec.defaultValue)>(value);
                    if (!std::isfinite(double(number)))
                        return bad("Expected a finite number");
                    if (spec.minimum && number < *spec.minimum)
                        return bad("Value is below minimum");
                    if (spec.maximum && number > *spec.maximum)
                        return bad("Value exceeds maximum");
                }
                if constexpr (std::is_same_v<S, ChoiceDefinition>)
                {
                    for (const auto& option : spec.options)
                        if (option.value == std::get<std::string>(value))
                            return std::move(value);
                    return bad("Value is not one of the choices");
                }
                return std::move(value);
            },
            definition.specification);
    }
    const SettingDefinition* SettingsDocument::Find(std::string_view id) const
    {
        auto it = index_.find(id);
        return it == index_.end() ? nullptr : it->second;
    }
    std::optional<Value> SettingsDocument::WorkingValue(std::string_view id) const
    {
        auto it = working_.find(id);
        return it == working_.end() ? std::nullopt : std::optional<Value>{it->second};
    }
    std::optional<Value> SettingsDocument::AppliedValue(std::string_view id) const
    {
        auto it = applied_.find(id);
        return it == applied_.end() ? std::nullopt : std::optional<Value>{it->second};
    }
    Status SettingsDocument::ReplaceDefinitions(std::vector<Definition> definitions)
    {
        if (notifying_ || completingEdits_)
            return std::unexpected(Error{"template", "Cannot replace definitions from a notification"});
        std::map<std::string, const SettingDefinition*, std::less<>> index;
        std::set<std::string> ids;
        Values values;
        auto walk = [&](auto&& self, const std::vector<Definition>& list, int depth) -> Status
        {
            if (depth > 64)
                return std::unexpected(Error{"template", "Group nesting exceeds 64 levels"});
            for (const auto& item : list)
            {
                auto& info = item.Info();
                if (info.id.empty() || info.label.empty() || !Utf8(info.id) || !Utf8(info.label) ||
                    !Utf8(info.description) || !ids.insert(info.id).second)
                    return std::unexpected(Error{info.id, "Invalid or duplicate metadata"});
                if (auto* group = std::get_if<GroupDefinition>(&item.content))
                {
                    auto result = self(self, group->children, depth + 1);
                    if (!result)
                        return result;
                }
                else
                {
                    auto& setting = std::get<SettingDefinition>(item.content);
                    auto metadata = std::visit(
                        [&](const auto& spec) -> Status
                        {
                            using S = std::decay_t<decltype(spec)>;
                            if constexpr (std::is_same_v<S, IntegerDefinition> || std::is_same_v<S, DecimalDefinition>)
                            {
                                if (!std::isfinite(double(spec.step)) || spec.step <= 0 ||
                                    (spec.minimum && !std::isfinite(double(*spec.minimum))) ||
                                    (spec.maximum && !std::isfinite(double(*spec.maximum))) ||
                                    (spec.minimum && spec.maximum && *spec.minimum > *spec.maximum))
                                    return std::unexpected(Error{setting.id, "Invalid numeric bounds or step"});
                                if (spec.presentation == NumericPresentation::Slider)
                                {
                                    if (const auto* error = internal::SliderRangeError(spec))
                                        return std::unexpected(Error{setting.id, error});
                                }
                                else if (spec.presentation != NumericPresentation::Numeric)
                                    return std::unexpected(Error{setting.id, "Unknown numeric presentation"});
                            }
                            if constexpr (std::is_same_v<S, ChoiceDefinition>)
                            {
                                std::set<std::string> options;
                                if (spec.options.empty())
                                    return std::unexpected(Error{setting.id, "Choice requires options"});
                                for (const auto& o : spec.options)
                                    if (o.label.empty() || o.value.empty() || !Utf8(o.label) || !Utf8(o.value) ||
                                        !options.insert(o.value).second)
                                        return std::unexpected(Error{setting.id, "Invalid or duplicate choice"});
                            }
                            return {};
                        },
                        setting.specification);
                    if (!metadata)
                        return metadata;
                    auto value = Validate(setting, setting.DefaultValue());
                    if (!value)
                        return std::unexpected(value.error());
                    index.emplace(setting.id, &setting);
                    values.emplace(setting.id, *value);
                }
            }
            return {};
        };
        auto result = walk(walk, definitions, 0);
        if (!result)
            return result;
        definitions_ = std::move(definitions);
        index_ = std::move(index);
        working_ = applied_ = saved_ = std::move(values);
        editStarts_.clear();
        persistence_ = {};
        OnReload.Raise();
        return {};
    }
    Status SettingsDocument::ReplaceValues(Values values)
    {
        if (notifying_ || completingEdits_)
            return std::unexpected(Error{"values", "Cannot replace values from a notification"});
        Values replacement;
        for (const auto& [id, definition] : index_)
        {
            auto found = values.find(id);
            auto value = Validate(*definition, found == values.end() ? definition->DefaultValue() : found->second);
            if (!value)
                return std::unexpected(value.error());
            replacement[id] = *value;
        }
        working_ = applied_ = saved_ = std::move(replacement);
        editStarts_.clear();
        persistence_ = {};
        OnValuesReload.Raise();
        return {};
    }
    void SettingsDocument::Notify()
    {
        if (notifying_)
            return;
        notifying_ = true;
        try
        {
            while (!notifications_.empty())
            {
                auto change = std::move(notifications_.front());
                notifications_.pop_front();
                OnChange.Raise(change);
            }
        }
        catch (...)
        {
            notifying_ = false;
            notifications_.clear();
            throw;
        }
        notifying_ = false;
    }
    Status SettingsDocument::SetValue(std::string_view id, Value value, EditPhase phase, ChangeOrigin origin)
    {
        auto* definition = Find(id);
        if (!definition)
            return std::unexpected(Error{std::string(id), "Unknown setting"});
        auto checked = Validate(*definition, std::move(value));
        if (!checked)
            return std::unexpected(checked.error());
        const std::string key(id);
        const bool changed = SetValidatedValue(key, std::move(*checked), phase, origin);
        Notify();
        if (origin == ChangeOrigin::Programmatic || origin == ChangeOrigin::Reset)
            OnRefresh.Raise(key);
        if (phase != EditPhase::Preview)
            OnEditFinished.Raise(key, phase, origin);
        if (phase == EditPhase::Commit && config_.applyPolicy == ApplyPolicy::Immediate && changed)
            AutoSave();
        return {};
    }
    bool SettingsDocument::SetValidatedValue(const std::string& key, Value value, EditPhase phase, ChangeOrigin origin)
    {
        Value old = working_.at(key);
        if (phase == EditPhase::Preview)
            editStarts_.try_emplace(key, old);
        auto edit = editStarts_.find(key);
        Value start = edit == editStarts_.end() ? old : edit->second;
        if (phase == EditPhase::Cancel)
            value = start;
        if (phase != EditPhase::Preview)
            editStarts_.erase(key);
        working_[key] = value;
        bool immediate = config_.applyPolicy == ApplyPolicy::Immediate;
        if (immediate)
            applied_[key] = value;
        Value previous = phase == EditPhase::Commit ? start : old;
        // Replacing a preview is a new assignment; confirming its current value
        // still reports the edit-wide old value and triggers commit persistence.
        if (phase == EditPhase::Commit && old != value &&
            (origin == ChangeOrigin::Programmatic || origin == ChangeOrigin::Reset))
            previous = old;
        if (previous != value)
            notifications_.push_back(
                {key, previous, value, phase, origin, immediate ? ValueDomain::Applied : ValueDomain::Working});
        return previous != value;
    }
    Status SettingsDocument::ResetValues(Values values)
    {
        // A group reset is one operation: listeners see the complete snapshot and
        // autosave writes it once. Keep batching private until another caller needs it.
        for (auto& [id, value] : values)
        {
            auto checked = Validate(*Find(id), std::move(value));
            if (!checked)
                return std::unexpected(checked.error());
            value = std::move(*checked);
        }
        bool changed = false;
        const bool outermost = !resetting_;
        {
            LWSUI::ScopedFlag scope(resetting_, true);
            for (auto& [id, value] : values)
                changed |= SetValidatedValue(id, std::move(value), EditPhase::Commit, ChangeOrigin::Reset);
            Notify();
            // Refresh even unchanged defaults, because an editor may still hold an invalid draft.
            for (const auto& [id, value] : values)
            {
                OnRefresh.Raise(id);
                OnEditFinished.Raise(id, EditPhase::Commit, ChangeOrigin::Reset);
            }
        }
        if (outermost) OnResetFinished.Raise();
        if (changed && config_.applyPolicy == ApplyPolicy::Immediate)
            AutoSave();
        return {};
    }
    Status SettingsDocument::Reset(std::string_view id)
    {
        if (auto* setting = Find(id))
            return SetValue(id, setting->DefaultValue(), EditPhase::Commit, ChangeOrigin::Reset);
        const GroupDefinition* found = nullptr;
        auto search = [&](auto&& self, const auto& list) -> void
        {
            for (const auto& d : list)
                if (auto* group = std::get_if<GroupDefinition>(&d.content))
                {
                    if (group->id == id)
                        found = group;
                    self(self, group->children);
                }
        };
        search(search, definitions_);
        if (!found)
            return std::unexpected(Error{std::string(id), "Unknown setting or group"});
        Values defaults;
        auto collect = [&](auto&& self, const auto& list) -> void
        {
            for (const auto& d : list)
                if (auto* group = std::get_if<GroupDefinition>(&d.content))
                    self(self, group->children);
                else
                {
                    const auto& setting = std::get<SettingDefinition>(d.content);
                    defaults.emplace(setting.id, setting.DefaultValue());
                }
        };
        collect(collect, found->children);
        return ResetValues(std::move(defaults));
    }
    Status SettingsDocument::ResetAll()
    {
        Values defaults;
        for (const auto& [id, setting] : index_)
            defaults.emplace(id, setting->DefaultValue());
        return ResetValues(std::move(defaults));
    }
    void SettingsDocument::QueueDifferences(const Values& before, const Values& after, EditPhase phase,
                                            ChangeOrigin origin, ValueDomain domain)
    {
        for (const auto& [id, value] : after)
            if (before.at(id) != value) notifications_.push_back({id, before.at(id), value, phase, origin, domain});
    }
    Status SettingsDocument::Apply()
    {
        const auto previous = applied_;
        applied_ = working_;
        editStarts_.clear();
        QueueDifferences(previous, applied_, EditPhase::Commit, ChangeOrigin::User, ValueDomain::Applied);
        Notify();
        if (config_.savePolicy == SavePolicy::OnCommit)
            AutoSave();
        return {};
    }
    void SettingsDocument::Cancel()
    {
        auto previous = working_;
        bool immediate = config_.applyPolicy == ApplyPolicy::Immediate;
        if (immediate)
        {
            for (const auto& [id, start] : editStarts_)
                working_[id] = start;
            applied_ = working_;
        }
        else
            working_ = applied_;
        editStarts_.clear();
        QueueDifferences(previous, working_, EditPhase::Cancel, ChangeOrigin::Cancel,
                         immediate ? ValueDomain::Applied : ValueDomain::Working);
        Notify();
    }
    void SettingsDocument::RevertSaved()
    {
        const auto previous = working_, previousApplied = applied_;
        working_ = applied_ = saved_;
        editStarts_.clear();
        persistence_.error.reset();
        if (persistence_.state == PersistenceState::Failed)
            persistence_.state = PersistenceState::NeverSaved;
        QueueDifferences(previous, working_, EditPhase::Cancel, ChangeOrigin::Cancel,
                         config_.applyPolicy == ApplyPolicy::Immediate ? ValueDomain::Applied : ValueDomain::Working);
        if (config_.applyPolicy == ApplyPolicy::OnApply)
            QueueDifferences(previousApplied, applied_, EditPhase::Cancel, ChangeOrigin::Cancel, ValueDomain::Applied);
        Notify();
        OnValuesReload.Raise();
    }
    void SettingsDocument::AutoSave()
    {
        if (!autoSaveSuspended_ && config_.savePolicy == SavePolicy::OnCommit)
        {
            // Finishing a draft and then applying it can reach autosave twice.
            // Skip only a snapshot we actually saved; failed/never-saved writes still retry.
            if (persistence_.state == PersistenceState::Saved && applied_ == saved_)
                return;
            auto result = Persist();
            if (!result)
                OnError.Raise(result.error());
        }
    }
    Status SettingsDocument::Persist()
    {
        return PersistValues(applied_);
    }
    Status SettingsDocument::PersistValues(Values snapshot)
    {
        if (config_.valuesFile.empty())
        {
            Error error{"save", "No valuesFile was configured"};
            persistence_ = {PersistenceState::Failed, error};
            OnPersistenceChanged.Raise();
            return std::unexpected(error);
        }
        Status result;
        try
        {
            result = config_.writeValues ? config_.writeValues(config_.valuesFile, snapshot)
                                         : SettingsStorage::Write(config_.valuesFile, snapshot);
        }
        catch (const std::exception& error)
        {
            result = std::unexpected(Error{config_.valuesFile.string(), error.what()});
        }
        if (!result)
        {
            persistence_ = {PersistenceState::Failed, result.error()};
            OnPersistenceChanged.Raise();
            return result;
        }
        saved_ = std::move(snapshot);
        persistence_ = {PersistenceState::Saved, {}};
        OnPersistenceChanged.Raise();
        return {};
    }
    Status SettingsDocument::CompleteEdits(Completion completion, const std::vector<std::string>& discarded,
                                            const std::function<Status()>& prepare)
    {
        if (notifying_ || completingEdits_)
            return std::unexpected(Error{"edit", "Cannot complete edits during another document operation"});
        LWSUI::ScopedFlag completing(completingEdits_, true);
        const auto working = working_, applied = applied_, saved = saved_, starts = editStarts_;
        bool restored = false;
        const auto restore = [&]
        {
            if (std::exchange(restored, true)) return;
            const auto previousWorking = working_, previousApplied = applied_;
            working_ = working; applied_ = applied; saved_ = saved; editStarts_ = starts;
            notifications_.clear();
            QueueDifferences(previousWorking, working_, EditPhase::Cancel, ChangeOrigin::Load,
                             config_.applyPolicy == ApplyPolicy::Immediate ? ValueDomain::Applied : ValueDomain::Working);
            if (config_.applyPolicy == ApplyPolicy::OnApply)
                QueueDifferences(previousApplied, applied_, EditPhase::Cancel, ChangeOrigin::Load, ValueDomain::Applied);
            Notify();
            OnValuesReload.Raise();
        };
        const bool previousAutoSave = autoSaveSuspended_;
        LWSUI::ScopedFlag suspend(autoSaveSuspended_, true);
        try
        {
            if (prepare)
            {
                auto result = prepare();
                if (!result) { restore(); return result; }
            }
            if (completion == Completion::Discard)
            {
                RevertSaved();
                return {};
            }
            for (const auto& id : discarded)
            {
                const auto value = WorkingValue(id);
                if (!value) { restore(); return std::unexpected(Error{id, "Unknown discarded field"}); }
                auto result = SetValue(id, *value, EditPhase::Cancel, ChangeOrigin::User);
                if (!result) { restore(); return result; }
            }
            // Cancellation callbacks may finish companion edits. Commit only edits still pending.
            const auto pending = editStarts_;
            for (const auto& [id, start] : pending)
                if (IsEditing(id))
                {
                    auto result = SetValue(id, working_.at(id), EditPhase::Commit, ChangeOrigin::User);
                    if (!result) { restore(); return result; }
                }
            if (completion == Completion::Apply) autoSaveSuspended_ = previousAutoSave;
            auto result = Apply();
            if (result && completion == Completion::Save) result = Persist();
            if (!result && completion == Completion::Save) restore();
            if (result && completion == Completion::Apply && config_.savePolicy == SavePolicy::OnCommit && persistence_.error)
                return std::unexpected(*persistence_.error);
            return result;
        }
        catch (...)
        {
            restore();
            throw;
        }
    }
    Status SettingsDocument::Save()
    {
        return Persist();
    }
}  // namespace LWSSettings
