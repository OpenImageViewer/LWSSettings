#include <LWSSettings/SettingsPage.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include "TextEditing.hpp"
using namespace LWSSettings;
using Json = nlohmann::json;
namespace
{
    Json Template()
    {
        return Json::parse(R"({"version":1,"children":[{"type":"group","id":"g","label":"Group","children":[
            {"type":"text","id":"name","label":"Name","default":"hello"},
            {"type":"integer","id":"count","label":"Count","default":3,"min":0,"max":10},
            {"type":"number","id":"ratio","label":"Ratio","default":1.5},
            {"type":"checkbox","id":"enabled","label":"Enabled","default":false},
            {"type":"color","id":"color","label":"Color","default":"#123456"},
            {"type":"combo","id":"choice","label":"Choice","default":"a","options":[{"label":"A","value":"a"},{"label":"B","value":"b"}]},
            {"type":"radio","id":"radio","label":"Radio","default":"a","options":[{"label":"A","value":"a"}]},
            {"type":"file","id":"file","label":"File","default":""}
        ]}]})");
    }
    struct TempFile
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     ("lwssettings-" + std::to_string(reinterpret_cast<uintptr_t>(this)) + ".json");
        ~TempFile()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    };
}  // namespace
TEST_CASE("Templates create typed defaults without a platform session")
{
    SettingsPage p;
    REQUIRE(p.LoadTemplate(Template()));
    REQUIRE(std::get<int64_t>(*p.GetValue("count")) == 3);
    REQUIRE(std::get<double>(*p.GetValue("ratio")) == 1.5);
    REQUIRE(p.ExportValues()["values"]["color"] == "#123456ff");
    REQUIRE_FALSE(p.GetValue("g"));
    REQUIRE_FALSE(p.IsDirty());
    REQUIRE_FALSE(p.Show());
}
TEST_CASE("Invalid templates preserve the complete previous model")
{
    SettingsPage p;
    REQUIRE(p.LoadTemplate(Template()));
    auto bad = Template();
    bad["children"][0]["children"][1]["id"] = "name";
    REQUIRE_FALSE(p.LoadTemplate(bad));
    REQUIRE(p.GetValue("count"));
    bad = Template();
    bad["children"][0]["children"][1]["default"] = 50;
    REQUIRE_FALSE(p.LoadTemplate(bad));
    REQUIRE(std::get<int64_t>(*p.GetValue("count")) == 3);
    bad = Template();
    bad["children"][0]["children"][0]["type"] = "unknown";
    REQUIRE_FALSE(p.LoadTemplate(bad));
}
TEST_CASE("Assignments validate types ranges and membership")
{
    SettingsPage p;
    REQUIRE(p.LoadTemplate(Template()));
    REQUIRE_FALSE(p.SetValue("count", int64_t{11}));
    REQUIRE_FALSE(p.SetValue("count", 1.5));
    REQUIRE_FALSE(p.SetValue("enabled", std::string("true")));
    REQUIRE_FALSE(p.SetValue("choice", std::string("missing")));
    REQUIRE_FALSE(p.SetValue("missing", false));
    REQUIRE(p.SetValue("ratio", int64_t{2}));
    REQUIRE(std::get<double>(*p.GetValue("ratio")) == 2);
}
TEST_CASE("Immediate changes notify once and suppress no-ops")
{
    SettingsPage p;
    REQUIRE(p.LoadTemplate(Template()));
    std::vector<Change> events;
    auto c = p.OnChange([&](const Change& e) { events.push_back(e); });
    REQUIRE(p.SetValue("count", int64_t{5}));
    REQUIRE(p.SetValue("count", int64_t{5}));
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].kind == ChangeKind::Confirmed);
    REQUIRE(events[0].origin == ChangeOrigin::Programmatic);
    REQUIRE(events[0].domain == ValueDomain::Effective);
    REQUIRE(std::get<int64_t>(events[0].oldValue) == 3);
    REQUIRE(p.GetValue("count") == p.GetEffectiveValue("count"));
    REQUIRE(p.IsDirty());
}
TEST_CASE("Staged edits apply as an atomic snapshot and cancel to last apply")
{
    SettingsConfig config;
    config.workflow = Workflow::Staged;
    SettingsPage p(config);
    REQUIRE(p.LoadTemplate(Template()));
    std::vector<Change> events;
    auto c = p.OnChange([&](const Change& e) { events.push_back(e); });
    REQUIRE(p.SetValue("count", int64_t{7}));
    REQUIRE(p.SetValue("name", std::string("new")));
    REQUIRE(p.HasPendingChanges());
    REQUIRE(std::get<int64_t>(*p.GetEffectiveValue("count")) == 3);
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].domain == ValueDomain::Working);
    REQUIRE(p.Apply());
    REQUIRE_FALSE(p.HasPendingChanges());
    REQUIRE(events.size() == 4);
    REQUIRE(events[2].kind == ChangeKind::Confirmed);
    REQUIRE(p.SetValue("count", int64_t{9}));
    p.Cancel();
    REQUIRE(std::get<int64_t>(*p.GetValue("count")) == 7);
    REQUIRE(events.back().origin == ChangeOrigin::Cancel);
}
TEST_CASE("JSON load validates atomically and fills missing values")
{
    SettingsPage p;
    REQUIRE(p.LoadTemplate(Template()));
    auto data = p.ExportValues();
    data["values"]["count"] = 8;
    data["values"]["enabled"] = "bad";
    REQUIRE_FALSE(p.LoadValues(data));
    REQUIRE(std::get<int64_t>(*p.GetValue("count")) == 3);
    data["values"].erase("enabled");
    data["values"]["future"] = 1;
    REQUIRE(p.LoadValues(data));
    REQUIRE(std::get<int64_t>(*p.GetValue("count")) == 8);
    REQUIRE_FALSE(p.IsDirty());
    auto invalid = data;
    invalid["values"]["color"] = "#xxxxxx";
    REQUIRE_FALSE(p.LoadValues(invalid));
}
TEST_CASE("Manual persistence round trips Unicode and uses CRLF")
{
    TempFile file;
    SettingsConfig config;
    config.valuesFile = file.path;
    SettingsPage p(config);
    REQUIRE(p.LoadTemplate(Template()));
    REQUIRE(p.SetValue("name", std::string("café · שלום · 日本語")));
    REQUIRE_FALSE(std::filesystem::exists(file.path));
    REQUIRE(p.Save());
    REQUIRE_FALSE(p.IsDirty());
    std::ifstream input(file.path, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)), {});
    REQUIRE(bytes.find("\r\n") != std::string::npos);
    input.close();
    SettingsPage other;
    REQUIRE(other.LoadTemplate(Template()));
    REQUIRE(other.LoadValuesFile(file.path));
    REQUIRE(other.GetValue("name") == p.GetValue("name"));
    REQUIRE(p.SetValue("name", std::string("replacement")));
    REQUIRE(p.Save());
    REQUIRE(other.LoadValuesFile(file.path));
    REQUIRE(other.GetValue("name") == p.GetValue("name"));
}
TEST_CASE("Autosave persists confirmed assignments and failed saving retains edits")
{
    TempFile file;
    SettingsConfig config;
    config.valuesFile = file.path;
    config.workflow = Workflow::ImmediateAutoSave;
    SettingsPage p(config);
    REQUIRE(p.LoadTemplate(Template()));
    REQUIRE(p.SetValue("count", int64_t{9}));
    REQUIRE(std::filesystem::exists(file.path));
    REQUIRE_FALSE(p.IsDirty());
    SettingsConfig bad;
    bad.workflow = Workflow::ImmediateAutoSave;
    SettingsPage failed(bad);
    REQUIRE(failed.LoadTemplate(Template()));
    REQUIRE(failed.SetValue("count", int64_t{9}));
    REQUIRE(failed.IsDirty());
    REQUIRE(failed.LastError());
    REQUIRE(std::get<int64_t>(*failed.GetEffectiveValue("count")) == 9);
}
TEST_CASE("Staged Save applies and persists, Reset respects workflow")
{
    TempFile file;
    SettingsConfig config;
    config.workflow = Workflow::Staged;
    config.valuesFile = file.path;
    SettingsPage p(config);
    REQUIRE(p.LoadTemplate(Template()));
    REQUIRE(p.SetValue("count", int64_t{6}));
    REQUIRE(p.Save());
    REQUIRE_FALSE(p.IsDirty());
    REQUIRE_FALSE(p.HasPendingChanges());
    REQUIRE(p.Reset("count"));
    REQUIRE(p.HasPendingChanges());
    REQUIRE(std::get<int64_t>(*p.GetEffectiveValue("count")) == 6);
    REQUIRE(p.ResetAll());
    p.Cancel();
    REQUIRE(std::get<int64_t>(*p.GetValue("count")) == 6);
}
TEST_CASE("Change callbacks may update a different setting")
{
    SettingsPage p;
    REQUIRE(p.LoadTemplate(Template()));
    std::vector<std::string> ids;
    auto c = p.OnChange(
        [&](const Change& e)
        {
            ids.push_back(e.id);
            if (e.id == "count")
                REQUIRE(p.SetValue("enabled", true));
        });
    REQUIRE(p.SetValue("count", int64_t{4}));
    REQUIRE(ids == std::vector<std::string>{"count", "enabled"});
}
TEST_CASE("Stable IDs survive regrouping")
{
    SettingsPage p;
    REQUIRE(p.LoadTemplate(Template()));
    REQUIRE(p.SetValue("count", int64_t{8}));
    auto saved = p.ExportValues(), schema = Template();
    schema["children"][0]["id"] = "renamed-group";
    REQUIRE(p.LoadTemplate(schema));
    REQUIRE(p.LoadValues(saved));
    REQUIRE(std::get<int64_t>(*p.GetValue("count")) == 8);
}
TEST_CASE("UTF-8 cursor movement preserves scalar boundaries")
{
    std::string text = "Aé日😀";
    REQUIRE(NextCharacter(text, 0) == 1);
    REQUIRE(NextCharacter(text, 1) == 3);
    REQUIRE(NextCharacter(text, 3) == 6);
    REQUIRE(NextCharacter(text, 6) == 10);
    REQUIRE(PreviousCharacter(text, 10) == 6);
    REQUIRE(PreviousCharacter(text, 6) == 3);
    REQUIRE(SingleLine("a\nb\rc\t") == "abc");
}

