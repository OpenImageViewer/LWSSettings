#include <LWSSettings/SettingsEditor.hpp>
#include <LWSSettings/SettingsStorage.hpp>
#include <LWSSettings/ThemeStorage.hpp>
#include <LWSSettings/ThemeEditor.hpp>
#include <LWSSettings/ThemeSelector.hpp>
#include <LWS/Platform.hpp>
#ifdef LWS_HAS_WIN32_BACKEND
#include <LWS/Win32/Platform.hpp>
#endif
#include <chrono>
#include <iostream>
#include <LWS/Timer.hpp>
#include <fstream>
#if LWSSETTINGS_ENABLE_EVENT_DEBUGGER
#include "EventDebugWindow.hpp"
#endif
#ifdef _WIN32
    #include <windows.h>
#endif
static void Diagnostic(const std::string& message, bool interactive, bool warning = false)
{
    std::cerr << message << '\n';
#ifdef _WIN32
    OutputDebugStringA((message + "\n").c_str());
    if (interactive)
        MessageBoxW(nullptr, LWSUI::NativeText(message).c_str(), L"LWSSettings",
                    MB_OK | (warning ? MB_ICONWARNING : MB_ICONERROR));
#else
    (void) interactive;
    (void) warning;
#endif
}
static void Snapshot(LWSSettings::SettingsView& view, const std::filesystem::path& path = "snapshot.ppm")
{
    LWSUI::Canvas canvas;
    auto* host = view.Host();
    if (!host) return;
    host->Update();
    const auto size = host->Window().GetClientAreaMetrics().logical;
    int width = size.x, height = size.y;
    canvas.Begin(width, height);
    canvas.Fill(0, 0, float(width), float(height), view.Style().background);
    view.Render(canvas);
    if (host->MainMenu()) host->MainMenu()->Render(canvas);
    auto pixels = canvas.End();
    std::ofstream output(path, std::ios::binary);
    output << "P6\n" << width << " " << height << "\n255\n";
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            auto* p = pixels.pixels.data() + y * pixels.rowPitch + x * 4;
            char rgb[] = {char(p[2]), char(p[1]), char(p[0])};
            output.write(rgb, 3);
        }
}
int main(int argc, char** argv)
{
    bool interactive = true;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--smoke" || arg == "--snapshot" || arg == "--export-theme" || arg == "--export-theme-template")
            interactive = false;
    }
#ifdef LWS_HAS_WIN32_BACKEND
    if (LWS::Win32::BootstrapProcess() != LWS::Result::Success)
    {
        Diagnostic("Cannot initialize the Windows process", interactive);
        return 1;
    }
#endif
    LWSSettings::EditorConfig config;
    config.document.valuesFile = "user-settings.json";
    config.saveValidationPolicy = LWSSettings::FormValidationPolicy::ShowDecisionDialog;
    config.closeValidationPolicy = LWSSettings::FormValidationPolicy::ShowDecisionDialog;
    int smokeMilliseconds = 0;
    bool snapshot = false;
    std::filesystem::path themeFile = "theme.json", exportTheme, exportTemplate;
    bool editTheme = false;
    bool explicitTheme = false;
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--icon" || arg == "--theme" || arg == "--export-theme" || arg == "--export-theme-template")
        {
            if (i + 1 == argc)
            {
                Diagnostic(arg + " requires a path", interactive);
                return 2;
            }
            if (arg == "--icon")
                config.iconPath = argv[++i];
            else if (arg == "--theme")
            {
                themeFile = argv[++i];
                explicitTheme = true;
            }
            else if (arg == "--export-theme-template")
                exportTemplate = argv[++i];
            else
                exportTheme = argv[++i];
        }
        else if (arg == "--no-live-resize")
            config.redrawOnResize = false;
        else if (arg == "--edit-theme")
            editTheme = true;
        else if (arg == "--staged")
            config.document.applyPolicy = LWSSettings::ApplyPolicy::OnApply;
        else if (arg == "--autosave")
            config.document.savePolicy = LWSSettings::SavePolicy::OnCommit;
        else if (arg == "--snapshot")
        {
            snapshot = true;
            smokeMilliseconds = 1500;
        }
        else if (arg == "--smoke")
            smokeMilliseconds = 1500;
    }
    if (!exportTemplate.empty())
    {
        auto result = LWSSettings::ThemeStorage::SaveTemplateFile(exportTemplate);
        if (!result)
        {
            Diagnostic(result.error().message, interactive);
            return 2;
        }
        return 0;
    }
    LWSSettings::ThemeSession themeSession(themeFile);
    std::error_code themeError;
    const bool themeExists = std::filesystem::exists(themeFile, themeError);
    if (themeError)
        Diagnostic("Theme warning: " + themeFile.string() + ": " + themeError.message() +
                   ". Using built-in defaults.", interactive, true);
    else if (themeExists)
    {
        auto loaded = themeSession.Load();
        if (loaded)
            config.theme = themeSession.Theme();
        else
            Diagnostic("Theme warning: " + loaded.error().location + ": " + loaded.error().message +
                       ". Using built-in defaults.", interactive, true);
    }
    else if (explicitTheme)
        Diagnostic("Theme warning: " + themeFile.string() + ": file does not exist. Using built-in defaults.",
                   interactive, true);
    if (!exportTheme.empty())
    {
        auto result = LWSSettings::ThemeStorage::SaveFile(exportTheme, config.theme);
        if (!result)
        {
            Diagnostic(result.error().location + ": " + result.error().message, interactive);
            return 2;
        }
        return 0;
    }
    LWS::PlatformContext platform;
    const LWS::PlatformConfig platformConfig{
#ifdef LWS_HAS_WIN32_BACKEND
        .backend = LWS::BackendId::Win32,
#else
        .backend = LWS::BackendId::Wayland,
#endif
    };
    if (platform.Init(platformConfig) != LWS::Result::Success)
    {
        Diagnostic("Cannot initialize the window platform", interactive);
        return 1;
    }
    LWSSettings::SettingsEditor editor(platform, config);
    auto& page = editor.Document();
    auto report = [interactive](const auto& result)
    {
        if (!result)
            Diagnostic(result.error().location + ": " + result.error().message, interactive);
        return bool(result);
    };
    if (!report(LWSSettings::SettingsStorage::LoadTemplateFile(page, "settings.json")))
        return 2;
    std::error_code valuesError;
    const bool valuesExist = std::filesystem::exists(config.document.valuesFile, valuesError);
    if (valuesError)
    {
        Diagnostic(config.document.valuesFile.string() + ": " + valuesError.message(), interactive);
        return 3;
    }
    if (valuesExist && !report(LWSSettings::SettingsStorage::LoadValuesFile(page, config.document.valuesFile)))
        return 3;
    bool errorReported = false;
    auto errors = page.OnError.Connect(
        [&](const LWSSettings::Error& error)
        {
            errorReported = true;
            Diagnostic(error.location + ": " + error.message, false);
        });
