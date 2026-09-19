#include "Internal.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <bit>

namespace LWSSettings
{
    namespace
    {
        int DecimalPlaces(double value)
        {
            const auto text = FormatValue(Value{value});
            auto e = text.find_first_of("eE");
            const auto dot = text.find('.');
            int digits = dot == std::string::npos ? 0 : int((e == std::string::npos ? text.size() : e) - dot - 1);
            if (e != std::string::npos)
                digits -= std::stoi(text.substr(e + 1));
            return std::max(0, digits);
        }
    }  // namespace
    std::optional<Value> StepValue(const Node& node, const Value& value, int direction, uint64_t multiplier)
    {
        if ((direction != 1 && direction != -1) || multiplier == 0)
            return {};
        if (node.control == Control::Integer)
        {
            const auto current = std::get<int64_t>(value);
            const auto lo = node.integerMinimum.value_or(INT64_MIN), hi = node.integerMaximum.value_or(INT64_MAX);
            const uint64_t base = uint64_t(node.integerStep);
            const uint64_t amount = multiplier > UINT64_MAX / base ? UINT64_MAX : base * multiplier;
            int64_t next;
            if (direction > 0)
                next = amount >= uint64_t(hi) - uint64_t(current) ? hi
                                                                  : std::bit_cast<int64_t>(uint64_t(current) + amount);
            else
                next = amount >= uint64_t(current) - uint64_t(lo) ? lo
                                                                  : std::bit_cast<int64_t>(uint64_t(current) - amount);
            if (next == current)
                return {};
            return Value{next};
        }
        if (node.control == Control::Number)
        {
            const auto current = std::get<double>(value);
            const auto lo = node.minimum.value_or(-std::numeric_limits<double>::max());
            const auto hi = node.maximum.value_or(std::numeric_limits<double>::max());
            const double step = node.numberStep > std::numeric_limits<double>::max() / double(multiplier)
                                    ? std::numeric_limits<double>::max()
                                    : node.numberStep * double(multiplier);
            double next;
            if (direction > 0 && current >= hi - step)
                next = hi;
            else if (direction < 0 && current <= lo + step)
                next = lo;
            else
            {
                next = current + direction * step;
                const auto precision = std::max(DecimalPlaces(current), DecimalPlaces(node.numberStep));
                if (precision <= 308)
                {
                    const auto factor = std::pow(10.0, precision);
                    if (std::isfinite(factor) && std::abs(next) <= std::numeric_limits<double>::max() / factor)
                        next = std::round(next * factor) / factor;
                }
                next = std::clamp(next, lo, hi);
            }
            if (!std::isfinite(next) || next == current)
                return {};
            return Value{next};
        }
        return {};
    }
    LLUtils::Color SettingsPage::Impl::ValueForeground(const Node& node) const
    {
        auto it = values.find(node.id);
        return it != values.end() && it->second != node.initial ? config.appearance.nonDefaultForeground
                                                                : config.appearance.foreground;
    }
    UiBox SettingsPage::Impl::DropdownBox() const
    {
        if (!dropdown)
            return {};
        for (const auto& row : rows)
            if (row.node->id == selected && row.node->control == Control::Combo)
            {
                const auto count = std::min(row.node->options.size(),
                                            size_t(std::max(1.f, std::min(8.f, std::floor((height - 290) / 28)))));
                return {ControlLeft(), std::min(100 + row.y - scroll + row.height, height - 190 - float(count) * 28),
                        width - ControlLeft() - 85, float(count) * 28};
            }
        return {};
    }
    void SettingsPage::Impl::UpdateHover()
    {
        const auto previous = hovered;
        hovered.clear();
        dropdownHovered.reset();
        if (pointer && !colorStart && !modal && !scrollbarDragging)
        {
            float x = pointer->x / Scales().input, y = pointer->y / Scales().input;
            const auto popup = DropdownBox();
            if (dropdown && popup.Contains(x, y))
            {
                const auto* node = Find(selected);
                const auto index = dropdownFirst + size_t((y - popup.y) / 28);
                if (node && index < node->options.size())
                    dropdownHovered = index;
                // The combo owns description hover over the whole popup, including borders.
                hovered = selected;
            }
            else if (y >= 100 && y < height - 190 && x >= 0 && x < width && !ScrollbarTrack().Contains(x, y))
                for (const auto& row : rows)
                    if (y >= 100 + row.y - scroll && y < 100 + row.y - scroll + row.height)
                    {
                        hovered = row.node->id;
                        break;
                    }
        }
        if (previous != hovered)
            descriptionScroll = 0;
    }
    bool SettingsPage::Impl::PointerOverSpin() const
    {
        if (!spin || !pointer)
            return false;
        for (const auto& row : rows)
            if (row.node->id == spin->id)
            {
                const UiBox button{width - 110, 100 + row.y - scroll + (spin->direction > 0 ? 7.f : 22.f), 25, 15};
                return button.Contains(pointer->x / Scales().input, pointer->y / Scales().input);
            }
        return false;
    }
    void SettingsPage::Impl::AbortSpin()
    {
        spin.reset();
        if (spinTimer)
            spinTimer->SetInterval(0);
    }
    bool SettingsPage::Impl::StartSpin(const Node& node, int direction, bool keyboard)
    {
        if (spin)
        {
            if (spin->id == node.id && spin->direction == direction && spin->keyboard == keyboard)
            {
                if (keyboard)
                    SpinOnce();
                return true;
            }
            StopSpin();
        }
        // A disabled direction must not end the current text edit.
        if (!StepValue(node, values.at(node.id), direction))
            return false;
        if (!FinishEdit())
            return false;
        if (!StepValue(node, values.at(node.id), direction))
            return false;
        BeginEdit(node);
        const auto now = std::chrono::steady_clock::now();
        spin = Spin{node.id, direction, keyboard, now + std::chrono::milliseconds(400), now};
        SpinOnce();
        if (!keyboard && window && spin)
        {
            if (!spinTimer)
            {
                spinTimer = std::make_unique<LWS::Timer>();
                spinTimer->SetTargetWindow(window->GetHandle());
                spinTimer->SetCallback([this] { SpinTick(std::chrono::steady_clock::now()); });
            }
            spinTimer->SetInterval(20);
        }
        return true;
    }
    void SettingsPage::Impl::SpinOnce(uint64_t multiplier)
    {
        if (!spin)
            return;
        auto id = spin->id;
        auto* node = Find(id);
        if (!node || !edit || edit->id != id)
        {
            AbortSpin();
            return;
        }
        auto next = StepValue(*node, values.at(id), spin->direction, multiplier);
        if (!next)
            return;
        edit->text = FormatValue(*next);
        edit->caret = edit->anchor = edit->text.size();
        edit->invalid = false;
        (void) Assign(id, *next, ChangeKind::Live, ChangeOrigin::User);
    }
    void SettingsPage::Impl::SpinTick(std::chrono::steady_clock::time_point now)
    {
        if (!spin || spin->keyboard || now < spin->next)
            return;
        spin->next = now + std::chrono::milliseconds(80);
        const double held = std::chrono::duration<double>(now - spin->started).count();
        // Double the distance per repeat every half-second; individual clicks remain precise.
        const auto multiplier = uint64_t(std::min(1'000'000.0, std::exp2(std::clamp((held - 0.4) * 2, 0.0, 20.0))));
        SpinOnce(multiplier);
    }
    void SettingsPage::Impl::StopSpin(bool cancel, bool keepEditing)
    {
        if (!spin)
            return;
        auto id = spin->id;
        AbortSpin();
        FinishEdit(cancel);
        if (keepEditing && !cancel)
            if (auto* node = Find(id))
                BeginEdit(*node);
        Paint();
    }

