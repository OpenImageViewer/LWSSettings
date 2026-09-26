#pragma once
#include <LWSSettings/SettingsDocument.hpp>
#include <nlohmann/json_fwd.hpp>
namespace LWSSettings
{
    // JSON is confined to this boundary. Loading validates a complete replacement
    // before touching the document. Unknown value IDs are ignored for profile evolution.
    class SettingsStorage
    {
      public:

        static Status LoadTemplate(SettingsDocument&, const nlohmann::json&);
        static Status LoadTemplateFile(SettingsDocument&, const std::filesystem::path&);
        static Status LoadValues(SettingsDocument&, const nlohmann::json&);
        static Status LoadValuesFile(SettingsDocument&, const std::filesystem::path&);
        static nlohmann::json EncodeValues(const Values&);
        static Status Write(const std::filesystem::path&, const Values&);
    };
}  // namespace LWSSettings
