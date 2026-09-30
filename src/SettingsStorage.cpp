#include <LWSSettings/SettingsStorage.hpp>
#include <nlohmann/json.hpp>
#include "JsonFile.hpp"
#include "ValueCodec.hpp"
#include <cmath>
#include <fstream>
#ifdef _WIN32
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <unistd.h>
#endif
namespace LWSSettings
{
    using Json = nlohmann::json;
    namespace
    {
        struct Invalid
        {
            Error error;
        };
        [[noreturn]] void Reject(std::string location, std::string message)
        {
            throw Invalid{{std::move(location), std::move(message)}};
        }
        std::string Required(const Json& json, const char* key, const std::string& location)
        {
            if (!json.contains(key) || !json[key].is_string() || json[key].get_ref<const std::string&>().empty())
                Reject(location + "/" + key, "Expected a nonempty string");
            return json[key].get<std::string>();
        }
        Value DecodeValue(const Json& json, const SettingDefinition& definition)
        {
            if (std::holds_alternative<ColorDefinition>(definition.specification))
            {
                auto color = Detail::DecodeColor(json);
                if (!color)
                    Reject(definition.id, color.error());
                return *color;
            }
            if (std::holds_alternative<DecimalDefinition>(definition.specification) && json.is_number())
                return json.get<double>();
            if (json.is_boolean())
                return json.get<bool>();
            if (json.is_number_integer())
            {
                if (json.is_number_unsigned() && json.get<uint64_t>() > uint64_t(INT64_MAX))
                    Reject(definition.id, "Expected a signed 64-bit integer");
                return json.get<int64_t>();
            }
            if (json.is_number_float())
                return json.get<double>();
            if (json.is_string())
                return json.get<std::string>();
            Reject(definition.id, "Expected a scalar setting value");
        }
        template <class T>
        T Number(const Json& json, const std::string& location)
        {
            if constexpr (std::is_integral_v<T>)
            {
                if (!json.is_number_integer() ||
                    (json.is_number_unsigned() && json.get<uint64_t>() > uint64_t(INT64_MAX)))
                    Reject(location, "Expected a signed 64-bit integer");
            }
            else if (!json.is_number() || !std::isfinite(json.get<double>()))
                Reject(location, "Expected a finite number");
            return json.get<T>();
        }
        template <class T>
        NumericDefinition<T> Numeric(const Json& json, const std::string& location, T step)
        {
            NumericDefinition<T> spec;
            spec.step = step;
            if (json.contains("min"))
                spec.minimum = Number<T>(json["min"], location + "/min");
            if (json.contains("max"))
                spec.maximum = Number<T>(json["max"], location + "/max");
            if (json.contains("step"))
                spec.step = Number<T>(json["step"], location + "/step");
            if (json.contains("presentation"))
            {
                if (!json["presentation"].is_string())
                    Reject(location + "/presentation", "Expected numeric or slider");
                const auto presentation = json["presentation"].get<std::string>();
                if (presentation == "slider") spec.presentation = NumericPresentation::Slider;
                else if (presentation != "numeric") Reject(location + "/presentation", "Unknown numeric presentation");
            }
            return spec;
        }
        Definition ReadDefinition(const Json& json, const std::string& location, int depth)
        {
            if (depth > 64)
                Reject(location, "Group nesting exceeds 64 levels");
            if (!json.is_object())
                Reject(location, "Expected a setting or group object");
            Metadata metadata{Required(json, "id", location), Required(json, "label", location),
                              json.value("description", std::string{})};
            auto type = Required(json, "type", location);
            if (json.contains("presentation") && type != "number" && type != "integer")
                Reject(location + "/presentation", "Presentation currently applies only to numeric settings");
            if (type == "group")
            {
                if (!json.contains("children") || !json["children"].is_array())
                    Reject(location, "Group requires children array");
                GroupDefinition group;
                static_cast<Metadata&>(group) = std::move(metadata);
                for (size_t i = 0; i < json["children"].size(); ++i)
                    group.children.push_back(
                        ReadDefinition(json["children"][i], location + "/children/" + std::to_string(i), depth + 1));
                return {std::move(group)};
            }
            SettingDefinition setting;
            static_cast<Metadata&>(setting) = std::move(metadata);
            if (type == "text")
                setting.specification = TextDefinition{};
            else if (type == "file")
                setting.specification = FileDefinition{};
            else if (type == "checkbox")
                setting.specification = BooleanDefinition{};
            else if (type == "color")
                setting.specification = ColorDefinition{};
            else if (type == "integer")
                setting.specification = Numeric<int64_t>(json, location, 1);
            else if (type == "number")
                setting.specification = Numeric<double>(json, location, .1);
            else if (type == "combo" || type == "radio")
            {
                if (!json.contains("options") || !json["options"].is_array())
                    Reject(location, "Choice requires options");
                ChoiceDefinition spec;
                spec.presentation = type == "radio" ? ChoicePresentation::Radio : ChoicePresentation::Combo;
                for (const auto& option : json["options"])
                    spec.options.push_back({Required(option, "label", location + "/options"),
                                            Required(option, "value", location + "/options")});
                setting.specification = std::move(spec);
            }
            else
                Reject(location + "/type", "Unknown control type: " + type);
            if (type != "number" && type != "integer" &&
                (json.contains("step") || json.contains("min") || json.contains("max")))
                Reject(location, "Bounds and steps apply only to numeric settings");
            if (!json.contains("default"))
                Reject(location, "Setting requires a default");
            auto initial = DecodeValue(json["default"], setting);
            // Defaults use the same typed validation as all subsequent mutations.
            auto checked = Validate(setting, std::move(initial));
            if (!checked)
                throw Invalid{checked.error()};
            std::visit([&](auto& spec)
                       { spec.defaultValue = std::get<std::decay_t<decltype(spec.defaultValue)>>(*checked); },
                       setting.specification);
            return {std::move(setting)};
        }
        std::expected<Json, Error> ReadFile(const std::filesystem::path& path)
        {
            try
            {
                std::ifstream input(path, std::ios::binary);
                if (!input)
                    return std::unexpected(Error{path.string(), "Cannot open file"});
                return Json::parse(input);
            }
            catch (const std::exception& e)
            {
                return std::unexpected(Error{path.string(), e.what()});
            }
        }
    }  // namespace
    Status SettingsStorage::LoadTemplate(SettingsDocument& document, const Json& json)
    {
        try
        {
            (void) json.dump();
            if (!json.is_object() || !json.contains("version") || !json["version"].is_number_integer() ||
                json["version"] != 1 || !json.contains("children") || !json["children"].is_array())
                return std::unexpected(Error{"/", "Expected version 1 and a children array"});
            std::vector<Definition> definitions;
            for (size_t i = 0; i < json["children"].size(); ++i)
                definitions.push_back(ReadDefinition(json["children"][i], "/children/" + std::to_string(i), 0));
            return document.ReplaceDefinitions(std::move(definitions));
        }
        catch (const Invalid& e)
        {
            return std::unexpected(e.error);
        }
        catch (const std::exception& e)
        {
            return std::unexpected(Error{"/", e.what()});
        }
    }
    Status SettingsStorage::LoadTemplateFile(SettingsDocument& document, const std::filesystem::path& file)
    {
        auto json = ReadFile(file);
        if (!json)
            return std::unexpected(json.error());
        return LoadTemplate(document, *json);
    }
    Status SettingsStorage::LoadValues(SettingsDocument& document, const Json& json)
    {
        try
        {
            (void) json.dump();
            if (!json.is_object() || !json.contains("version") || !json["version"].is_number_integer() ||
                json["version"] != 1 || !json.contains("values") || !json["values"].is_object())
                return std::unexpected(Error{"/", "Expected version 1 and a values object"});
            Values values;
            for (const auto& [id, value] : json["values"].items())
                if (auto* definition = document.Find(id))
                    values[id] = DecodeValue(value, *definition);
            return document.ReplaceValues(std::move(values));
        }
        catch (const Invalid& e)
        {
            return std::unexpected(e.error);
        }
        catch (const std::exception& e)
        {
            return std::unexpected(Error{"/", e.what()});
        }
    }
    Status SettingsStorage::LoadValuesFile(SettingsDocument& document, const std::filesystem::path& file)
    {
        auto json = ReadFile(file);
        if (!json)
            return std::unexpected(json.error());
        return LoadValues(document, *json);
    }
    Json SettingsStorage::EncodeValues(const Values& values)
    {
        Json json{{"version", 1}, {"values", Json::object()}};
        for (const auto& [id, value] : values)
            json["values"][id] = Detail::EncodeValue(value);
        return json;
    }
    Status SettingsStorage::Write(const std::filesystem::path& path, const Values& values)
    {
        return Detail::WriteJsonFile(path, EncodeValues(values));
    }
    Status Detail::WriteJsonFile(const std::filesystem::path& path, const Json& document)
    {
        auto Fail = [](std::string location, std::string message) -> Status
        { return std::unexpected(Error{std::move(location), std::move(message)}); };
        try
        {
            std::string bytes = document.dump(2) + "\n";
            std::string crlf;
            for (char c : bytes)
            {
                if (c == '\n')
                    crlf += '\r';
                crlf += c;
            }
            auto temporary = path;
#ifdef _WIN32
            temporary += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64());
            HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                                      nullptr);
            if (file == INVALID_HANDLE_VALUE)
                return Fail("save", "Cannot create temporary settings file");
            DWORD written = 0;
            bool ok = crlf.size() <= MAXDWORD && WriteFile(file, crlf.data(), DWORD(crlf.size()), &written, nullptr) &&
                      written == crlf.size() && FlushFileBuffers(file);
            CloseHandle(file);
            if (ok)
                ok = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            if (!ok)
            {
                DeleteFileW(temporary.c_str());
                return Fail("save", "Cannot write or replace settings file");
            }
#else
            temporary += ".tmp.XXXXXX";
            std::string temporaryPath = temporary.string();
            int fd = mkstemp(temporaryPath.data());
            if (fd < 0)
                return Fail("save", "Cannot create temporary settings file");
            size_t count = 0;
            while (count < crlf.size())
            {
                auto n = write(fd, crlf.data() + count, crlf.size() - count);
                if (n < 0 && errno == EINTR)
                    continue;
                if (n <= 0)
                    break;
                count += size_t(n);
            }
            bool ok = count == crlf.size() && fsync(fd) == 0;
            if (close(fd) != 0)
                ok = false;
            if (ok)
                ok = rename(temporaryPath.c_str(), path.c_str()) == 0;
            if (!ok)
            {
                unlink(temporaryPath.c_str());
                return Fail("save", "Cannot write or replace settings file");
            }
#endif
            return {};
        }
        catch (const std::exception& e)
        {
            return Fail(path.string(), e.what());
        }
    }
}  // namespace LWSSettings