    UiBox SettingsPage::Impl::ScrollbarTrack() const
    {
        const float viewport = std::max(0.f, height - 290);
        if (viewport <= 0 || contentHeight <= viewport)
            return {};
        return {width - 22, 100, 16, viewport};
    }
    UiBox SettingsPage::Impl::ScrollbarThumb() const
    {
        const auto track = ScrollbarTrack();
        if (track.height <= 0)
            return {};
        const float thumbHeight = std::min(track.height, std::max(std::min(24.f, track.height / 2),
                                                                  track.height * track.height / contentHeight));
        const float fraction = std::clamp(scroll / (contentHeight - track.height), 0.f, 1.f);
        return {track.x + 2, track.y + (track.height - thumbHeight) * fraction, track.width - 4, thumbHeight};
    }
    void SettingsPage::Impl::DragScrollbar(float y)
    {
        if (!scrollbarDragging)
            return;
        const auto track = ScrollbarTrack(), thumb = ScrollbarThumb();
        const float travel = track.height - thumb.height;
        if (travel <= 0)
        {
            EndScrollbarDrag();
            return;
        }
        const float fraction = std::clamp((y - track.y - scrollbarGrab * thumb.height) / travel, 0.f, 1.f);
        scroll = fraction * (contentHeight - track.height);
        Paint();
    }
    void SettingsPage::Impl::EndScrollbarDrag()
    {
        if (!scrollbarDragging)
            return;
        scrollbarDragging = false;
        if (window)
            (void) window->SetMouseCapture(false);
    }
    UiBox SettingsPage::Impl::ColorPanel() const
    {
        return {(width - 440) / 2, (height - 380) / 2, 440, 380};
    }
    UiBox SettingsPage::Impl::ColorSlider(int channel) const
    {
        const auto panel = ColorPanel();
        return {panel.x + (channel < 4 ? 48.f : 258.f), panel.y + 148 + (channel < 4 ? channel : channel - 4) * 37.f,
                112, 24};
    }
    void SettingsPage::Impl::SyncColorHsl()
    {
        if (!colorStart)
            return;
        const auto rgba = std::get<LLUtils::Color>(values.at(colorId));
        if (rgba != colorHslSource)
        {
            colorHsl = RgbToHsl(rgba, colorHsl.hue);
            colorHslSource = rgba;
        }
    }
    double SettingsPage::Impl::ColorChannelMaximum(int channel) const
    {
        return channel < 4 ? 255 : channel == 4 ? 360 : 100;
    }
    double SettingsPage::Impl::ColorChannelValue(int channel) const
    {
        if (channel < 4)
            return std::get<LLUtils::Color>(values.at(colorId)).channels[channel];
        if (channel == 4)
            return colorHsl.hue;
        return (channel == 5 ? colorHsl.saturation : colorHsl.lightness) * 100;
    }
    void SettingsPage::Impl::SetColorChannel(int channel, double value)
    {
        if (!colorStart || channel < 0 || channel > 6)
            return;
        SyncColorHsl();
        auto rgba = std::get<LLUtils::Color>(values.at(colorId));
        value = std::clamp(value, 0.0, ColorChannelMaximum(channel));
        if (channel < 4)
        {
            rgba.channels[channel] = uint8_t(std::lround(value));
            if (channel < 3)
                colorHsl = RgbToHsl(rgba, colorHsl.hue);
        }
        else
        {
            if (channel == 4)
                colorHsl.hue = value;
            else if (channel == 5)
                colorHsl.saturation = value / 100;
            else
                colorHsl.lightness = value / 100;
            rgba = HslToRgb(colorHsl, rgba.A());
        }
        colorHslSource = rgba;
        (void) Assign(colorId, rgba, ChangeKind::Live, ChangeOrigin::User);
        Paint();
    }
    void SettingsPage::Impl::DragColorChannel(float x)
    {
        const auto slider = ColorSlider(colorChannel);
        SetColorChannel(colorChannel, std::clamp(double((x - slider.x) / slider.width), 0.0, 1.0) *
                                          ColorChannelMaximum(colorChannel));
    }

}  // namespace LWSSettings
