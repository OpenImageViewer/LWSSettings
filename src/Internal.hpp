#pragma once
#include <LWSSettings/SettingsPage.hpp>
#include <LWS/Timer.hpp>
#include <deque>
#include <map>
#include <set>
#include <vector>
#include "Canvas.hpp"
#include "UiMetrics.hpp"
#include "ColorMath.hpp"
#include <chrono>

namespace LWSSettings
{
    enum class Control
    {
        Group,
        Text,
        Integer,
        Number,
        Combo,
        Radio,
        Checkbox,
        Color,
        File
    };
    struct Option
    {
        std::string label;
        std::string value;
    };
    struct Node
    {
        std::string id, label, description;
        Control control = Control::Group;
        Value initial = false;
        std::optional<double> minimum, maximum;
        std::optional<int64_t> integerMinimum, integerMaximum;
        int64_t integerStep = 1;
        double numberStep = 0.1;
        std::vector<Option> options;
        std::vector<Node> children;
    };
    using Values = std::map<std::string, Value, std::less<>>;
    std::string FormatValue(const Value& value);
    std::expected<Value, Error> ParseValue(const Node& node, const nlohmann::json& value);
    std::string Lower(std::string text);
    std::optional<Value> StepValue(const Node& node, const Value& value, int direction, uint64_t multiplier = 1);

    struct SettingsPage::Impl
    {
        explicit Impl(SettingsConfig c) : config(std::move(c)) {}
        ~Impl();
        SettingsConfig config;
        std::vector<Node> roots;
        std::map<std::string, const Node*, std::less<>> nodes;
        Values values, applied, saved;
        LLUtils::Event<void(const Change&)> changes;
        std::optional<Error> error;
        std::deque<Change> notifications;
        bool notifying = false;
        bool batch = false;
        bool painting = false;
        bool presented = false;
        bool modal = false;
        std::unique_ptr<LWS::Window> window;
        LWS::EventListenerGuard listener;
        std::unique_ptr<Canvas> canvas;
        std::unique_ptr<LWS::Timer> timer;
        std::shared_ptr<int> lifetime = std::make_shared<int>(0);
        struct Row
        {
            const Node* node;
            int depth;
            float y;
            float height;
        };
        std::vector<Row> rows;
        std::map<std::string, std::string, std::less<>> breadcrumbs;
        LWS::DpiInfo dpiInfo;
        UiScales Scales() const { return CalculateScales(config.appearance, dpiInfo); }
        float ControlLeft() const { return std::max(200.f, width * .47f); }
        float LabelLeft(int depth) const
        {
            return 26 + std::min(depth * 18.f, std::max(0.f, ControlLeft() - 26 - 174));
        }
        LLUtils::Color ValueForeground(const Node& n) const;
        UiBox DropdownBox() const;
        void UpdateHover();
        std::optional<LWS::Point> pointer;
        std::optional<size_t> dropdownHovered;
        struct Spin
        {
            std::string id;
            int direction;
            bool keyboard;
            std::chrono::steady_clock::time_point next;
            std::chrono::steady_clock::time_point started;
        };
        std::optional<Spin> spin;
        std::unique_ptr<LWS::Timer> spinTimer;
        void AbortSpin();
        bool PointerOverSpin() const;
        bool StartSpin(const Node& node, int direction, bool keyboard);
        void SpinOnce(uint64_t multiplier = 1);
        void SpinTick(std::chrono::steady_clock::time_point now);
        void StopSpin(bool cancel = false, bool keepEditing = false);
        std::set<std::string> collapsed;
        std::string selected, hovered, filter;
        float scroll = 0, contentHeight = 0;
        bool scrollbarDragging = false;
        float scrollbarGrab = 0;
        UiBox ScrollbarTrack() const;
        UiBox ScrollbarThumb() const;
        void DragScrollbar(float y);
        void EndScrollbarDrag();
        float descriptionScroll = 0, descriptionHeight = 0;
        bool textSelecting = false;
        int colorChannel = 0;
        bool colorDragging = false;
        HslColor colorHsl;
        LLUtils::Color colorHslSource;
        UiBox ColorPanel() const;
        UiBox ColorSlider(int channel) const;
        double ColorChannelValue(int channel) const;
        double ColorChannelMaximum(int channel) const;
        void SyncColorHsl();
        void SetColorChannel(int channel, double value);
        void DragColorChannel(float x);
        void PlaceCaret(float x, bool extend);
        bool searchFocus = false;
        bool searchAll = false;
        size_t dropdownFirst = 0;
        bool dropdown = false;
        struct Edit
        {
            std::string id, text;
            Value start;
            size_t caret = 0, anchor = 0;
            bool invalid = false;
        };
        std::optional<Edit> edit;
        std::optional<Value> colorStart;
        std::string colorId;
        float width = 820, height = 680;
        bool caretVisible = true;
        std::string status;
        void Index();
        void Notify(Change change);
        void DrainNotifications();
        Status Fail(std::string location, std::string message);
        Status Assign(std::string_view id, Value value, ChangeKind kind, ChangeOrigin origin);
        Status Apply();
        void Cancel();
        Status Save();
        Status Write();
        void AutoSave();
        bool FinishEdit(bool cancel = false);
        void BeginEdit(const Node& node);
        void EditText(std::string text);
        void ReplaceSelection(std::string text);
        void Select(std::string id, bool reveal = true);
        void SetFilter(std::string text);
        void RebuildRows();
        void Paint();
        Status Show();
        bool Event(const LWS::AnyEvent& event);
        void Click(float x, float y);
        void Key(LWS::KeyCode key);
        void Browse(const Node& node);
        void OpenColor(const Node& node);
        void CloseColor(bool cancel);
        Status ResetNode(std::string_view id);
        Status ResetAll();
        void ScrollSelection();
        bool ControlPressed() const;
        bool ShiftPressed() const;
        const Node* Find(std::string_view id) const;
        std::string Description() const;
    };
}  // namespace LWSSettings
