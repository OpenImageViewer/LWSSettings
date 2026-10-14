#pragma once
#include <LWSSettings/SettingsDocument.hpp>
#include <nlohmann/json.hpp>
namespace LWSSettings::Detail
{
    std::expected<nlohmann::json, Error> ReadJsonFile(const std::filesystem::path& path);
    Status WriteJsonFile(const std::filesystem::path& path, const nlohmann::json& document);
}
