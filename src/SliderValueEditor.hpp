#pragma once
#include "SliderRange.hpp"
#include <LWSUI/Composites.hpp>
namespace LWSSettings::internal
{
    // Keep this composition private until a second consumer needs the same layout/edit policy.
    template <class T>
    class SliderValueEditor final : public LWSUI::Container
    {
        using Phase = LWSUI::EditPhase;
        class Thumb final : public LWSUI::Slider
        {
          public:

            explicit Thumb(SliderValueEditor& owner) : owner_(owner) {}
            void Abort()
            {
                dragging_ = false;
                ReleaseCapture();
            }

          protected:

            bool OnInput(const LWSUI::Input& input) override
            {
                using Key = LWS::KeyCode;
                if (input.kind == LWSUI::InputKind::Down)
                {
                    if (!owner_.number_->Finish(Phase::Commit))
                        return true;
                    owner_.start_ = owner_.value_;
                    const auto b = Bounds();
                    const float thumb = std::min(Style().sliderThumbWidth, b.width);
                    const float left = b.x + float(Value()) * std::max(0.f, b.width - thumb);
                    // Preserve the grab point, especially when the slider is narrow.
                    grab_ = input.x >= left && input.x <= left + thumb ? input.x - left : thumb / 2;
                }
                if (input.kind == LWSUI::InputKind::KeyDown && (input.key == Key::Left || input.key == Key::Right))
                {
                    Finish(Phase::Commit);
                    owner_.Step(input.key == Key::Right ? 1 : -1, input.shift ? 10 : 1);
                    return true;
                }
                return Slider::OnInput(input);
            }

            double Fraction(float x, float) const override
            {
                const auto b = Bounds();
                const float travel = std::max(1.f, b.width - std::min(Style().sliderThumbWidth, b.width));
                return std::clamp((x - b.x - grab_) / travel, 0.f, 1.f);
            }

          private:

            float grab_ = 0;
            SliderValueEditor& owner_;
        };

      public:

        explicit SliderValueEditor(const NumericDefinition<T>& spec) : spec_(spec)
        {
            if (const auto* error = SliderRangeError(spec_))
                throw std::invalid_argument(error);
            slider_ = &Emplace<Thumb>(*this);
            number_ = &Emplace<LWSUI::NumericEdit<T>>(LWSUI::NumericSpec<T>{spec.minimum, spec.maximum, spec.step});
            sliderConnection_ = slider_->OnEdit.Connect(
                [this](double fraction, Phase phase)
                {
                    if (muted_)
                        return;
                    value_ = phase == Phase::Cancel ? start_ : FromFraction(fraction);
                    number_->SetValue(value_);
                    slider_->SetValue(Fraction());
                    OnEdit.Raise(value_, phase);
                });
            numberConnection_ = number_->OnEdit.Connect(
                [this](T value, Phase phase)
                {
                    if (muted_)
                        return;
                    value_ = value;
                    slider_->SetValue(Fraction());
                    OnEdit.Raise(value_, phase);
                });
            SetValue(spec.defaultValue);
        }
        void SetValue(T value)
        {
            if (!std::isfinite(double(value)) || value < *spec_.minimum || value > *spec_.maximum)
                throw std::invalid_argument("Slider value is outside its bounds");
            // External writes supersede both drag and typing/repeat sessions, silently.
            struct Restore
            {
                bool& flag;
                bool previous;
                ~Restore() { flag = previous; }
            } restore{muted_, muted_};
            muted_ = true;
            slider_->Abort();
            number_->Finish(Phase::Cancel);
            value_ = start_ = value;
            number_->SetValue(value);
            slider_->SetValue(Fraction());
        }
        T Value() const { return value_; }
        bool Finish(Phase phase) override { return number_->Finish(phase) && slider_->Finish(phase); }
        LWSUI::Event<void(T, Phase)> OnEdit;

      protected:

