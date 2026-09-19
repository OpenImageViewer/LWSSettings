#pragma once
#include <LWSSettings/SettingsPage.hpp>
#include <algorithm>
namespace LWSSettings
{
    struct UiScales
    {
        float render = 1;  // Physical pixels per design unit (the original 14 px layout).
        float input = 1;   // Native client/input units per design unit.
        float window = 1;  // Native units per configured window-size unit.
    };
    inline UiScales CalculateScales(const Appearance& appearance, LWS::DpiInfo dpi)
    {
        const float density = std::max(1U, dpi.dpiY) / 96.f;
        const float ratio = std::max(1.f, dpi.framebufferScale);
        const float render = appearance.scale * density * appearance.fontSize / 14.f;
        return {render, render / ratio, appearance.scale * density / ratio};
    }
    struct UiBox
    {
        float x = 0, y = 0, width = 0, height = 0;
        bool Contains(float px, float py) const { return px >= x && py >= y && px < x + width && py < y + height; }
    };
}  // namespace LWSSettings