#include "Internal.hpp"
#include <LWS/Platform.hpp>
#include <LWS/TextClipboard.hpp>
#include <thread>
#include <cstdlib>
namespace LWSSettings
{
    struct SettingsPageTestAccess
    {
        static auto& Get(SettingsPage& page) { return *page.impl_; }
    };
}  // namespace LWSSettings
TEST_CASE("Text edits publish previews and one edit-wide confirmation")
{
    SettingsPage p;
    REQUIRE(p.LoadTemplate(Template()));
    auto& ui = SettingsPageTestAccess::Get(p);
    std::vector<Change> events;
    auto c = p.OnChange([&](const Change& e) { events.push_back(e); });
    ui.BeginEdit(*ui.Find("name"));
    ui.EditText("a");
    ui.EditText("ab");
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].kind == ChangeKind::Live);
    REQUIRE(ui.FinishEdit());
    REQUIRE(events.size() == 3);
    REQUIRE(events.back().kind == ChangeKind::Confirmed);
    REQUIRE(std::get<std::string>(events.back().oldValue) == "hello");
    REQUIRE(std::get<std::string>(events.back().newValue) == "ab");
    REQUIRE(ui.FinishEdit());
    REQUIRE(events.size() == 3);
}
TEST_CASE("Invalid drafts block Save and Escape restores the edit-start value")
{
    TempFile file;
    SettingsConfig config;
    config.valuesFile = file.path;
    SettingsPage p(config);
    REQUIRE(p.LoadTemplate(Template()));
    auto& ui = SettingsPageTestAccess::Get(p);
    std::vector<Change> events;
    auto c = p.OnChange([&](const Change& e) { events.push_back(e); });
    ui.BeginEdit(*ui.Find("count"));
    ui.EditText("7");
    ui.EditText("-");
    REQUIRE(std::get<int64_t>(*p.GetValue("count")) == 7);
    REQUIRE_FALSE(p.Save());
    REQUIRE_FALSE(std::filesystem::exists(file.path));
    REQUIRE(ui.FinishEdit(true));
    REQUIRE(std::get<int64_t>(*p.GetValue("count")) == 3);
    REQUIRE(events.size() == 2);
    REQUIRE(events.back().origin == ChangeOrigin::Cancel);
    REQUIRE(events.back().kind == ChangeKind::Live);
}
TEST_CASE("Autosave waits for confirmation and color cancellation restores previews")
{
    TempFile file;
    SettingsConfig config;
    config.valuesFile = file.path;
    config.workflow = Workflow::ImmediateAutoSave;
    SettingsPage p(config);
    REQUIRE(p.LoadTemplate(Template()));
    auto& ui = SettingsPageTestAccess::Get(p);
    ui.BeginEdit(*ui.Find("name"));
    ui.EditText("new");
    REQUIRE_FALSE(std::filesystem::exists(file.path));
    REQUIRE(ui.FinishEdit());
    REQUIRE(std::filesystem::exists(file.path));
    auto original = p.GetValue("color");
    ui.OpenColor(*ui.Find("color"));
    REQUIRE(ui.Assign("color", LLUtils::Color{uint32_t{0xffaabbff}}, ChangeKind::Live, ChangeOrigin::User));
    ui.CloseColor(true);
    REQUIRE(p.GetValue("color") == original);
}
TEST_CASE("Staged edit confirmation remains local until Apply")
{
    SettingsConfig config;
    config.workflow = Workflow::Staged;
    SettingsPage p(config);
    REQUIRE(p.LoadTemplate(Template()));
    auto& ui = SettingsPageTestAccess::Get(p);
    std::vector<Change> events;
    auto c = p.OnChange([&](const Change& e) { events.push_back(e); });
    ui.BeginEdit(*ui.Find("name"));
    ui.EditText("working");
    REQUIRE(ui.FinishEdit());
    REQUIRE(events.size() == 1);
    REQUIRE(events.back().domain == ValueDomain::Working);
    REQUIRE(p.Apply());
    REQUIRE(events.size() == 2);
    REQUIRE(events.back().domain == ValueDomain::Effective);
}
TEST_CASE("Programmatic updates supersede drafts only after validation")
{
    SettingsPage p;
    REQUIRE(p.LoadTemplate(Template()));
    auto& ui = SettingsPageTestAccess::Get(p);
    ui.BeginEdit(*ui.Find("count"));
    ui.EditText("-");
    REQUIRE_FALSE(p.SetValue("count", int64_t{999}));
    REQUIRE(ui.edit);
    REQUIRE(p.SetValue("count", int64_t{3}));
    REQUIRE_FALSE(ui.edit);
    REQUIRE_FALSE(p.SetValue("name", std::string("\xff")));
}
TEST_CASE("Filtering reveals descendants and restores collapsed groups")
{
    SettingsPage p;
    auto schema = Template();
    schema["children"][0]["children"][0]["description"] = "Description";
    REQUIRE(p.LoadTemplate(schema));
    auto& ui = SettingsPageTestAccess::Get(p);
    ui.collapsed.insert("g");
    ui.RebuildRows();
    REQUIRE(ui.rows.size() == 1);
    p.SetFilter("description");
    REQUIRE(ui.rows.size() == 2);
    REQUIRE(ui.rows[1].node->id == "name");
    p.SetFilter("");
    REQUIRE(ui.rows.size() == 1);
    ui.selected = "name";
    REQUIRE(ui.Description() == "Description");
    ui.hovered = "count";
    REQUIRE(ui.Description().empty());
    ui.hovered.clear();
    REQUIRE(ui.Description() == "Description");
}
TEST_CASE("UTF-8 selection replacement uses byte-safe boundaries")
{
    SettingsPage p;
    REQUIRE(p.LoadTemplate(Template()));
    auto& ui = SettingsPageTestAccess::Get(p);
    REQUIRE(p.SetValue("name", std::string("Aé日😀")));
    ui.BeginEdit(*ui.Find("name"));
    ui.edit->anchor = 1;
    ui.edit->caret = 6;
    ui.ReplaceSelection("שלום");
    REQUIRE(std::get<std::string>(*p.GetValue("name")) == "Aשלום😀");
}
TEST_CASE("Viewer renders all controls and survives close and reopen", "[viewer]")
{
    LWS::Platform::Session session;
    REQUIRE(bool(session));
    SettingsPage p;
    auto schema = Template();
    std::string description;
    for (int i = 0; i < 20; ++i)
        description += "Unicode description: café, שלום, 日本語. Longer help text wraps and scrolls independently. ";
    schema["children"][0]["children"][0]["description"] = description;
    schema["children"][0]["children"][0]["default"] = "café · שלום · 日本語 · 😀";
    REQUIRE(p.LoadTemplate(schema));
    REQUIRE(p.Show());
    auto& ui = SettingsPageTestAccess::Get(p);
    for (int i = 0; i < 10; ++i)
    {
        std::ignore = LWS::Platform::processMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE_FALSE(p.LastError());
    REQUIRE(ui.presented);
    REQUIRE(ui.canvas->Pixels().pixels.size() > 10000);
    auto capture = [&](const char* name)
    {
        const char* dir = std::getenv("LWSSETTINGS_TEST_ARTIFACTS");
        if (!dir)
            return;
        std::filesystem::create_directories(dir);
        std::ofstream output(std::filesystem::path(dir) / name, std::ios::binary);
        auto bitmap = ui.canvas->Pixels();
        output << "P6\n" << bitmap.width << ' ' << bitmap.height << "\n255\n";
        for (uint32_t y = 0; y < bitmap.height; ++y)
            for (uint32_t x = 0; x < bitmap.width; ++x)
            {
                auto offset = size_t(y) * bitmap.rowPitch + x * 4;
                char rgb[]{char(bitmap.pixels[offset + 2]), char(bitmap.pixels[offset + 1]),
                           char(bitmap.pixels[offset])};
                output.write(rgb, 3);
            }
    };
    capture("settings.ppm");
    ui.selected = "name";
    ui.pointer.reset();
    ui.hovered.clear();
    ui.Paint();
    REQUIRE(ui.descriptionHeight > 76);
    ui.Event(LWS::EventMouseWheel{-1200, {40, int((ui.height - 100) * ui.Scales().input)}});
    REQUIRE(ui.descriptionScroll > 0);
    capture("description.ppm");
    ui.OpenColor(*ui.Find("color"));
    capture("color.ppm");
    ui.CloseColor(true);
    ui.SetFilter("name");
    capture("search.ppm");
    ui.SetFilter("");
    ui.config.appearance.scale = 1.5f;
    ui.Paint();
    capture("scaled.ppm");
    REQUIRE_FALSE(p.LastError());
    ui.Event(LWS::EventClose{});
    REQUIRE_FALSE(p.IsVisible());
    REQUIRE(p.Show());
    REQUIRE(p.IsVisible());
}
#ifdef _WIN32
TEST_CASE("Windows text clipboard round trips Unicode", "[clipboard]")
{
    LWS::Platform::Session session;
    REQUIRE(bool(session));
    LWS::Window window;
    REQUIRE(window.Create() == LWS::Result::Success);
    REQUIRE(LWS::SetClipboardText(window, "café · שלום · 😀") == LWS::ClipboardResult::Success);
    bool called = false;
    LWS::RequestClipboardText(window,
                              [&](auto result, std::string text)
                              {
                                  called = true;
                                  REQUIRE(result == LWS::ClipboardResult::Success);
                                  REQUIRE(text == "café · שלום · 😀");
                              });
    REQUIRE(called);
}
#endif

TEST_CASE("Integer bounds stay exact beyond double precision")
{
    auto schema = Template();
    auto& count = schema["children"][0]["children"][1];
    count["min"] = int64_t{9007199254740992LL};
    count["max"] = int64_t{9007199254740994LL};
    count["default"] = int64_t{9007199254740993LL};
    SettingsPage p;
    REQUIRE(p.LoadTemplate(schema));
    REQUIRE_FALSE(p.SetValue("count", int64_t{9007199254740995LL}));
    REQUIRE(p.SetValue("count", int64_t{9007199254740994LL}));
}
TEST_CASE("Subscribers added during a callback start on the next notification")
{
    SettingsPage p;
    REQUIRE(p.LoadTemplate(Template()));
    std::vector<SettingsPage::Connection> extra;
    int called = 0;
    auto first = p.OnChange(
        [&](const Change&)
        {
            if (extra.empty())
                for (int i = 0; i < 50; ++i)
                    extra.push_back(p.OnChange([&](const Change&) { ++called; }));
        });
    REQUIRE(p.SetValue("count", int64_t{4}));
    REQUIRE(called == 0);
    REQUIRE(p.SetValue("count", int64_t{5}));
    REQUIRE(called == 50);
}

TEST_CASE("Long combo menus stay scrollable inside the tree viewport")
{
    SettingsPage p;
    auto schema = Template();
    auto& choice = schema["children"][0]["children"][5];
    choice["options"] = Json::array();
    choice["default"] = "v0";
    for (int i = 0; i < 40; ++i)
        choice["options"].push_back({{"label", "Option " + std::to_string(i)}, {"value", "v" + std::to_string(i)}});
    REQUIRE(p.LoadTemplate(schema));
    p.SetFilter("choice");
    auto& ui = SettingsPageTestAccess::Get(p);
    ui.selected = "choice";
    ui.dropdown = true;
    for (int i = 0; i < 20; ++i)
        ui.Key(LWS::KeyCode::Right);
    REQUIRE(std::get<std::string>(*p.GetValue("choice")) == "v20");
    REQUIRE(ui.dropdownFirst > 0);
    ui.Event(LWS::EventMouseWheel{-120, {500, 180}});
    REQUIRE(ui.dropdown);
    const auto first = ui.dropdownFirst;
    ui.Click(500, 198);
    REQUIRE_FALSE(ui.dropdown);
    REQUIRE(std::get<std::string>(*p.GetValue("choice")) == "v" + std::to_string(first));
}

#include "EventLogging.hpp"
#include <catch2/catch_approx.hpp>
TEST_CASE("DPI metrics keep physical text and native input aligned")
{
    Appearance appearance;
    for (uint32_t dpi : {96U, 120U, 144U, 192U})
    {
        auto win = CalculateScales(appearance, {dpi, dpi, 1});
        REQUIRE(14 * win.render == Catch::Approx(16 * dpi / 96.f));
        REQUIRE(win.input == win.render);
        auto wayland = CalculateScales(appearance, {dpi, dpi, dpi / 96.f});
        REQUIRE(14 * wayland.render == Catch::Approx(16 * dpi / 96.f));
        REQUIRE(wayland.input == Catch::Approx(16.f / 14));
        appearance.scale = 1.5f;
        REQUIRE(14 * CalculateScales(appearance, {dpi, dpi, 1}).render == Catch::Approx(24 * dpi / 96.f));
        appearance.scale = 1;
    }
}
TEST_CASE("Numeric steps validate metadata and preserve arithmetic boundaries")
{
    SettingsPage p;
    auto schema = Template();
    schema["children"][0]["children"][1]["step"] = 3;
    schema["children"][0]["children"][2]["step"] = 0.25;
    REQUIRE(p.LoadTemplate(schema));
    auto& ui = SettingsPageTestAccess::Get(p);
    REQUIRE(std::get<int64_t>(*StepValue(*ui.Find("count"), int64_t{9}, 1)) == 10);
    REQUIRE(std::get<int64_t>(*StepValue(*ui.Find("count"), int64_t{1}, -1)) == 0);
    REQUIRE_FALSE(StepValue(*ui.Find("count"), int64_t{10}, 1));
    REQUIRE(std::get<double>(*StepValue(*ui.Find("ratio"), 1.5, 1)) == 1.75);
    Node integer;
    integer.control = Control::Integer;
    integer.integerStep = INT64_MAX;
    REQUIRE(std::get<int64_t>(*StepValue(integer, int64_t{1}, 1)) == INT64_MAX);
    REQUIRE(std::get<int64_t>(*StepValue(integer, int64_t{-2}, -1)) == INT64_MIN);
    Node decimal;
    decimal.control = Control::Number;
    Value v = 0.0;
    for (int i = 0; i < 3; ++i)
        v = *StepValue(decimal, v, 1);
    REQUIRE(FormatValue(v) == "0.3");
    REQUIRE(FormatValue(*StepValue(decimal, Value{0.15}, 1)) == "0.25");
    for (Json bad : std::vector<Json>{0, -1, 0.5, "bad"})
    {
        schema["children"][0]["children"][1]["step"] = bad;
        REQUIRE_FALSE(p.LoadTemplate(schema));
    }
    schema = Template();
    schema["children"][0]["children"][2]["step"] = 0;
    REQUIRE_FALSE(p.LoadTemplate(schema));
}
TEST_CASE("Rocker holds preview repeatedly and confirm once for autosave")
{
    TempFile file;
    SettingsConfig config;
    config.workflow = Workflow::ImmediateAutoSave;
    config.valuesFile = file.path;
    SettingsPage p(config);
    REQUIRE(p.LoadTemplate(Template()));
    auto& ui = SettingsPageTestAccess::Get(p);
    std::vector<Change> events;
    auto connection = p.OnChange([&](const auto& e) { events.push_back(e); });
    REQUIRE(ui.StartSpin(*ui.Find("count"), 1, false));
    REQUIRE(std::get<int64_t>(*p.GetValue("count")) == 4);
    auto due = ui.spin->next;
    ui.SpinTick(due - std::chrono::milliseconds(1));
    REQUIRE(events.size() == 1);
    ui.SpinTick(due);
    ui.SpinTick(due + std::chrono::milliseconds(80));
    REQUIRE(std::get<int64_t>(*p.GetValue("count")) == 6);
    REQUIRE_FALSE(std::filesystem::exists(file.path));
    ui.StopSpin();
    REQUIRE(events.size() == 4);
    REQUIRE(events.back().kind == ChangeKind::Confirmed);
    REQUIRE(std::get<int64_t>(events.back().oldValue) == 3);
    REQUIRE(std::filesystem::exists(file.path));
    ui.SpinTick(due + std::chrono::seconds(2));
    REQUIRE(events.size() == 4);
}
TEST_CASE("Rocker cancellation invalid drafts and external updates stop repeats")
{
    SettingsPage p;
    REQUIRE(p.LoadTemplate(Template()));
    auto& ui = SettingsPageTestAccess::Get(p);
    ui.BeginEdit(*ui.Find("count"));
    ui.EditText("-");
    REQUIRE_FALSE(ui.StartSpin(*ui.Find("count"), 1, false));
    REQUIRE(std::get<int64_t>(*p.GetValue("count")) == 3);
    REQUIRE(ui.FinishEdit(true));
    REQUIRE(ui.StartSpin(*ui.Find("count"), 1, true));
    ui.SpinOnce();
    ui.StopSpin(true);
    REQUIRE(std::get<int64_t>(*p.GetValue("count")) == 3);
    REQUIRE(ui.StartSpin(*ui.Find("count"), 1, false));
    REQUIRE(p.SetValue("count", int64_t{9}));
    REQUIRE_FALSE(ui.spin);
    REQUIRE_FALSE(ui.edit);
}
TEST_CASE("Staged numeric rocker commits only on Apply and keyboard release ends a hold")
{
    SettingsConfig config;
    config.workflow = Workflow::Staged;
    SettingsPage p(config);
    REQUIRE(p.LoadTemplate(Template()));
    auto& ui = SettingsPageTestAccess::Get(p);
    std::vector<Change> events;
    auto connection = p.OnChange([&](const auto& e) { events.push_back(e); });
    ui.BeginEdit(*ui.Find("count"));
    ui.Key(LWS::KeyCode::Up);
    ui.Key(LWS::KeyCode::Up);
    ui.Event(LWS::EventKeyUp{LWS::KeyCode::Up});
    REQUIRE_FALSE(ui.spin);
    REQUIRE(events.size() == 2);
    REQUIRE(std::get<int64_t>(*p.GetEffectiveValue("count")) == 3);
    REQUIRE(p.Apply());
    REQUIRE(events.size() == 3);
    REQUIRE(events.back().kind == ChangeKind::Confirmed);
}
TEST_CASE("Combo popup hover owns hit testing and description without changing values")
{
    auto schema = Template();
    schema["children"][0]["children"][5]["description"] = "Combo description";
    schema["children"][0]["children"][0]["description"] = "Name description";
    SettingsPage p;
    REQUIRE(p.LoadTemplate(schema));
    auto& ui = SettingsPageTestAccess::Get(p);
    ui.height = 900;
    ui.RebuildRows();
    ui.selected = "choice";
    ui.dropdown = true;
    const auto before = p.ExportValues();
    int notifications = 0;
    auto subscription = p.OnChange([&](const auto&) { ++notifications; });
    auto box = ui.DropdownBox();
    const auto input = ui.Scales().input;
    ui.Event(LWS::EventMouseMove{{int((box.x + 10) * input), int((box.y + 42) * input)}, {}});
    REQUIRE(ui.dropdownHovered == 1);
    REQUIRE(ui.hovered == "choice");
    REQUIRE(ui.Description() == "Combo description");
    REQUIRE(p.ExportValues() == before);
    REQUIRE(notifications == 0);
    ui.Event(LWS::EventMouseMove{{int(40 * input), int(160 * input)}, {}});
    REQUIRE_FALSE(ui.dropdownHovered);
    REQUIRE(ui.hovered == "name");
    REQUIRE(ui.Description() == "Name description");
    ui.Event(LWS::EventMouseLeave{});
    REQUIRE_FALSE(ui.dropdownHovered);
    REQUIRE(ui.hovered.empty());
}
TEST_CASE("Deep groups retain label space breadcrumbs search and reset behavior")
{
    auto leaf = Json{{"type", "text"},
                     {"id", "leaf"},
                     {"label", "Leaf"},
                     {"default", "default"},
                     {"description", "Deep setting"}};
    for (int i = 63; i >= 0; --i)
        leaf = Json{{"type", "group"},
                    {"id", "g" + std::to_string(i)},
                    {"label", "Group " + std::to_string(i)},
                    {"children", Json::array({leaf})}};
    SettingsPage p;
    REQUIRE(p.LoadTemplate(Json{{"version", 1}, {"children", Json::array({leaf})}}));
    auto& ui = SettingsPageTestAccess::Get(p);
    ui.width = 480;
    ui.RebuildRows();
    REQUIRE(ui.rows.size() == 65);
    REQUIRE(ui.ControlLeft() - ui.LabelLeft(64) - 14 >= 160);
    REQUIRE(ui.breadcrumbs.at("leaf").find("Group 63 / Leaf") != std::string::npos);
    ui.collapsed.insert("g0");
    ui.RebuildRows();
    REQUIRE(ui.rows.size() == 1);
    p.SetFilter("Deep setting");
    REQUIRE(ui.rows.size() == 65);
    p.SetFilter("");
    REQUIRE(ui.rows.size() == 1);
    REQUIRE(p.SetValue("leaf", std::string("different")));
    REQUIRE(ui.ValueForeground(*ui.Find("leaf")) == ui.config.appearance.nonDefaultForeground);
    REQUIRE(p.Reset("leaf"));
    REQUIRE(ui.ValueForeground(*ui.Find("leaf")) == ui.config.appearance.foreground);
}
TEST_CASE("Non-default highlighting survives save and follows staged cancellation")
{
    TempFile file;
    SettingsConfig config;
    config.workflow = Workflow::Staged;
    config.valuesFile = file.path;
    SettingsPage p(config);
    REQUIRE(p.LoadTemplate(Template()));
    auto& ui = SettingsPageTestAccess::Get(p);
    REQUIRE(p.SetValue("count", int64_t{8}));
    REQUIRE(ui.ValueForeground(*ui.Find("count")) == config.appearance.nonDefaultForeground);
    p.Cancel();
    REQUIRE(ui.ValueForeground(*ui.Find("count")) == config.appearance.foreground);
    REQUIRE(p.SetValue("count", int64_t{8}));
    REQUIRE(p.Save());
    REQUIRE_FALSE(p.IsDirty());
    REQUIRE(ui.ValueForeground(*ui.Find("count")) == config.appearance.nonDefaultForeground);
}
TEST_CASE("Console notifications serialize every event field with typed values")
{
    Change event{"unicode-שלום",        int64_t{1}, int64_t{2}, ChangeKind::Confirmed, ChangeOrigin::Programmatic,
                 ValueDomain::Effective};
    std::ostringstream output;
    Example::PrintEvent(output, event);
    auto record = Json::parse(output.str());
    REQUIRE(record.size() == 7);
    REQUIRE(record["id"] == event.id);
    REQUIRE(record["oldValue"].is_number_integer());
    REQUIRE(record["newValue"] == 2);
    REQUIRE(record["kind"] == "confirmed");
    REQUIRE(record["origin"] == "programmatic");
    REQUIRE(record["domain"] == "effective");
    std::vector<Value> types = {true, int64_t{8}, 1.25, std::string("line\nquoted \"value\""),
                                LLUtils::Color{uint32_t{0x10203040}}};
    for (const auto& v : types)
    {
        event.oldValue = event.newValue = v;
        event.kind = ChangeKind::Live;
        event.domain = ValueDomain::Working;
        event.origin = ChangeOrigin::Cancel;
        auto j = Example::EventJson(event);
        REQUIRE(j["kind"] == "live");
        REQUIRE(j["domain"] == "working");
        REQUIRE(j["origin"] == "cancel");
        if (std::holds_alternative<LLUtils::Color>(v))
            REQUIRE(j["newValue"] == "#10203040");
    }
}
TEST_CASE("Showcase contains all editors across three nested scopes")
{
    SettingsPage p;
    REQUIRE(p.LoadTemplateFile(std::filesystem::path(LWSSETTINGS_EXAMPLES_DIR) / "settings.json"));
    REQUIRE(p.LoadValuesFile(std::filesystem::path(LWSSETTINGS_EXAMPLES_DIR) / "showcase-values.json"));
    auto& ui = SettingsPageTestAccess::Get(p);
    ui.RebuildRows();
    std::set<Control> controls;
    int maxDepth = 0;
    for (const auto& row : ui.rows)
    {
        controls.insert(row.node->control);
        maxDepth = std::max(maxDepth, row.depth);
    }
    REQUIRE(controls.size() == 9);
    REQUIRE(maxDepth == 3);
    REQUIRE_FALSE(p.IsDirty());
    REQUIRE(ui.ValueForeground(*ui.Find("fullscreen")) == ui.config.appearance.nonDefaultForeground);
}

TEST_CASE("Rocker mouse hit areas stop repeating when the pointer departs")
{
    SettingsPage p;
    REQUIRE(p.LoadTemplate(Template()));
    auto& ui = SettingsPageTestAccess::Get(p);
    ui.height = 900;
    ui.RebuildRows();
    auto row = *std::find_if(ui.rows.begin(), ui.rows.end(), [](const auto& r) { return r.node->id == "count"; });
    const auto scale = ui.Scales().input;
    LWS::Point press{int((ui.width - 98) * scale), int((100 + row.y + 14) * scale)};
    int confirms = 0;
    auto listener = p.OnChange(
        [&](const auto& e)
        {
            if (e.kind == ChangeKind::Confirmed)
                ++confirms;
        });
    ui.Event(LWS::EventMouseButton{LWS::MouseButton::Left, true, press});
    REQUIRE(ui.spin);
    REQUIRE(std::get<int64_t>(*p.GetValue("count")) == 4);
    ui.Event(LWS::EventMouseMove{{0, 0}, {}});
    REQUIRE_FALSE(ui.spin);
    REQUIRE(confirms == 1);
}
namespace
{
    void CaptureSettingsFrame(LWS::BitmapBuffer bitmap, const char* name)
    {
        const auto* directory = std::getenv("LWSSETTINGS_TEST_ARTIFACTS");
        if (!directory)
            return;
        std::filesystem::create_directories(directory);
        std::ofstream out(std::filesystem::path(directory) / name, std::ios::binary);
        out << "P6\n" << bitmap.width << ' ' << bitmap.height << "\n255\n";
        for (uint32_t y = 0; y < bitmap.height; ++y)
            for (uint32_t x = 0; x < bitmap.width; ++x)
            {
                const auto offset = size_t(y) * bitmap.rowPitch + x * 4;
                char pixel[]{char(bitmap.pixels[offset + 2]), char(bitmap.pixels[offset + 1]),
                             char(bitmap.pixels[offset])};
                out.write(pixel, 3);
            }
    }
}  // namespace
TEST_CASE("Showcase previews transparent colors and highlights combo hover", "[viewer]")
{
    LWS::Platform::Session session;
    REQUIRE(bool(session));
    SettingsPage p;
    REQUIRE(p.LoadTemplateFile(std::filesystem::path(LWSSETTINGS_EXAMPLES_DIR) / "settings.json"));
    REQUIRE(p.LoadValuesFile(std::filesystem::path(LWSSETTINGS_EXAMPLES_DIR) / "showcase-values.json"));
    REQUIRE(p.Show());
    auto& ui = SettingsPageTestAccess::Get(p);
    for (int i = 0; i < 10; ++i)
    {
        std::ignore = LWS::Platform::processMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ui.pointer.reset();
    ui.selected = "scale";
    ui.Paint();
    REQUIRE(ui.presented);
    REQUIRE_FALSE(p.LastError());
    CaptureSettingsFrame(ui.canvas->Pixels(), "showcase.ppm");
    ui.Select("accent");
    ui.Paint();
    CaptureSettingsFrame(ui.canvas->Pixels(), "nested.ppm");
    ui.Select("theme");
    ui.dropdown = true;
    ui.scroll = 0;
    ui.RebuildRows();
    auto popup = ui.DropdownBox();
    ui.pointer = LWS::Point{int((popup.x + 12) * ui.Scales().input), int((popup.y + 12) * ui.Scales().input)};
    const auto before = p.ExportValues();
    ui.Paint();
    REQUIRE(ui.dropdownHovered == 0);
    REQUIRE(ui.Description() == ui.Find("theme")->description);
    REQUIRE(p.ExportValues() == before);
    CaptureSettingsFrame(ui.canvas->Pixels(), "combo-hover.ppm");
    const auto color = *p.GetValue("accent");
    ui.pointer.reset();
    ui.OpenColor(*ui.Find("accent"));
    REQUIRE(ui.Assign("accent", LLUtils::Color{uint32_t{0x3978db40}}, ChangeKind::Live, ChangeOrigin::User));
    REQUIRE(*ui.colorStart == color);
    CaptureSettingsFrame(ui.canvas->Pixels(), "color-preview.ppm");
    ui.CloseColor(true);
    REQUIRE(*p.GetValue("accent") == color);
}
#ifdef _WIN32
TEST_CASE("F4 opens and cancels the file picker while its text editor is focused", "[viewer][dialog]")
{
    LWS::Platform::Session session;
    REQUIRE(bool(session));
    SettingsPage p;
    REQUIRE(p.LoadTemplate(Template()));
    REQUIRE(p.Show());
    auto& ui = SettingsPageTestAccess::Get(p);
    ui.Select("file");
    ui.BeginEdit(*ui.Find("file"));
    int changes = 0;
    auto listener = p.OnChange([&](const auto&) { ++changes; });
    struct Driver
    {
        HWND owner;
        DWORD thread;
        std::atomic_bool appeared = false;
        std::jthread worker;
        Driver(HWND w)
            : owner(w), thread(GetCurrentThreadId()),
              worker(
                  [this](std::stop_token stop)
                  {
                      while (!stop.stop_requested())
                      {
                          EnumThreadWindows(
                              thread,
                              [](HWND candidate, LPARAM data) -> BOOL
                              {
                                  auto& self = *reinterpret_cast<Driver*>(data);
                                  wchar_t kind[32]{};
                                  if (GetWindow(candidate, GW_OWNER) != self.owner || !IsWindowVisible(candidate))
                                      return TRUE;
                                  GetClassNameW(candidate, kind, 32);
                                  if (std::wstring_view(kind) == L"#32770")
                                  {
                                      self.appeared = true;
                                      PostMessageW(candidate, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
                                  }
                                  return TRUE;
                              },
                              reinterpret_cast<LPARAM>(this));
                          std::this_thread::sleep_for(std::chrono::milliseconds(100));
                      }
                  })
        {
        }
        void Stop()
        {
            worker.request_stop();
            if (worker.joinable())
                worker.join();
        }
        ~Driver() { Stop(); }
    } driver(reinterpret_cast<HWND>(ui.window->GetHandle()));
    ui.Key(LWS::KeyCode::F4);
    driver.Stop();
    REQUIRE(driver.appeared);
    REQUIRE_FALSE(ui.modal);
    REQUIRE_FALSE(p.LastError());
    REQUIRE(changes == 0);
    REQUIRE(std::get<std::string>(*p.GetValue("file")).empty());
}
#endif
