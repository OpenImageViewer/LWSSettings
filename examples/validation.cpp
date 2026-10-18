#include <LWSSettings/SettingsEditor.hpp>
#include <LWS/Platform.hpp>
#include <LWS/Timer.hpp>
#ifdef LWS_HAS_WIN32_BACKEND
#include <LWS/Win32/Platform.hpp>
#endif
#include <iostream>

int main(int argc, char** argv)
{
#ifdef LWS_HAS_WIN32_BACKEND
    if (LWS::Win32::BootstrapProcess() != LWS::Result::Success) return 1;
#endif
    LWS::PlatformContext platform;
    if (platform.Init({
#ifdef LWS_HAS_WIN32_BACKEND
        .backend = LWS::BackendId::Win32
#else
        .backend = LWS::BackendId::Wayland
#endif
    }) != LWS::Result::Success) return 1;
    LWSSettings::EditorConfig config;
    config.title = "Validation playground";
    config.size = {960, 900};
    config.saveValidationPolicy = LWSSettings::FormValidationPolicy::ShowDecisionDialog;
    config.closeValidationPolicy = LWSSettings::FormValidationPolicy::ShowDecisionDialog;
    config.document.applyPolicy = LWSSettings::ApplyPolicy::OnApply;
    config.document.valuesFile = "validation-demo-values.json";
    LWSSettings::SettingsEditor editor(platform, config);
    std::vector<LWSSettings::Definition> fields;
    const char* ids[] = {"block", "discard", "keep"};
    const char* labels[] = {"1. Block", "2. Discard", "3. Keep invalid"};
    const char* descriptions[] = {
        "Enter 999, then try Tab or another control. Navigation is blocked with an explanation. Closing follows the selected close policy.",
        "Enter 999, then leave this field. The previous valid value is restored automatically.",
        "Enter 999, then leave this field. Your draft stays visible. Save, Apply and Close follow their form policies."
    };
    for (unsigned i = 0; i < 3; ++i)
    {
        LWSSettings::SettingDefinition field;
        field.id = ids[i];
        field.label = labels[i];
        field.description = std::string(descriptions[i]) +
            " Valid range: 1 to 100. Try typing a letter to see filter feedback.";
        field.specification = LWSSettings::IntegerDefinition{.defaultValue = 25, .minimum = 1, .maximum = 100};
        fields.push_back({std::move(field)});
    }
    // Keep invalid drafts visible so several errors can coexist.
    LWSSettings::SettingDefinition decimal;
    decimal.id = "ratio";
    decimal.label = "4. Decimal from 0 to 1";
    decimal.description = "Try 1.5 or a lone minus sign, then leave. Save lists this error together with the other invalid fields. Letters are rejected with feedback.";
    decimal.specification = LWSSettings::DecimalDefinition{.defaultValue = 0.5, .minimum = 0., .maximum = 1., .step = 0.1};
    fields.push_back({std::move(decimal)});
    LWSSettings::SettingDefinition color;
    color.id = "color";
    color.label = "5. Incomplete color";
    color.description = "Select all and enter #12. The incomplete draft can lose focus but cannot be saved. Escape restores the color. Double-click switches hex and decimal.";
    color.specification = LWSSettings::ColorDefinition{LLUtils::Color{uint32_t{0x3399ffff}}};
    fields.push_back({std::move(color)});
    LWSSettings::SettingDefinition text;
    text.id = "notes";
    text.label = "6. Unrestricted text";
    text.description = "This field accepts letters and numbers. Use it as a navigation target while comparing the validation policies.";
    text.specification = LWSSettings::TextDefinition{"Try another field, then return here"};
    fields.push_back({std::move(text)});
    if (!editor.Document().ReplaceDefinitions(std::move(fields))) return 2;
    if (!editor.Show()) return 3;

    editor.View()->SetFieldInvalidEditPolicy("block", LWSUI::InvalidEditPolicy::BlockOnLeave);
    editor.View()->SetFieldInvalidEditPolicy("discard", LWSUI::InvalidEditPolicy::DiscardOnLeave);
    editor.View()->SetFieldInvalidEditPolicy("keep", LWSUI::InvalidEditPolicy::KeepInvalid);
    auto& sections = editor.View()->OperationOptions().Emplace<LWSUI::Grid>();
    sections.columns = {-1, -1};
    LWSUI::ConnectionSet policyConnections;
    auto section = [&](bool closing)
    {
        auto& panel = sections.Emplace<LWSUI::StackPanel>();
        auto& heading = panel.Emplace<LWSUI::Label>(closing ? "Close form" : "Save / Apply form");
        heading.fontRole = LWSUI::Control::FontRole::Heading;
        auto& mode = panel.Emplace<LWSUI::RadioGroup>(std::vector<LWSUI::Choice>{
            {closing ? "Save valid fields silently and close" : "Save valid fields silently", "silent"},
            {"Show decision dialog", "dialog"}});
        mode.SetValue("dialog");
        auto& save = panel.Emplace<LWSUI::CheckBox>(closing ? "Allow save valid fields and close" : "Allow save valid fields");
        save.SetValue(true);
        LWSUI::CheckBox* discard = nullptr;
        if (closing)
        {
            discard = &panel.Emplace<LWSUI::CheckBox>("Allow discard all and close");
            discard->SetValue(true);
        }
        auto update = [&, closing, mode = &mode, save = &save, discard]
        {
            const bool dialog = mode->Value() == "dialog";
            const auto policy = dialog ? LWSSettings::FormValidationPolicy::ShowDecisionDialog
                                       : LWSSettings::FormValidationPolicy::SaveValidFields;
            const LWSSettings::ValidationDialogOptions options{save->Value(), discard && discard->Value()};
            save->SetEnabled(dialog);
            if (discard) discard->SetEnabled(dialog);
            if (closing) { editor.SetCloseValidationPolicy(policy); editor.SetCloseDialogOptions(options); }
            else { editor.SetSaveValidationPolicy(policy); editor.SetSaveDialogOptions(options); }
        };
        policyConnections.Connect(mode.OnChange, [update](const std::string&) { update(); });
        policyConnections.Connect(save.OnChange, [update](bool) { update(); });
        if (discard) policyConnections.Connect(discard->OnChange, [update](bool) { update(); });
        update();
        panel.Emplace<LWSUI::Label>(closing ? "Cancel close is always available" : "Keep editing is always available");
    };
    section(false);
    section(true);
    auto hidden = editor.OnHidden.Connect([&] { platform.RequestQuit(); });
    auto errors = editor.Document().OnError.Connect([](const auto& error) { std::cerr << error.message << '\n'; });
    LWS::HighPrecisionTimer smoke(platform, [&] { platform.RequestQuit(); });
    if (argc > 1 && std::string_view(argv[1]) == "--smoke")
    {
        smoke.SetDueTime(300);
        smoke.SetRepeatInterval(0);
        smoke.Enable(true);
    }
    return platform.RunMessageLoop() == LWS::LoopResult::Failed ? 4 : 0;
}
