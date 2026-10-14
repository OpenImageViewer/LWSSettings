#include <nlohmann/json.hpp>
#include "ThemeFields.hpp"
#include <LWSSettings/ThemeStorage.hpp>
#include "JsonFile.hpp"
#include "ValueCodec.hpp"
#include <fstream>
namespace LWSSettings
{
    namespace
    {
        using Json = nlohmann::json;
        struct Invalid
        {
            Error error;
        };
        [[noreturn]] void Reject(std::string path, std::string message)
        {
            throw Invalid{{std::move(path), std::move(message)}};
        }
        Value DecodeValue(const Json& j, const ThemeField& field)
        {
            if (std::holds_alternative<ColorDefinition>(field.definition.specification))
            {
                auto color = Detail::DecodeColor(j, "Invalid color");
                if (!color)
                    Reject(field.path, color.error());
                return *color;
            }
            if (j.is_boolean())
                return j.get<bool>();
            if (j.is_number_integer())
            {
                if (j.is_number_unsigned() && j.get<uint64_t>() > uint64_t(INT64_MAX))
                    Reject(field.path, "Integer exceeds int64 range");
                return j.get<int64_t>();
            }
            if (j.is_number_float())
                return j.get<double>();
            if (j.is_string())
                return j.get<std::string>();
            Reject(field.path, "Expected a scalar theme value");
        }
        bool IsGroup(std::string_view path)
        {
            const std::string prefix = std::string(path) + "/";
            for (const auto& field : ThemeFields::All())
                if (field.path.starts_with(prefix))
                    return true;
            return false;
        }
        void Read(const Json& object, const std::string& path, ThemeOverrides& values)
        {
            if (!object.is_object())
                Reject(path, "Expected an object");
            for (const auto& [key, j] : object.items())
            {
                if (path.empty() && key == "version")
                    continue;
                std::string child = path.empty() ? key : path + "/" + key;
                if (const auto* field = ThemeFields::Find(child))
                {
                    auto checked = Validate(field->definition, DecodeValue(j, *field));
                    if (!checked)
                        throw Invalid{checked.error()};
                    values.emplace(child, std::move(*checked));
                }
                else if (IsGroup(child))
                    Read(j, child, values);
                else
                    Reject(child, "Unknown theme field");
            }
        }
        Json DefinitionJson(const Definition& node)
        {
            const auto& info = node.Info();
            Json j = {{"id", info.id}, {"label", info.label}, {"description", info.description}};
            if (auto* group = std::get_if<GroupDefinition>(&node.content))
            {
                j["type"] = "group";
                j["children"] = Json::array();
                for (const auto& child : group->children)
                    j["children"].push_back(DefinitionJson(child));
            }
            else
            {
                auto& setting = std::get<SettingDefinition>(node.content);
                j["default"] = Detail::EncodeValue(setting.DefaultValue());
                std::visit(
                    [&](const auto& spec)
                    {
                        using T = std::decay_t<decltype(spec)>;
                        if constexpr (std::is_same_v<T, BooleanDefinition>)
                            j["type"] = "checkbox";
                        else if constexpr (std::is_same_v<T, ColorDefinition>)
                            j["type"] = "color";
                        else if constexpr (std::is_same_v<T, IntegerDefinition> || std::is_same_v<T, DecimalDefinition>)
                        {
                            j["type"] = std::is_same_v<T, IntegerDefinition> ? "integer" : "number";
                            if (spec.minimum)
                                j["min"] = *spec.minimum;
                            if (spec.maximum)
                                j["max"] = *spec.maximum;
                            j["step"] = spec.step;
                            if (spec.presentation == NumericPresentation::Slider) j["presentation"] = "slider";
                        }
                        else
                            j["type"] = "text";
                    },
                    setting.specification);
            }
            return j;
        }
    }  // namespace
    nlohmann::json ThemeStorage::Encode(const SettingsTheme& theme)
    {
        ThemeOverrides values;
        for (const auto& field : ThemeFields::All())
            values.emplace(field.path, field.read(theme));
        return EncodeOverrides(values);
    }
    nlohmann::json ThemeStorage::EncodeOverrides(const ThemeOverrides& values)
    {
        Json j = {{"version", 1}};
        for (const auto& [path, value] : values)
            j[Json::json_pointer("/" + path)] = Detail::EncodeValue(value);
        return j;
    }
    std::expected<ThemeOverrides, Error> ThemeStorage::DecodeOverrides(const Json& input)
    {
        try
        {
            (void) input.dump();
            if (!input.is_object() || !input.contains("version") || !input["version"].is_number_integer() ||
                input["version"] != 1)
                Reject("/version", "Expected theme version 1");
            ThemeOverrides values;
            Read(input, "", values);
            return values;
        }
        catch (const Invalid& e)
        {
            return std::unexpected(e.error);
        }
        catch (const std::exception& e)
        {
            return std::unexpected(Error{"theme", e.what()});
        }
    }
    std::expected<SettingsTheme, Error> ThemeStorage::Decode(const Json& input)
    {
        auto profile = DecodeProfile(input);
        if (!profile) return std::unexpected(profile.error());
        return ThemeFields::Resolve(profile->overrides, profile->preset);
    }
    std::expected<ThemeOverrides, Error> ThemeStorage::LoadOverridesFile(const std::filesystem::path& path)
    {
        try
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return std::unexpected(Error{path.string(), "Cannot open theme file"});
            return DecodeOverrides(Json::parse(file));
        }
        catch (const std::exception& e)
        {
            return std::unexpected(Error{path.string(), e.what()});
        }
    }
    std::expected<SettingsTheme, Error> ThemeStorage::LoadFile(const std::filesystem::path& path)
    {
        auto profile = LoadProfileFile(path);
        if (!profile) return std::unexpected(profile.error());
        return ThemeFields::Resolve(profile->overrides, profile->preset);
    }
    Status ThemeStorage::SaveOverridesFile(const std::filesystem::path& path, const ThemeOverrides& values)
    {
        auto checked = ThemeFields::Resolve(values);
        if (!checked)
            return std::unexpected(checked.error());
        return Detail::WriteJsonFile(path, EncodeOverrides(values));
    }
    Status ThemeStorage::SaveFile(const std::filesystem::path& path, const SettingsTheme& theme)
    {
        auto checked = ThemeFields::Validate(theme);
        if (!checked)
            return checked;
        return Detail::WriteJsonFile(path, Encode(theme));
    }
    std::expected<ThemeProfile, Error> ThemeStorage::DecodeProfile(const Json& input)
    {
        auto fields = input;
        ThemeProfile profile;
        if (fields.is_object() && fields.contains("preset"))
        {
            if (fields["preset"] == "light") profile.preset = LWSUI::ThemePreset::Light;
            else if (fields["preset"] == "warm") profile.preset = LWSUI::ThemePreset::Warm;
            else if (fields["preset"] == "dark") profile.preset = LWSUI::ThemePreset::Dark;
            else return std::unexpected(Error{"preset", "Unknown theme preset"});
            fields.erase("preset");
        }
        auto values = DecodeOverrides(fields);
        if (!values) return std::unexpected(values.error());
        profile.overrides = std::move(*values);
        return profile;
    }
    nlohmann::json ThemeStorage::EncodeProfile(const ThemeProfile& profile)
    {
        auto json = EncodeOverrides(profile.overrides);
        json["preset"] = LWSUI::ThemePresetName(profile.preset);
        return json;
    }
    std::expected<ThemeProfile, Error> ThemeStorage::LoadProfileFile(const std::filesystem::path& path)
    {
        try
        {
            std::ifstream file(path, std::ios::binary);
            if (!file) return std::unexpected(Error{path.string(), "Cannot open theme file"});
            return DecodeProfile(Json::parse(file));
        }
        catch (const std::exception& e) { return std::unexpected(Error{path.string(), e.what()}); }
    }
    Status ThemeStorage::SaveProfileFile(const std::filesystem::path& path, const ThemeProfile& profile)
    {
        auto checked = ThemeFields::Resolve(profile.overrides, profile.preset);
        if (!checked) return std::unexpected(checked.error());
        return Detail::WriteJsonFile(path, EncodeProfile(profile));
    }
    nlohmann::json ThemeStorage::Template()
    {
        Json result = {{"version", 1}, {"children", Json::array()}};
        for (const auto& node : ThemeFields::CreateDefinitions())
            result["children"].push_back(DefinitionJson(node));
        return result;
    }
    Status ThemeStorage::SaveTemplateFile(const std::filesystem::path& path)
    {
        return Detail::WriteJsonFile(path, Template());
    }
}  // namespace LWSSettings
