#pragma once
#include <LLUtils/Color.h>
#include <LWSUI/Event.hpp>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <deque>
#include <optional>
#include <string>
#include <variant>
namespace LWSSettings
{
    using Value = std::variant<bool, int64_t, double, std::string, LLUtils::Color>;
    using Values = std::map<std::string, Value, std::less<>>;
    struct Error
    {
        std::string location, message;
    };
    using Status = std::expected<void, Error>;
    enum class NumericPresentation { Numeric, Slider };
    template <class T>
    struct NumericDefinition
    {
        T defaultValue{};
        std::optional<T> minimum, maximum;
        T step = T{1};
        NumericPresentation presentation = NumericPresentation::Numeric;
    };
    using IntegerDefinition = NumericDefinition<int64_t>;
    using DecimalDefinition = NumericDefinition<double>;
    struct TextDefinition
    {
        std::string defaultValue;
    };
    struct FileDefinition
    {
        std::string defaultValue;
    };
    struct BooleanDefinition
    {
        bool defaultValue = false;
    };
    struct ColorDefinition
    {
        LLUtils::Color defaultValue;
    };
    struct Option
    {
        std::string label, value;
    };
    enum class ChoicePresentation
    {
        Combo,
        Radio
    };
    struct ChoiceDefinition
    {
        std::string defaultValue;
        std::vector<Option> options;
        ChoicePresentation presentation = ChoicePresentation::Combo;
    };
    struct Metadata
    {
        std::string id, label, description;
    };
    struct SettingDefinition : Metadata
    {
        std::variant<TextDefinition, FileDefinition, BooleanDefinition, ColorDefinition, IntegerDefinition,
                     DecimalDefinition, ChoiceDefinition>
            specification;
        // C++ adapters may keep private state in the document without creating UI.
        // Ordinary JSON template settings are visible by default.
        bool visibleInEditor = true;
        Value DefaultValue() const;
    };
    struct Definition;
    struct GroupDefinition : Metadata
    {
        std::vector<Definition> children;
    };
    struct Definition
    {
        std::variant<GroupDefinition, SettingDefinition> content;
        const Metadata& Info() const;
    };
    enum class ApplyPolicy
    {
        Immediate,
        OnApply
    };
    enum class SavePolicy
    {
        Manual,
        OnCommit
    };
    enum class EditPhase
    {
        Preview,
        Commit,
        Cancel
    };
    enum class ChangeOrigin
    {
        User,
        Programmatic,
        Reset,
        Load,
        Cancel
    };
    enum class ValueDomain
    {
        Working,
        Applied
    };
    struct Change
    {
        std::string id;
        Value oldValue, newValue;
        EditPhase phase;
        ChangeOrigin origin;
        ValueDomain domain;
    };
    struct DocumentConfig
    {
        ApplyPolicy applyPolicy = ApplyPolicy::Immediate;
        SavePolicy savePolicy = SavePolicy::Manual;
        std::filesystem::path valuesFile;
        // Optional boundary serializer; empty keeps ordinary settings JSON persistence.
        std::function<Status(const std::filesystem::path&, const Values&)> writeValues;
    };
    enum class PersistenceState
    {
        NeverSaved,
        Saved,
        Failed
    };
    struct PersistenceStatus
    {
        PersistenceState state = PersistenceState::NeverSaved;
        std::optional<Error> error;
    };
    std::string FormatValue(const Value& value);
    std::expected<Value, Error> Validate(const SettingDefinition& definition, Value value);
    // UI-thread confined, but independent of windows. Typed mutations share Validate.
    // Preview starts an edit; Commit/Cancel ends it. Commit reports the edit-wide old value.
    // Notifications are queued after complete snapshot updates. Callbacks may update values;
    // replacing definitions/values during OnChange dispatch is rejected. Callbacks must not destroy
    // the document. Connections may outlive it. Automatic save failures do not reject edits.
    class SettingsDocument
    {
      public:

        explicit SettingsDocument(DocumentConfig config = {}) : config_(std::move(config)) {}
        SettingsDocument(const SettingsDocument&) = delete;
        SettingsDocument& operator=(const SettingsDocument&) = delete;
        const DocumentConfig& Config() const { return config_; }
        const std::vector<Definition>& Definitions() const { return definitions_; }
        const SettingDefinition* Find(std::string_view id) const;
        Status ReplaceDefinitions(std::vector<Definition> definitions);
        Status ReplaceValues(Values values);
        std::optional<Value> WorkingValue(std::string_view id) const;
        std::optional<Value> AppliedValue(std::string_view id) const;
        const Values& WorkingValues() const { return working_; }
        Status SetValue(std::string_view id, Value value, EditPhase phase = EditPhase::Commit,
                        ChangeOrigin origin = ChangeOrigin::Programmatic);
        Status Reset(std::string_view id);
        Status ResetAll();
        Status Apply();
        void Cancel();
        void RevertSaved();
        Status Save();
        bool IsEditing(std::string_view id) const { return editStarts_.contains(id); }
        bool IsDirty() const { return working_ != saved_; }
        bool HasPendingChanges() const { return working_ != applied_; }
        const PersistenceStatus& Persistence() const { return persistence_; }
        LWSUI::Event<void(const Change&)> OnChange;
        // Includes no-op confirmations, so adapters can finish associated edit sessions.
        LWSUI::Event<void(const std::string&, EditPhase, ChangeOrigin)> OnEditFinished;
        LWSUI::Event<void(const Error&)> OnError;
        // Definition changes rebuild controls; value reloads silently refresh existing controls.
        // Replacing definitions while a view is attached changes its tree: UI callbacks must
        // defer that operation with UIHost::Post, just like other structural UI mutations.
        LWSUI::Event<void()> OnReload;
        LWSUI::Event<void()> OnValuesReload;
        LWSUI::Event<void()> OnPersistenceChanged;
      private:
        friend class SettingsView;
        friend class ThemeEditor;
        // Private adapter boundary: per-field notifications remain intact, but
        // complete-theme synchronization waits until the outer reset finishes.
        bool resetting_ = false;
        LWSUI::Event<void()> OnResetFinished;
        // Binding-only refresh for silent/no-op external assignments.
        LWSUI::Event<void(const std::string&)> OnRefresh;


        // Shared mutation path after validation; notification and persistence belong to the caller.
        bool SetValidatedValue(const std::string& id, Value value, EditPhase phase, ChangeOrigin origin);
        Status ResetValues(Values values);
        void QueueDifferences(const Values& before, const Values& after, EditPhase phase, ChangeOrigin origin, ValueDomain domain);
        void Notify();
        void AutoSave();
        Status Persist();
        DocumentConfig config_;
        std::vector<Definition> definitions_;
        std::map<std::string, const SettingDefinition*, std::less<>> index_;
        Values working_, applied_, saved_, editStarts_;
        PersistenceStatus persistence_;
        std::deque<Change> notifications_;
        bool notifying_ = false;
    };
}  // namespace LWSSettings
