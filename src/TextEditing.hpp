#pragma once
#include <algorithm>
#include <string>
#include <string_view>
namespace LWSSettings
{
    inline size_t PreviousCharacter(std::string_view text, size_t at)
    {
        if (!at)
            return 0;
        --at;
        while (at && (static_cast<unsigned char>(text[at]) & 0xc0) == 0x80)
            --at;
        return at;
    }
    inline size_t NextCharacter(std::string_view text, size_t at)
    {
        if (at >= text.size())
            return text.size();
        ++at;
        while (at < text.size() && (static_cast<unsigned char>(text[at]) & 0xc0) == 0x80)
            ++at;
        return at;
    }
    inline std::string SingleLine(std::string text)
    {
        text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; }),
                   text.end());
        return text;
    }
}  // namespace LWSSettings
