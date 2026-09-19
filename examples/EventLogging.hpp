#pragma once
#include <LWSSettings/SettingsPage.hpp>
#include <iomanip>
#include <ostream>
#include <sstream>
namespace LWSSettings::Example
{
    inline nlohmann::json EventJson(const Change& event)
    {
        auto encode = [](const Value& value) -> nlohmann::json
        {
            return std::visit(
                [](const auto& v) -> nlohmann::json
                {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, LLUtils::Color>)
                    {
                        std::ostringstream text;
                        text << '#' << std::hex << std::setfill('0');
                        for (auto channel : v.channels)
                            text << std::setw(2) << int(channel);
                        return text.str();
                    }
                    else
                        return v;
                },
                value);
        };
        const char* origin = "unknown";
        switch (event.origin)
        {
            case ChangeOrigin::User:
                origin = "user";
                break;
            case ChangeOrigin::Programmatic:
                origin = "programmatic";
                break;
            case ChangeOrigin::Reset:
                origin = "reset";
                break;
            case ChangeOrigin::Load:
                origin = "load";
                break;
            case ChangeOrigin::Cancel:
                origin = "cancel";
                break;
        }
        const char* types[] = {"boolean", "integer", "number", "string", "color"};
        return {{"id", event.id},
                {"oldValue", encode(event.oldValue)},
                {"newValue", encode(event.newValue)},
                {"valueType", types[event.newValue.index()]},
                {"kind", event.kind == ChangeKind::Live ? "live" : "confirmed"},
                {"origin", origin},
                {"domain", event.domain == ValueDomain::Working ? "working" : "effective"}};
    }
    inline void PrintEvent(std::ostream& output, const Change& event)
    {
        output << EventJson(event).dump() << std::endl;
    }
}  // namespace LWSSettings::Example
