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
}  // namespace LWSSettings::Detail
