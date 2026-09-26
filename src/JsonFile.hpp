#pragma once
#include <LWSSettings/SettingsDocument.hpp>
#include <nlohmann/json.hpp>
namespace LWSSettings::Detail
{
    Status WriteJsonFile(const std::filesystem::path& path, const nlohmann::json& document);
}