#if LWSSETTINGS_ENABLE_EVENT_DEBUGGER
    LWSSettings::Example::EventDebugWindow eventWindow(platform, config.theme.base,
        [&](const auto& error) { page.OnError.Raise(error); });
    auto subscription = page.OnChange.Connect([&](const auto& change) { eventWindow.Append(change); });
    LWSUI::Event<void(LWS::KeyCode)>::Connection themeHotkey;
#endif
    if (!report(editor.Show()))
        return 4;
    std::unique_ptr<LWSSettings::ThemeEditor> themeEditor;
    auto preview = themeSession.OnChange.Connect([&] { report(editor.SetTheme(themeSession.Theme())); });
    LWSUI::Event<void(const LWSSettings::Error&)>::Connection themeErrors;
    auto lifetime = std::make_shared<int>(0);
    bool themePending = false;
    auto openTheme = [&]
    {
        if (themePending) return;
        themePending = true;
        if (platform.PostTask([&, weak = std::weak_ptr(lifetime)]
        {
            if (weak.expired()) return;
            themePending = false;
            if (!editor.IsVisible()) return;
            if (!themeEditor)
            {
                auto candidate = std::make_unique<LWSSettings::ThemeEditor>(platform, themeSession, config.iconPath);
                candidate->SetRedrawOnResize(config.redrawOnResize);
                auto loaded = candidate->Load();
                if (report(loaded))
                {
                    themeErrors = candidate->OnError.Connect(
                        [&](const auto& error) { Diagnostic(error.location + ": " + error.message, false); });
#if LWSSETTINGS_ENABLE_EVENT_DEBUGGER
                    themeHotkey = candidate->OnKeyDown.Connect([&](LWS::KeyCode key)
                    { if (key == LWS::KeyCode::F8) eventWindow.RequestToggle(); });
#endif
                    themeEditor = std::move(candidate);
                }
            }
            if (themeEditor)
                report(themeEditor->Show());
        }) != LWS::Result::Success)
        {
            themePending = false;
            errorReported = true;
            platform.RequestQuit();
        }
    };
    editor.View()->HeaderActions().Emplace<LWSSettings::ThemeSelector>(themeSession,
        [&] { return themeEditor ? themeEditor->Save() : themeSession.Save(); },
        [&] { if (themeEditor) themeEditor->Revert(); else themeSession.Revert(); }, openTheme);
    auto hotkey = editor.OnKeyDown.Connect([&](LWS::KeyCode key)
    {
        if (key == LWS::KeyCode::F6) openTheme();
#if LWSSETTINGS_ENABLE_EVENT_DEBUGGER
        if (key == LWS::KeyCode::F8) eventWindow.RequestToggle();
#endif
    });
    auto hidden = editor.OnHidden.Connect([&] { platform.RequestQuit(); });
    if (editTheme) openTheme();
    LWS::HighPrecisionTimer smokeExit(platform, [&] { platform.RequestQuit(); });
    if (smokeMilliseconds)
    {
        smokeExit.SetDueTime(smokeMilliseconds);
        smokeExit.SetRepeatInterval(0);
        smokeExit.Enable(true);
    }
    if (platform.RunMessageLoop() == LWS::LoopResult::Failed) return 5;
    if (snapshot && themeEditor && themeEditor->Editor().View())
        Snapshot(*themeEditor->Editor().View(), "theme-editor.ppm");
    if (themeEditor)
        themeEditor->Hide();
    if (snapshot)
        Snapshot(*editor.View());
    if (errorReported)
        return 5;
    if (page.Persistence().error)
    {
        Diagnostic(page.Persistence().error->message, false);
        return 5;
    }
}
