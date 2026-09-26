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
#include "EventLogging.hpp"
#ifdef _WIN32
    #include <windows.h>
#endif
static void Snapshot(LWSSettings::SettingsView& view, const std::filesystem::path& path = "snapshot.ppm")
{
    LWSUI::Canvas canvas;
    auto bounds = view.Bounds();
    int width = int(bounds.width), height = int(bounds.height);
    canvas.Begin(width, height);
    canvas.Fill(0, 0, float(width), float(height), view.Style().background);
    view.Render(canvas);
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
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    LWSSettings::EditorConfig config;
    config.document.valuesFile = "user-settings.json";
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
                std::cerr << arg << " requires a path\n";
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
            std::cerr << result.error().message << '\n';
            return 2;
        }
        return 0;
    }
    LWSSettings::ThemeSession themeSession(themeFile);
    if (explicitTheme || std::filesystem::exists(themeFile) || std::filesystem::exists("theme-default.json"))
    {
        auto loaded = themeSession.Load(explicitTheme ? std::filesystem::path{} : std::filesystem::path{"theme-default.json"});
        if (loaded)
            config.theme = themeSession.Theme();
        else
            std::cerr << "Theme warning: " << loaded.error().location << ": " << loaded.error().message
                      << "; using built-in defaults.\n";
    }
    if (!exportTheme.empty())
    {
        auto result = LWSSettings::ThemeStorage::SaveFile(exportTheme, config.theme);
        if (!result)
        {
            std::cerr << result.error().location << ": " << result.error().message << '\n';
            return 2;
        }
        return 0;
    }
#ifdef LWS_HAS_WIN32_BACKEND
    if (LWS::Win32::BootstrapProcess() != LWS::Result::Success) return 1;
#endif
    LWS::PlatformContext platform;
    const LWS::PlatformConfig platformConfig{
#ifdef LWS_HAS_WIN32_BACKEND
        .backend = LWS::BackendId::Win32,
#else
        .backend = LWS::BackendId::Wayland,
#endif
    };
    if (platform.Init(platformConfig) != LWS::Result::Success)
        return 1;
    LWSSettings::SettingsEditor editor(platform, config);
    auto& page = editor.Document();
    auto report = [](const auto& result)
    {
        if (!result)
            std::cerr << result.error().location << ": " << result.error().message << '\n';
        return bool(result);
    };
    if (!report(LWSSettings::SettingsStorage::LoadTemplateFile(page, "settings.json")))
        return 2;
    const auto initialValues = std::filesystem::exists(config.document.valuesFile)
                                   ? config.document.valuesFile
                                   : std::filesystem::path("showcase-values.json");
    if (!report(LWSSettings::SettingsStorage::LoadValuesFile(page, initialValues)))
        return 3;
    std::cout << "LWSSettings: live settings events (preview / commit / cancel) are printed below." << std::endl;
    auto subscription = page.OnChange.Connect([](const LWSSettings::Change& change)
                                              { LWSSettings::Example::PrintEvent(std::cout, change); });
    bool errorReported = false;
    auto errors = page.OnError.Connect(
        [&](const LWSSettings::Error& error)
        {
            errorReported = true;
            std::cerr << error.location << ": " << error.message << '\n';
        });
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
                auto loaded = candidate->Load("theme-settings.json");
                if (report(loaded))
                {
                    themeErrors = candidate->OnError.Connect(
                        [&](const auto& error) { std::cerr << error.location << ": " << error.message << '\n'; });
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
    });
    auto hidden = editor.OnHidden.Connect([&] { platform.RequestQuit(); });
    if (editTheme) openTheme();
    std::cout << "Press F6 to open the live theme editor. Theme edits preview immediately; Save writes the file."
              << std::endl;
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
        std::cerr << page.Persistence().error->message << '\n';
        return 5;
    }
}
