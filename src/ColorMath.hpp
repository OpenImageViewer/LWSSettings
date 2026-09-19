#pragma once
#include <LLUtils/Color.h>
#include <algorithm>
#include <cmath>

namespace LWSSettings
{
    struct HslColor
    {
        double hue = 0;         // Degrees, 0 through 360.
        double saturation = 0;  // Unit interval.
        double lightness = 0;   // Unit interval.
    };
    inline HslColor RgbToHsl(const LLUtils::Color& color, double achromaticHue = 0)
    {
        const double r = color.R() / 255.0, g = color.G() / 255.0, b = color.B() / 255.0;
        const double hi = std::max({r, g, b}), lo = std::min({r, g, b}), delta = hi - lo;
        HslColor hsl{achromaticHue, 0, (hi + lo) / 2};
        if (delta == 0)
            return hsl;
        hsl.saturation = delta / (1 - std::abs(2 * hsl.lightness - 1));
        if (hi == r)
            hsl.hue = 60 * std::fmod((g - b) / delta, 6.0);
        else if (hi == g)
            hsl.hue = 60 * ((b - r) / delta + 2);
        else
            hsl.hue = 60 * ((r - g) / delta + 4);
        if (hsl.hue < 0)
            hsl.hue += 360;
        return hsl;
    }
    inline LLUtils::Color HslToRgb(HslColor hsl, uint8_t alpha)
    {
        const double hue = std::fmod(std::clamp(hsl.hue, 0.0, 360.0), 360.0) / 60;
        const double saturation = std::clamp(hsl.saturation, 0.0, 1.0), lightness = std::clamp(hsl.lightness, 0.0, 1.0);
        const double chroma = (1 - std::abs(2 * lightness - 1)) * saturation;
        const double x = chroma * (1 - std::abs(std::fmod(hue, 2.0) - 1)), m = lightness - chroma / 2;
        double r = 0, g = 0, b = 0;
        if (hue < 1)
        {
            r = chroma;
            g = x;
        }
        else if (hue < 2)
        {
            r = x;
            g = chroma;
        }
        else if (hue < 3)
        {
            g = chroma;
            b = x;
        }
        else if (hue < 4)
        {
            g = x;
            b = chroma;
        }
        else if (hue < 5)
        {
            r = x;
            b = chroma;
        }
        else
        {
            r = chroma;
            b = x;
        }
        LLUtils::Color color;
        color.R() = uint8_t(std::lround(std::clamp(r + m, 0.0, 1.0) * 255));
        color.G() = uint8_t(std::lround(std::clamp(g + m, 0.0, 1.0) * 255));
        color.B() = uint8_t(std::lround(std::clamp(b + m, 0.0, 1.0) * 255));
        color.A() = alpha;
        return color;
    }
}  // namespace LWSSettings