        LWSUI::Size OnMeasure(LWSUI::Size available) override
        {
            const float field = FieldWidth(available.width);
            const auto numeric = number_->Measure({field, available.height});
            const auto slider = slider_->Measure(
                {std::max(0.f, available.width - field - Style().padding), available.height});
            return {available.width, std::max(numeric.height, slider.height)};
        }
        void OnArrange() override
        {
            const auto b = Bounds();
            const float field = FieldWidth(b.width);
            const float gap = std::min(std::max(0.f, Style().padding), std::max(0.f, b.width - field));
            slider_->Arrange({b.x, b.y, std::max(0.f, b.width - field - gap), b.height});
            number_->Arrange({b.x + b.width - field, b.y, field, b.height});
        }
        void OnDetach() override { slider_->Abort(); }

      private:

        float FieldWidth(float width) const
        {
            return std::min(std::max(0.f, width), std::max(80.f, 4 * Font().size + Style().rockerWidth));
        }
        double Fraction() const
        {
            return std::clamp(double(value_ - *spec_.minimum) / (double(*spec_.maximum) - double(*spec_.minimum)), 0.,
                              1.);
        }
        static int DecimalPlaces(double value)
        {
            char buffer[80];
            auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value);
            if (error != std::errc{})
                return 0;
            const std::string text(buffer, end);
            const auto exponent = text.find_first_of("eE"), dot = text.find('.');
            int digits = dot == std::string::npos
                             ? 0
                             : int((exponent == std::string::npos ? text.size() : exponent) - dot - 1);
            if (exponent != std::string::npos)
                digits -= std::stoi(text.substr(exponent + 1));
            return std::max(0, digits);
        }
        double Rounded(double value, double origin) const
        {
            const int digits = std::max(DecimalPlaces(origin), DecimalPlaces(double(spec_.step)));
            const double factor = std::pow(10., digits);
            if (std::isfinite(factor) && std::abs(value) <= std::numeric_limits<double>::max() / factor)
                value = std::round(value * factor) / factor;
            return std::clamp(value, double(*spec_.minimum), double(*spec_.maximum));
        }
        T FromFraction(double fraction) const
        {
            if (fraction <= 0)
                return *spec_.minimum;
            if (fraction >= 1)
                return *spec_.maximum;
            const double span = double(*spec_.maximum) - double(*spec_.minimum);
            const double steps = std::round(fraction * span / double(spec_.step));
            if constexpr (std::is_integral_v<T>)
            {
                // Validated bounds/span permit exact signed arithmetic. Check before multiplying.
                const T count = T(steps);
                const T range = *spec_.maximum - *spec_.minimum;
                if (count > range / spec_.step)
                    return *spec_.maximum;
                return *spec_.minimum + count * spec_.step;
            }
            else
                return Rounded(std::fma(steps, spec_.step, *spec_.minimum), *spec_.minimum);
        }
        void Step(int direction, int count)
        {
            if (!number_->Finish(Phase::Commit))
                return;
            T next = value_;
            for (int i = 0; i < count; ++i)
                if constexpr (std::is_integral_v<T>)
                {
                    if (direction > 0)
                        next = *spec_.maximum - next < spec_.step ? *spec_.maximum : next + spec_.step;
                    else
                        next = next - *spec_.minimum < spec_.step ? *spec_.minimum : next - spec_.step;
                }
                else
                    next = Rounded(next + direction * spec_.step, next);
            value_ = next;
            number_->SetValue(value_);
            slider_->SetValue(Fraction());
            OnEdit.Raise(value_, Phase::Commit);
        }
        NumericDefinition<T> spec_;
        T value_{}, start_{};
        bool muted_ = false;
        Thumb* slider_;
        LWSUI::NumericEdit<T>* number_;
        LWSUI::Event<void(double, Phase)>::Connection sliderConnection_;
        LWSUI::Event<void(T, Phase)>::Connection numberConnection_;
    };
}  // namespace LWSSettings::internal
