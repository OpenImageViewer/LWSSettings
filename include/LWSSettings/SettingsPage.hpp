#pragma once
#include <LWS/Window.hpp>
#include <LLUtils/Event.h>
#include <nlohmann/json.hpp>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <variant>

namespace LWSSettings
{
    using Value = std::variant<bool, int64_t, double, std::string, LLUtils::Color>;
    struct Error
    {
        std::string location;
        std::string message;
    };
    using Status = std::expected<void, Error>;
    enum class Workflow
    {
        ImmediateManualSave,
        ImmediateAutoSave,
        Staged
    };
    enum class ChangeKind
    {
        Live,
        Confirmed
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
        Effective
    };
    struct Change
    {
        std::string id;
        Value oldValue;
        Value newValue;
        ChangeKind kind;
        ChangeOrigin origin;
        ValueDomain domain;
    };
    struct Appearance
    {
        float scale = 1.0f;
        float fontSize = 16.0f;  // Body font in logical pixels at 96 DPI.
        LLUtils::Color nonDefaultForeground{uint32_t{0xf2c66dff}};
        LLUtils::Color background{uint32_t{0x171c25ff}};
        LLUtils::Color foreground{uint32_t{0xe5eaf2ff}};
        LLUtils::Color accent{uint32_t{0x66b7ffff}};
    };
    struct SettingsConfig
    {
        Workflow workflow = Workflow::ImmediateManualSave;
        std::filesystem::path valuesFile;
        std::string title = "Settings";
        LWS::Window* owner = nullptr;  // Must outlive this page.
        LWS::Size size{820, 680};
        Appearance appearance;
    };

    // UI-thread confined. Construction and data operations do not require a window.
    // Connections must be destroyed before their SettingsPage; callbacks must not destroy the page.
    class SettingsPage
    {
      public:

        explicit SettingsPage(SettingsConfig config = {});
        ~SettingsPage();
        SettingsPage(const SettingsPage&) = delete;
        SettingsPage& operator=(const SettingsPage&) = delete;
        using Connection = LLUtils::Event<void(const Change&)>::Connection;
        [[nodiscard]] Connection OnChange(std::function<void(const Change&)> callback);
        [[nodiscard]] Status LoadTemplate(const nlohmann::json& document);
        [[nodiscard]] Status LoadTemplateFile(const std::filesystem::path& file);
        [[nodiscard]] Status LoadValues(const nlohmann::json& document);
        [[nodiscard]] Status LoadValuesFile(const std::filesystem::path& file);
        [[nodiscard]] nlohmann::json ExportValues() const;
        [[nodiscard]] std::optional<Value> GetValue(std::string_view id) const;
        [[nodiscard]] std::optional<Value> GetEffectiveValue(std::string_view id) const;
        [[nodiscard]] Status SetValue(std::string_view id, Value value);
        [[nodiscard]] Status Reset(std::string_view id);
        [[nodiscard]] Status ResetAll();
        [[nodiscard]] Status Apply();
        void Cancel();
        [[nodiscard]] Status Save();
        [[nodiscard]] bool IsDirty() const;
        [[nodiscard]] bool HasPendingChanges() const;
        [[nodiscard]] const std::optional<Error>& LastError() const;
        [[nodiscard]] Status Show();
        void Hide();
        [[nodiscard]] bool IsVisible() const;
        void SetFilter(std::string text);

      private:

        friend struct SettingsPageTestAccess;
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}  // namespace LWSSettings
