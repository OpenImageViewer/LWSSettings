#include <LWSSettings/SettingsPage.hpp>
#include <LWS/Platform.hpp>
#include <chrono>
#include <iostream>
#include <thread>
#include "EventLogging.hpp"
#ifdef _WIN32
    #include <windows.h>
#endif
int main(int argc, char** argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    LWS::Platform::Session platform;
    if (!platform)
        return 1;
    LWSSettings::SettingsConfig config;
    config.valuesFile = "user-settings.json";
    int smokeMilliseconds = 0;
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--staged")
            config.workflow = LWSSettings::Workflow::Staged;
        else if (arg == "--autosave")
            config.workflow = LWSSettings::Workflow::ImmediateAutoSave;
        else if (arg == "--smoke")
            smokeMilliseconds = 1500;
    }
    LWSSettings::SettingsPage page(config);
    auto report = [](const auto& result)
    {
        if (!result)
            std::cerr << result.error().location << ": " << result.error().message << '\n';
        return bool(result);
    };
    if (!report(page.LoadTemplateFile("settings.json")))
        return 2;
    const auto initialValues = std::filesystem::exists(config.valuesFile)
                                   ? config.valuesFile
                                   : std::filesystem::path("showcase-values.json");
    if (!report(page.LoadValuesFile(initialValues)))
        return 3;
    auto subscription = page.OnChange([](const LWSSettings::Change& change)
                                      { LWSSettings::Example::PrintEvent(std::cout, change); });
    if (!report(page.Show()))
        return 4;
    const auto start = std::chrono::steady_clock::now();
    while (page.IsVisible())
    {
        if (LWS::Platform::processMessages())
            break;
        if (smokeMilliseconds &&
            std::chrono::steady_clock::now() - start > std::chrono::milliseconds(smokeMilliseconds))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    if (page.LastError())
    {
        std::cerr << page.LastError()->message << '\n';
        return 5;
    }
}
