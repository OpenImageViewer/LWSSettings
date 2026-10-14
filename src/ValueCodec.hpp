#pragma once
#include <LWSSettings/SettingsDocument.hpp>
#include <nlohmann/json.hpp>
#include <charconv>
namespace LWSSettings::Detail
{
    inline nlohmann::json EncodeValue(const Value& value)
    {
        return std::visit(
            [](const auto& v) -> nlohmann::json
            {
                if constexpr (std::is_same_v<std::decay_t<decltype(v)>, LLUtils::Color>)
                    return FormatValue(Value{v});
                else
                    return v;
            },
            value);
    }
    inline std::expected<LLUtils::Color, std::string> DecodeColor(const nlohmann::json& json,
                                                                  std::string invalid = "Invalid hexadecimal color")
    {
        if (!json.is_string())
            return std::unexpected("Expected #RRGGBB or #RRGGBBAA");
        const auto& text = json.get_ref<const std::string&>();
        if ((text.size() != 7 && text.size() != 9) || text[0] != '#')
            return std::unexpected("Expected #RRGGBB or #RRGGBBAA");
        uint32_t color = 0;
        const auto [end, error] = std::from_chars(text.data() + 1, text.data() + text.size(), color, 16);
        if (error != std::errc{} || end != text.data() + text.size())
            return std::unexpected(std::move(invalid));
        return LLUtils::Color{text.size() == 7 ? (color << 8) | 255 : color};
    }
    struct Invalid
    {
        Error error;
    };
    [[noreturn]] inline void Reject(std::string path, std::string message)
    {
        throw Invalid{{std::move(path), std::move(message)}};
    }
    inline Value DecodeValue(const nlohmann::json& json, const SettingDefinition& definition, bool decimalNumbers = true)
    {
        if (std::holds_alternative<ColorDefinition>(definition.specification))
        {
            auto color = DecodeColor(json);
            if (!color) Reject(definition.id, color.error());
            return *color;
        }
        if (decimalNumbers && std::holds_alternative<DecimalDefinition>(definition.specification) && json.is_number())
            return json.get<double>();
        if (json.is_boolean()) return json.get<bool>();
        if (json.is_number_integer())
        {
            if (json.is_number_unsigned() && json.get<uint64_t>() > uint64_t(INT64_MAX))
                Reject(definition.id, "Expected a signed 64-bit integer");
            return json.get<int64_t>();
        }
        if (json.is_number_float()) return json.get<double>();
        if (json.is_string()) return json.get<std::string>();
        Reject(definition.id, "Expected a scalar value");
    }
}  // namespace LWSSettings::Detail
