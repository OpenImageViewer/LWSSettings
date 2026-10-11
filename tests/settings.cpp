#include "ThemeFields.hpp"
#include <LWSSettings/SettingsEditor.hpp>
#include <LWSSettings/SettingsStorage.hpp>
#include <nlohmann/json.hpp>
#include <LWSSettings/ThemeStorage.hpp>
#include <LWSSettings/ThemeEditor.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <LWSUI/TextEditing.hpp>
#include <LWS/Platform.hpp>
#ifdef LWS_HAS_WIN32_BACKEND
#include <LWS/Win32/Platform.hpp>
#include <LWS/Win32/WindowExtensions.hpp>
#include <commctrl.h>
#endif
#include <LWS/TextClipboard.hpp>
#include <catch2/catch_approx.hpp>
#include "EventLogging.hpp"
#include "SettingsIcon.hpp"
#include <thread>
#include <set>
#include <atomic>
#include <iostream>
namespace LWSSettings::internal
{
    struct ThemeEditorTestAccess
    {
        static SettingsEditor& Editor(ThemeEditor& editor) { return editor.editor_; }
    };
}
namespace LWSUI::internal
{
    struct UIHostTestAccess
    {
        static void RenderTooltip(UIHost& host, Canvas& canvas) { host.RenderTooltip(canvas); }
    };
}
using LWSUI::NextCharacter;
using LWSUI::PreviousCharacter;
using LWSUI::SingleLine;
using namespace LWSSettings;
using Json = nlohmann::json;
namespace
{
    struct TestPlatform
    {
        LWS::PlatformContext value;
        operator LWS::PlatformContext&() { return value; }
        TestPlatform()
        {
#ifdef LWS_HAS_WIN32_BACKEND
            REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
            REQUIRE(value.Init({LWS::BackendId::Win32}) == LWS::Result::Success);
#else
            REQUIRE(value.Init({LWS::BackendId::Wayland}) == LWS::Result::Success);
#endif
        }
    };
    void WaitForConfiguration(LWS::Window& window)
    {
        // Wayland publishes its first pixel metrics asynchronously after showing.
        for (int i = 0; i < 500 && !window.IsConfigured(); ++i)
        {
            REQUIRE(window.GetPlatformContext().ProcessMessages() == LWS::LoopResult::Continue);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        REQUIRE(window.IsConfigured());
    }
    void PumpFor(LWS::PlatformContext& platform, int milliseconds)
    {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        do
        {
            REQUIRE(platform.ProcessMessages() == LWS::LoopResult::Continue);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (std::chrono::steady_clock::now() < until);
    }
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

namespace
{
    using Phase = LWSUI::EditPhase;
    using Input = LWSUI::Input;
    using Kind = LWSUI::InputKind;
    using Key = LWS::KeyCode;
    void KeyPress(LWSUI::Control& control, Key key, bool shift = false, bool ctrl = false)
    {
        Input input{Kind::KeyDown};
        input.key = key;
        input.shift = shift;
        input.control = ctrl;
        control.Dispatch(input);
    }
    void Draft(LWSUI::TextBox& text, std::string value)
    {
        KeyPress(text, Key::A, false, true);
        Input input{Kind::Text};
        input.text = std::move(value);
        text.Dispatch(input);
    }
    template <class T>
    T* FindControl(LWSUI::Control& control)
    {
        if (auto* match = dynamic_cast<T*>(&control))
            return match;
        if (auto* parent = dynamic_cast<LWSUI::Container*>(&control))
            for (auto& child : parent->Children())
                if (auto* match = FindControl<T>(*child))
                    return match;
        return nullptr;
    }
    template <class T>
    std::vector<T*> Controls(LWSUI::Control& control)
    {
        std::vector<T*> result;
        if (auto* match = dynamic_cast<T*>(&control))
            result.push_back(match);
        if (auto* parent = dynamic_cast<LWSUI::Container*>(&control))
            for (auto& child : parent->Children())
            {
                auto list = Controls<T>(*child);
                result.insert(result.end(), list.begin(), list.end());
            }
        return result;
    }
    LWSUI::Button* FindButton(LWSUI::Control& root, std::string_view caption)
    {
        for (auto* button : Controls<LWSUI::Button>(root))
            if (button->Text() == caption) return button;
        return nullptr;
    }
    void ClickButton(LWSUI::Button& button)
    {
        const auto bounds = button.Bounds();
        Input input{Kind::Down};
        input.x = bounds.x + bounds.width / 2;
        input.y = bounds.y + bounds.height / 2;
        if (button.Host()) button.Host()->Route(input);
        else button.Dispatch(input);
        input.kind = Kind::Up;
        if (button.Host()) button.Host()->Route(input);
        else button.Dispatch(input);
    }
    void Layout(LWSUI::Control& control, float width = 820, float height = 680)
    {
        control.Measure({width, height});
        control.Arrange({0, 0, width, height});
    }
}  // namespace
TEST_CASE("Templates create typed defaults without a platform session")
{
    SettingsDocument p;
    REQUIRE(SettingsStorage::LoadTemplate(p, Template()));
    REQUIRE(std::get<int64_t>(*p.WorkingValue("count")) == 3);
    REQUIRE(std::get<double>(*p.WorkingValue("ratio")) == 1.5);
    REQUIRE(SettingsStorage::EncodeValues(p.WorkingValues())["values"]["color"] == "#123456ff");
    REQUIRE_FALSE(p.WorkingValue("g"));
    REQUIRE_FALSE(p.IsDirty());
    LWS::PlatformContext platform; // Headless document/theme operations do not initialize a backend.
    SettingsEditor editor(platform);
    REQUIRE_FALSE(editor.Show());
    SettingsTheme theme;
    REQUIRE(theme.search.spacing == 8);
    REQUIRE(theme.base.spacing == 0);
    auto sparseTheme = ThemeStorage::Decode(Json{{"version", 1}});
    REQUIRE(sparseTheme);
    REQUIRE(sparseTheme->search.spacing == 8);
    auto baseSpacing = ThemeStorage::Decode(Json::parse(
        R"({"version":1,"base":{"metrics":{"spacing":12}}})"));
    REQUIRE(baseSpacing);
    REQUIRE(baseSpacing->search.spacing == 12);
    for (float spacing : {16.f, 0.f})
    {
        auto custom = Json::parse(R"({"version":1,"base":{"metrics":{"spacing":12}},"sections":{"search":{"metrics":{"spacing":0}}}})");
        custom["sections"]["search"]["metrics"]["spacing"] = spacing;
        auto resolved = ThemeStorage::Decode(custom);
        REQUIRE(resolved);
        REQUIRE(resolved->search.spacing == spacing);
        REQUIRE(resolved->tree.spacing == 12);
    }
    auto exported = ThemeStorage::Encode(theme);
    auto decoded = ThemeStorage::Decode(exported);
    REQUIRE(decoded);
    REQUIRE(ThemeStorage::Encode(*decoded) == exported);
    auto partial = ThemeStorage::Decode(Json::parse(
        R"({"version":1,"base":{"typography":{"font":{"family":"Arial","size":22,"weight":600,"italic":true}}},"sections":{"description":{"colors":{"background":"#203040"}},"tree":{"colors":{"searchBackground":"#ffaa3380"}}},"layout":{"descriptionHeight":220}})"));
    REQUIRE(partial);
    REQUIRE(partial->controls.font.size == 22);
    REQUIRE(partial->description.font.family == "Arial");
    REQUIRE(partial->description.background == LLUtils::Color{uint32_t{0x203040ff}});
    REQUIRE(partial->tree.searchBackground == LLUtils::Color{uint32_t{0xffaa3380}});
    REQUIRE(partial->layout.descriptionHeight == 220);
    REQUIRE(partial->tree.resetRightMargin == SettingsTheme{}.layout.horizontalPadding);
    auto marginTheme = ThemeStorage::Decode(Json::parse(
        R"({"version":1,"base":{"metrics":{"resetRightMargin":24}},"sections":{"tree":{"metrics":{"resetRightMargin":0,"padding":120}}}})"));
    REQUIRE(marginTheme);
    REQUIRE(marginTheme->header.resetRightMargin == 24);
    REQUIRE(marginTheme->tree.resetRightMargin == 0);
    REQUIRE(marginTheme->tree.padding == 120);
    REQUIRE(marginTheme->tree.resetWidth == SettingsTheme{}.tree.resetWidth);
    REQUIRE_FALSE(ThemeStorage::Decode(Json::parse(
        R"({"version":1,"sections":{"tree":{"metrics":{"resetRightMargin":-1}}}})")));

    REQUIRE_FALSE(ThemeStorage::Decode(Json::parse(R"({"version":1,"base":{"typography":{"font":{"size":-1}}}})")));
    REQUIRE_FALSE(ThemeStorage::Decode(
        Json::parse(R"({"version":1,"sections":{"description":{"colors":{"background":"wrong"}}}})")));
    REQUIRE_FALSE(ThemeStorage::Decode(Json::parse(R"({"version":1,"base":{"metrics":{"unknown":1}}})")));
    TempFile themeFile;
    REQUIRE(ThemeStorage::SaveFile(themeFile.path, *partial));
    auto loaded = ThemeStorage::LoadFile(themeFile.path);
    REQUIRE(loaded);
    REQUIRE(ThemeStorage::Encode(*loaded) == ThemeStorage::Encode(*partial));
    std::ifstream themeInput(themeFile.path, std::ios::binary);
    std::string themeBytes((std::istreambuf_iterator<char>(themeInput)), {});
    REQUIRE(themeBytes.find("\r\n") != std::string::npos);
    std::ifstream templateFile(std::filesystem::path(LWSSETTINGS_EXAMPLES_DIR) / "theme-settings.json");
    REQUIRE(Json::parse(templateFile) == ThemeStorage::Template());
    TempFile liveFile;
    REQUIRE(ThemeStorage::SaveOverridesFile(liveFile.path, {}));
    ThemeEditor live(platform, liveFile.path);
    REQUIRE(internal::ThemeEditorTestAccess::Editor(live).Document().Definitions().empty());  // Editor definitions are lazy.
    REQUIRE(live.Load());
    REQUIRE(internal::ThemeEditorTestAccess::Editor(live).Document().WorkingValues().size() == ThemeFields::All().size() * 2 + 1);
    auto oldTemplate = ThemeStorage::Template();
    auto removeField = [&](auto&& self, Json& nodes, std::string_view suffix) -> void
    {
        for (auto it = nodes.begin(); it != nodes.end();)
        {
            if (it->at("id").get<std::string>().ends_with(suffix)) it = nodes.erase(it);
            else
            {
                if (it->contains("children")) self(self, (*it)["children"], suffix);
                ++it;
            }
        }
    };
    for (auto suffix :
         {"/resetRightMargin", "/menuBarHeight", "/menuBarPadding", "/menuArrowWidth", "/menuHoverDelayMs"})
        removeField(removeField, oldTemplate["children"], suffix);
    oldTemplate["children"][0]["label"] = "Custom base group";
    oldTemplate["children"][0]["children"][0]["children"][0]["label"] = "Custom background";
    TempFile legacyTemplate;
    auto writeLegacy = [&]
    {
        std::ofstream output(legacyTemplate.path, std::ios::binary);
        output << oldTemplate.dump() << "\r\n";
    };
    writeLegacy();
    ThemeEditor legacy(platform, liveFile.path);
    REQUIRE(legacy.Load(legacyTemplate.path));
    const auto& migrated = internal::ThemeEditorTestAccess::Editor(legacy).Document();
    REQUIRE(migrated.Definitions()[0].Info().label == "Custom base group");
    REQUIRE(migrated.Find("base/colors/background")->label == "Custom background");
    for (const auto& field : ThemeFields::All())
        if (field.path.ends_with("/resetRightMargin"))
        {
            REQUIRE(migrated.Find(field.path));
            REQUIRE(migrated.WorkingValue(field.path) == field.definition.DefaultValue());
        }
    REQUIRE_FALSE(legacy.Overrides()->contains("sections/tree/metrics/resetRightMargin"));
    auto oldMarginDefaults = ThemeStorage::Template();
    auto oldMargin = [&](auto&& self, Json& nodes) -> void
    {
        for (auto& node : nodes)
            if (node.contains("children")) self(self, node["children"]);
            else if (node["id"].get<std::string>().ends_with("/colors/changed")) node["default"] = "#f2c66dff";
            else if (node["id"].get<std::string>().ends_with("/metrics/resetRightMargin")) node["default"] = 8;
            else if (node["id"] == "sections/search/metrics/spacing") node["default"] = 0;
            else if (node["id"].get<std::string>().ends_with("/metrics/popupWidth")) node["default"] = double(3520.f / 7);
            else if (node["id"].get<std::string>().ends_with("/metrics/popupHeight")) node["default"] = double(3040.f / 7);
    };
    oldMargin(oldMargin, oldMarginDefaults["children"]);
    {
        std::ofstream output(legacyTemplate.path, std::ios::binary);
        output << oldMarginDefaults.dump() << "\r\n";
    }
    REQUIRE(legacy.Load(legacyTemplate.path));
    REQUIRE(internal::ThemeEditorTestAccess::Editor(legacy).Document().WorkingValue("sections/tree/metrics/resetRightMargin") ==
            Value{double(SettingsTheme{}.layout.horizontalPadding)});
    REQUIRE(internal::ThemeEditorTestAccess::Editor(legacy).Document().WorkingValue("sections/search/metrics/spacing") == Value{8.0});
    REQUIRE(internal::ThemeEditorTestAccess::Editor(legacy).Document().WorkingValue("sections/popups/metrics/popupWidth") == Value{720.0});
    REQUIRE(internal::ThemeEditorTestAccess::Editor(legacy).Document().WorkingValue("sections/popups/metrics/popupHeight") == Value{600.0});
    auto explicitPopup = ThemeStorage::Decode(Json::parse(
        R"({"version":1,"sections":{"popups":{"metrics":{"popupWidth":500,"popupHeight":400}}}})"));
    REQUIRE(explicitPopup);
    REQUIRE(explicitPopup->popups.popupWidth == 500);
    REQUIRE(explicitPopup->popups.popupHeight == 400);
    REQUIRE_FALSE(legacy.Overrides()->contains("sections/search/metrics/spacing"));
    REQUIRE(internal::ThemeEditorTestAccess::Editor(legacy).Document().WorkingValue("base/colors/changed") == Value{LLUtils::Color{uint32_t{0xfbbf24ff}}});
    REQUIRE(ThemeStorage::SaveOverridesFile(liveFile.path, {{"base/colors/changed", LLUtils::Color{uint32_t{0xf2c66dff}}}}));
    REQUIRE(legacy.Load(legacyTemplate.path));
    REQUIRE(legacy.Overrides()->contains("base/colors/changed"));
    REQUIRE(internal::ThemeEditorTestAccess::Editor(legacy).Document().WorkingValue("base/colors/changed") == Value{LLUtils::Color{uint32_t{0xf2c66dff}}});
    REQUIRE(ThemeStorage::SaveOverridesFile(liveFile.path, {{"sections/search/metrics/spacing", 0.0}}));
    REQUIRE(legacy.Load(legacyTemplate.path));
    REQUIRE(internal::ThemeEditorTestAccess::Editor(legacy).Document().WorkingValue("sections/search/metrics/spacing") == Value{0.0});
    REQUIRE(legacy.Overrides()->contains("sections/search/metrics/spacing"));
    REQUIRE(ThemeStorage::SaveOverridesFile(liveFile.path, {}));
    auto explicitMargin = ThemeStorage::Decode(Json::parse(
        R"({"version":1,"sections":{"tree":{"metrics":{"resetRightMargin":8}}}})"));
    REQUIRE(explicitMargin);
    REQUIRE(explicitMargin->tree.resetRightMargin == 8); // Explicit overrides remain authoritative.
    removeField(removeField, oldTemplate["children"], "base/metrics/rowHeight");
    writeLegacy();
    REQUIRE_FALSE(legacy.Load(legacyTemplate.path)); // Unrelated missing fields still fail.

    SettingsDocument publicTemplate;
    REQUIRE(SettingsStorage::LoadTemplate(publicTemplate, ThemeStorage::Template()));
    REQUIRE(publicTemplate.WorkingValues().size() == ThemeFields::All().size());
    for (const auto& field : ThemeFields::All())
    {
        REQUIRE(publicTemplate.Find(field.path));
        REQUIRE(publicTemplate.Find(field.path)->visibleInEditor);
    }
    auto& document = internal::ThemeEditorTestAccess::Editor(live).Document();
    SettingsTheme preview;
    auto previewConnection = live.OnPreview.Connect([&](const SettingsTheme& t) { preview = t; });
    const std::string base = "base/colors/accent", section = "sections/description/colors/accent";
    const LLUtils::Color green{uint32_t{0x118833ff}}, red{uint32_t{0xee2244ff}};
    REQUIRE(document.SetValue(base, green));
    REQUIRE(live.Overrides()->contains(base));
    REQUIRE(preview.description.accent == green);
    REQUIRE(document.WorkingValue(section) == Value{green});
    REQUIRE(document.SetValue(section, red, EditPhase::Preview, ChangeOrigin::User));
    REQUIRE(live.Overrides()->contains(section));
    REQUIRE(preview.description.accent == red);
    REQUIRE(document.SetValue(section, red, EditPhase::Cancel, ChangeOrigin::User));
    REQUIRE_FALSE(live.Overrides()->contains(section));
    REQUIRE(preview.description.accent == green);
    REQUIRE(document.SetValue(section, red));
    REQUIRE(document.SetValue(base, LLUtils::Color{uint32_t{0x1111eeff}}));
    REQUIRE(preview.description.accent == red);
    REQUIRE(document.Reset(section));
    REQUIRE(preview.description.accent == preview.base.accent);
    REQUIRE(live.Save());
    auto savedProfile = ThemeStorage::LoadProfileFile(liveFile.path);
    REQUIRE(savedProfile);
    std::optional<ThemeOverrides> patch{savedProfile->overrides};
    REQUIRE(patch.has_value());
    REQUIRE(patch->size() == 1);
    REQUIRE(patch->contains(base));
    REQUIRE(document.SetValue(section, red));
    live.Revert();
    REQUIRE(live.Overrides());
    REQUIRE(*live.Overrides() == *patch);
    REQUIRE_FALSE(document.IsDirty());
    // Returning a preview to its starting value must still finish its override session.
    const auto inherited = *document.WorkingValue(section);
    REQUIRE(document.SetValue(section, red, EditPhase::Preview, ChangeOrigin::User));
    REQUIRE(document.SetValue(section, inherited, EditPhase::Preview, ChangeOrigin::User));
    REQUIRE(document.SetValue(section, inherited, EditPhase::Commit, ChangeOrigin::User));
    REQUIRE(document.SetValue(section, red, EditPhase::Preview, ChangeOrigin::User));
    REQUIRE(document.SetValue(section, red, EditPhase::Cancel, ChangeOrigin::User));
    REQUIRE(live.Overrides()->contains(section));
    // Removing an explicit value equal to its inherited value still changes override state.
    REQUIRE(document.Reset(section));
    REQUIRE_FALSE(live.Overrides()->contains(section));
    REQUIRE(document.WorkingValue(section) == inherited);
    REQUIRE(document.ResetAll());
    REQUIRE(live.Overrides()->empty());
    REQUIRE(preview.description.accent == SettingsTheme{}.description.accent);
    live.Revert();
    REQUIRE(live.Overrides());
    REQUIRE(*live.Overrides() == *patch);
    REQUIRE(document.SetValue(section, red, EditPhase::Preview, ChangeOrigin::User));
    REQUIRE(document.ResetAll());
    REQUIRE(live.Overrides()->empty());
    ThemeEditor failing(platform, liveFile.path / "cannot-write.json");
    REQUIRE(failing.Load());
    REQUIRE(internal::ThemeEditorTestAccess::Editor(failing).Document().SetValue(base, red));
    REQUIRE_FALSE(failing.Save());
    REQUIRE(internal::ThemeEditorTestAccess::Editor(failing).Document().Persistence().error);
    failing.Revert();
    REQUIRE(failing.Overrides()->empty());

    using Preset = LWSUI::ThemePreset;
    REQUIRE(ThemeStorage::Encode(SettingsTheme(Preset::Dark)) == ThemeStorage::Encode(SettingsTheme{}));
    REQUIRE_FALSE(ThemeStorage::DecodeProfile(Json{{"version", 1}, {"preset", "unknown"}}));
    REQUIRE(ThemeStorage::DecodeProfile(Json{{"version", 1}})->preset == Preset::Dark);
    const auto luminance = [](LLUtils::Color color)
    {
        auto channel = [](double c) { c /= 255; return c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4); };
        return .2126*channel(color.R()) + .7152*channel(color.G()) + .0722*channel(color.B());
    };
    auto contrast = [&](auto a, auto b) { auto x=luminance(a), y=luminance(b); return (std::max(x,y)+.05)/(std::min(x,y)+.05); };
    for (auto preset : {Preset::Dark, Preset::Light, Preset::Warm})
    {
        const SettingsTheme presetTheme(preset);
        REQUIRE(ThemeFields::Validate(presetTheme));
        REQUIRE(presetTheme.base.font == SettingsTheme{}.base.font);
        REQUIRE(presetTheme.base.rowHeight == SettingsTheme{}.base.rowHeight);
        REQUIRE(contrast(presetTheme.base.foreground, presetTheme.base.background) >= 4.5);
        REQUIRE(contrast(presetTheme.base.muted, presetTheme.base.background) >= 4.5);
        REQUIRE(contrast(presetTheme.base.foreground, presetTheme.base.surface) >= 4.5);
        REQUIRE(contrast(presetTheme.base.selectionForeground, presetTheme.base.selection) >= 4.5);
        ThemeProfile profile{preset, {{base, green}, {section, red}}};
        auto decodedProfile = ThemeStorage::DecodeProfile(ThemeStorage::EncodeProfile(profile));
        REQUIRE(decodedProfile); REQUIRE((*decodedProfile == profile));
        auto resolvedPreset = ThemeStorage::Decode(ThemeStorage::EncodeProfile(profile));
        REQUIRE(resolvedPreset); REQUIRE(resolvedPreset->base.accent == green);
        REQUIRE(resolvedPreset->description.accent == red);
    }
    TempFile profileFile;
    ThemeSession session(profileFile.path);
    REQUIRE(session.Load()); REQUIRE_FALSE(session.IsDirty());
    ThemeEditor shared(platform, session);
    REQUIRE(internal::ThemeEditorTestAccess::Editor(shared).Document().Definitions().empty());
    REQUIRE(session.SelectPreset(Preset::Light)); REQUIRE(session.IsDirty());
    REQUIRE(internal::ThemeEditorTestAccess::Editor(shared).Document().Definitions().empty()); // Switching never loads the template.
    REQUIRE(shared.Load());
    REQUIRE(internal::ThemeEditorTestAccess::Editor(shared).Document().IsDirty());
    REQUIRE(internal::ThemeEditorTestAccess::Editor(shared).Document().Find(base)->DefaultValue() == Value{session.Theme().base.accent});
    REQUIRE(internal::ThemeEditorTestAccess::Editor(shared).Document().SetValue(base, green));
    REQUIRE(session.Current().overrides.contains(base));
    REQUIRE(session.SelectPreset(Preset::Light)); REQUIRE(session.Current().overrides.empty());
    REQUIRE(shared.Overrides()->empty());
    REQUIRE(shared.Save()); REQUIRE_FALSE(session.IsDirty());
    REQUIRE_FALSE(internal::ThemeEditorTestAccess::Editor(shared).Document().IsDirty());
    auto lightFile = ThemeStorage::LoadProfileFile(profileFile.path);
    REQUIRE(lightFile); REQUIRE(lightFile->preset == Preset::Light); REQUIRE(lightFile->overrides.empty());
    REQUIRE(ThemeStorage::EncodeProfile(*lightFile).size() == 2);
    REQUIRE(session.SelectPreset(Preset::Warm)); REQUIRE(internal::ThemeEditorTestAccess::Editor(shared).Document().IsDirty());
    shared.Revert(); REQUIRE(session.Current().preset == Preset::Light);
    REQUIRE_FALSE(internal::ThemeEditorTestAccess::Editor(shared).Document().IsDirty());
    TempFile defaultsFile, writableFile;
    REQUIRE(ThemeStorage::SaveProfileFile(defaultsFile.path, {Preset::Light,{}}));
    ThemeSession defaultsSession(writableFile.path);
    REQUIRE(defaultsSession.Load(defaultsFile.path));
    REQUIRE(defaultsSession.Current().preset == Preset::Light); REQUIRE_FALSE(defaultsSession.IsDirty());
    REQUIRE(defaultsSession.SelectPreset(Preset::Warm)); REQUIRE(defaultsSession.Save());
    REQUIRE(ThemeStorage::SaveProfileFile(defaultsFile.path, {Preset::Dark,{}}));
    REQUIRE(defaultsSession.Load(defaultsFile.path)); REQUIRE(defaultsSession.Current().preset == Preset::Warm);
    STATIC_REQUIRE((std::is_same_v<decltype(std::declval<ThemeEditor&>().Editor()), const SettingsEditor&>));
    ThemeEditor damaged(platform, profileFile.path);
    REQUIRE(damaged.Load());
    auto& damagedDocument = internal::ThemeEditorTestAccess::Editor(damaged).Document();
    unsigned schemaErrors = 0;
    auto schemaError = damaged.OnError.Connect([&](const Error&) { ++schemaErrors; });
    REQUIRE(SettingsStorage::LoadTemplate(damagedDocument, ThemeStorage::Template()));
    REQUIRE(damagedDocument.SetValue(base, green));
    REQUIRE(schemaErrors > 0); // Malformed private metadata reports an error, never an unchecked optional access.
    TempFile pullFile;
    ThemeSession pullSession(pullFile.path);
    ThemeEditor pullEditor(platform, pullSession);
    REQUIRE(pullEditor.Load());
    size_t completions = 0;
    auto completed = internal::ThemeEditorTestAccess::Editor(pullEditor).Document().OnEditFinished.Connect(
        [&](const auto&, auto, auto) { ++completions; });
    REQUIRE(pullSession.Load());
    REQUIRE(completions == 0); // Clean pulls do not replay the whole catalog.
    REQUIRE(pullSession.SetProfile({Preset::Dark, {{section, red}}}));
    REQUIRE(completions == 2); // One changed field and its private override flag.
    REQUIRE(pullSession.Save());
    REQUIRE(completions == 2);
    auto beforeInvalid = session.Current();
    REQUIRE_FALSE(session.SelectPreset(static_cast<Preset>(999)));
    REQUIRE((session.Current() == beforeInvalid));
}
TEST_CASE("Invalid templates preserve the complete previous model")
{
    SettingsDocument p;
    REQUIRE(SettingsStorage::LoadTemplate(p, Template()));
    auto bad = Template();
    bad["children"][0]["children"][1]["id"] = "name";
    REQUIRE_FALSE(SettingsStorage::LoadTemplate(p, bad));
    REQUIRE(p.WorkingValue("count"));
    bad = Template();
    bad["children"][0]["children"][1]["default"] = 50;
    REQUIRE_FALSE(SettingsStorage::LoadTemplate(p, bad));
    REQUIRE(std::get<int64_t>(*p.WorkingValue("count")) == 3);
    bad = Template();
    bad["children"][0]["children"][0]["type"] = "unknown";
    REQUIRE_FALSE(SettingsStorage::LoadTemplate(p, bad));
}
TEST_CASE("Assignments validate types ranges and membership")
{
    SettingsDocument p;
    REQUIRE(SettingsStorage::LoadTemplate(p, Template()));
    REQUIRE_FALSE(p.SetValue("count", int64_t{11}));
    REQUIRE_FALSE(p.SetValue("count", 1.5));
    REQUIRE_FALSE(p.SetValue("enabled", std::string("true")));
    REQUIRE_FALSE(p.SetValue("choice", std::string("missing")));
    REQUIRE_FALSE(p.SetValue("missing", false));
    REQUIRE(p.SetValue("ratio", int64_t{2}));
    REQUIRE(std::get<double>(*p.WorkingValue("ratio")) == 2);
}
TEST_CASE("Immediate changes notify once and suppress no-ops")
{
    SettingsDocument p;
    REQUIRE(SettingsStorage::LoadTemplate(p, Template()));
    std::vector<Change> events;
    auto c = p.OnChange.Connect([&](const Change& e) { events.push_back(e); });
    REQUIRE(p.SetValue("count", int64_t{5}));
    REQUIRE(p.SetValue("count", int64_t{5}));
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].phase == EditPhase::Commit);
    REQUIRE(events[0].origin == ChangeOrigin::Programmatic);
    REQUIRE(events[0].domain == ValueDomain::Applied);
    REQUIRE(std::get<int64_t>(events[0].oldValue) == 3);
    REQUIRE(p.WorkingValue("count") == p.AppliedValue("count"));
    REQUIRE(p.IsDirty());
}
TEST_CASE("Staged edits apply as an atomic snapshot and cancel to last apply")
{
    DocumentConfig config;
    config.applyPolicy = ApplyPolicy::OnApply;
    SettingsDocument p(config);
    REQUIRE(SettingsStorage::LoadTemplate(p, Template()));
    std::vector<Change> events;
    auto c = p.OnChange.Connect([&](const Change& e) { events.push_back(e); });
    REQUIRE(p.SetValue("count", int64_t{7}));
    REQUIRE(p.SetValue("name", std::string("new")));
    REQUIRE(p.HasPendingChanges());
    REQUIRE(std::get<int64_t>(*p.AppliedValue("count")) == 3);
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].domain == ValueDomain::Working);
    REQUIRE(p.Apply());
    REQUIRE_FALSE(p.HasPendingChanges());
    REQUIRE(events.size() == 4);
    REQUIRE(events[2].phase == EditPhase::Commit);
    REQUIRE(p.SetValue("count", int64_t{9}));
    p.Cancel();
    REQUIRE(std::get<int64_t>(*p.WorkingValue("count")) == 7);
    REQUIRE(events.back().origin == ChangeOrigin::Cancel);
    events.clear();
    p.RevertSaved();
    REQUIRE(p.AppliedValue("count") == Value{int64_t{3}});
    REQUIRE(std::ranges::any_of(events, [](const Change& e) { return e.id == "count" && e.domain == ValueDomain::Applied && e.oldValue == Value{int64_t{7}} && e.newValue == Value{int64_t{3}}; }));
    REQUIRE(p.SetValue("count", int64_t{8})); REQUIRE(p.Apply());
    REQUIRE(p.SetValue("count", int64_t{3})); events.clear();
    p.RevertSaved();
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].domain == ValueDomain::Applied);
    REQUIRE(events[0].oldValue == Value{int64_t{8}});
}
TEST_CASE("JSON load validates atomically and fills missing values")
{
    SettingsDocument p;
    REQUIRE(SettingsStorage::LoadTemplate(p, Template()));
    auto data = SettingsStorage::EncodeValues(p.WorkingValues());
    data["values"]["count"] = 8;
    data["values"]["enabled"] = "bad";
    REQUIRE_FALSE(SettingsStorage::LoadValues(p, data));
    REQUIRE(std::get<int64_t>(*p.WorkingValue("count")) == 3);
    data["values"].erase("enabled");
    data["values"]["future"] = 1;
    REQUIRE(SettingsStorage::LoadValues(p, data));
    REQUIRE(std::get<int64_t>(*p.WorkingValue("count")) == 8);
    REQUIRE_FALSE(p.IsDirty());
    auto invalid = data;
    invalid["values"]["color"] = "#xxxxxx";
    REQUIRE_FALSE(SettingsStorage::LoadValues(p, invalid));
    REQUIRE(SettingsStorage::LoadValues(p, Json::parse(R"({"version":1,"values":{"ratio":9223372036854775808}})")));
    REQUIRE(p.WorkingValue("ratio") == Value{std::ldexp(1.,63)});
    auto numericTemplate = Template();
    numericTemplate["children"][0]["children"][2]["default"] = uint64_t{1} << 63;
    REQUIRE(SettingsStorage::LoadTemplate(p, numericTemplate));
    REQUIRE(p.WorkingValue("ratio") == Value{std::ldexp(1.,63)});
}
TEST_CASE("Manual persistence round trips Unicode and uses CRLF")
{
    TempFile file;
    DocumentConfig config;
    config.valuesFile = file.path;
    SettingsDocument p(config);
    REQUIRE(SettingsStorage::LoadTemplate(p, Template()));
    REQUIRE(p.SetValue("name", std::string("café · שלום · 日本語")));
    REQUIRE_FALSE(std::filesystem::exists(file.path));
    REQUIRE(p.Save());
    REQUIRE_FALSE(p.IsDirty());
    std::ifstream input(file.path, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)), {});
    REQUIRE(bytes.find("\r\n") != std::string::npos);
    const auto savedJson = Json::parse(bytes);
    REQUIRE(savedJson["values"].size() == p.WorkingValues().size());
    REQUIRE(savedJson["values"]["count"] == 3); // Unchanged defaults remain in the saved snapshot.
    input.close();
    SettingsDocument other;
    REQUIRE(SettingsStorage::LoadTemplate(other, Template()));
    REQUIRE(SettingsStorage::LoadValuesFile(other, file.path));
    REQUIRE(other.WorkingValue("name") == p.WorkingValue("name"));
    REQUIRE(p.SetValue("name", std::string("replacement")));
    REQUIRE(p.Save());
    REQUIRE(SettingsStorage::LoadValuesFile(other, file.path));
    REQUIRE(other.WorkingValue("name") == p.WorkingValue("name"));
}
TEST_CASE("Autosave persists confirmed assignments and failed saving retains edits")
{
    TempFile file;
    DocumentConfig config;
    config.valuesFile = file.path;
    config.savePolicy = SavePolicy::OnCommit;
    SettingsDocument p(config);
    REQUIRE(SettingsStorage::LoadTemplate(p, Template()));
    REQUIRE(p.SetValue("count", int64_t{9}));
    REQUIRE(std::filesystem::exists(file.path));
    REQUIRE_FALSE(p.IsDirty());
    DocumentConfig bad;
    bad.savePolicy = SavePolicy::OnCommit;
    SettingsDocument failed(bad);
    REQUIRE(SettingsStorage::LoadTemplate(failed, Template()));
    REQUIRE(failed.SetValue("count", int64_t{9}));
    REQUIRE(failed.IsDirty());
    REQUIRE(failed.Persistence().error);
    REQUIRE(std::get<int64_t>(*failed.AppliedValue("count")) == 9);

    // A group/all reset publishes a complete snapshot and persists only once.
    for (bool all : {false, true})
    {
        REQUIRE(p.SetValue("count", int64_t{9}));
        REQUIRE(p.SetValue("enabled", true));
        unsigned writes = 0, changes = 0;
        auto saved = p.OnPersistenceChanged.Connect([&] { ++writes; });
        auto reset = p.OnChange.Connect(
            [&](const Change& change)
            {
                if (change.origin == ChangeOrigin::Reset)
                {
                    ++changes;
                    REQUIRE(p.WorkingValue("count") == Value{int64_t{3}});
                    REQUIRE(p.WorkingValue("enabled") == Value{false});
                }
            });
        REQUIRE((all ? p.ResetAll() : p.Reset("g")));
        REQUIRE(changes == 2);
        REQUIRE(writes == 1);
        SettingsDocument loaded;
        REQUIRE(SettingsStorage::LoadTemplate(loaded, Template()));
        REQUIRE(SettingsStorage::LoadValuesFile(loaded, file.path));
        REQUIRE(loaded.WorkingValues() == p.WorkingValues());
        REQUIRE(p.Apply());
        REQUIRE(writes == 1);  // Already persisted; confirming again must not rewrite it.
    }
}
TEST_CASE("Staged Save applies and persists, Reset respects workflow")
{
    TempFile file;
    DocumentConfig config;
    config.applyPolicy = ApplyPolicy::OnApply;
    config.valuesFile = file.path;
    SettingsDocument p(config);
    REQUIRE(SettingsStorage::LoadTemplate(p, Template()));
    REQUIRE(p.SetValue("count", int64_t{6}));
    REQUIRE(p.Apply());
    REQUIRE(p.Save());
    REQUIRE_FALSE(p.IsDirty());
    REQUIRE_FALSE(p.HasPendingChanges());
    REQUIRE(p.Reset("count"));
    REQUIRE(p.HasPendingChanges());
    REQUIRE(std::get<int64_t>(*p.AppliedValue("count")) == 6);
    REQUIRE(p.ResetAll());
    p.Cancel();
    REQUIRE(std::get<int64_t>(*p.WorkingValue("count")) == 6);
}
TEST_CASE("Change callbacks may update a different setting")
{
    SettingsDocument p;
    REQUIRE(SettingsStorage::LoadTemplate(p, Template()));
    std::vector<std::string> ids;
    auto c = p.OnChange.Connect(
        [&](const Change& e)
        {
            ids.push_back(e.id);
            if (e.id == "count")
                REQUIRE(p.SetValue("enabled", true));
        });
    REQUIRE(p.SetValue("count", int64_t{4}));
    REQUIRE(ids == std::vector<std::string>{"count", "enabled"});
    REQUIRE(p.SetValue("enabled", false));
    SettingsView view(p);
    auto* check = FindControl<LWSUI::CheckBox>(view);
    bool overridden = false;
    auto overrideSame = p.OnChange.Connect([&](const Change& e)
    { if (e.id == "enabled" && e.newValue == Value{true}) { overridden = true; REQUIRE(p.SetValue("enabled", false)); } });
    KeyPress(*check, Key::Space);
    REQUIRE(overridden);
    REQUIRE(p.WorkingValue("enabled") == Value{false}); REQUIRE_FALSE(check->Value());
    overrideSame.Disconnect();
    auto throwing = p.OnChange.Connect([](const Change& e) { if(e.id=="enabled") throw std::runtime_error("subscriber"); });
    REQUIRE_THROWS(KeyPress(*check, Key::Space)); throwing.Disconnect();
    REQUIRE(p.SetValue("enabled", false)); REQUIRE_FALSE(check->Value());
    auto* numeric = FindControl<LWSUI::NumericEdit<int64_t>>(view);
    auto* numberText = FindControl<LWSUI::TextBox>(*numeric);
    Draft(*numberText, "invalid");
    REQUIRE_FALSE(numberText->InputError().empty());
    REQUIRE(view.Finish(Phase::Commit));
    Draft(*numberText, "-");
    REQUIRE_FALSE(view.Finish(Phase::Commit)); REQUIRE_FALSE(view.ValidationMessage().empty());
    REQUIRE(SettingsStorage::LoadTemplate(p, Template()));
    REQUIRE(view.ValidationMessage().empty());
    SettingsDocument early;
    REQUIRE(SettingsStorage::LoadTemplate(early, Template()));
    auto earlyThrow = early.OnChange.Connect([](const Change&) { throw std::runtime_error("early subscriber"); });
    SettingsView earlyView(early);
    auto* earlyCheck = FindControl<LWSUI::CheckBox>(earlyView);
    REQUIRE_THROWS(KeyPress(*earlyCheck, Key::Space));
    REQUIRE(earlyCheck->Value());
    REQUIRE(earlyCheck->Parent()->Children()[0]->Font().weight == 700);
    bool unsaved = false;
    for(auto* label : Controls<LWSUI::Label>(earlyView)) unsaved |= label->Text() == "Unsaved";
    REQUIRE(unsaved);

}
TEST_CASE("Stable IDs survive regrouping")
{
    SettingsDocument p;
    REQUIRE(SettingsStorage::LoadTemplate(p, Template()));
    REQUIRE(p.SetValue("count", int64_t{8}));
    auto saved = SettingsStorage::EncodeValues(p.WorkingValues()), schema = Template();
    schema["children"][0]["id"] = "renamed-group";
    REQUIRE(SettingsStorage::LoadTemplate(p, schema));
    REQUIRE(SettingsStorage::LoadValues(p, saved));
    REQUIRE(std::get<int64_t>(*p.WorkingValue("count")) == 8);
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
TEST_CASE("Text edits publish previews and one edit-wide confirmation")
{
    SettingsDocument p;
    REQUIRE(SettingsStorage::LoadTemplate(p, Template()));
    SettingsView view(p);
    auto texts = Controls<LWSUI::TextBox>(view);
    auto& text = *texts[1];
    std::vector<Change> events;
    auto c = p.OnChange.Connect([&](const Change& e) { events.push_back(e); });
    Draft(text, "a");
    Draft(text, "ab");
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].phase == EditPhase::Preview);
    REQUIRE(text.Finish(Phase::Commit));
    REQUIRE(events.size() == 3);
    REQUIRE(events.back().oldValue == Value{std::string("hello")});
    REQUIRE(text.Finish(Phase::Commit));
    REQUIRE(events.size() == 3);
}
TEST_CASE("Invalid drafts block Save and Escape restores the edit-start value")
{
    SettingsDocument p;
    REQUIRE(SettingsStorage::LoadTemplate(p, Template()));
    SettingsView view(p);
    auto* numeric = FindControl<LWSUI::NumericEdit<int64_t>>(view);
    auto* text = FindControl<LWSUI::TextBox>(*numeric);
    Draft(*text, "7");
    Draft(*text, "-");
    REQUIRE(p.WorkingValue("count") == Value{int64_t{7}});
    REQUIRE_FALSE(view.Finish(Phase::Commit));
    REQUIRE(view.Finish(Phase::Cancel));
    REQUIRE(p.WorkingValue("count") == Value{int64_t{3}});
    TempFile saved, existing;
    existing.path += std::filesystem::path(u8"-日本.txt");
    { std::ofstream output(existing.path); output << "existing"; }
    auto pathText = [](const std::filesystem::path& path)
    {
        const auto utf8 = path.u8string();
        return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    };
    SettingsDocument files({ApplyPolicy::Immediate, SavePolicy::OnCommit, saved.path});
    REQUIRE(SettingsStorage::LoadTemplate(files, Template()));
    SettingsView fileView(files);
    auto* filePicker = FindControl<LWSUI::FilePicker>(fileView);
    auto* fileText = FindControl<LWSUI::TextBox>(*filePicker);
    const auto missing = pathText(existing.path) + ".missing";
    Draft(*fileText, missing);
    REQUIRE_FALSE(fileText->Validation().empty());
    REQUIRE(files.WorkingValue("file") == Value{std::string{}});
    REQUIRE_FALSE(fileView.Finish(Phase::Commit));
    REQUIRE_FALSE(std::filesystem::exists(saved.path));
    REQUIRE(fileView.Finish(Phase::Cancel));
    Draft(*fileText, pathText(existing.path.parent_path()));
    REQUIRE_FALSE(fileView.Finish(Phase::Commit));
    REQUIRE(fileView.Finish(Phase::Cancel));
    Draft(*fileText, pathText(existing.path));
    REQUIRE(fileText->Validation().empty());
    REQUIRE(files.WorkingValue("file") == Value{pathText(existing.path)});
    REQUIRE_FALSE(std::filesystem::exists(saved.path));
    std::filesystem::remove(existing.path);
    REQUIRE_FALSE(fileView.Finish(Phase::Commit)); // Revalidate when a previously valid file disappears.
    REQUIRE_FALSE(std::filesystem::exists(saved.path));
    REQUIRE(fileView.Finish(Phase::Cancel));
    { std::ofstream output(existing.path); output << "restored"; }
    Draft(*fileText, pathText(existing.path));
    REQUIRE(fileView.Finish(Phase::Commit));
    REQUIRE(std::filesystem::exists(saved.path));
    Draft(*fileText, "");
    REQUIRE(fileView.Finish(Phase::Commit));
    REQUIRE(files.WorkingValue("file") == Value{std::string{}});
    const auto relative = std::filesystem::relative(std::filesystem::path(LWSSETTINGS_EXAMPLES_DIR) / "settings.json");
    REQUIRE_FALSE(relative.empty());
    Draft(*fileText, pathText(relative));
    REQUIRE(fileView.Finish(Phase::Commit));
    REQUIRE(files.SetValue("file", missing));
    REQUIRE(fileView.Finish(Phase::Commit)); // Programmatic values are not filesystem-validated.
    auto loaded = SettingsStorage::EncodeValues(files.WorkingValues());
    REQUIRE(SettingsStorage::LoadValues(files, loaded));
    REQUIRE(fileView.Finish(Phase::Commit));
    Draft(*fileText, missing + "x");
    REQUIRE_FALSE(fileView.Finish(Phase::Commit));
    REQUIRE(fileView.Finish(Phase::Cancel));
    REQUIRE(filePicker->Value() == missing); // Cancel may restore a missing loaded value.
}
TEST_CASE("Autosave waits for confirmation and color cancellation restores previews")
{
    TempFile file;
    SettingsDocument p({ApplyPolicy::Immediate, SavePolicy::OnCommit, file.path});
    REQUIRE(SettingsStorage::LoadTemplate(p, Template()));
    REQUIRE(p.SetValue("name", std::string("new"), EditPhase::Preview, ChangeOrigin::User));
    REQUIRE_FALSE(std::filesystem::exists(file.path));
    REQUIRE(p.SetValue("name", std::string("new"), EditPhase::Commit, ChangeOrigin::User));
    REQUIRE(std::filesystem::exists(file.path));
    auto original = p.WorkingValue("color");
    REQUIRE(p.SetValue("color", LLUtils::Color{uint32_t{0xffaabbff}}, EditPhase::Preview));
    REQUIRE(p.SetValue("color", *original, EditPhase::Cancel));
    REQUIRE(p.WorkingValue("color") == original);
}
TEST_CASE("Staged edit confirmation remains local until Apply")
{
    SettingsDocument p({ApplyPolicy::OnApply});
    REQUIRE(SettingsStorage::LoadTemplate(p, Template()));
    std::vector<Change> events;
    auto c = p.OnChange.Connect([&](const Change& e) { events.push_back(e); });
    REQUIRE(p.SetValue("name", std::string("working"), EditPhase::Preview));
    REQUIRE(p.SetValue("name", std::string("working"), EditPhase::Commit));
    REQUIRE(events.size() == 2);
    REQUIRE(events.back().domain == ValueDomain::Working);
    REQUIRE(p.Apply());
    REQUIRE(events.size() == 3);
    REQUIRE(events.back().domain == ValueDomain::Applied);
}
TEST_CASE("Programmatic updates supersede drafts only after validation")
{
    SettingsDocument p;
    REQUIRE(SettingsStorage::LoadTemplate(p, Template()));
    SettingsView view(p);
    auto* numeric = FindControl<LWSUI::NumericEdit<int64_t>>(view);
    auto* text = FindControl<LWSUI::TextBox>(*numeric);
    Draft(*text, "-");
    REQUIRE_FALSE(p.SetValue("count", int64_t{999}));
    REQUIRE_FALSE(text->Validation().empty());
    REQUIRE(p.SetValue("count", int64_t{3}));
    REQUIRE(text->Validation().empty());
    REQUIRE_FALSE(p.SetValue("name", std::string("\xff")));
}
TEST_CASE("Filtering reveals descendants and restores collapsed groups")
{
    SettingsDocument p;
    auto schema = Template();
    schema["children"][0]["children"][0]["description"] = "Description";
    REQUIRE(SettingsStorage::LoadTemplate(p, schema));
    SettingsView view(p);
    auto* branch = FindControl<LWSUI::TreeView::Branch>(view);
    branch->SetExpanded(false);
    REQUIRE_FALSE(branch->Content().Visible());
    view.SetFilter("description");
    REQUIRE(branch->Content().Visible());
    REQUIRE(branch->Content().Children()[0]->Visible());
    REQUIRE_FALSE(branch->Content().Children()[1]->Visible());
    view.SetFilter("");
    REQUIRE_FALSE(branch->Content().Visible());
    auto values = SettingsStorage::EncodeValues(p.WorkingValues());
    values["values"]["count"] = 8;
    auto* numeric = FindControl<LWSUI::NumericEdit<int64_t>>(view);
    unsigned edits = 0;
    auto changes = p.OnChange.Connect([&](const Change&) { ++edits; });
    REQUIRE(SettingsStorage::LoadValues(p, values));
    REQUIRE(FindControl<LWSUI::TreeView::Branch>(view) == branch);
    REQUIRE(FindControl<LWSUI::NumericEdit<int64_t>>(view) == numeric);
    REQUIRE_FALSE(branch->Content().Visible());
    REQUIRE(numeric->Value() == 8);
    REQUIRE(edits == 0);
    REQUIRE(LWSUI::FindTextMatches("Name name NAME", "name") == std::vector<LWSUI::TextRange>{{0, 4}, {5, 4}, {10, 4}});
    REQUIRE(LWSUI::FindTextMatches("banana", "ana") == std::vector<LWSUI::TextRange>{{1, 5}});
    REQUIRE(LWSUI::FindTextMatches("日x日", "日") == std::vector<LWSUI::TextRange>{{0, 3}, {4, 3}});
    REQUIRE(LWSUI::FindTextMatches("name", "").empty());
    view.SetFilter("hello");
    REQUIRE(branch->Content().Children()[0]->Visible());
    auto* editor = FindControl<LWSUI::TextBox>(*branch->Content().Children()[0]);
    REQUIRE(editor->HighlightSpans("Hello hello").size() == 2);
    REQUIRE(p.SetValue("name", std::string("other")));
    REQUIRE(branch->Content().Children()[0]->Visible());  // Stable membership until the next query.
    REQUIRE(editor->HighlightSpans(editor->Text()).empty());
    view.SetFilter("enabled");
    REQUIRE(branch->Content().Children()[3]->Visible());
    view.SetFilter("B");
    REQUIRE(branch->Content().Children()[5]->Visible());  // Unselected choice label.
    view.SetFilter("");
    REQUIRE(editor->HighlightSpans("hello").empty());
    REQUIRE_FALSE(branch->Content().Visible());
    REQUIRE(p.ResetAll());
    auto* nameRow = branch->Content().Children()[0].get();
    auto* countRow = branch->Content().Children()[1].get();
    view.SetValueFilter(SettingValueFilter::Changed);
    REQUIRE_FALSE(branch->Visible());
    REQUIRE(p.SetValue("count", int64_t{8}));
    REQUIRE(countRow->Visible());
    REQUIRE(branch->Visible());
    REQUIRE_FALSE(nameRow->Visible());
    view.SetFilter("Group"); // Group matches must still obey the state filter.
    REQUIRE(countRow->Visible());
    REQUIRE_FALSE(nameRow->Visible());
    view.SetFilter("Name");
    REQUIRE_FALSE(branch->Visible());
    view.SetFilter("");
    REQUIRE(countRow->Visible());
    REQUIRE(SettingsStorage::LoadValues(p, SettingsStorage::EncodeValues(p.WorkingValues())));
    REQUIRE_FALSE(p.IsDirty());
    REQUIRE(countRow->Visible()); // Saved values can still be non-default.
    view.SetValueFilter(SettingValueFilter::Default);
    REQUIRE_FALSE(countRow->Visible());
    REQUIRE(nameRow->Visible());
    REQUIRE(p.SetValue("name", std::string("preview"), EditPhase::Preview, ChangeOrigin::User));
    REQUIRE(nameRow->Visible());
    REQUIRE(p.SetValue("name", std::string("preview"), EditPhase::Commit, ChangeOrigin::User));
    REQUIRE_FALSE(nameRow->Visible());
    REQUIRE(p.Reset("name"));
    REQUIRE(nameRow->Visible());
    REQUIRE(p.SetValue("name", std::string("temporary"), EditPhase::Preview, ChangeOrigin::User));
    REQUIRE(nameRow->Visible());
    REQUIRE(p.SetValue("name", std::string("temporary"), EditPhase::Cancel, ChangeOrigin::User));
    REQUIRE(nameRow->Visible());
    view.SetValueFilter(SettingValueFilter::All);
    REQUIRE_FALSE(branch->Content().Visible());
    REQUIRE(view.GetValueFilter() == SettingValueFilter::All);
    SettingsDocument grouped;
    auto nestedSchema = Template();
    auto leaves = nestedSchema["children"][0]["children"];
    nestedSchema["children"][0]["children"] = Json::array({
        Json{{"type", "group"}, {"id", "nested"}, {"label", "Nested"}, {"children", leaves}},
        Json{{"type", "group"}, {"id", "empty"}, {"label", "Empty"}, {"children", Json::array()}}});
    REQUIRE(SettingsStorage::LoadTemplate(grouped, nestedSchema));
    SettingsView nested(grouped, "Nested", {}, false, true);
    auto* expand = FindButton(nested, "Expand all");
    auto* collapse = FindButton(nested, "Collapse all");
    REQUIRE(expand);
    REQUIRE(collapse);
    auto branches = Controls<LWSUI::TreeView::Branch>(nested);
    REQUIRE(branches.size() == 3);
    Layout(nested);
    REQUIRE(expand->Enabled());
    REQUIRE_FALSE(collapse->Enabled());
    const auto originalValues = grouped.WorkingValues();
    ClickButton(*expand);
    REQUIRE(branches[0]->Expanded());
    REQUIRE(branches[1]->Expanded());
    REQUIRE_FALSE(branches[2]->Visible());
    REQUIRE_FALSE(branches[2]->Expanded());
    REQUIRE_FALSE(expand->Enabled());
    REQUIRE(collapse->Enabled());
    branches[0]->SetExpanded(false);
    Layout(nested);
    REQUIRE(expand->Enabled());
    REQUIRE(collapse->Enabled()); // The nested branch still has its own expanded state.
    ClickButton(*collapse);
    REQUIRE_FALSE(branches[0]->Expanded());
    REQUIRE_FALSE(branches[1]->Expanded());
    REQUIRE(grouped.WorkingValues() == originalValues);
    REQUIRE(grouped.SetValue("count", int64_t{8}));
    for (auto mode : {SettingValueFilter::All, SettingValueFilter::Changed, SettingValueFilter::Default})
    {
        nested.SetValueFilter(mode);
        nested.SetFilter("Group");
        REQUIRE_FALSE(expand->Enabled());
        REQUIRE_FALSE(collapse->Enabled());
        REQUIRE(branches[0]->Content().Visible());
        ClickButton(*collapse);
        REQUIRE_FALSE(branches[0]->Expanded());
        nested.SetFilter("");
        if (mode != SettingValueFilter::All)
        {
            REQUIRE_FALSE(expand->Enabled());
            REQUIRE_FALSE(collapse->Enabled());
        }
    }
    nested.SetValueFilter(SettingValueFilter::All);
    REQUIRE_FALSE(branches[0]->Content().Visible());
    REQUIRE(expand->Enabled());
    REQUIRE_FALSE(collapse->Enabled());
    SettingsDocument emptyDocument;
    SettingsView emptyView(emptyDocument);
    Layout(emptyView);
    REQUIRE_FALSE(FindButton(emptyView, "Expand all")->Enabled());
    REQUIRE_FALSE(FindButton(emptyView, "Collapse all")->Enabled());
    SettingsDocument flatDocument;
    REQUIRE(SettingsStorage::LoadTemplate(flatDocument, Json{{"version", 1}, {"children", leaves}}));
    SettingsView flatView(flatDocument);
    Layout(flatView);
    REQUIRE_FALSE(FindButton(flatView, "Expand all")->Enabled());
    REQUIRE_FALSE(FindButton(flatView, "Collapse all")->Enabled());
}
TEST_CASE("UTF-8 selection replacement uses byte-safe boundaries")
{
    LWSUI::TextBox text("Aé日😀");
    text.SetText("Aé日😀");
    KeyPress(text, Key::Left);
    KeyPress(text, Key::Left, true);
    KeyPress(text, Key::Left, true);
    Input input{Kind::Text};
    input.text = "שלום";
    text.Dispatch(input);
    REQUIRE(text.Text() == "Aשלום😀");
    // An unavailable clipboard must never turn Cut into a destructive delete.
    KeyPress(text, Key::A, false, true);
    KeyPress(text, Key::X, false, true);
    REQUIRE(text.Text() == "Aשלום😀");
    std::vector<Phase> phases;
    auto edits = text.OnEdit.Connect([&](const auto&, Phase phase) { phases.push_back(phase); });
    text.SetText("original");
    Draft(text, "draft");
    text.SetReadOnly(true);
    REQUIRE(text.Text() == "original");
    REQUIRE(phases.back() == Phase::Cancel);
    const auto count = phases.size();
    REQUIRE(text.Focusable());
    KeyPress(text, Key::A, false, true);
    for (auto key : {Key::Backspace, Key::Delete}) KeyPress(text, key);
    KeyPress(text, Key::X, false, true);
    KeyPress(text, Key::V, false, true);
    Draft(text, "blocked");
    REQUIRE(text.Text() == "original");
    REQUIRE(phases.size() == count);
    text.SetText("programmatic");
    REQUIRE(text.Text() == "programmatic");
    REQUIRE(phases.size() == count);
    text.SetReadOnly(false);
    Draft(text, "editable again");
    REQUIRE(text.Text() == "editable again");
    REQUIRE(phases.back() == Phase::Preview);
}
TEST_CASE("Viewer renders all controls and survives close and reopen", "[viewer]")
{
    TestPlatform platform;
    EditorConfig invalid;
    invalid.theme.controls.checkerSize = 1e-8f;
    SettingsEditor invalidEditor(platform, invalid);
    const auto rejected = invalidEditor.Show();
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error().location == "sections/controls/metrics/checkerSize");
    REQUIRE(invalidEditor.View() == nullptr);
    EditorConfig config;
    config.theme.description.accent = LLUtils::Color{uint32_t{0x22bb88ff}};
    config.theme.footer.muted = LLUtils::Color{uint32_t{0xaabbccff}};
    SettingsEditor editor(platform, config);
    editor.SetValueFilter(SettingValueFilter::Default);
    REQUIRE(editor.GetValueFilter() == SettingValueFilter::Default);
    REQUIRE(SettingsStorage::LoadTemplate(editor.Document(), Template()));
    REQUIRE(editor.Show());
    WaitForConfiguration(editor.View()->Host()->Window());
    editor.View()->Host()->Update();
    REQUIRE(editor.GetValueFilter() == SettingValueFilter::Default);
    editor.SetValueFilter(SettingValueFilter::All);
    editor.View()->Host()->Update();
    for (auto* button : Controls<LWSUI::Button>(*editor.View()))
        if (button->Text() == "Reset" || button->Text() == "Reset all")
            REQUIRE(button->Foreground() == config.theme.footer.muted);
    for (auto* label : Controls<LWSUI::Label>(*editor.View()))
        if (label->Text() == "Setting details")
            REQUIRE(label->Foreground() == config.theme.description.accent);
    REQUIRE(editor.IsVisible());
    std::ostringstream console;
    auto logging = editor.Document().OnChange.Connect(
        [&](const Change& change)
        {
            Example::PrintEvent(console, change);
            Example::PrintEvent(std::cout, change);
        });
    auto* checkbox = FindControl<LWSUI::CheckBox>(*editor.View());
    auto* treeScroll = Controls<LWSUI::ScrollView>(*editor.View())[0];
    // The native DPI can differ from the pre-creation monitor estimate. Scroll
    // the target into view rather than clicking through a smaller tree viewport.
    const float bottom = checkbox->Bounds().y + checkbox->Bounds().height;
    const float viewportBottom = treeScroll->Bounds().y + treeScroll->Bounds().height;
    if (bottom > viewportBottom)
    {
        treeScroll->SetOffset(treeScroll->Offset() + bottom - viewportBottom);
        editor.View()->Host()->Update();
    }
    auto bounds = checkbox->Bounds();
    CAPTURE(bounds.x, bounds.y, bounds.width, bounds.height,
            editor.View()->Bounds().width, editor.View()->Bounds().height,
            editor.View()->Host()->Window().GetClientAreaMetrics().logical.y);
    Input down{Kind::Down};
    down.x = bounds.x + 5;
    down.y = bounds.y + 5;
    editor.View()->Host()->Route(down);
    down.kind = Kind::Up;
    editor.View()->Host()->Route(down);
    REQUIRE(editor.Document().WorkingValue("enabled") == Value{true});
    REQUIRE(Json::parse(console.str())["id"] == "enabled");
    logging.Disconnect();
    treeScroll->SetOffset(0);
    editor.View()->Host()->Update();
    auto* name = Controls<LWSUI::TextBox>(*editor.View())[1];
    for (const auto& [text, offset] : std::vector<std::pair<std::string,size_t>>{{"אבג",2},{"ab אבג cd",5},{"مرحبا",4}})
    {
        REQUIRE(editor.Document().SetValue("name",text));
        auto* textHost = editor.View()->Host(); textHost->Update(); REQUIRE(textHost->Focus(name));
        const float x = textHost->MeasureCaret(text,offset,name->Font());
        REQUIRE(textHost->HitTestText(text,x+.01f,name->Font()) == offset);
        Input pointer{Kind::Down}; pointer.x = name->Bounds().x + name->Style().textPadding + x + .01f;
        pointer.y = name->Bounds().y + 10; textHost->Route(pointer); pointer.kind=Kind::Up; textHost->Route(pointer);
        Input insertion{Kind::Text}; insertion.text="!"; textHost->Route(insertion);
        auto expected=text; expected.insert(offset,"!"); REQUIRE(name->Text()==expected);
    }
    REQUIRE(editor.Document().SetValue("name", std::string("WWWWWWWW")));
    auto* host = editor.View()->Host();
    REQUIRE(host->Focus(name));
    host->Update();
    Draft(*name, "i");
    // No paint between shortening the string, placing the caret, and typing.
    Input click{Kind::Down};
    click.x = name->Bounds().x + 6 + host->MeasureCaret("i", 1, name->Font()) + .1f;
    click.y = name->Bounds().y + 10;
    host->Route(click);
    click.kind = Kind::Up;
    host->Route(click);
    Input typing{Kind::Text};
    typing.text = "X";
    host->Route(typing);
    REQUIRE(name->Text() == "iX");
    auto* picker = FindControl<LWSUI::ColorPicker>(*editor.View());
    KeyPress(*FindControl<LWSUI::ColorSwatch>(*picker), Key::Enter);
    host->Update();
    REQUIRE(host->HasPopup());
    Input adjust{Kind::KeyDown};
    adjust.key = Key::Right;
    host->Route(adjust);
    auto replacement = SettingsStorage::EncodeValues(editor.Document().WorkingValues());
    replacement["values"]["color"] = "#abcdef80";
    unsigned reloadEdits = 0;
    auto reloadChanges = editor.Document().OnChange.Connect([&](const Change&) { ++reloadEdits; });
    REQUIRE(SettingsStorage::LoadValues(editor.Document(), replacement));
    REQUIRE_FALSE(host->HasPopup());
    REQUIRE(FindControl<LWSUI::ColorPicker>(*editor.View()) == picker);
    REQUIRE(picker->Value() == LLUtils::Color{uint32_t{0xabcdef80}});
    REQUIRE(editor.Document().WorkingValue("color") == Value{LLUtils::Color{uint32_t{0xabcdef80}}});
    REQUIRE(reloadEdits == 0);
    auto* branch = FindControl<LWSUI::TreeView::Branch>(*editor.View());
    REQUIRE(host->Focus(name));
    Draft(*name, "runtime theme draft");
    auto runtimeTheme = config.theme;
    runtimeTheme.controls.font.size = 22;
    REQUIRE(editor.SetTheme(runtimeTheme));
    runtimeTheme.controls.font.size = 24;
    runtimeTheme.description.background = LLUtils::Color{uint32_t{0x112233ff}};
    REQUIRE(editor.SetTheme(runtimeTheme));
    REQUIRE(name->Font().size != 24);
    host->Update();
    REQUIRE(name->Font().size == 24);
    REQUIRE(name->Text() == "runtime theme draft");
    REQUIRE(host->IsFocused(*name));
    REQUIRE(FindControl<LWSUI::TreeView::Branch>(*editor.View()) == branch);
    KeyPress(*name, Key::Escape);
    REQUIRE(name->Text() == "iX");
    KeyPress(*FindControl<LWSUI::ColorSwatch>(*picker), Key::Enter);
    REQUIRE(host->HasPopup());
    REQUIRE(editor.SetTheme(config.theme));
    host->Update();
    REQUIRE_FALSE(host->HasPopup());
    REQUIRE(name->Font().size == config.theme.controls.font.size);
    TempFile editedTheme;
    ThemeEditor themeEditor(platform, editedTheme.path);
    REQUIRE(themeEditor.Load(std::filesystem::path(LWSSETTINGS_EXAMPLES_DIR) / "theme-settings.json"));
    auto livePreview = themeEditor.OnPreview.Connect([&](const SettingsTheme& theme)
                                                     { REQUIRE(editor.SetTheme(theme)); });
    REQUIRE(themeEditor.Show());
    WaitForConfiguration(internal::ThemeEditorTestAccess::Editor(themeEditor).View()->Host()->Window());
    internal::ThemeEditorTestAccess::Editor(themeEditor).View()->Host()->Update();
    internal::ThemeEditorTestAccess::Editor(themeEditor).SetFilter("sections/description/colors/background");
    internal::ThemeEditorTestAccess::Editor(themeEditor).View()->Host()->Update();
    LWSUI::ColorPicker* themeColor = nullptr;
    for (auto* candidate : Controls<LWSUI::ColorPicker>(*internal::ThemeEditorTestAccess::Editor(themeEditor).View()))
    {
        bool visible = true;
        for (auto* parent = static_cast<LWSUI::Control*>(candidate); parent; parent = parent->Parent())
            visible &= parent->Visible();
        if (visible)
        {
            themeColor = candidate;
            break;
        }
    }
    REQUIRE(themeColor);
    size_t booleanFields = 0;
    for (const auto& field : ThemeFields::All())
        booleanFields += std::holds_alternative<BooleanDefinition>(field.definition.specification);
    REQUIRE(Controls<LWSUI::CheckBox>(*internal::ThemeEditorTestAccess::Editor(themeEditor).View()).size() == booleanFields);
    auto* reset = static_cast<LWSUI::Button*>(themeColor->Parent()->Children().back().get());
    REQUIRE(reset->Text() == "Inherited");
    REQUIRE_FALSE(reset->Enabled());
    auto* themeText = FindControl<LWSUI::TextBox>(*themeColor);
    REQUIRE(internal::ThemeEditorTestAccess::Editor(themeEditor).View()->Host()->Focus(themeText));
    Draft(*themeText, "#334455ff");
    host->Update();
    REQUIRE(Controls<LWSUI::ScrollView>(*editor.View())[1]->Style().background == LLUtils::Color{uint32_t{0x334455ff}});
    REQUIRE(themeText->Text() == "#334455ff");
    REQUIRE(reset->Text() == "Use inherited");
    REQUIRE(reset->Enabled());
    REQUIRE(internal::ThemeEditorTestAccess::Editor(themeEditor).View()->Host()->IsFocused(*themeText));
    REQUIRE(themeText->Finish(Phase::Cancel));
    host->Update();
    REQUIRE_FALSE(themeEditor.Overrides()->contains("sections/description/colors/background"));
    REQUIRE(reset->Text() == "Inherited");
    REQUIRE_FALSE(reset->Enabled());
    // An explicit default has no value-change notification when Reset is requested.
    const std::string background = "sections/description/colors/background";
    REQUIRE(internal::ThemeEditorTestAccess::Editor(themeEditor).Document().SetValue(background, LLUtils::Color{uint32_t{0x334455ff}}));
    REQUIRE(internal::ThemeEditorTestAccess::Editor(themeEditor).Document().SetValue(background, SettingsTheme{}.description.background));
    REQUIRE(themeEditor.Overrides()->contains(background));
    internal::ThemeEditorTestAccess::Editor(themeEditor).SetValueFilter(SettingValueFilter::Changed);
    internal::ThemeEditorTestAccess::Editor(themeEditor).View()->Host()->Update();
    REQUIRE_FALSE(themeColor->Parent()->Visible());
    internal::ThemeEditorTestAccess::Editor(themeEditor).SetValueFilter(SettingValueFilter::Default);
    internal::ThemeEditorTestAccess::Editor(themeEditor).View()->Host()->Update();
    REQUIRE(themeColor->Parent()->Visible());
    internal::ThemeEditorTestAccess::Editor(themeEditor).SetValueFilter(SettingValueFilter::All);
    internal::ThemeEditorTestAccess::Editor(themeEditor).View()->Host()->Update();
    KeyPress(*reset, Key::Enter);
    REQUIRE_FALSE(themeEditor.Overrides()->contains(background));
    REQUIRE_FALSE(reset->Enabled());
    themeEditor.Hide();
    REQUIRE_FALSE(internal::ThemeEditorTestAccess::Editor(themeEditor).IsVisible());
    {
        using Preset = LWSUI::ThemePreset;
        TempFile sharedThemeFile;
        ThemeSession session(sharedThemeFile.path);
        REQUIRE(session.SelectPreset(Preset::Warm)); REQUIRE(session.Save());
        ThemeEditor shared(platform, session);
        REQUIRE(shared.Show());
        auto* themeHost = internal::ThemeEditorTestAccess::Editor(shared).View()->Host();
        WaitForConfiguration(themeHost->Window()); themeHost->Update();
        REQUIRE(themeHost->Style().background == LWSUI::MakeTheme(Preset::Warm).background);
        REQUIRE(session.SelectPreset(Preset::Light)); themeHost->Update();
        REQUIRE(internal::ThemeEditorTestAccess::Editor(shared).Document().IsDirty());
        REQUIRE(shared.Overrides()->empty());
        REQUIRE(themeHost->Style().background == LWSUI::MakeTheme(Preset::Warm).background);
        auto* selector = FindButton(internal::ThemeEditorTestAccess::Editor(shared).View()->HeaderActions(), "Theme: Light *");
        REQUIRE(selector);
        auto* themeBar = themeHost->MainMenu();
        const float compactWidth = std::max(480.f, themeBar->MinimumWidth());
        const auto originalBounds = themeBar->Bounds();
        themeBar->Measure({compactWidth, originalBounds.height});
        themeBar->Arrange({0, 0, compactWidth, originalBounds.height});
        REQUIRE(selector->Bounds().width > 0);
        REQUIRE(selector->Bounds().x >= themeBar->Content()->Bounds().x);
        REQUIRE(selector->Bounds().x + selector->Bounds().width <=
                themeBar->Content()->Bounds().x + themeBar->Content()->Bounds().width);
        REQUIRE(selector->Bounds().y == themeBar->Bounds().y);
        themeBar->Arrange(originalBounds);
        KeyPress(*selector, Key::Enter);
        REQUIRE(themeHost->HasContextMenu());
        Input pick{Kind::KeyDown}; pick.key=Key::Home; themeHost->Route(pick);
        pick.key=Key::Down; themeHost->Route(pick); themeHost->Route(pick);
        pick.key=Key::Enter; themeHost->Route(pick); themeHost->Update(); themeHost->Update();
        REQUIRE(session.Current().preset == Preset::Warm);
        REQUIRE(themeHost->Style().background == LWSUI::MakeTheme(Preset::Warm).background);
        REQUIRE(session.SelectPreset(Preset::Light)); themeHost->Update();
        REQUIRE(shared.Save()); REQUIRE_FALSE(session.IsDirty()); themeHost->Update();
        shared.Hide(); REQUIRE(shared.Show()); themeHost->Update();
        REQUIRE(themeHost->Style().background == LWSUI::MakeTheme(Preset::Light).background);
        REQUIRE(session.SelectPreset(Preset::Warm)); themeHost->Update();
        shared.Hide(); REQUIRE(session.Current().preset == Preset::Light);
        REQUIRE(session.SelectPreset(Preset::Warm));
        shared.Hide(); // Hiding an already hidden editor must not replay its stale saved document.
        REQUIRE(session.Current().preset == Preset::Warm);
        REQUIRE(shared.Show()); themeHost->Update();
        REQUIRE(themeHost->Style().background == LWSUI::MakeTheme(Preset::Warm).background);
        shared.Hide(); REQUIRE(session.Current().preset == Preset::Light);
        REQUIRE(shared.Show());
        REQUIRE(session.SelectPreset(Preset::Warm));
        REQUIRE(shared.Save()); // Save before the deferred pull must save Warm, not stale Light values.
        REQUIRE(session.Saved().preset == Preset::Warm);
        REQUIRE_FALSE(internal::ThemeEditorTestAccess::Editor(shared).Document().IsDirty());
        shared.Hide();
    }
    int hiddenNotifications = 0;
    auto hidden = editor.OnHidden.Connect([&] { ++hiddenNotifications; });
    REQUIRE(editor.Hide());
    REQUIRE_FALSE(editor.IsVisible());
    REQUIRE(hiddenNotifications == 1);
    REQUIRE(editor.Hide());
    REQUIRE(hiddenNotifications == 1);
    REQUIRE(editor.Show());
    WaitForConfiguration(editor.View()->Host()->Window());
    editor.View()->Host()->Update();
    editor.SetFilter("");
    REQUIRE(editor.Document().ResetAll());
    editor.SetValueFilter(SettingValueFilter::Default);
    host->Update();
    auto* nameRow = name->Parent();
    REQUIRE(host->Focus(name));
    Draft(*name, "changed");
    host->Update();
    REQUIRE(nameRow->Visible());
    REQUIRE(name->Finish(Phase::Commit));
    REQUIRE(nameRow->Visible()); // Hiding is deferred out of input/notification callbacks.
    host->Update();
    REQUIRE_FALSE(nameRow->Visible());
    REQUIRE_FALSE(host->HasFocusWithin(*nameRow));
    editor.SetValueFilter(SettingValueFilter::Changed);
    host->Update();
    REQUIRE(nameRow->Visible());
    REQUIRE(editor.Hide());
    REQUIRE(editor.Show());
    WaitForConfiguration(editor.View()->Host()->Window());
    editor.View()->Host()->Update();
    REQUIRE(editor.GetValueFilter() == SettingValueFilter::Changed);
    REQUIRE(editor.Document().ResetAll());
    REQUIRE(nameRow->Visible());
    host->Update();
    REQUIRE_FALSE(nameRow->Visible());
    editor.SetValueFilter(SettingValueFilter::All);
    host->Update();
    auto* color = FindControl<LWSUI::ColorPicker>(*editor.View());
    REQUIRE(editor.Document().SetValue("color", LLUtils::Color{uint32_t{0xff0000ff}}));
    editor.SetValueFilter(SettingValueFilter::Changed);
    host->Update();
    KeyPress(*FindControl<LWSUI::ColorSwatch>(*color), Key::Enter);
    REQUIRE(host->HasPopup());
    REQUIRE(editor.Document().Reset("color"));
    host->Update();
    REQUIRE_FALSE(host->HasPopup());
    REQUIRE_FALSE(color->Parent()->Visible());
    REQUIRE(color->Value() == std::get<LLUtils::Color>(*editor.Document().WorkingValue("color")));

    auto* search = Controls<LWSUI::TextBox>(*editor.View())[0];
    LWSUI::Button* clearSearch = nullptr;
    for (auto* button : Controls<LWSUI::Button>(*editor.View()))
        if (button->Text() == "×")
            clearSearch = button;
    REQUIRE(clearSearch);
    REQUIRE_FALSE(clearSearch->Visible());
    REQUIRE(editor.Document().SetValue("count", int64_t{8}));
    const auto valuesBeforeClear = editor.Document().WorkingValues();
    auto* countRow = FindControl<LWSUI::NumericEdit<int64_t>>(*editor.View())->Parent();
    for (auto mode : {SettingValueFilter::All, SettingValueFilter::Changed, SettingValueFilter::Default})
    {
        editor.SetValueFilter(mode);
        editor.SetFilter("count");
        host->Update();
        REQUIRE(clearSearch->Visible());
        REQUIRE_FALSE(nameRow->Visible());
        REQUIRE(name->HighlightSpans("count count").size() == 2);
        auto clearBounds = clearSearch->Bounds();
        Input clearClick{Kind::Down};
        clearClick.x = clearBounds.x + clearBounds.width / 2;
        clearClick.y = clearBounds.y + clearBounds.height / 2;
        host->Route(clearClick);
        clearClick.kind = Kind::Up;
        host->Route(clearClick);
        host->Update();
        REQUIRE(search->Text().empty());
        REQUIRE_FALSE(clearSearch->Visible());
        REQUIRE(host->IsFocused(*search));
        REQUIRE(editor.GetValueFilter() == mode);
        REQUIRE(nameRow->Visible() == (mode != SettingValueFilter::Changed));
        REQUIRE(countRow->Visible() == (mode != SettingValueFilter::Default));
        REQUIRE(name->HighlightSpans("count count").empty());
        REQUIRE(editor.Document().WorkingValues() == valuesBeforeClear);

        Draft(*search, "count");
        host->Update();
        REQUIRE(clearSearch->Visible());
        Input escape{Kind::KeyDown};
        escape.key = Key::Escape;
        host->Route(escape);
        host->Update();
        REQUIRE(search->Text().empty());
        REQUIRE_FALSE(clearSearch->Visible());
        REQUIRE(host->IsFocused(*search));
        REQUIRE(editor.GetValueFilter() == mode);
        REQUIRE(nameRow->Visible() == (mode != SettingValueFilter::Changed));
        REQUIRE(countRow->Visible() == (mode != SettingValueFilter::Default));
        REQUIRE(name->HighlightSpans("count count").empty());
        host->Route(escape); // Empty search remains focused too.
        REQUIRE(host->IsFocused(*search));
    }
    editor.SetFilter("count");
    host->Update();
    REQUIRE(host->Focus(clearSearch));
    Input activate{Kind::KeyDown};
    activate.key = Key::Space;
    host->Route(activate);
    REQUIRE(search->Text().empty());
    REQUIRE(host->IsFocused(*search));
    REQUIRE(editor.GetValueFilter() == SettingValueFilter::Default);
    editor.SetFilter("count");
    host->Update();
    REQUIRE(host->Focus(clearSearch));
    editor.SetFilter(""); // Programmatic clearing must not leave focus on a hidden button.
    REQUIRE_FALSE(clearSearch->Visible());
    REQUIRE(host->IsFocused(*search));
    std::array<LWSUI::Button*, 3> filterButtons{};
    const std::array filterNames{"All", "Changed", "Default"};
    for (auto* button : Controls<LWSUI::Button>(*editor.View()))
        for (size_t i = 0; i < filterNames.size(); ++i)
            if (button->Text() == filterNames[i]) filterButtons[i] = button;
    for (auto* button : filterButtons) REQUIRE(button);
    auto* expandAll = FindButton(*editor.View(), "Expand all");
    auto* collapseAll = FindButton(*editor.View(), "Collapse all");
    REQUIRE(expandAll);
    REQUIRE(collapseAll);
    for (float fontSize : {16.f, 28.f})
    for (float spacing : {SettingsTheme{}.search.spacing, 16.f, 0.f})
    {
        SettingsTheme toolbarTheme;
        toolbarTheme.search.spacing = spacing;
        toolbarTheme.search.font.size = fontSize;
        REQUIRE(editor.SetTheme(toolbarTheme));
        host->Update();
        for (const auto& query : {std::string{}, std::string(200, 'x')})
        {
            editor.SetFilter(query);
            host->Update();
            for (float width : {820.f, 480.f, 300.f})
            {
                Layout(*editor.View(), width, 680);
                const auto textBounds = search->Bounds(), clearBounds = clearSearch->Bounds();
                const auto selector = filterButtons[0]->Bounds();
                REQUIRE(textBounds.width > 0);
                REQUIRE(clearSearch->Visible() == !query.empty());
                const float searchRight = clearSearch->Visible() ? clearBounds.x + clearBounds.width
                                                                 : textBounds.x + textBounds.width;
                if (clearSearch->Visible())
                {
                    REQUIRE(clearBounds.width > 0);
                    REQUIRE(textBounds.x + textBounds.width == Catch::Approx(clearBounds.x));
                    REQUIRE(clearBounds.y == textBounds.y);
                    REQUIRE(clearBounds.height == textBounds.height);
                }
                REQUIRE(searchRight <= width);
                if (selector.y == textBounds.y)
                    REQUIRE(selector.x - searchRight == Catch::Approx(spacing).margin(.001));
                else
                    REQUIRE(selector.y - textBounds.y - textBounds.height == Catch::Approx(spacing).margin(.001));
                if (width == 820 && fontSize == 16) REQUIRE(selector.y == textBounds.y);
                if (width == 300) REQUIRE(selector.y > textBounds.y);
                for (size_t i = 1; i < filterButtons.size(); ++i)
                {
                    const auto previous = filterButtons[i - 1]->Bounds(), current = filterButtons[i]->Bounds();
                    REQUIRE(current.x == Catch::Approx(previous.x + previous.width));
                    REQUIRE(current.y == selector.y);
                }
                const auto last = filterButtons.back()->Bounds();
                REQUIRE(last.x + last.width <= width);
                const auto expandBounds = expandAll->Bounds(), collapseBounds = collapseAll->Bounds();
                REQUIRE(expandBounds.width > 0);
                REQUIRE(expandBounds.width == collapseBounds.width);
                REQUIRE(expandBounds.width == Catch::Approx(expandBounds.height));
                REQUIRE(expandAll->flat);
                REQUIRE(collapseAll->flat);
                REQUIRE(collapseBounds.x - expandBounds.x - expandBounds.width == Catch::Approx(spacing).margin(.001));
                REQUIRE(collapseBounds.y == expandBounds.y);
                REQUIRE(collapseBounds.x + collapseBounds.width <= width);
                if (expandBounds.y == selector.y)
                    REQUIRE(expandBounds.x >= last.x + last.width + spacing - .001f);
                else
                    REQUIRE(expandBounds.y - selector.y - selector.height == Catch::Approx(spacing).margin(.001));
                // Intrinsic actions may share the selector row when all controls fit.
            }
        }
    }
    REQUIRE(editor.SetTheme(SettingsTheme{}));
    editor.SetFilter("");
    editor.SetValueFilter(SettingValueFilter::All);
    host->Update();
    auto* group = FindControl<LWSUI::TreeView::Branch>(*editor.View());
    REQUIRE(group->Expanded());
    REQUIRE_FALSE(expandAll->Enabled());
    REQUIRE(collapseAll->Enabled());

    SettingsTheme hintTheme;
    hintTheme.popups.background = LLUtils::Color{uint32_t{0xff00ffff}};
    REQUIRE(editor.SetTheme(hintTheme));
    host->Update();
    auto hintVisible = [&]
    {
        LWSUI::Canvas canvas;
        const auto viewBounds = editor.View()->Bounds();
        canvas.Begin(int(viewBounds.width), int(viewBounds.height));
        editor.View()->Render(canvas);
        LWSUI::internal::UIHostTestAccess::RenderTooltip(*host, canvas);
        const auto pixels = canvas.End();
        const auto actionBounds = collapseAll->Bounds();
        const int top = int(std::ceil(actionBounds.y + actionBounds.height + hintTheme.search.spacing));
        for (int y = top; y < std::min(top + 40, int(pixels.height)); ++y)
            for (uint32_t x = 0; x < pixels.width; ++x)
            {
                const auto* pixel = pixels.pixels.data() + y * pixels.rowPitch + x * 4;
                if (pixel[0] == std::byte{255} && pixel[1] == std::byte{0} && pixel[2] == std::byte{255}) return true;
            }
        return false;
    };
    REQUIRE(host->Focus(search));
    REQUIRE_FALSE(hintVisible());
    REQUIRE_FALSE(expandAll->Focusable());
    REQUIRE_FALSE(collapseAll->Focusable());
    REQUIRE(host->Focus(filterButtons.back()));
    Input tab{Kind::KeyDown}; tab.key = Key::Tab;
    host->Route(tab);
    REQUIRE_FALSE(host->IsFocused(*expandAll));
    REQUIRE_FALSE(host->IsFocused(*collapseAll));
    REQUIRE(host->HasFocusWithin(*treeScroll));
    REQUIRE_FALSE(hintVisible());
    REQUIRE(host->Focus(search));
    Input hoverAction{Kind::Move};
    hoverAction.x = collapseAll->Bounds().x + collapseAll->Bounds().width / 2;
    hoverAction.y = collapseAll->Bounds().y + collapseAll->Bounds().height / 2;
    host->Route(hoverAction);
    REQUIRE(hintVisible());
    Input leaveAction{Kind::Move}; leaveAction.x = -10; leaveAction.y = -10;
    host->Route(leaveAction);
    REQUIRE_FALSE(hintVisible());
    host->Route(hoverAction);
    REQUIRE(hintVisible());
    editor.SetFilter("count");
    REQUIRE_FALSE(expandAll->Enabled());
    REQUIRE_FALSE(collapseAll->Enabled());
    REQUIRE_FALSE(hintVisible()); // Disabled icons cannot leave a stale hover hint.
    host->Update();
    editor.SetFilter("");
    REQUIRE_FALSE(expandAll->Enabled());
    REQUIRE_FALSE(collapseAll->Enabled()); // Clearing the query still has a pending filter pass.
    host->Update();
    REQUIRE(collapseAll->Enabled());
    editor.SetValueFilter(SettingValueFilter::Default);
    host->Update();
    editor.SetValueFilter(SettingValueFilter::All);
    REQUIRE_FALSE(expandAll->Enabled());
    REQUIRE_FALSE(collapseAll->Enabled());
    host->Update();
    REQUIRE(collapseAll->Enabled());
    ClickButton(*collapseAll);
    editor.SetFilter("count");
    editor.SetFilter(""); // Net-empty filtering must still reject the earlier queued collapse.
    REQUIRE_FALSE(collapseAll->Enabled());
    host->Update();
    REQUIRE(group->Expanded());
    REQUIRE(collapseAll->Enabled());
    const auto beforeExpansion = editor.Document().WorkingValues();
    treeScroll->SetOffset(10000);
    host->Update();
    ClickButton(*collapseAll);
    REQUIRE_FALSE(host->IsFocused(*collapseAll));
    REQUIRE(group->Expanded()); // Applying collapse is deferred out of input dispatch.
    host->Update();
    REQUIRE_FALSE(group->Expanded());
    REQUIRE_FALSE(host->IsFocused(*expandAll));
    REQUIRE_FALSE(hintVisible());
    REQUIRE(treeScroll->Offset() == 0);
    REQUIRE(host->Focus(filterButtons.back()));
    host->Route(tab);
    REQUIRE(host->HasFocusWithin(*treeScroll));
    REQUIRE_FALSE(host->IsFocused(*expandAll));
    REQUIRE(host->Focus(&group->Header()));
    Input treeKey{Kind::KeyDown}; treeKey.key = Key::Right;
    host->Route(treeKey);
    host->Update();
    REQUIRE(group->Expanded());
    treeKey.key = Key::Left;
    host->Route(treeKey);
    host->Update();
    REQUIRE_FALSE(group->Expanded());
    auto boundsExpand = expandAll->Bounds();
    Input clickExpand{Kind::Down};
    clickExpand.x = boundsExpand.x + boundsExpand.width / 2;
    clickExpand.y = boundsExpand.y + boundsExpand.height / 2;
    host->Route(clickExpand);
    clickExpand.kind = Kind::Up;
    host->Route(clickExpand);
    host->Update();
    REQUIRE(group->Expanded());
    REQUIRE_FALSE(host->IsFocused(*expandAll));
    REQUIRE_FALSE(host->IsFocused(*collapseAll));
    REQUIRE_FALSE(hintVisible());
    REQUIRE(editor.Document().WorkingValues() == beforeExpansion);

    ClickButton(*collapseAll);
    editor.SetFilter("count"); // A queued action must respect a subsequently changed filter.
    host->Update();
    REQUIRE(group->Expanded());
    REQUIRE_FALSE(expandAll->Enabled());
    REQUIRE_FALSE(collapseAll->Enabled());
    REQUIRE_FALSE(host->IsFocused(*expandAll));
    REQUIRE_FALSE(host->IsFocused(*collapseAll));
    editor.SetFilter("");
    host->Update();
    auto* numeric = FindControl<LWSUI::NumericEdit<int64_t>>(*editor.View());
    auto* numericText = FindControl<LWSUI::TextBox>(*numeric);
    REQUIRE(host->Focus(numericText));
    Draft(*numericText, "-");
    ClickButton(*collapseAll);
    REQUIRE(host->IsFocused(*numericText)); // Invalid drafts block the mouse action.
    collapseAll->OnClick.Raise(); // Deferred validation also protects programmatic activation.
    host->Update();
    REQUIRE(group->Expanded());
    REQUIRE(host->IsFocused(*numericText));
    REQUIRE_FALSE(editor.View()->ValidationMessage().empty());
    REQUIRE(host->Finish(Phase::Cancel));
    REQUIRE_FALSE(clearSearch->Visible());
    REQUIRE(FindButton(*internal::ThemeEditorTestAccess::Editor(themeEditor).View(), "Expand all"));
    REQUIRE(FindButton(*internal::ThemeEditorTestAccess::Editor(themeEditor).View(), "Collapse all"));
    REQUIRE(editor.Hide());
}
TEST_CASE("Windows text clipboard round trips Unicode", "[clipboard]")
{
#ifdef _WIN32
    TestPlatform platform;
    LWS::Window window(platform);
    LWS::WindowConfig config;
    REQUIRE(window.Create(config) == LWS::Result::Success);
    REQUIRE(LWS::SetClipboardText(window, "café 日本語") == LWS::ClipboardResult::Success);
    bool called = false;
    LWS::RequestClipboardText(window,
                              [&](auto result, std::string value)
                              {
                                  REQUIRE(result == LWS::ClipboardResult::Success);
                                  REQUIRE(value == "café 日本語");
                                  called = true;
                              });
    REQUIRE(called);
    LWSUI::UIHost host(window);
    auto root = std::make_unique<LWSUI::TextBox>();
    auto& text = *root;
    host.SetRoot(std::move(root));
    REQUIRE(host.Focus(&text));
    text.SetText("café 日本語");
    KeyPress(text, Key::A, false, true);
    KeyPress(text, Key::X, false, true);
    REQUIRE(text.Text().empty());
    KeyPress(text, Key::V, false, true);
    REQUIRE(text.Text() == "café 日本語");
    // Invalid UTF-8 cannot be published, so the selected draft must survive Cut.
    const std::string invalid(1, char(0xff));
    text.SetText(invalid);
    KeyPress(text, Key::A, false, true);
    KeyPress(text, Key::X, false, true);
    REQUIRE(text.Text() == invalid);
#endif
}
TEST_CASE("Integer bounds stay exact beyond double precision")
{
    auto schema = Template();
    auto& count = schema["children"][0]["children"][1];
    count["min"] = int64_t{9007199254740992LL};
    count["max"] = int64_t{9007199254740994LL};
    count["default"] = int64_t{9007199254740993LL};
    SettingsDocument p;
    REQUIRE(SettingsStorage::LoadTemplate(p, schema));
    REQUIRE_FALSE(p.SetValue("count", int64_t{9007199254740995LL}));
    REQUIRE(p.SetValue("count", int64_t{9007199254740994LL}));
}
TEST_CASE("Subscribers added during a callback start on the next notification")
{
    SettingsDocument p;
    REQUIRE(SettingsStorage::LoadTemplate(p, Template()));
    std::vector<LWSUI::Event<void(const Change&)>::Connection> extra;
    int called = 0;
    auto first = p.OnChange.Connect(
        [&](const Change&)
        {
            if (extra.empty())
                for (int i = 0; i < 50; ++i)
                    extra.push_back(p.OnChange.Connect([&](const Change&) { ++called; }));
        });
    REQUIRE(p.SetValue("count", int64_t{4}));
    REQUIRE(called == 0);
    REQUIRE(p.SetValue("count", int64_t{5}));
    REQUIRE(called == 50);
    LWSUI::Event<void()>::Connection survivor;
    {
        auto publisher = std::make_unique<LWSUI::Event<void()>>();
        survivor = publisher->Connect([] {});
    }
    survivor.Disconnect(); // Disconnecting after the publisher is safe.
    LWSUI::Event<void()> event;
    int laterCalls = 0;
    LWSUI::Event<void()>::Connection firstConnection, laterConnection;
    firstConnection = event.Connect([&, capture = std::make_shared<int>(7)]
    {
        firstConnection.Disconnect();
        laterConnection.Disconnect();
        REQUIRE(*capture == 7);
    });
    laterConnection = event.Connect([&] { ++laterCalls; });
    event.Raise();
    event.Raise();
    REQUIRE(laterCalls == 0);
}
TEST_CASE("Long combo menus stay scrollable inside the tree viewport")
{
    auto list = std::make_unique<LWSUI::StackPanel>();
    for (int i = 0; i < 40; ++i)
        list->Emplace<LWSUI::Button>("Option " + std::to_string(i));
    LWSUI::ScrollView scroll;
    scroll.SetContent(std::move(list));
    Layout(scroll, 300, 240);
    scroll.SetOffset(400);
    Layout(scroll, 300, 240);
    REQUIRE(scroll.Offset() == 400);
    REQUIRE(scroll.HitTest(10, 20) != nullptr);
    scroll.SetOffset(100000);
    Layout(scroll, 300, 240);
    REQUIRE(scroll.Offset() < 1600);
}
TEST_CASE("DPI metrics keep physical text and native input aligned")
{
    for (double factor : {1.0, 1.25, 1.5, 2.0})
    {
        LWS::ClientAreaMetrics metrics{{160, 100}, LWS::PixelSize{int(160 * factor), int(100 * factor)}};
        const auto scale = metrics.Scale();
        REQUIRE(scale);
        REQUIRE(scale->x == Catch::Approx(factor));
        REQUIRE(scale->y == Catch::Approx(factor));
        LWSUI::Canvas canvas;
        canvas.Begin(metrics.pixels->x, metrics.pixels->y, *scale);
        canvas.Fill(0, 0, 160, 100, LLUtils::Color{uint32_t{0xff0000ff}});
        const auto pixels = canvas.End();
        REQUIRE(pixels.width == metrics.pixels->x);
        REQUIRE(pixels.height == metrics.pixels->y);
    }
    LWS::ClientAreaMetrics rounded{{161, 101}, LWS::PixelSize{202, 127}};
    REQUIRE(rounded.Scale()->x != rounded.Scale()->y);
    LWSUI::Canvas roundedCanvas;
    roundedCanvas.Begin(202, 127, *rounded.Scale());
    roundedCanvas.Fill(0, 0, 161, 101, LLUtils::Color{uint32_t{0xff0000ff}});
    const auto roundedPixels = roundedCanvas.End();
    const auto lastPixel = roundedPixels.pixels.data() + 126 * roundedPixels.rowPitch + 201 * 4;
    REQUIRE(std::to_integer<unsigned>(lastPixel[2]) >= 250);
    REQUIRE(std::to_integer<unsigned>(lastPixel[3]) >= 250);
    LWS::ClientAreaMetrics unconfigured{{160, 100}, std::nullopt};
    REQUIRE_FALSE(unconfigured.Scale());

}
TEST_CASE("Numeric steps validate metadata and preserve arithmetic boundaries")
{
    LWSUI::NumericEdit<int64_t> integer({0, 10, 3});
    integer.SetValue(9);
    KeyPress(integer, Key::Up);
    REQUIRE(integer.Value() == 10);
    integer.Finish(Phase::Commit);
    integer.SetValue(1);
    KeyPress(integer, Key::Down);
    REQUIRE(integer.Value() == 0);
    integer.Finish(Phase::Commit);
    LWSUI::NumericEdit<int64_t> large({{}, {}, INT64_MAX});
    large.SetValue(1);
    KeyPress(large, Key::Up);
    REQUIRE(large.Value() == INT64_MAX);
    large.Finish(Phase::Commit);
    large.SetValue(-2);
    KeyPress(large, Key::Down);
    REQUIRE(large.Value() == INT64_MIN);
    LWSUI::NumericEdit<double> decimal;
    for (int i = 0; i < 3; ++i)
    {
        KeyPress(decimal, Key::Up);
        decimal.Finish(Phase::Commit);
    }
    REQUIRE(FormatValue(Value{decimal.Value()}) == "0.3");
    decimal.SetValue(.15);
    KeyPress(decimal, Key::Up);
    decimal.Finish(Phase::Commit);
    REQUIRE(FormatValue(Value{decimal.Value()}) == "0.25");
    SettingsDocument p;
    auto schema = Template();
    REQUIRE(SettingsStorage::LoadTemplate(p, schema));
    REQUIRE(std::get<IntegerDefinition>(p.Find("count")->specification).presentation == NumericPresentation::Numeric);
    for (Json bad : std::vector<Json>{"unknown", "", 1, false})
    {
        auto invalid = Template();
        invalid["children"][0]["children"][1]["presentation"] = bad;
        REQUIRE_FALSE(SettingsStorage::LoadTemplate(p, invalid));
    }
    auto invalid = Template();
    invalid["children"][0]["children"][0]["presentation"] = "slider";
    REQUIRE_FALSE(SettingsStorage::LoadTemplate(p, invalid));
    auto integerSlider = Template();
    auto& definition = integerSlider["children"][0]["children"][1];
    definition["presentation"] = "slider";
    REQUIRE(SettingsStorage::LoadTemplate(p, integerSlider));
    definition.erase("min");
    REQUIRE_FALSE(SettingsStorage::LoadTemplate(p, integerSlider));
    definition["min"] = 3;
    definition["max"] = 3;
    REQUIRE_FALSE(SettingsStorage::LoadTemplate(p, integerSlider));
    definition["min"] = -9007199254740991LL;
    definition["max"] = 9007199254740991LL;
    REQUIRE_FALSE(SettingsStorage::LoadTemplate(p, integerSlider)); // Span too large.
    definition["min"] = 9007199254740987LL;
    definition["default"] = 9007199254740990LL;
    REQUIRE(SettingsStorage::LoadTemplate(p, integerSlider));
    {
        SettingsView view(p);
        auto* numeric = FindControl<LWSUI::NumericEdit<int64_t>>(view);
        auto* slider = FindControl<LWSUI::Slider>(*numeric->Parent());
        REQUIRE(slider);
        KeyPress(*slider, Key::Right);
        REQUIRE(p.WorkingValue("count") == Value{int64_t{9007199254740991LL}});
        KeyPress(*slider, Key::Left);
        REQUIRE(p.WorkingValue("count") == Value{int64_t{9007199254740990LL}});
    }
    definition["max"] = 9007199254740992LL;
    REQUIRE_FALSE(SettingsStorage::LoadTemplate(p, integerSlider));
    definition["presentation"] = "numeric";
    REQUIRE(SettingsStorage::LoadTemplate(p, integerSlider)); // Ordinary integer precision stays unrestricted.
    auto decimalSlider = Template();
    auto& dec = decimalSlider["children"][0]["children"][2];
    dec["presentation"] = "slider";
    dec["min"] = 0.5; dec["max"] = 3; dec["step"] = 0.1;
    REQUIRE(SettingsStorage::LoadTemplate(p, decimalSlider));
    {
        SettingsView view(p);
        auto* numeric = FindControl<LWSUI::NumericEdit<double>>(view);
        auto* slider = FindControl<LWSUI::Slider>(*numeric->Parent());
        auto* text = FindControl<LWSUI::TextBox>(*numeric);
        REQUIRE(slider);
        Layout(*numeric->Parent(), 300, 40);
        REQUIRE(p.SetValue("ratio", 1.03));
        Input down{Kind::Down};
        down.x = slider->TrackBounds().x + slider->TrackBounds().width * .08f;
        slider->Dispatch(down);
        REQUIRE(FormatValue(*p.WorkingValue("ratio")) == "0.7");
        KeyPress(*slider, Key::Escape);
        REQUIRE(p.WorkingValue("ratio") == Value{1.03});
        REQUIRE(text->Text() == "1.03");
        KeyPress(*slider, Key::Right);
        REQUIRE(p.WorkingValue("ratio") == Value{1.13});
        Draft(*text, "1.27");
        REQUIRE(numeric->Finish(Phase::Commit));
        REQUIRE(p.WorkingValue("ratio") == Value{1.27});
        REQUIRE(slider->Value() == Catch::Approx((1.27 - .5) / 2.5));
        down.x = slider->TrackBounds().x;
        slider->Dispatch(down); slider->Dispatch({Kind::Up});
        REQUIRE(p.WorkingValue("ratio") == Value{.5});
        down.x = slider->TrackBounds().x + slider->TrackBounds().width - .01f;
        slider->Dispatch(down); slider->Dispatch({Kind::Up});
        REQUIRE(p.WorkingValue("ratio") == Value{3.0});
        for (float width : {300.f, 140.f, 80.f})
        {
            Layout(*numeric->Parent(), width, 40);
            REQUIRE(slider->TrackBounds().width >= 0);
            REQUIRE(slider->TrackBounds().x + slider->TrackBounds().width <= numeric->Bounds().x);
            REQUIRE(numeric->Bounds().x + numeric->Bounds().width <= width);
            if (slider->TrackBounds().width > slider->Style().sliderThumbWidth)
            {
                REQUIRE(p.SetValue("ratio", 1.0));
                const float thumb = slider->Style().sliderThumbWidth;
                down.x = slider->TrackBounds().x + float(slider->Value()) * (slider->TrackBounds().width - thumb) +
                         thumb / 2;
                slider->Dispatch(down);
                REQUIRE(p.WorkingValue("ratio") == Value{1.0}); // Grabbing the thumb must not jump.
                slider->Dispatch({Kind::Up});
            }
        }
    }
    for (Json bad : std::vector<Json>{0, -1, 0.5, "bad"})
    {
        schema["children"][0]["children"][1]["step"] = bad;
        REQUIRE_FALSE(SettingsStorage::LoadTemplate(p, schema));
    }
}
TEST_CASE("Rocker holds preview repeatedly and confirm once for autosave")
{
    TempFile file;
    SettingsDocument p({ApplyPolicy::Immediate, SavePolicy::OnCommit, file.path});
    REQUIRE(SettingsStorage::LoadTemplate(p, Template()));
    TestPlatform platform;
    LWS::Window window(platform);
    LWS::WindowConfig config;
    config.visible = true;
    REQUIRE(window.Create(config) == LWS::Result::Success);
    LWSUI::UIHost host(window);
    auto view = std::make_unique<SettingsView>(p);
    auto* numeric = FindControl<LWSUI::NumericEdit<int64_t>>(*view);
    host.SetRoot(std::move(view));
    WaitForConfiguration(window);
    host.Update();
    std::vector<Change> events;
    auto c = p.OnChange.Connect([&](const Change& e) { events.push_back(e); });
    KeyPress(*numeric, Key::Up);
    REQUIRE(numeric->Value() == 4);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (numeric->Value() == 4 && std::chrono::steady_clock::now() < deadline)
        PumpFor(platform, 2);
    REQUIRE(numeric->Value() == 5);
    REQUIRE_FALSE(std::filesystem::exists(file.path));
    numeric->Finish(Phase::Commit);
    PumpFor(platform, 100);
    REQUIRE(numeric->Value() == 5);
    REQUIRE(events.size() == 3);
    REQUIRE(events.back().phase == EditPhase::Commit);
    REQUIRE(events.back().oldValue == Value{int64_t{3}});
    REQUIRE(std::filesystem::exists(file.path));
    TempFile sliderFile;
    SettingsDocument sliderDoc({ApplyPolicy::Immediate, SavePolicy::OnCommit, sliderFile.path});
    auto schema = Template();
    schema["children"][0]["children"][1]["presentation"] = "slider";
    schema["children"][0]["children"][1]["step"] = 3;
    REQUIRE(SettingsStorage::LoadTemplate(sliderDoc, schema));
    SettingsView sliderView(sliderDoc);
    auto* field = FindControl<LWSUI::NumericEdit<int64_t>>(sliderView);
    auto* slider = FindControl<LWSUI::Slider>(*field->Parent());
    REQUIRE(slider);
    Layout(*field->Parent(), 300, 40);
    std::vector<Change> sliderEvents;
    auto listener = sliderDoc.OnChange.Connect([&](const Change& change) { sliderEvents.push_back(change); });
    Input drag{Kind::Down};
    drag.x = slider->TrackBounds().width * .6f;
    slider->Dispatch(drag);
    REQUIRE(sliderDoc.WorkingValue("count") == Value{int64_t{6}});
    REQUIRE_FALSE(std::filesystem::exists(sliderFile.path));
    slider->Dispatch({Kind::Up});
    REQUIRE(sliderEvents.size() == 2);
    REQUIRE(sliderEvents[0].phase == EditPhase::Preview);
    REQUIRE(sliderEvents[1].phase == EditPhase::Commit);
    REQUIRE(sliderEvents[1].oldValue == Value{int64_t{3}});
    REQUIRE(std::filesystem::exists(sliderFile.path));
}
TEST_CASE("Rocker cancellation invalid drafts and external updates stop repeats")
{
    TestPlatform platform;
    LWS::Window window(platform);
    LWS::WindowConfig config;
    config.visible = true;
    REQUIRE(window.Create(config) == LWS::Result::Success);
    LWSUI::UIHost host(window);
    auto control = std::make_unique<LWSUI::NumericEdit<int64_t>>(LWSUI::NumericSpec<int64_t>{0, 10, 1});
    auto& numeric = *control;
    auto theme = host.Style();
    theme.repeatDelayMs = 20;
    theme.repeatIntervalMs = 20;
    host.SetTheme(theme);
    host.SetRoot(std::move(control));
    WaitForConfiguration(window);
    host.Update();
    numeric.SetValue(3);
    auto* text = FindControl<LWSUI::TextBox>(numeric);
    Draft(*text, "-");
    KeyPress(numeric, Key::Up);
    REQUIRE(numeric.Value() == 3);
    numeric.Finish(Phase::Cancel);
    KeyPress(numeric, Key::Up);
    REQUIRE(numeric.Value() == 4);
    numeric.Finish(Phase::Cancel);
    REQUIRE(numeric.Value() == 3);
    KeyPress(numeric, Key::Up);
    numeric.SetValue(9);
    PumpFor(platform, 100);
    REQUIRE(numeric.Value() == 9);
    KeyPress(numeric, Key::Down);
    REQUIRE(numeric.Value() == 8);
    numeric.Dispatch({Kind::Blur});
    PumpFor(platform, 100);
    REQUIRE(numeric.Value() == 8);
    KeyPress(numeric, Key::Down);
    auto detached = static_cast<LWSUI::Control*>(&numeric);
    auto oldHandle = detached->Handle();
    host.SetRoot(nullptr);
    PumpFor(platform, 100);
    REQUIRE_FALSE(oldHandle);
    SettingsDocument p;
    auto schema = Template();
    schema["children"][0]["children"][1]["presentation"] = "slider";
    schema["children"][0]["children"][1]["step"] = 3;
    REQUIRE(SettingsStorage::LoadTemplate(p, schema));
    SettingsView view(p);
    auto* field = FindControl<LWSUI::NumericEdit<int64_t>>(view);
    auto* slider = FindControl<LWSUI::Slider>(*field->Parent());
    auto* input = FindControl<LWSUI::TextBox>(*field);
    Layout(*field->Parent(), 300, 40);
    REQUIRE(p.SetValue("count", int64_t{4}));
    Input drag{Kind::Down};
    drag.x = slider->TrackBounds().width * .6f;
    slider->Dispatch(drag);
    REQUIRE(p.WorkingValue("count") == Value{int64_t{6}});
    KeyPress(*slider, Key::Escape);
    REQUIRE(p.WorkingValue("count") == Value{int64_t{4}});
    REQUIRE(input->Text() == "4");
    Draft(*input, "-");
    slider->Dispatch(drag);
    REQUIRE(p.WorkingValue("count") == Value{int64_t{4}});
    REQUIRE_FALSE(field->Finish(Phase::Commit));
    field->Finish(Phase::Cancel);
    slider->Dispatch(drag);
    REQUIRE(p.SetValue("count", int64_t{2}));
    drag.kind = Kind::Move;
    drag.x = slider->TrackBounds().width;
    slider->Dispatch(drag); slider->Dispatch({Kind::Up});
    REQUIRE(p.WorkingValue("count") == Value{int64_t{2}});
    REQUIRE(field->Value() == 2);
    drag.kind = Kind::Down;
    slider->Dispatch(drag);
    REQUIRE(p.Reset("count"));
    KeyPress(*slider, Key::Escape);
    REQUIRE(p.WorkingValue("count") == Value{int64_t{3}});
}
TEST_CASE("Staged numeric rocker commits only on Apply and keyboard release ends a hold")
{
    SettingsDocument p({ApplyPolicy::OnApply});
    REQUIRE(SettingsStorage::LoadTemplate(p, Template()));
    SettingsView view(p);
    auto* numeric = FindControl<LWSUI::NumericEdit<int64_t>>(view);
    KeyPress(*numeric, Key::Up);
    Input release{Kind::KeyUp};
    release.key = Key::Up;
    numeric->Dispatch(release);
    REQUIRE(p.AppliedValue("count") == Value{int64_t{3}});
    REQUIRE(p.Apply());
    REQUIRE(p.AppliedValue("count") == Value{int64_t{4}});
    SettingsDocument staged({ApplyPolicy::OnApply});
    auto schema = Template();
    schema["children"][0]["children"][1]["presentation"] = "slider";
    REQUIRE(SettingsStorage::LoadTemplate(staged, schema));
    SettingsView sliderView(staged);
    auto* field = FindControl<LWSUI::NumericEdit<int64_t>>(sliderView);
    auto* composite = field->Parent();
    auto* row = composite->Parent();
    auto* slider = FindControl<LWSUI::Slider>(*composite);
    Layout(*composite, 300, 40);
    sliderView.SetValueFilter(SettingValueFilter::Default);
    Input drag{Kind::Down};
    drag.x = slider->TrackBounds().width * .7f;
    slider->Dispatch(drag);
    REQUIRE(staged.WorkingValue("count") == Value{int64_t{7}});
    REQUIRE(staged.AppliedValue("count") == Value{int64_t{3}});
    REQUIRE(row->Visible());
    slider->Dispatch({Kind::Up});
    REQUIRE_FALSE(row->Visible());
    REQUIRE(staged.AppliedValue("count") == Value{int64_t{3}});
    REQUIRE(staged.Apply());
    REQUIRE(staged.AppliedValue("count") == Value{int64_t{7}});
    sliderView.SetValueFilter(SettingValueFilter::Changed);
    REQUIRE(row->Visible());
    KeyPress(*slider, Key::Left);
    REQUIRE(staged.WorkingValue("count") == Value{int64_t{6}});
    staged.Cancel();
    REQUIRE(field->Value() == 7);
    REQUIRE(staged.WorkingValue("count") == Value{int64_t{7}});
    REQUIRE(staged.Reset("count"));
    REQUIRE_FALSE(row->Visible());
}
TEST_CASE("Combo popup hover owns hit testing and description without changing values")
{
    TestPlatform platform;
    LWS::Window window(platform);
    LWS::WindowConfig config;
    config.clientSize = {600, 500};
    config.visible = true;
    REQUIRE(window.Create(config) == LWS::Result::Success);
    WaitForConfiguration(window);
    LWSUI::UIHost host(window);
    auto root = std::make_unique<LWSUI::StackPanel>();
    auto& combo = root->Emplace<LWSUI::ComboBox>(std::vector<LWSUI::Choice>{{"A", "a"}, {"B", "b"}});
    auto& covered = root->Emplace<LWSUI::Button>("Covered");
    int ownerHover = 0, coveredHover = 0;
    auto a = combo.OnHover.Connect([&] { ++ownerHover; });
    auto b = covered.OnHover.Connect([&] { ++coveredHover; });
    host.SetRoot(std::move(root));
    host.Update();
    KeyPress(combo, Key::Enter);
    host.Update();
    REQUIRE(host.HasPopup());
    Input move{Kind::Move};
    move.x = 20;
    move.y = 50;
    host.Route(move);
    REQUIRE(ownerHover == 1);
    REQUIRE(coveredHover == 0);
    REQUIRE(combo.Value() == "a");
    host.ClosePopup(Phase::Cancel);
    auto disabledControl = std::make_unique<LWSUI::Button>("Disabled");
    auto& disabledButton = *disabledControl;
    auto disabledStyle = host.Style();
    disabledStyle.foreground = LLUtils::Color{uint32_t{0xff0000ff}};
    disabledStyle.muted = LLUtils::Color{uint32_t{0x999999ff}};
    disabledStyle.surface = LLUtils::Color{uint32_t{0x202020ff}};
    disabledButton.SetStyle(disabledStyle);
    host.SetRoot(std::move(disabledControl));
    host.Update();
    REQUIRE(host.Focus(&disabledButton));
    move.x = 20; move.y = 20;
    host.Route(move);
    REQUIRE(disabledButton.Hovered());
    Input press{Kind::Down}; press.x = 20; press.y = 20;
    host.Route(press);
    disabledButton.SetEnabled(false);
    REQUIRE(host.IsFocused(disabledButton));
    REQUIRE(disabledButton.Hovered());
    REQUIRE_FALSE(disabledButton.Dispatch(press));
    auto renderButton = [](LWSUI::Button& button)
    {
        LWSUI::Canvas canvas;
        canvas.Begin(160, 50);
        canvas.Fill(0, 0, 160, 50, button.Style().background);
        button.Arrange({0, 0, 160, 50});
        button.Render(canvas);
        const auto frame = canvas.End();
        return std::vector<std::byte>(frame.pixels.begin(), frame.pixels.end());
    };
    LWSUI::Button mutedReference("Disabled");
    auto referenceStyle = disabledStyle;
    referenceStyle.foreground = referenceStyle.muted;
    referenceStyle.line = referenceStyle.surface;
    mutedReference.SetStyle(referenceStyle);
    for (bool flat : {false, true})
    {
        disabledButton.flat = mutedReference.flat = flat;
        REQUIRE(renderButton(disabledButton) == renderButton(mutedReference));
    }
    struct CountingControl : LWSUI::Control
    {
        int renders = 0;
        void OnRender(LWSUI::Canvas&) override { ++renders; }
    };
    auto counting = std::make_unique<CountingControl>();
    auto& counter = *counting;
    host.SetRoot(std::move(counting));
    PumpFor(platform, 80);
    const int initialRenders = counter.renders;
    REQUIRE(initialRenders > 0);
    int deferred = 0;
    for (int i = 0; i < 20; ++i) host.Invalidate();
    host.Post(counter.Handle(), [&](auto& owner)
    {
        ++deferred;
        host.Post(owner.Handle(), [&](auto&) { ++deferred; });
    });
    PumpFor(platform, 50);
    REQUIRE(deferred == 2);
    REQUIRE(counter.renders == initialRenders + 1);
    PumpFor(platform, 60);
    REQUIRE(counter.renders == initialRenders + 1);

    REQUIRE(window.SetVisible(false) == LWS::Result::Success);
    host.Post(counter.Handle(), [&](auto&) { ++deferred; });
    PumpFor(platform, 30);
    REQUIRE(deferred == 2);
    REQUIRE(window.SetVisible(true) == LWS::Result::Success);
    WaitForConfiguration(window);
    PumpFor(platform, 50);
    REQUIRE(deferred == 3);

    int expiredWork = 0;
    host.Post(counter.Handle(), [&](auto&) { ++expiredWork; });
    host.SetRoot(std::make_unique<LWSUI::Control>());
    {
        LWSUI::UIHost temporary(window);
        temporary.SetRoot(std::make_unique<LWSUI::Control>());
        temporary.Post(temporary.Root()->Handle(), [&](auto&) { ++expiredWork; });
    }
    PumpFor(platform, 30);
    REQUIRE(expiredWork == 0);

    // Context menus retain editor focus and can sit above an existing popup.
    auto commands = std::make_unique<LWSUI::StackPanel>();
    auto& edit = commands->Emplace<LWSUI::TextBox>("original");
    auto& action = commands->Emplace<LWSUI::Button>("Actions");
    int clicks = 0, chosen = 0, requests = 0, parentRequests = 0;
    auto clickConnection = action.OnClick.Connect([&] { ++clicks; });
    commands->SetContextMenuProvider([&](auto&, const auto&)
    { ++parentRequests; return std::vector<LWSUI::ContextMenuItem>{{"Parent", "", [&](auto&) { chosen = 3; }}}; });
    action.SetContextMenuProvider([&](auto& owner, const auto& request)
    {
        REQUIRE(request.target.Get() == &owner);
        ++requests;
        return std::vector<LWSUI::ContextMenuItem>{
            LWSUI::ContextMenuItem::Separator(),
            {"Unavailable", "", [&](auto&) { chosen = -1; }, false},
            {"One", "", [&](auto&) { chosen = 1; }, true, true},
            LWSUI::ContextMenuItem::Separator(),
            {"Two", "Ctrl+2", [&](auto&) { chosen = 2; }},
            LWSUI::ContextMenuItem::Separator()};
    });
    host.SetRoot(std::move(commands)); host.Update();
    auto menuKey = [&](Key key, bool shift = false)
    { Input input{Kind::KeyDown}; input.key = key; input.shift = shift; host.Route(input); };
    auto rightClick = [&](LWSUI::Control& control, float offset = 2.f)
    {
        const auto bounds = control.Bounds();
        Input input{Kind::Down}; input.button = LWS::MouseButton::Right;
        input.x = bounds.x + offset; input.y = bounds.y + bounds.height / 2;
        host.Route(input); input.kind = Kind::Up; host.Route(input);
    };
    REQUIRE(host.Focus(&edit)); Draft(edit, "invalid draft"); edit.SetValidation("Invalid");
    const auto draft = edit.Text();
    menuKey(Key::F10, true);
    REQUIRE(host.HasContextMenu()); REQUIRE(host.IsFocused(edit));
    REQUIRE(edit.Text() == draft); REQUIRE_FALSE(edit.Validation().empty());
    menuKey(Key::Escape);
    REQUIRE_FALSE(host.HasContextMenu()); REQUIRE(edit.Text() == draft);
    rightClick(action); REQUIRE_FALSE(host.HasContextMenu()); REQUIRE(requests == 0);
    REQUIRE(host.IsFocused(edit)); // Another control cannot bypass this invalid edit.
    REQUIRE(edit.Finish(Phase::Cancel));
    rightClick(action); REQUIRE(host.HasContextMenu()); REQUIRE(clicks == 0);
    REQUIRE(requests == 1); REQUIRE(parentRequests == 0);
    menuKey(Key::Home); menuKey(Key::Enter);
    REQUIRE_FALSE(host.HasContextMenu()); REQUIRE(chosen == 0);
    host.Update(); REQUIRE(chosen == 1); REQUIRE(host.IsFocused(action));
    menuKey(Key::F10, true); menuKey(Key::Home); menuKey(Key::Down); menuKey(Key::Space);
    host.Update(); REQUIRE(chosen == 2);
    menuKey(Key::F10, true); menuKey(Key::End); menuKey(Key::Down); menuKey(Key::Enter);
    host.Update(); REQUIRE(chosen == 1); // Navigation wraps and skips separators/disabled commands.
    menuKey(Key::F10, true); menuKey(Key::Tab); REQUIRE_FALSE(host.HasContextMenu());
    action.SetContextMenuProvider([](auto&, const auto&) { return std::vector<LWSUI::ContextMenuItem>{}; });
    rightClick(action); REQUIRE_FALSE(host.HasContextMenu()); REQUIRE(parentRequests == 0);
    action.SetContextMenuProvider({});
    rightClick(action); REQUIRE(host.HasContextMenu()); REQUIRE(parentRequests == 1);
    menuKey(Key::Home); menuKey(Key::Enter); host.Update(); REQUIRE(chosen == 3);

    REQUIRE(host.Focus(&edit)); menuKey(Key::F10, true);
    Input outside{Kind::Down}; outside.x = 599; outside.y = 499;
    host.Route(outside); outside.kind = Kind::Up; host.Route(outside);
    REQUIRE_FALSE(host.HasContextMenu()); REQUIRE(host.IsFocused(edit)); REQUIRE(clicks == 0);
    menuKey(Key::F10, true); edit.SetEnabled(false); host.Update();
    REQUIRE_FALSE(host.HasContextMenu()); edit.SetEnabled(true);
    menuKey(Key::F10, true); edit.SetVisible(false); host.Update();
    REQUIRE_FALSE(host.HasContextMenu()); edit.SetVisible(true);

    auto popup = std::make_unique<LWSUI::TextBox>("popup"); auto& popupEdit = *popup;
    unsigned popupClosed = 0;
    REQUIRE(host.OpenPopup(edit, std::move(popup), {20, 20, 260, 50}, [&](auto) { ++popupClosed; }, true));
    host.Update(); REQUIRE(host.Focus(&popupEdit));
    Draft(popupEdit, "invalid"); popupEdit.SetValidation("Invalid");
    REQUIRE_FALSE(host.OpenPopup(edit,std::make_unique<LWSUI::Button>("Replacement"),{0,0,100,40}));
    REQUIRE(host.HasPopup()); REQUIRE(popupClosed==0); REQUIRE(popupEdit.Text()=="invalid");
    menuKey(Key::F10, true); REQUIRE(host.HasContextMenu()); REQUIRE(host.HasPopup());
    menuKey(Key::Escape); REQUIRE_FALSE(host.HasContextMenu()); REQUIRE(host.HasPopup());
    REQUIRE(popupEdit.Text() == "invalid"); REQUIRE_FALSE(popupEdit.Validation().empty());
    menuKey(Key::F10, true); host.ClosePopup(Phase::Cancel);
    REQUIRE_FALSE(host.HasContextMenu()); REQUIRE_FALSE(host.HasPopup());
    REQUIRE(popupClosed==1);
    REQUIRE(host.OpenPopup(edit,std::make_unique<LWSUI::Button>("First"),{0,0,100,40},[&](auto) {
        REQUIRE(host.OpenPopup(edit,std::make_unique<LWSUI::Button>("Reentrant"),{0,0,100,40}));
    }));
    REQUIRE_FALSE(host.OpenPopup(edit,std::make_unique<LWSUI::Button>("Do not replace"),{0,0,100,40}));
    REQUIRE(host.HasPopup()); host.ClosePopup(Phase::Cancel);
    auto resizePopup = std::make_unique<LWSUI::TextBox>("original"); auto* resizeText=resizePopup.get();
    unsigned cancelled=0;
    REQUIRE(host.OpenPopup(edit,std::move(resizePopup),{0,0,500,300},[&](auto phase) { REQUIRE(phase==Phase::Cancel); ++cancelled; },true));
    REQUIRE(host.Focus(resizeText)); Draft(*resizeText,"invalid"); resizeText->SetValidation("Invalid");
    REQUIRE(window.RequestPlacement({.clientSize=LWS::LogicalSize{480,440}})==LWS::Result::Success);
    const auto resizeDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(host.HasPopup()&&std::chrono::steady_clock::now()<resizeDeadline) PumpFor(platform,5);
    REQUIRE_FALSE(host.HasPopup()); REQUIRE(cancelled==1);
    struct CancelOpener : LWSUI::Control
    {
        std::function<void()> action;
        bool OnInput(const Input& e) override
        {
            if(e.kind==Kind::Down) { Capture(); return true; }
            if(e.kind==Kind::Cancel) { ReleaseCapture(); action(); return true; }
            return false;
        }
    };
    auto& captured = static_cast<LWSUI::Container&>(*host.Root()).Emplace<CancelOpener>();
    captured.action = [&] { REQUIRE(host.OpenPopup(edit,std::make_unique<LWSUI::Button>("Capture replacement"),{0,0,100,40})); };
    captured.Dispatch({Kind::Down});
    REQUIRE_FALSE(host.OpenPopup(edit,std::make_unique<LWSUI::Button>("Outer replacement"),{0,0,100,40}));
    REQUIRE(host.HasPopup()); host.ClosePopup(Phase::Cancel);


    std::vector<LWSUI::ContextMenuItem> longMenu;
    for (int i = 0; i < 40; ++i)
        longMenu.push_back({"Long command " + std::to_string(i), "", [&, i](auto&) { chosen = i; }});
    REQUIRE(host.ShowContextMenu(edit, {edit.Handle(), 599, 499, true}, std::move(longMenu)));
    menuKey(Key::End); host.Update(); menuKey(Key::Enter); host.Update(); REQUIRE(chosen == 39);
    REQUIRE(host.ShowContextMenu(edit, {edit.Handle(), 0, 0, false},
        {{"Mouse command", "", [&](auto&) { chosen = 50; }}}));
    Input menuClick{Kind::Down}; menuClick.x = 8; menuClick.y = 8;
    host.Route(menuClick); menuClick.kind = Kind::Up; host.Route(menuClick);
    REQUIRE_FALSE(host.HasContextMenu()); host.Update(); REQUIRE(chosen == 50);
    REQUIRE(host.ShowContextMenu(edit, {edit.Handle(), 0, 0, true},
        {{"Expired command", "", [&](auto&) { chosen = -50; }}}));
    menuKey(Key::Enter);
    host.SetRoot(std::make_unique<LWSUI::Control>()); host.Update();
    REQUIRE(chosen == 50); REQUIRE_FALSE(host.HasContextMenu());
    auto focusTree = std::make_unique<LWSUI::Container>();
    auto& parent = focusTree->Emplace<LWSUI::Container>();
    auto& child = parent.Emplace<LWSUI::TextBox>("visible");
    host.SetRoot(std::move(focusTree));
    REQUIRE(host.Focus(&child));
    Input textInput{Kind::Text}; textInput.text = "hidden input";
    parent.SetVisible(false);
    host.Route(textInput); REQUIRE(child.Text() == "visible");
    REQUIRE(host.IsFocused(child)); REQUIRE_FALSE(host.Focus(&child));
    parent.SetVisible(true); parent.SetEnabled(false);
    host.Route(textInput); REQUIRE(child.Text() == "visible");
    REQUIRE_FALSE(host.Focus(&child));
    parent.SetEnabled(true); REQUIRE(host.Focus(&child));
    LWSUI::TextBox detached;
    REQUIRE_FALSE(host.Focus(&detached));
    {
        LWSUI::UIHost foreign(window);
        foreign.SetRoot(std::make_unique<LWSUI::TextBox>());
        REQUIRE_FALSE(host.Focus(foreign.Root()));
        REQUIRE(host.IsFocused(child));
    }
    auto radioEditor = std::make_unique<LWSUI::RadioGroup>(std::vector<LWSUI::Choice>{{"A","a"},{"B","b"}});
    auto& radio = *radioEditor; radio.SetValue("b"); radio.SetEnabled(false);
    auto& property = static_cast<LWSUI::Container&>(*host.Root()).Emplace<LWSUI::TreeView::Row>(
        std::make_unique<LWSUI::Label>("Radio"), std::move(radioEditor));
    REQUIRE(host.Focus(&property));
    menuKey(Key::Space); REQUIRE(radio.Value() == "b");
    radio.SetEnabled(true); radio.SetVisible(false);
    menuKey(Key::Space); REQUIRE(radio.Value() == "b");
    radio.SetVisible(true); menuKey(Key::Space); REQUIRE(radio.Value() == "a");
}
TEST_CASE("Deep groups retain label space breadcrumbs search and reset behavior")
{
    Json leaf{{"type", "text"},
              {"id", "leaf"},
              {"label", "Leaf"},
              {"default", "default"},
              {"description", "Deep setting"}};
    for (int i = 63; i >= 0; --i)
        leaf = Json{{"type", "group"},
                    {"id", "g" + std::to_string(i)},
                    {"label", "Group " + std::to_string(i)},
                    {"children", Json::array({leaf})}};
    SettingsDocument p;
    REQUIRE(SettingsStorage::LoadTemplate(p, Json{{"version", 1}, {"children", Json::array({leaf})}}));
    SettingsView view(p);
    Layout(view, 480, 680);
    auto branches = Controls<LWSUI::TreeView::Branch>(view);
    REQUIRE(branches.size() == 64);
    branches[0]->SetExpanded(false);
    view.SetFilter("Deep setting");
    REQUIRE(branches[0]->Content().Visible());
    auto texts = Controls<LWSUI::TextBox>(view);
    Layout(view, 480, 680);
    // The larger outer margin uses some editor space, but its default value must
    // remain readable even at the minimum form width and maximum group depth.
    LWSUI::Canvas metrics;
    REQUIRE(texts.back()->Bounds().width >=
            metrics.Measure(texts.back()->Text(), texts.back()->Font()) + 2 * texts.back()->Style().textPadding);
    view.SetFilter("");
    REQUIRE_FALSE(branches[0]->Content().Visible());
    REQUIRE(p.SetValue("leaf", std::string("different")));
    REQUIRE(p.Reset("g0"));
    REQUIRE(p.WorkingValue("leaf") == Value{std::string("default")});
    auto valueControl = std::make_unique<LWSUI::TextBox>();
    auto& value = *valueControl;
    auto actionControl = std::make_unique<LWSUI::Button>("Reset");
    auto& reset = *actionControl;
    LWSUI::TreeView::Row row(std::make_unique<LWSUI::Label>("Setting"), std::move(valueControl), std::move(actionControl));
    STATIC_REQUIRE_FALSE((std::is_base_of_v<LWSUI::Grid, LWSUI::TreeView::Row>));
    auto style = LWSUI::Theme{};
    style.resetWidth = 100;
    style.resetRightMargin = 16;
    row.SetStyle(style);
    Layout(row, 500, 60);
    const auto initial = reset.Bounds();
    REQUIRE(initial.width == 100);
    REQUIRE(initial.x + initial.width == 484);
    for (float padding : {0.f, 120.f, 10000.f})
    {
        style.padding = padding;
        row.SetStyle(style);
        Layout(row, 500, 60);
        REQUIRE(reset.Bounds().width == initial.width);
        REQUIRE(reset.Bounds().x == initial.x);
        REQUIRE(value.Bounds().x + value.Bounds().width <= reset.Bounds().x);
    }
    style.padding = 8;
    style.resetRightMargin = 48;
    row.SetStyle(style);
    Layout(row, 500, 60);
    REQUIRE(reset.Bounds().width == initial.width);
    REQUIRE(reset.Bounds().x == initial.x - 32);
    style.resetRightMargin = 10000;
    style.padding = 10000;
    row.SetStyle(style);
    for (float width : {480.f, 140.f, 40.f})
    {
        Layout(row, width, 60);
        REQUIRE(reset.Bounds().width == std::min(100.f, width));
        REQUIRE(reset.Bounds().x >= 0);
        REQUIRE(reset.Bounds().x + reset.Bounds().width <= width);
    }
    reset.SetText("Use inherited");
    style.resetRightMargin = 16;
    style.padding = 8;
    style.font.size = 28;
    row.SetStyle(style);
    Layout(row, 800, 80);
    REQUIRE(reset.Bounds().width > style.resetWidth);
    REQUIRE(reset.Bounds().x + reset.Bounds().width == 784);
    struct MeasuredSlot : LWSUI::Control
    {
        float measuredWidth = 0;
        LWSUI::Size OnMeasure(LWSUI::Size size) override { measuredWidth = size.width; return {size.width, 30}; }
    };
    auto slot = std::make_unique<MeasuredSlot>(); auto& measured = *slot;
    LWSUI::TreeView::Row noAction(std::make_unique<LWSUI::Label>("Value"), std::move(slot));
    style.font.size = 16; noAction.SetStyle(style);
    Layout(noAction, 500, 60);
    REQUIRE(noAction.ActionControl() == nullptr);
    REQUIRE(measured.measuredWidth == measured.Bounds().width);
    REQUIRE(measured.Bounds().x + measured.Bounds().width == 484);
    REQUIRE(&noAction.EditorControl() == &measured);
    REQUIRE_THROWS_AS((LWSUI::TreeView::Row(nullptr, std::make_unique<LWSUI::Control>())), std::invalid_argument);

}
TEST_CASE("Non-default highlighting survives save and follows staged cancellation")
{
    TempFile file;
    SettingsDocument p({ApplyPolicy::OnApply, SavePolicy::Manual, file.path});
    REQUIRE(SettingsStorage::LoadTemplate(p, Template()));
    SettingsView view(p);
    auto* branch = FindControl<LWSUI::TreeView::Branch>(view);
    auto* label = FindControl<LWSUI::Label>(*branch->Content().Children()[1]);
    REQUIRE(p.SetValue("count", int64_t{8}));
    REQUIRE(label->Foreground() == view.Style().changed);
    view.SetValueFilter(SettingValueFilter::Changed);
    REQUIRE(label->Parent()->Visible());
    p.Cancel();
    REQUIRE_FALSE(label->Parent()->Visible());
    REQUIRE(label->Foreground() == view.Style().foreground);
    REQUIRE(p.SetValue("count", int64_t{8}));
    REQUIRE(p.Apply());
    REQUIRE(p.Save());
    REQUIRE_FALSE(p.IsDirty());
    REQUIRE(label->Foreground() == view.Style().changed);
    REQUIRE(label->Font().weight == 700);
    for (auto preset : {LWSUI::ThemePreset::Dark, LWSUI::ThemePreset::Light, LWSUI::ThemePreset::Warm})
    {
        SettingsTheme theme(preset);
        const auto changedColor = preset == LWSUI::ThemePreset::Dark ? LLUtils::Color{uint32_t{0xfbbf24ff}} : theme.base.foreground;
        REQUIRE(theme.tree.changed == changedColor);
        view.SetTheme(theme); // A live switch preserves changed state without arranging.
        REQUIRE(label->Foreground() == changedColor);
        REQUIRE(label->Font().weight == 700);

        SettingsDocument model;
        auto definitions = Template();
        definitions["children"][0]["children"][2]["presentation"] = "slider";
        definitions["children"][0]["children"][2]["min"] = 0;
        definitions["children"][0]["children"][2]["max"] = 10;
        definitions["children"][0]["children"][6]["options"].push_back({{"label","B"},{"value","b"}});
        REQUIRE(SettingsStorage::LoadTemplate(model, definitions));
        SettingsView sample(model, "Changed styles", theme);
        auto* group = FindControl<LWSUI::TreeView::Branch>(sample);
        const Values edits{{"name",std::string("new name")},{"count",int64_t{8}},{"ratio",2.5},{"enabled",true},
            {"color",LLUtils::Color{uint32_t{0xaabbccff}}},{"choice",std::string("b")},
            {"radio",std::string("b")},{"file",std::string("programmatic-path")}};
        size_t index = 0;
        for (const auto& node : definitions["children"][0]["children"])
        {
            const auto id = node["id"].get<std::string>();
            auto& row = *static_cast<LWSUI::Container*>(group->Content().Children()[index++].get());
            auto& name = *row.Children()[0]; auto& value = *row.Children()[1]; auto& reset = *row.Children()[2];
            auto controls = Controls<LWSUI::Control>(value);
            std::vector<LWSUI::FontSpec> fonts;
            for (auto* control : controls) fonts.push_back(control->Font());
            const auto resetFont = reset.Font();
            REQUIRE(name.Font().weight == theme.tree.font.weight);
            REQUIRE(model.SetValue(id, edits.at(id), EditPhase::Preview));
            REQUIRE(name.Font().weight == 700);
            REQUIRE(name.Foreground() == theme.tree.changed);
            REQUIRE(value.Foreground() == theme.controls.changed);
            REQUIRE(reset.Font() == resetFont);
            for (size_t i = 0; i < controls.size(); ++i) REQUIRE(controls[i]->Font() == fonts[i]);
            REQUIRE(model.SetValue(id, edits.at(id), EditPhase::Cancel));
            REQUIRE(name.Font().weight == theme.tree.font.weight);
            REQUIRE(name.Foreground() == theme.tree.foreground);
            REQUIRE(model.SetValue(id, edits.at(id)));
            REQUIRE(model.Reset(id));
            REQUIRE(name.Font().weight == theme.tree.font.weight);
        }
        REQUIRE(model.ReplaceValues(edits));
        for (auto& row : group->Content().Children())
            REQUIRE(static_cast<LWSUI::Container&>(*row).Children()[0]->Font().weight == 700);
        REQUIRE(model.ReplaceValues({}));
        for (auto& row : group->Content().Children())
            REQUIRE(static_cast<LWSUI::Container&>(*row).Children()[0]->Font().weight == theme.tree.font.weight);
        REQUIRE(model.SetValue("count",int64_t{8}));
        SettingsView loaded(model, "Loaded", theme);
        auto* loadedLabel = FindControl<LWSUI::Label>(*FindControl<LWSUI::TreeView::Branch>(loaded)->Content().Children()[1]);
        REQUIRE(loadedLabel->Font().weight == 700);
        theme.tree.font.weight = 800; theme.tree.font.size = 24; theme.tree.font.family = "Arial";
        theme.tree.changed = LLUtils::Color{uint32_t{0xaabbccff}};
        theme.controls.changed = LLUtils::Color{uint32_t{0xccddaaff}};
        sample.SetTheme(theme);
        auto& countRow = static_cast<LWSUI::Container&>(*group->Content().Children()[1]);
        REQUIRE(countRow.Children()[0]->Font() == theme.tree.font);
        REQUIRE(countRow.Children()[0]->Foreground() == theme.tree.changed);
        REQUIRE(countRow.Children()[1]->Foreground() == theme.controls.changed);
        REQUIRE(model.Reset("count"));
        REQUIRE(countRow.Children()[0]->Font().weight == 800);
    }
}
TEST_CASE("Console notifications serialize every event field with typed values")
{
    Change event{"unicode-שלום",      int64_t{1}, int64_t{2}, EditPhase::Commit, ChangeOrigin::Programmatic,
                 ValueDomain::Applied};
    std::ostringstream output;
    Example::PrintEvent(output, event);
    auto record = Json::parse(output.str());
    REQUIRE(record.size() == 7);
    REQUIRE(record["id"] == event.id);
    REQUIRE(record["oldValue"].is_number_integer());
    REQUIRE(record["newValue"] == 2);
    REQUIRE(record["phase"] == "commit");
    REQUIRE(record["origin"] == "programmatic");
    REQUIRE(record["domain"] == "applied");
    std::vector<Value> types = {true, int64_t{8}, 1.25, std::string("line\nquoted \"value\""),
                                LLUtils::Color{uint32_t{0x10203040}}};
    for (const auto& v : types)
    {
        event.oldValue = event.newValue = v;
        event.phase = EditPhase::Preview;
        event.domain = ValueDomain::Working;
        event.origin = ChangeOrigin::Cancel;
        auto j = Example::EventJson(event);
        REQUIRE(j["phase"] == "preview");
        REQUIRE(j["domain"] == "working");
        REQUIRE(j["origin"] == "cancel");
        if (std::holds_alternative<LLUtils::Color>(v))
            REQUIRE(j["newValue"] == "#10203040");
    }
}
TEST_CASE("Showcase contains all editors across three nested scopes")
{
    SettingsDocument p;
    REQUIRE(SettingsStorage::LoadTemplateFile(p, std::filesystem::path(LWSSETTINGS_EXAMPLES_DIR) / "settings.json"));
    REQUIRE(
        SettingsStorage::LoadValuesFile(p, std::filesystem::path(LWSSETTINGS_EXAMPLES_DIR) / "showcase-values.json"));
    std::set<size_t> types;
    int maxDepth = 0;
    auto walk = [&](auto&& self, const auto& list, int depth) -> void
    {
        for (const auto& d : list)
            if (auto* g = std::get_if<GroupDefinition>(&d.content))
                self(self, g->children, depth + 1);
            else
            {
                types.insert(std::get<SettingDefinition>(d.content).specification.index());
                maxDepth = std::max(depth, maxDepth);
            }
    };
    walk(walk, p.Definitions(), 0);
    REQUIRE(types.size() == 7);
    REQUIRE(std::get<DecimalDefinition>(p.Find("scale")->specification).presentation == NumericPresentation::Slider);
    REQUIRE(std::get<DecimalDefinition>(p.Find("large_range")->specification).presentation == NumericPresentation::Numeric);
    SettingsView view(p);
    auto* scale = FindControl<LWSUI::NumericEdit<double>>(view);
    REQUIRE(FindControl<LWSUI::Slider>(*scale->Parent()));
    REQUIRE(maxDepth == 3);
    REQUIRE_FALSE(p.IsDirty());
}
TEST_CASE("Rocker mouse hit areas stop repeating when the pointer departs")
{
    TestPlatform platform;
    LWS::Window window(platform);
    LWS::WindowConfig config;
    config.visible = true;
    REQUIRE(window.Create(config) == LWS::Result::Success);
    LWSUI::UIHost host(window);
    auto control = std::make_unique<LWSUI::NumericEdit<int64_t>>(LWSUI::NumericSpec<int64_t>{0, 100, 1});
    auto& numeric = *control;
    auto theme = host.Style();
    theme.repeatDelayMs = 20;
    theme.repeatIntervalMs = 20;
    host.SetTheme(theme);
    host.SetRoot(std::move(control));
    WaitForConfiguration(window);
    host.Update();
    numeric.SetValue(3);
    Layout(numeric, 300, 34);
    auto* rocker = numeric.Children()[2].get();
    auto bounds = rocker->Bounds();
    Input down{Kind::Down};
    down.x = bounds.x + 5;
    down.y = bounds.y + 5;
    rocker->Dispatch(down);
    REQUIRE(numeric.Value() == 4);
    Input move{Kind::Move};
    move.x = -10;
    move.y = -10;
    rocker->Dispatch(move);
    PumpFor(platform, 100);
    REQUIRE(numeric.Value() == 4);
    move.x = bounds.x + 5;
    move.y = bounds.y + 5;
    rocker->Dispatch(move);
    PumpFor(platform, 45);
    REQUIRE(numeric.Value() > 4);
    rocker->Dispatch({Kind::Up});
    const auto released = numeric.Value();
    PumpFor(platform, 60);
    REQUIRE(numeric.Value() == released);
}
TEST_CASE("Showcase previews transparent colors and highlights combo hover", "[viewer]")
{
    TestPlatform platform;
    LWSUI::Canvas canvas;
    auto icon = internal::LoadSettingsIcon({});
    REQUIRE(icon);
    REQUIRE((*icon)->GetBuffer().width == 128);
    REQUIRE_FALSE(internal::LoadSettingsIcon("missing-settings-icon.png"));
    TempFile iconFile;
    const unsigned char png[]{137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,3,0,0,0,1,8,6,0,0,0,27,224,20,180,0,0,0,19,73,68,65,84,120,156,99,248,207,192,240,159,225,63,67,3,136,2,0,32,111,4,125,232,102,223,49,0,0,0,0,73,69,78,68,174,66,96,130};
    {
        std::ofstream output(iconFile.path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(png), sizeof(png));
    }
    auto customIcon = internal::LoadSettingsIcon(iconFile.path);
    REQUIRE(customIcon);
    auto image = (*customIcon)->GetBuffer();
    REQUIRE(image.width == 3);
    REQUIRE(image.height == 3);
    REQUIRE(image.pixels[3] == std::byte{0}); // Transparent square padding.
    REQUIRE(image.pixels[image.rowPitch+3] == std::byte{255});
    REQUIRE(image.pixels[image.rowPitch+7] == std::byte{128});
    REQUIRE(image.pixels[image.rowPitch+5] == std::byte{128}); // Premultiplied green.
    REQUIRE(image.pixels[image.rowPitch+11] == std::byte{0});
    {
        SettingsDocument doc;
        REQUIRE(SettingsStorage::LoadTemplate(doc, Template()));
        SettingsView iconView(doc);
        iconView.SetIcon(*customIcon);
        Layout(iconView, 640, 480);
        canvas.Begin(640,480);
        iconView.Render(canvas);
        auto before=canvas.End();
        std::vector<std::byte> copy(before.pixels.begin(), before.pixels.end());
        iconView.SetTheme(SettingsTheme{});
        Layout(iconView, 640,480);
        canvas.Begin(640,480);
        iconView.Render(canvas);
        auto after=canvas.End();
        REQUIRE(std::equal(copy.begin(), copy.end(), after.pixels.begin(), after.pixels.end()));
    }
    for (float scale : {1.f, 1.25f, 1.5f, 2.f})
    {
        LWSUI::Button button("Center");
        auto theme = LWSUI::Theme{};
        theme.line = LLUtils::Color{uint32_t{0x004488ff}};
        theme.foreground = LLUtils::Color{uint32_t{0xffffffff}};
        theme.font.size = 20;
        button.SetStyle(theme);
        button.Arrange({0,0,160,60});
        canvas.Begin(int(160*scale), int(60*scale), {scale, scale});
        button.Render(canvas);
        auto frame = canvas.End();
        unsigned left=frame.width, right=0, top=frame.height, bottom=0;
        for (unsigned y=0; y<frame.height; ++y)
            for (unsigned x=0; x<frame.width; ++x)
            {
                const auto* p=frame.pixels.data()+y*frame.rowPitch+x*4;
                if (std::to_integer<unsigned>(p[0])>190 && std::to_integer<unsigned>(p[1])>190 &&
                    std::to_integer<unsigned>(p[2])>190)
                { left=std::min(left,x); right=std::max(right,x); top=std::min(top,y); bottom=std::max(bottom,y); }
            }
        REQUIRE(left < right);
        REQUIRE(std::abs(float(left+right+1)-frame.width) <= 3);
        REQUIRE(std::abs(float(top+bottom+1)-frame.height) <= 3);
        LWSUI::CheckBox checkbox;
        theme.checkboxSize = 24;
        theme.accent = LLUtils::Color{uint32_t{0x004488ff}};
        theme.surface = LLUtils::Color{uint32_t{0xffffffff}};
        checkbox.SetStyle(theme); checkbox.SetValue(true); checkbox.Arrange({0,0,24,48});
        canvas.Begin(int(24*scale), int(48*scale), {scale, scale});
        canvas.Fill(0,0,24,48,LLUtils::Color{uint32_t{0x000000ff}});
        checkbox.Render(canvas);
        frame=canvas.End(); left=frame.width; right=0; top=frame.height; bottom=0;
        for (unsigned y=0; y<frame.height; ++y)
            for (unsigned x=0; x<frame.width; ++x)
            {
                const auto* p=frame.pixels.data()+y*frame.rowPitch+x*4;
                if (std::to_integer<unsigned>(p[0])>190 && std::to_integer<unsigned>(p[1])>190 &&
                    std::to_integer<unsigned>(p[2])>190)
                { left=std::min(left,x); right=std::max(right,x); top=std::min(top,y); bottom=std::max(bottom,y); }
            }
        REQUIRE(left < right);
        REQUIRE(std::abs(float(left+right+1)-frame.width) <= 3);
        REQUIRE(std::abs(float(top+bottom+1)-frame.height) <= 3);
    }
    for (float scale : {1.f, 1.25f, 1.5f, 2.f})
    for (float fontSize : {12.f, 28.f})
    for (bool selected : {false, true})
    {
        CAPTURE(scale, fontSize, selected);
        LWSUI::RadioButton radio("Center");
        auto style = LWSUI::Theme{};
        style.checkboxSize = 24;
        style.borderWidth = 2;
        style.font.size = fontSize;
        style.accent = style.muted = LLUtils::Color{uint32_t{0xffffffff}};
        style.foreground = LLUtils::Color{uint32_t{0xff0000ff}};
        radio.SetStyle(style);
        radio.SetValue(selected);
        radio.Arrange({8, 8, 184, 48});
        canvas.Begin(int(200 * scale), int(64 * scale), {scale, scale});
        canvas.Fill(0, 0, 200, 64, LLUtils::Color{uint32_t{0x000000ff}});
        radio.Render(canvas);
        const auto frame = canvas.End();
        const auto intensity = [&](float x, float y)
        {
            return std::to_integer<unsigned>(frame.pixels[size_t(y * scale) * frame.rowPitch + size_t(x * scale) * 4]);
        };
        REQUIRE(intensity(20, 32) == (selected ? 255 : 0));
        REQUIRE(intensity(28, 32) == 0); // Visible gap between the small dot and the ring.
        REQUIRE(intensity(31, 32) > 127);
        REQUIRE(intensity(30, 42) == 0); // The outer contour is circular, not a filled square.
        int left = int(frame.width), right = -1, top = int(frame.height), bottom = -1;
        int dotLeft = left, dotRight = -1, dotTop = top, dotBottom = -1;
        int labelTop = top, labelBottom = -1;
        for (int y = 0; y < int(frame.height); ++y)
            for (int x = 0; x < int(frame.width); ++x)
            {
                const auto* pixel = frame.pixels.data() + y * frame.rowPitch + x * 4;
                if (std::to_integer<unsigned>(pixel[0]) > 127 && std::to_integer<unsigned>(pixel[1]) > 127)
                {
                    left = std::min(left, x); right = std::max(right, x);
                    top = std::min(top, y); bottom = std::max(bottom, y);
                    const float dx = (x + .5f) / scale - 20, dy = (y + .5f) / scale - 32;
                    if (dx * dx + dy * dy < 64)
                    {
                        dotLeft = std::min(dotLeft, x); dotRight = std::max(dotRight, x);
                        dotTop = std::min(dotTop, y); dotBottom = std::max(dotBottom, y);
                    }
                }
                if (std::to_integer<unsigned>(pixel[2]) > 190 && std::to_integer<unsigned>(pixel[1]) < 64)
                {
                    labelTop = std::min(labelTop, y); labelBottom = std::max(labelBottom, y);
                }
            }
        REQUIRE(left < right);
        REQUIRE(std::abs(float(left + right + 1) - 40 * scale) <= 2);
        REQUIRE(std::abs(float(top + bottom + 1) - 64 * scale) <= 2);
        REQUIRE(std::abs((right - left) - (bottom - top)) <= 1);
        REQUIRE(labelTop < labelBottom);
        REQUIRE(std::abs(float(labelTop + labelBottom + 1) - 64 * scale) <= 8 * scale);
        if (selected)
        {
            REQUIRE(dotLeft < dotRight);
            REQUIRE(std::abs(float(dotLeft + dotRight + 1) - 40 * scale) <= 2);
            REQUIRE(std::abs(float(dotTop + dotBottom + 1) - 64 * scale) <= 2);
            REQUIRE(dotRight - dotLeft < (right - left) * .65f);
            REQUIRE(dotRight - dotLeft > (right - left) * .35f);
        }
        else REQUIRE(dotRight == -1);
    }
    LWSUI::TextBox selectable("Original");
    auto selectableStyle = LWSUI::Theme{};
    selectableStyle.surface = LLUtils::Color{uint32_t{0xff0000ff}};
    selectable.SetStyle(selectableStyle);
    selectable.SetReadOnly(true);
    selectable.Arrange({0, 0, 150, 40});
    for (bool borderless : {false, true})
    {
        selectable.SetBorderless(borderless);
        canvas.Begin(150, 40);
        canvas.Fill(0, 0, 150, 40, LLUtils::Color{uint32_t{0x0000ffff}});
        selectable.Render(canvas);
        const auto frame = canvas.End();
        const auto* corner = frame.pixels.data() + frame.rowPitch + 4;
        REQUIRE(corner[0] == std::byte(borderless ? 255 : 0));
        REQUIRE(corner[2] == std::byte(borderless ? 0 : 255));
    }
    LWSUI::ColorSwatch swatch;
    swatch.SetValue(LLUtils::Color{uint32_t{0x10203000}});
    Layout(swatch, 32, 32);
    canvas.Begin(32, 32);
    swatch.Render(canvas);
    auto pixels = canvas.End();
    REQUIRE(pixels.width == 32);
    REQUIRE(pixels.height == 32);
    const LWSUI::FontSpec highlightFont{18, "", 500, false};
    auto ranges = LWSUI::FindTextMatches("alpha alpha alpha", "alpha");
    std::vector<LWSUI::TextSpan> spans;
    for (auto range : ranges)
        spans.push_back({range, LLUtils::Color{uint32_t{0x000000ff}}, LLUtils::Color{uint32_t{0xffd166ff}}});
    canvas.Begin(130, 150);
    canvas.Fill(0, 0, 130, 150, LLUtils::Color{uint32_t{0x171c25ff}});
    canvas.Text("alpha alpha alpha", 0, 0, 90, 150, LLUtils::Color{uint32_t{0xffffffff}}, highlightFont, true, spans);
    auto highlighted = canvas.End();
    bool firstLine = false, wrappedLine = false;
    for (unsigned y = 0; y < highlighted.height; ++y)
        for (unsigned x = 0; x < highlighted.width; ++x)
        {
            auto* p = highlighted.pixels.data() + y * highlighted.rowPitch + x * 4;
            if (std::to_integer<unsigned>(p[0]) == 0x66 && std::to_integer<unsigned>(p[1]) == 0xd1 &&
                std::to_integer<unsigned>(p[2]) == 0xff)
                (y < 24 ? firstLine : wrappedLine) = true;
        }
    REQUIRE(firstLine);
    REQUIRE(wrappedLine);
    canvas.Begin(130, 150);
    canvas.Fill(0, 0, 130, 150, LLUtils::Color{uint32_t{0x171c25ff}});
    canvas.Text("alpha alpha alpha", 0, 0, 90, 150, LLUtils::Color{uint32_t{0xffffffff}}, highlightFont, true);
    auto plain = canvas.End();
    bool staleHighlight = false;
    for (unsigned y = 0; y < plain.height; ++y)
        for (unsigned x = 0; x < plain.width; ++x)
        {
            auto* p = plain.pixels.data() + y * plain.rowPitch + x * 4;
            staleHighlight |= std::to_integer<unsigned>(p[0]) == 0x66 && std::to_integer<unsigned>(p[1]) == 0xd1 &&
                              std::to_integer<unsigned>(p[2]) == 0xff;
        }
    REQUIRE_FALSE(staleHighlight);
    REQUIRE(canvas.Measure("wide", LWSUI::FontSpec{30}) > canvas.Measure("wide", LWSUI::FontSpec{12}));
    auto hsl = LWSUI::RgbToHsl(LLUtils::Color{uint32_t{0xff000080}});
    REQUIRE(hsl.hue == 0);
    REQUIRE(LWSUI::HslToRgb(hsl, 128) == LLUtils::Color{uint32_t{0xff000080}});
    LWS::Window window(platform);
    LWS::WindowConfig config;
    config.clientSize = {900, 720};
    config.visible = true;
    REQUIRE(window.Create(config) == LWS::Result::Success);
    WaitForConfiguration(window);
    SettingsDocument document;  // The document must outlive the host-owned SettingsView.
    LWSUI::UIHost host(window);
    std::vector<std::string> errors;
    auto errorConnection = host.OnError.Connect([&](const std::string& error) { errors.push_back(error); });
    auto root = std::make_unique<LWSUI::StackPanel>();
    auto& picker = root->Emplace<LWSUI::ColorPicker>();
    picker.SetValue(LLUtils::Color{uint32_t{0xff000080}});
    auto original = picker.Value();
    std::vector<Phase> phases;
    auto colorConnection = picker.OnEdit.Connect([&](auto, Phase phase) { phases.push_back(phase); });
    host.SetRoot(std::move(root));
    host.Update();
    KeyPress(*FindControl<LWSUI::ColorSwatch>(picker), Key::Enter);
    host.Update();
    REQUIRE(host.HasPopup());
    Input left{Kind::KeyDown};
    left.key = Key::Left;
    host.Route(left);
    REQUIRE(picker.Value().R() == 254);
    REQUIRE(picker.Value().G() == 1); // Left now adjusts saturation in the initial S/L field.
    REQUIRE(phases.back() == Phase::Preview);
    host.ClosePopup(Phase::Cancel);
    REQUIRE(picker.Value() == original);
    REQUIRE(phases.back() == Phase::Cancel);
    KeyPress(*FindControl<LWSUI::ColorSwatch>(picker), Key::Enter);
    host.Update();
    host.Route(left);
    Input accept{Kind::KeyDown};
    accept.key = Key::Enter;
    host.Route(accept);
    REQUIRE_FALSE(host.HasPopup());
    REQUIRE(picker.Value().R() == 254);
    REQUIRE(phases.back() == Phase::Commit);
    host.Update();
    auto key = [&](Key code, bool shift = false, bool control = false, bool alt = false)
    {
        Input input{Kind::KeyDown}; input.key = code; input.shift = shift; input.control = control; input.alt = alt;
        host.Route(input);
    };
    auto draft = [&](std::string value)
    {
        key(Key::A, false, true);
        Input input{Kind::Text}; input.text = std::move(value); host.Route(input);
    };
    picker.SetValue(LLUtils::Color{uint32_t{0xff000080}});
    KeyPress(*FindControl<LWSUI::ColorSwatch>(picker), Key::Enter);
    host.Update();
    for (int i = 0; i < 16; ++i) key(Key::Tab); // Color area -> vertical slider -> channels -> New hex.
    draft("#00Ff0080");
    REQUIRE(picker.Value() == LLUtils::Color{uint32_t{0x00ff0080}});
    key(Key::Enter);
    REQUIRE(host.HasPopup()); // Enter in a field commits that field, not the popup.
    draft("#bad");
    REQUIRE(picker.Value() == LLUtils::Color{uint32_t{0x00ff0080}});
    key(Key::Down, false, true); // Invalid hex blocks picker-wide adjustment.
    key(Key::Right, false, false, true);
    REQUIRE(picker.Value() == LLUtils::Color{uint32_t{0x00ff0080}});
    key(Key::Enter, false, true);
    REQUIRE(host.HasPopup());
    host.ClosePopup(Phase::Commit);
    REQUIRE(host.HasPopup());
    draft("#00ff00");
    REQUIRE(picker.Value().A() == 255);
    key(Key::Enter);
    for (int i = 0; i < 13; ++i) key(Key::Tab, true); // New hex -> R value.
    draft("256");
    REQUIRE(picker.Value().R() == 0);
    key(Key::Tab); // Invalid field blocks focus loss.
    draft("1.5");
    REQUIRE(picker.Value().R() == 0);
    draft("64"); key(Key::Enter);
    REQUIRE(picker.Value().R() == 64);
    for (int i = 0; i < 8; ++i) key(Key::Tab); // R value -> H value.
    auto expectedHsl = LWSUI::RgbToHsl(picker.Value());
    expectedHsl.hue = 120.5;
    draft("120.5");
    REQUIRE(picker.Value() == LWSUI::HslToRgb(expectedHsl, 255));
    key(Key::Left);
    Input insert{Kind::Text}; insert.text = "0"; host.Route(insert); // Preserve caret: 120.5 -> 120.05.
    expectedHsl.hue = 120.05;
    REQUIRE(picker.Value() == LWSUI::HslToRgb(expectedHsl, 255));
    key(Key::Enter);
    key(Key::Tab, true); key(Key::Tab, true); // H value -> A value.
    const auto beforeAlpha = picker.Value();
    draft("64"); key(Key::Enter);
    REQUIRE(picker.Value().A() == 64);
    REQUIRE(picker.Value().R() == beforeAlpha.R());
    REQUIRE(picker.Value().G() == beforeAlpha.G());
    REQUIRE(picker.Value().B() == beforeAlpha.B());
    draft("invalid");
    key(Key::Escape);
    REQUIRE_FALSE(host.HasPopup());
    REQUIRE(picker.Value() == LLUtils::Color{uint32_t{0xff000080}});
    REQUIRE(phases.back() == Phase::Cancel);
    picker.SetValue(LLUtils::Color{uint32_t{0xff000080}});
    KeyPress(*FindControl<LWSUI::ColorSwatch>(picker), Key::Enter); host.Update();
    key(Key::Down, true, true); // Vertical slider from the color area, without moving focus.
    REQUIRE(picker.Value() == LWSUI::HslToRgb({10, 1, .5}, 128));
    key(Key::Left); // Unmodified arrows still reach the color area.
    REQUIRE(picker.Value() == LWSUI::HslToRgb({10, .99, .5}, 128));
    key(Key::Tab); // Focus the vertical slider.
    key(Key::Down, true, false, true); // Color area's vertical axis from the slider.
    REQUIRE(picker.Value() == LWSUI::HslToRgb({10, .99, .4}, 128));
    key(Key::Up); // Focus stayed on the slider.
    REQUIRE(picker.Value() == LWSUI::HslToRgb({9, .99, .4}, 128));
    key(Key::Escape);
    REQUIRE(picker.Value() == LLUtils::Color{uint32_t{0xff000080}});
    KeyPress(*FindControl<LWSUI::ColorSwatch>(picker), Key::Enter); host.Update();
    for (int i = 0; i < 16; ++i) key(Key::Tab);
    draft("#33669980");
    const auto baseHsl = LWSUI::RgbToHsl(picker.Value());
    key(Key::Down, false, true);
    auto adjusted = baseHsl; adjusted.hue += 1;
    REQUIRE(picker.Value() == LWSUI::HslToRgb(adjusted, 128));
    draft("#abcdef80"); // The hex editor retains focus after a global shortcut.
    REQUIRE(picker.Value() == LLUtils::Color{uint32_t{0xabcdef80}});
    key(Key::Enter, false, true);
    REQUIRE_FALSE(host.HasPopup());
    REQUIRE(picker.Value() == LLUtils::Color{uint32_t{0xabcdef80}});
    picker.SetValue(LLUtils::Color{uint32_t{0x80808080}});
    KeyPress(*FindControl<LWSUI::ColorSwatch>(picker), Key::Enter); host.Update();
    key(Key::Tab); key(Key::Down, true); // Hue changes by 10 degrees even while gray.
    REQUIRE(picker.Value() == LLUtils::Color{uint32_t{0x80808080}});
    key(Key::Tab, true); key(Key::Right);
    REQUIRE(picker.Value() == LWSUI::HslToRgb({10, .01, 128. / 255}, 128));
    key(Key::Enter);
    REQUIRE_FALSE(host.HasPopup());
    REQUIRE(phases.back() == Phase::Commit);
    host.Update();
    // A callback requests structural mutation on a surviving parent. Dispatch
    // completes with the old tree; the next update safely removes the children.
    auto removal = host.Root()->OnHover.Connect(
        [&]
        {
            host.Post(host.Root()->Handle(),
                      [](LWSUI::Control& parent) { static_cast<LWSUI::Container&>(parent).Clear(); });
        });
    Input move{Kind::Move};
    move.x = picker.Bounds().x + 2;
    move.y = picker.Bounds().y + 2;
    colorConnection.Disconnect(); // The deferred clear destroys the event publisher.
    host.Route(move);
    REQUIRE(static_cast<LWSUI::Container*>(host.Root())->Children().size() == 1);
    host.Update();
    REQUIRE(static_cast<LWSUI::Container*>(host.Root())->Children().empty());
    REQUIRE(errors.empty());
    auto themed = ThemeStorage::Decode(Json::parse(
        R"({"version":1,"base":{"typography":{"font":{"size":28,"family":"Arial"},"headingFont":{"size":30}}},"sections":{"description":{"colors":{"background":"#163832"}}},"layout":{"descriptionHeight":230}})"));
    REQUIRE(themed);
    REQUIRE(SettingsStorage::LoadTemplate(document, Template()));
    auto view = std::make_unique<SettingsView>(document, "Large type", *themed);
    auto* value = FindControl<LWSUI::NumericEdit<int64_t>>(*view);
    host.SetPopupStyle(themed->popups);
    removal.Disconnect(); // SetRoot destroys the old root and its OnHover event.
    host.SetRoot(std::move(view));
    host.Update();
    REQUIRE(value->Bounds().height >= 28);
    REQUIRE(FindControl<LWSUI::TextBox>(*value)->Font().family == "Arial");
    REQUIRE(errors.empty());

    auto& settingsView = static_cast<SettingsView&>(*host.Root());
    auto locationTheme = *themed;
    locationTheme.description.smallFont.size = 64; // The location must still be smaller than the body.
    locationTheme.description.smallFont.weight = 700;
    settingsView.SetTheme(locationTheme);
    auto schema = Template();
    std::string groupName = "Nested location";
    std::string bodyText;
    for (int i = 0; i < 24; ++i)
    {
        groupName += " / Long group name";
        bodyText += "A longer description that can scroll independently. ";
    }
    schema["children"][0]["label"] = groupName;
    schema["children"][0]["children"][1]["description"] = bodyText;
    REQUIRE(SettingsStorage::LoadTemplate(document, schema));
    auto* described = FindControl<LWSUI::NumericEdit<int64_t>>(settingsView);
    described->Parent()->OnHover.Raise();
    host.Update();
    const auto pathText = "Location: " + groupName;
    LWSUI::Label* location = nullptr;
    LWSUI::Label* body = nullptr;
    for (auto* label : Controls<LWSUI::Label>(settingsView))
    {
        if (label->Text() == pathText) location = label;
        if (label->Text() == bodyText) body = label;
    }
    REQUIRE(location);
    REQUIRE(body); // The body contains description text only, with no duplicate breadcrumb.
    auto* locationScroll = dynamic_cast<LWSUI::ScrollView*>(location->Parent());
    auto* bodyScroll = dynamic_cast<LWSUI::ScrollView*>(body->Parent());
    REQUIRE(locationScroll);
    REQUIRE(bodyScroll);
    REQUIRE(location->wrap);
    REQUIRE(location->Font().size <= body->Font().size * .85f);
    REQUIRE(location->Font().weight == 400);
    REQUIRE(location->Foreground() == locationTheme.description.muted);
    REQUIRE(location->Text() == pathText);
    bool headingFound = false;
    for (auto* label : Controls<LWSUI::Label>(settingsView))
        if (label->fontRole == LWSUI::Control::FontRole::Heading && label->Text() == "Count") headingFound = true;
    REQUIRE(headingFound); // The setting name remains in the heading, not the location.
    settingsView.SetFilter("Long group");
    host.Update();
    REQUIRE(location->HighlightSpans(location->Text()).size() == 24);
    for (float width : {820.f, 480.f})
    {
        Layout(settingsView, width, 680);
        const float bottom = locationScroll->Bounds().y + locationScroll->Bounds().height;
        REQUIRE(locationScroll->Bounds().height > 0);
        REQUIRE(locationScroll->Bounds().y >= bodyScroll->Bounds().y + bodyScroll->Bounds().height);
        REQUIRE(locationScroll->Bounds().height <= bodyScroll->Bounds().height + .01f);
        REQUIRE(location->DesiredSize().height > locationScroll->Bounds().height);
        bodyScroll->SetOffset(10000);
        locationScroll->SetOffset(10000);
        Layout(settingsView, width, 680);
        REQUIRE(bodyScroll->Offset() > 0);
        REQUIRE(locationScroll->Offset() > 0);
        REQUIRE(locationScroll->Bounds().y + locationScroll->Bounds().height == Catch::Approx(bottom));
        settingsView.Report(std::unexpected(Error{"test", "Validation message"}));
        Layout(settingsView, width, 680);
        LWSUI::Label* status = nullptr;
        for (auto* label : Controls<LWSUI::Label>(settingsView))
            if (label->Text() == "test: Validation message") status = label;
        REQUIRE(status);
        REQUIRE(status->Bounds().y >= bodyScroll->Bounds().y + bodyScroll->Bounds().height);
        REQUIRE(status->Bounds().y + status->Bounds().height <= locationScroll->Bounds().y + .01f);
        REQUIRE(locationScroll->Bounds().y + locationScroll->Bounds().height == Catch::Approx(bottom));
        for (auto* button : Controls<LWSUI::Button>(settingsView))
            if (button->Text() == "Save") REQUIRE(bottom < button->Bounds().y);
        settingsView.Report({});
    }
    REQUIRE(SettingsStorage::LoadTemplate(document, Template()));
    REQUIRE(location->Text().empty());
    REQUIRE_FALSE(locationScroll->Visible());
    FindControl<LWSUI::TreeView::Branch>(settingsView)->OnHover.Raise();
    REQUIRE(location->Text().empty()); // Root groups have no parent location.
    REQUIRE_FALSE(locationScroll->Visible());
    REQUIRE(errors.empty());
}
TEST_CASE("F4 opens and cancels the file picker while its text editor is focused", "[viewer][dialog]")
{
#ifdef _WIN32
    TestPlatform platform;
    LWS::Window window(platform);
    LWS::WindowConfig config;
    config.visible = true;
    config.clientSize = {600, 300};
    REQUIRE(window.Create(config) == LWS::Result::Success);
    WaitForConfiguration(window);
    LWSUI::UIHost host(window);
    auto picker = std::make_unique<LWSUI::FilePicker>();
    auto* file = picker.get();
    host.SetRoot(std::move(picker));
    host.Update();
    auto* text = FindControl<LWSUI::TextBox>(*file);
    REQUIRE(host.Focus(text));
    int changes = 0;
    auto connection = file->OnEdit.Connect([&](const auto&, auto) { ++changes; });
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
    } driver(*LWS::Win32::GetHwnd(window));
    Draft(*text, "missing-file-for-picker-test.invalid");
    REQUIRE_FALSE(text->Validation().empty());
    Input key{Kind::KeyDown};
    key.key = Key::F4;
    host.Route(key);
    driver.Stop();
    REQUIRE(driver.appeared);
    REQUIRE(changes == 1); // Cancellation of the invalid draft; no missing path was published.
    REQUIRE(text->Validation().empty());
    REQUIRE(file->Value().empty());
#else
    SUCCEED("Native portal interaction requires a desktop portal; covered by the demo workflow.");
#endif
}

namespace
{
    template <class T>
    constexpr bool FixedChildRoles =
        !requires(T& value) { value.Clear(); } &&
        !requires(T& value, LWSUI::Control& child) { value.Remove(child); } &&
        !requires(T& value) { value.Add(std::make_unique<LWSUI::Control>()); } &&
        !requires(T& value) { value.template Emplace<LWSUI::Control>(); };

    // Models a wrapped child's width-dependent height without a native font backend.
    class WrappingControl final : public LWSUI::Control
    {
      protected:
        LWSUI::Size OnMeasure(LWSUI::Size available) override
        {
            return {available.width, 10 * std::ceil(100.f / std::max(1.f, available.width))};
        }
    };
}
TEST_CASE("Fixed composites restrict structural mutation and layouts honor allocated widths", "[regression][layout]")
{
    STATIC_REQUIRE(FixedChildRoles<LWSUI::ScrollView>);
    STATIC_REQUIRE(FixedChildRoles<LWSUI::TreeView::Row>);
    STATIC_REQUIRE(FixedChildRoles<LWSUI::TreeView::Branch>);
    STATIC_REQUIRE(FixedChildRoles<LWSUI::RadioGroup>);
    STATIC_REQUIRE(FixedChildRoles<LWSUI::NumericEdit<int64_t>>);
    STATIC_REQUIRE_FALSE((std::is_base_of_v<LWSUI::Grid, LWSUI::NumericEdit<int64_t>>));
    STATIC_REQUIRE(FixedChildRoles<LWSUI::ColorPicker>);
    STATIC_REQUIRE(FixedChildRoles<LWSUI::FilePicker>);
    STATIC_REQUIRE(FixedChildRoles<SettingsView>);
    STATIC_REQUIRE_FALSE(FixedChildRoles<LWSUI::StackPanel>);
    STATIC_REQUIRE_FALSE(FixedChildRoles<LWSUI::Grid>);

    LWSUI::Grid grid;
    grid.spacing = 0;
    grid.columns = {-.25f, -.25f};
    auto& left = grid.Emplace<WrappingControl>();
    auto& right = grid.Emplace<WrappingControl>();
    auto measured = grid.Measure({100, 100});
    grid.Arrange({0, 0, 100, measured.height});
    REQUIRE(left.Bounds().width == Catch::Approx(50));
    REQUIRE(right.Bounds().width == Catch::Approx(50));
    REQUIRE(right.Bounds().x == Catch::Approx(50));
    grid.columns = {-1, -1};
    REQUIRE(grid.Measure({100, 100}).height == measured.height);
    grid.Arrange({0, 0, 100, measured.height});
    REQUIRE(right.Bounds().width == Catch::Approx(50));

    LWSUI::StackPanel row;
    row.orientation = LWSUI::Orientation::Horizontal;
    row.spacing = 10;
    auto& first = row.Emplace<WrappingControl>();
    auto& second = row.Emplace<WrappingControl>();
    auto& hidden = row.Emplace<WrappingControl>();
    hidden.SetVisible(false);
    const auto desired = row.Measure({110, 100});
    row.Arrange({0, 0, 110, desired.height});
    REQUIRE(desired.height == Catch::Approx(20));
    REQUIRE(first.DesiredSize().width == first.Bounds().width);
    REQUIRE(second.DesiredSize().width == second.Bounds().width);
    REQUIRE(second.Bounds().x == Catch::Approx(60));
    REQUIRE(second.Bounds().height == Catch::Approx(20));
}
TEST_CASE("External assignments notify when replacing a valid preview", "[regression][document]")
{
    for (auto policy : {ApplyPolicy::Immediate, ApplyPolicy::OnApply})
        for (int operation = 0; operation < 4; ++operation)
        {
            CAPTURE(policy, operation);
            SettingsDocument document({policy});
            REQUIRE(SettingsStorage::LoadTemplate(document, Template()));
            SettingsView view(document);
            auto* number = FindControl<LWSUI::NumericEdit<int64_t>>(view);
            Draft(*FindControl<LWSUI::TextBox>(*number), "7");
            REQUIRE(document.WorkingValue("count") == Value{int64_t{7}});
            std::vector<Change> changes;
            auto connection = document.OnChange.Connect([&](const Change& change) { changes.push_back(change); });
            auto result = operation == 0 ? document.SetValue("count", int64_t{3})
                        : operation == 1 ? document.Reset("count")
                        : operation == 2 ? document.Reset("g") : document.ResetAll();
            REQUIRE(result);
            REQUIRE(changes.size() == 1);
            REQUIRE(changes[0].oldValue == Value{int64_t{7}});
            REQUIRE(changes[0].newValue == Value{int64_t{3}});
            REQUIRE(changes[0].domain == (policy == ApplyPolicy::Immediate ? ValueDomain::Applied : ValueDomain::Working));
            REQUIRE_FALSE(document.IsEditing("count"));
            REQUIRE(number->Value() == 3);
            REQUIRE(document.WorkingValue("count") == Value{int64_t{3}});
            REQUIRE(document.AppliedValue("count") == Value{int64_t{3}});
        }
}
TEST_CASE("Theme resets publish one complete preview per batch", "[regression][theme]")
{
    LWS::PlatformContext platform;
    TempFile file;
    ThemeEditor editor(platform, file.path);
    REQUIRE(editor.Load());
    auto& document = internal::ThemeEditorTestAccess::Editor(editor).Document();
    const LLUtils::Color red{uint32_t{0xee2244ff}}, green{uint32_t{0x118833ff}};
    for (bool all : {false, true})
    {
        REQUIRE(document.SetValue("base/colors/accent", red));
        REQUIRE(document.SetValue("base/colors/background", green));
        int previews = 0;
        auto connection = editor.OnPreview.Connect([&](const SettingsTheme& theme)
        {
            ++previews;
            REQUIRE(theme.base.accent == SettingsTheme{}.base.accent);
            REQUIRE(theme.base.background == SettingsTheme{}.base.background);
            REQUIRE(editor.Session().Current().overrides.empty());
        });
        REQUIRE((all ? document.ResetAll() : document.Reset("group/base/colors")));
        REQUIRE(previews == 1);
        REQUIRE(editor.Overrides()->empty());
        REQUIRE((all ? document.ResetAll() : document.Reset("group/base/colors")));
        REQUIRE(previews == 1); // No change notification for an already-default theme.
    }
    // Exceptions must not leave the private batching guard active.
    auto throwing = document.OnEditFinished.Connect([](const auto&, auto, auto origin)
    { if (origin == ChangeOrigin::Reset) throw std::runtime_error("reset interrupted"); });
    REQUIRE_THROWS_AS(document.ResetAll(), std::runtime_error);
    throwing.Disconnect();
    REQUIRE(document.SetValue("base/colors/accent", red));
    REQUIRE(editor.Session().Theme().base.accent == red);
}
TEST_CASE("Checker rendering progresses for tiny or invalid standalone styles", "[regression][theme]")
{
    auto profile = Json::parse(R"({"version":1,"sections":{"controls":{"metrics":{"checkerSize":1e-8}}}})");
    REQUIRE_FALSE(ThemeStorage::Decode(profile));
    profile["sections"]["controls"]["metrics"]["checkerSize"] = 1;
    REQUIRE(ThemeStorage::Decode(profile));
    LWSUI::Canvas canvas;
    LWSUI::ColorSwatch swatch;
    swatch.SetValue(LLUtils::Color{uint32_t{0}});
    swatch.Arrange({100, 100, 8, 8});
    for (float cell : {1e-8f, 0.f, -1.f, std::numeric_limits<float>::quiet_NaN()})
    {
        LWSUI::Theme style;
        style.checkerSize = cell;
        style.checkerLight = LLUtils::Color{uint32_t{0xffffffff}};
        style.checkerDark = LLUtils::Color{uint32_t{0x000000ff}};
        swatch.SetStyle(style);
        canvas.Begin(112, 112);
        swatch.Render(canvas);
        const auto pixels = canvas.End();
        const auto channel = [&](int x, int y)
        { return std::to_integer<unsigned>(pixels.pixels[size_t(y) * pixels.rowPitch + size_t(x) * 4]); };
        REQUIRE(channel(100, 100) == 255);
        REQUIRE(channel(101, 100) == 0);
        REQUIRE(channel(100, 101) == 0);
        REQUIRE(channel(101, 101) == 255);
    }
}

TEST_CASE("Fixed numeric layout preserves text and rocker geometry", "[regression][layout]")
{
    LWSUI::NumericEdit<float> number({0.f, 1.f, .25f});
    LWSUI::Theme style;
    style.rockerWidth = 40;
    style.glyphFont.size = 25;
    number.SetStyle(style);
    const auto measured = number.Measure({180, 200});
    number.Arrange({0, 0, 180, measured.height});
    const auto* text = FindControl<LWSUI::TextBox>(number);
    const auto buttons = Controls<LWSUI::Button>(number);
    REQUIRE(text->Bounds().width == Catch::Approx(140));
    REQUIRE(text->DesiredSize().width == text->Bounds().width);
    REQUIRE(buttons.size() == 2);
    for (auto* button : buttons)
    {
        REQUIRE(button->Bounds().x == Catch::Approx(140));
        REQUIRE(button->Bounds().width == Catch::Approx(40));
        REQUIRE(button->Bounds().height >= style.glyphFont.size * 1.4f);
    }
    number.SetValue(.75f);
    int commits = 0;
    auto changed = number.OnEdit.Connect([&](float, Phase phase) { commits += phase == Phase::Commit; });
    for (int i = 0; i < 2; ++i) { KeyPress(number, Key::Up); REQUIRE(number.Finish(Phase::Commit)); }
    REQUIRE(number.Value() == 1.f);
    REQUIRE(commits == 2); // Reaching a bound must still complete an edit.
}
TEST_CASE("Container clearing retains teardown order and handle lifetime", "[regression][layout]")
{
    struct Child : LWSUI::Control
    {
        Child(int id, std::vector<int>& finished) : id(id), finished(finished) {}
        bool Finish(Phase) override { finished.push_back(id); return true; }
        int id;
        std::vector<int>& finished;
    };
    std::vector<int> finished;
    LWSUI::Container parent;
    auto& first = parent.Emplace<Child>(1, finished);
    auto& middle = parent.Emplace<Child>(2, finished);
    auto& last = parent.Emplace<Child>(3, finished);
    const auto firstHandle = first.Handle(), lastHandle = last.Handle();
    auto removed = parent.Remove(middle);
    REQUIRE(removed.get() == &middle);
    REQUIRE(finished == std::vector<int>{2});
    parent.Clear();
    REQUIRE(parent.Children().empty());
    REQUIRE(finished == std::vector<int>{2, 3, 1});
    REQUIRE_FALSE(firstHandle);
    REQUIRE_FALSE(lastHandle);
    parent.Clear();
    REQUIRE(finished.size() == 3);
}

TEST_CASE("Clipped controls skip painting without changing pixels or tree state", "[regression][rendering]")
{
    struct Paint : LWSUI::Control
    {
        explicit Paint(LLUtils::Color color) : color(color) {}
        LLUtils::Color color;
        int paints = 0;
        void Draw(LWSUI::Canvas& canvas)
        {
            const auto b = Bounds();
            canvas.Fill(b.x, b.y, b.width, b.height, color);
        }
        void OnRender(LWSUI::Canvas& canvas) override { ++paints; Draw(canvas); }
    };
    LWSUI::Container root;
    root.Arrange({10.25f, 10.25f, 30.25f, 20.25f});
    auto& content = root.Emplace<LWSUI::Container>();
    content.Arrange({-20.25f, -20.25f, 100, 100});
    auto& visible = content.Emplace<Paint>(LLUtils::Color{uint32_t{0xff000080}});
    visible.Arrange({12.5f, 12.5f, 8.25f, 8.25f});
    auto& partial = content.Emplace<Paint>(LLUtils::Color{uint32_t{0x00ff0080}});
    partial.Arrange({30.125f, 25.125f, 20, 20});
    auto& outside = content.Emplace<Paint>(LLUtils::Color{uint32_t{0x0000ffff}});
    outside.Arrange({12, 45, 8, 8});
    auto& edge = content.Emplace<Paint>(LLUtils::Color{uint32_t{0xff00ffff}});
    edge.Arrange({40.55f, 15, .2f, 5}); // Logical separation may still share a rasterized clip-edge pixel.
    const auto handle = outside.Handle();
    const auto outsideBounds = outside.Bounds();
    // The reference invokes all drawing callbacks and relies solely on native clipping.
    auto reference = [&](auto&& self, LWSUI::Control& control, LWSUI::Canvas& canvas) -> void
    {
        const auto b = control.Bounds();
        LWSUI::Canvas::ClipScope clip(canvas, b.x, b.y, b.width, b.height);
        if (auto* paint = dynamic_cast<Paint*>(&control)) paint->Draw(canvas);
        if (auto* group = dynamic_cast<LWSUI::Container*>(&control))
            for (auto& child : group->Children()) self(self, *child, canvas);
    };
    LWSUI::Canvas actual, expected;
    for (double scale : {1., 1.25, 1.5, 2.})
    {
        CAPTURE(scale);
        const auto size = int(80 * scale);
        actual.Begin(size, size, {scale, scale});
        expected.Begin(size, size, {scale, scale});
        actual.Fill(0, 0, 80, 80, LLUtils::Color{uint32_t{0x222222ff}});
        expected.Fill(0, 0, 80, 80, LLUtils::Color{uint32_t{0x222222ff}});
        visible.paints = partial.paints = outside.paints = edge.paints = 0;
        root.Render(actual);
        reference(reference, root, expected);
        const auto a = actual.End(), e = expected.End();
        REQUIRE(std::ranges::equal(a.pixels, e.pixels));
        REQUIRE(visible.paints == 1);
        REQUIRE(partial.paints == 1);
        REQUIRE(outside.paints == 0);
        REQUIRE(handle.Get() == &outside);
        REQUIRE(outside.Bounds().y == outsideBounds.y);
        REQUIRE(root.HitTest(13, 46) == nullptr);
        // Rendering a child independently must not inherit clips from its parent pointers.
        actual.Begin(size, size, {scale, scale});
        outside.Render(actual);
        (void) actual.End();
        REQUIRE(outside.paints == 1);
    }
    // Clip scopes must restore culling state after an exception, including an empty intersection.
    actual.Begin(80, 80);
    try
    {
        LWSUI::Canvas::ClipScope clip(actual, 0, 0, 2, 2);
        root.Render(actual);
        throw std::runtime_error("render interrupted");
    }
    catch (const std::runtime_error&) {}
    const int previous = outside.paints;
    outside.Render(actual);
    (void) actual.End();
    REQUIRE(outside.paints == previous + 1);
}

TEST_CASE("Label measurement reuse follows content style and host changes", "[regression][measurement]")
{
    auto root = std::make_unique<LWSUI::Container>();
    auto* parent = root.get();
    auto& label = root->Emplace<LWSUI::Label>("A long label with several words for wrapping");
    const auto detachedHeight = label.Measure({140, 500}).height;
    REQUIRE(detachedHeight == Catch::Approx(std::max(label.Style().rowHeight,
        label.Font().size * 1.4f + 2 * label.Style().labelPadding)));
    TestPlatform platform;
    LWS::Window window(platform);
    LWS::WindowConfig config;
    config.clientSize = {600, 500}; config.visible = true;
    REQUIRE(window.Create(config) == LWS::Result::Success);
    WaitForConfiguration(window);
    LWSUI::UIHost host(window);
    host.SetRoot(std::move(root)); host.Update();
    auto verify = [&](LWSUI::Label& control, float width)
    {
        const float padding = control.Style().labelPadding;
        const auto native = control.Host()->MeasureText(control.Text(), std::max(1.f, width - 2 * padding),
                                                        control.wrap, control.Font());
        const float wanted = std::max(control.Style().rowHeight, native.height + 2 * padding);
        for (int repeat = 0; repeat < 3; ++repeat)
        {
            const auto measured = control.Measure({width, 500});
            REQUIRE(measured.width == width);
            REQUIRE(measured.height == Catch::Approx(wanted));
        }
    };
    verify(label, 140);
    verify(label, 300); // Unwrapped width changes must still update DesiredSize.width.
    label.wrap = true;
    verify(label, 140); verify(label, 55); verify(label, 300);
    for (const auto& text : {std::string(""), std::string("short"), std::string("שלום · café · 日本語 with wrapping words")})
    { label.SetText(text); verify(label, 90); }
    label.SetText(label.Text()); verify(label, 90);
    LWSUI::Theme inherited;
    inherited.font.size = 23;
    parent->SetStyle(inherited); verify(label, 90);
    inherited.font.family = "Arial"; inherited.font.weight = 700; inherited.font.italic = true;
    parent->SetStyle(inherited); verify(label, 90);
    inherited.headingFont.size = 31;
    parent->SetStyle(inherited); label.fontRole = LWSUI::Control::FontRole::Heading; verify(label, 90);
    inherited.labelPadding = 13; inherited.rowHeight = 100;
    parent->SetStyle(inherited); verify(label, 90);
    inherited.foreground = LLUtils::Color{uint32_t{0xff0088ff}};
    parent->SetStyle(inherited); verify(label, 90);
    label.wrap = false; verify(label, 20);
    auto detached = parent->Remove(label);
    const auto fallback = label.Measure({90, 500});
    REQUIRE(fallback.height == Catch::Approx(std::max(label.Style().rowHeight,
        label.Font().size * 1.4f + 2 * label.Style().labelPadding)));
    LWS::Window second(platform);
    config.clientSize = {420, 300};
    REQUIRE(second.Create(config) == LWS::Result::Success);
    WaitForConfiguration(second);
    LWSUI::Theme other;
    other.headingFont.size = 36;
    LWSUI::UIHost otherHost(second, other);
    otherHost.SetRoot(std::move(detached)); otherHost.Update();
    verify(label, 90);
    label.wrap = true; verify(label, 75);
}

TEST_CASE("Settings windows use only minimize and close without a native caption", "[viewer][window-controls]")
{
    TestPlatform platform;
    SettingsEditor editor(platform);
    REQUIRE(SettingsStorage::LoadTemplate(editor.Document(), Template()));
    REQUIRE(editor.Show());
    auto* host = editor.View()->Host();
    WaitForConfiguration(host->Window());
    host->Update();
    auto* bar = host->MainMenu();
    REQUIRE(bar != nullptr);
    REQUIRE(bar->Items().empty());
    REQUIRE(bar->WindowControls().minimize);
    REQUIRE_FALSE(bar->WindowControls().maximize);
    REQUIRE(bar->WindowControls().draggable);
    REQUIRE(bool(bar->WindowControls().requestClose));
    REQUIRE(host->Window().GetWindowStyles() == LWS::WindowStyle::ResizableBorder);
    REQUIRE(editor.View()->Bounds().y >= bar->Bounds().y + bar->Bounds().height);
#ifdef LWS_HAS_WIN32_BACKEND
    const auto hwnd = *LWS::Win32::GetHwnd(host->Window());
    REQUIRE((GetWindowLongW(hwnd, GWL_STYLE) & WS_CAPTION) == 0);
    REQUIRE((GetWindowLongW(hwnd, GWL_STYLE) & WS_THICKFRAME) != 0);
#endif
    unsigned hidden = 0;
    auto connection = editor.OnHidden.Connect([&] { ++hidden; });
    const auto close = [&]
    {
        REQUIRE(host->Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::F10}));
        REQUIRE(host->Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Left}));
        REQUIRE(host->Route({.kind = LWSUI::InputKind::KeyDown, .key = LWS::KeyCode::Enter}));
        PumpFor(platform.value, 30);
    };
    auto* text = FindControl<LWSUI::TextBox>(*editor.View());
    REQUIRE(text != nullptr);
    REQUIRE(host->Focus(text));
    Draft(*text, "draft");
    text->SetValidation("Invalid draft");
    close();
    REQUIRE(editor.IsVisible());
    REQUIRE(hidden == 0);
    text->SetValidation("");
    KeyPress(*text, LWS::KeyCode::Escape);
    close();
    REQUIRE_FALSE(editor.IsVisible());
    REQUIRE(hidden == 1);
    REQUIRE(editor.Show());
    REQUIRE(editor.View()->Host()->MainMenu() == bar);
    REQUIRE(editor.SetTheme(SettingsTheme(LWSUI::ThemePreset::Light)));
    host->Update();
    REQUIRE(host->MainMenu() == bar);
    REQUIRE(host->MainMenu()->Items().empty());
    close();
    REQUIRE(hidden == 2);
}

TEST_CASE("Settings headers move into the bar without dangling controls", "[viewer][header]")
{
    TestPlatform platform;
    EditorConfig config; config.size = {480, 680};
    SettingsEditor editor(platform, config);
    REQUIRE(SettingsStorage::LoadTemplate(editor.Document(), Template()));
    REQUIRE(editor.Show());
    auto* view = editor.View();
    auto* host = view->Host();
    WaitForConfiguration(host->Window()); host->Update();
    auto* bar = host->MainMenu();
    REQUIRE(bar->Icon() != nullptr);
    REQUIRE(bar->Content() != nullptr);
    const auto under = [](LWSUI::Control& control, LWSUI::Control* ancestor)
    {
        for (auto* p = control.Parent(); p; p = p->Parent())
            if (p == ancestor)
                return true;
        return false;
    };
    REQUIRE(under(view->HeaderActions(), bar->Content()));
    REQUIRE(view->HeaderActions().Host() == host);
    auto* search = FindControl<LWSUI::TextBox>(*view);
    REQUIRE(search != nullptr);
    REQUIRE(search->Bounds().y == Catch::Approx(view->Bounds().y));
    SettingsView standalone(editor.Document()); Layout(standalone);
    REQUIRE(FindControl<LWSUI::TextBox>(standalone)->Bounds().y > 0);
    for (auto* label : Controls<LWSUI::Label>(standalone))
        if (label->Text() == "Settings")
        {
            const auto b = label->Bounds();
            REQUIRE(standalone.HitTest(b.x + 1, b.y + 1) == label);
        }
    const auto passiveLabels = Controls<LWSUI::Label>(*bar->Content());
    REQUIRE(passiveLabels.size() == 2);
    for (auto* label : passiveLabels)
    {
        const auto b = label->Bounds();
        REQUIRE(b.width > 0);
        REQUIRE(bar->Content()->HitTest(b.x + 1, b.y + 1) == nullptr);
    }
#ifdef LWS_HAS_WIN32_BACKEND
    unsigned moves = 0;
    const SUBCLASSPROC observer = [](HWND h, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) -> LRESULT
    {
        if (message == WM_NCLBUTTONDOWN && w == HTCAPTION) { ++*reinterpret_cast<unsigned*>(data); return 0; }
        return DefSubclassProc(h, message, w, l);
    };
    const auto hwnd = *LWS::Win32::GetHwnd(host->Window());
    REQUIRE(SetWindowSubclass(hwnd, observer, 9, reinterpret_cast<DWORD_PTR>(&moves)));
    POINT cursor{}; const bool pointer = GetCursorPos(&cursor) != FALSE;
    for (auto* label : passiveLabels)
    {
        const auto b = label->Bounds();
        const auto before = moves;
        REQUIRE(host->Route({.kind = LWSUI::InputKind::Down, .x = b.x + 1, .y = b.y + 1}));
        REQUIRE(host->Route({.kind = LWSUI::InputKind::Up, .x = b.x + 1, .y = b.y + 1}));
        if (pointer) REQUIRE(moves == before + 1);
    }
    REQUIRE(RemoveWindowSubclass(hwnd, observer, 9));
#endif
    auto& action = view->HeaderActions().Emplace<LWSUI::Button>(std::string(35, 'W'));
    unsigned clicks = 0;
    auto click = action.OnClick.Connect([&] { ++clicks; });
    host->Update(); PumpFor(platform.value, 30); host->Update();
    REQUIRE(host->Window().GetClientSizeLimits().minimum.x >= int(bar->MinimumWidth()));
    REQUIRE(host->Window().GetClientAreaMetrics().logical.x >= int(bar->MinimumWidth()));
    ClickButton(action); REQUIRE(clicks == 1);
    const auto handle = action.Handle();
    REQUIRE(editor.SetTheme(SettingsTheme(LWSUI::ThemePreset::Light)));
    host->Update();
    REQUIRE(handle);
    REQUIRE(editor.Hide()); REQUIRE(editor.Show());
    REQUIRE(handle);
    REQUIRE(editor.Document().SetValue("enabled", true));
    host->Update();
    bool unsaved = false;
    for (auto* label : Controls<LWSUI::Label>(*bar->Content())) unsaved |= label->Text() == "Unsaved";
    REQUIRE(unsaved);
    const auto icon = bar->Icon();
    view->SetIcon(nullptr); host->Update(); REQUIRE_FALSE(bar->Icon());
    view->SetIcon(icon); host->Update(); REQUIRE(bar->Icon() == icon);
    bar->SetContent(nullptr);
    REQUIRE_FALSE(handle);
    REQUIRE_THROWS_AS(view->HeaderActions(), std::logic_error);
    REQUIRE(editor.Document().SetValue("enabled", false));
    REQUIRE(editor.SetTheme(SettingsTheme(LWSUI::ThemePreset::Warm))); host->Update();
    REQUIRE(editor.Hide());
    REQUIRE(host->Window().Destroy() == LWS::Result::Success);
    REQUIRE(editor.Show());
    WaitForConfiguration(editor.View()->Host()->Window()); editor.View()->Host()->Update();
    REQUIRE(editor.View()->Host()->MainMenu()->Content() != nullptr);
    REQUIRE(under(editor.View()->HeaderActions(), editor.View()->Host()->MainMenu()->Content()));
}

TEST_CASE("Settings live resize policy survives window recreation", "[viewer][resize]")
{
    TestPlatform platform;
    EditorConfig config;
    REQUIRE(config.redrawOnResize);
    SECTION("enabled default") {}
    SECTION("configuration opt-out")
    {
        config.redrawOnResize = false;
    }
    SettingsEditor editor(platform, config);
    REQUIRE(SettingsStorage::LoadTemplate(editor.Document(), Template()));
    REQUIRE(editor.Show());
    WaitForConfiguration(editor.View()->Host()->Window());
    REQUIRE(editor.RedrawOnResize() == config.redrawOnResize);
    REQUIRE(editor.View()->Host()->RedrawOnResize() == config.redrawOnResize);
    editor.SetRedrawOnResize(!config.redrawOnResize);
    REQUIRE(editor.Hide());
    REQUIRE(editor.Show());
    REQUIRE(editor.View()->Host()->RedrawOnResize() == !config.redrawOnResize);
    REQUIRE(editor.Hide());
    REQUIRE(editor.View()->Host()->Window().Destroy() == LWS::Result::Success);
    REQUIRE(editor.Show());
    WaitForConfiguration(editor.View()->Host()->Window());
    REQUIRE(editor.View()->Host()->RedrawOnResize() == !config.redrawOnResize);
    TempFile themeFile;
    ThemeEditor theme(platform, themeFile.path);
    REQUIRE(theme.RedrawOnResize());
    theme.SetRedrawOnResize(false);
    REQUIRE(theme.Load());
    REQUIRE(theme.Show());
    REQUIRE_FALSE(theme.Editor().View()->Host()->RedrawOnResize());
    theme.SetRedrawOnResize(true);
    REQUIRE(theme.Editor().View()->Host()->RedrawOnResize());
}

TEST_CASE("Schema versions reject coercion without changing document state", "[storage][version]")
{
    SettingsDocument document;
    REQUIRE(SettingsStorage::LoadTemplate(document, Template()));
    REQUIRE(document.SetValue("name", std::string{"retained"}));
    const auto before = document.WorkingValues();
    for (const Json version : {Json(1.5), Json(1.0), Json("1"), Json(true), Json(2), Json(nullptr)})
    {
        auto schema = Template();
        schema["version"] = version;
        REQUIRE_FALSE(SettingsStorage::LoadTemplate(document, schema));
        REQUIRE(document.WorkingValues() == before);
        auto values = SettingsStorage::EncodeValues(before);
        values["version"] = version;
        REQUIRE_FALSE(SettingsStorage::LoadValues(document, values));
        REQUIRE(document.WorkingValues() == before);
    }
}
TEST_CASE("Menu theme metrics preserve typed values and validate their ranges", "[theme][menu]")
{
    SettingsTheme theme;
    theme.header.menuBarHeight = 64;
    theme.header.menuBarPadding = 13;
    theme.header.menuArrowWidth = 19;
    theme.header.menuHoverDelayMs = 0;
    auto decoded = ThemeStorage::Decode(ThemeStorage::Encode(theme));
    REQUIRE(decoded);
    REQUIRE(decoded->header.menuBarHeight == 64);
    REQUIRE(decoded->header.menuBarPadding == 13);
    REQUIRE(decoded->header.menuArrowWidth == 19);
    REQUIRE(decoded->header.menuHoverDelayMs == 0);
    for (auto field : {"menuBarHeight", "menuBarPadding", "menuArrowWidth"})
    {
        auto json = ThemeStorage::Encode(theme);
        json["sections"]["header"]["metrics"][field] = -1;
        REQUIRE_FALSE(ThemeStorage::Decode(json));
    }
}
TEST_CASE("Narrow staged footers wrap without overlapping actions", "[viewer][layout]")
{
    SettingsDocument document({ApplyPolicy::OnApply, SavePolicy::Manual});
    REQUIRE(SettingsStorage::LoadTemplate(document, Template()));
    for (float font : {16.f, 28.f})
    {
        SettingsTheme theme;
        theme.footer.font.size = font;
        SettingsView view(document, "Settings", theme, true);
        Layout(view, 480, 680);
        std::vector<LWSUI::Button*> actions;
        for (auto caption : {"Reset all", "Cancel", "Apply", "Revert", "Save"})
        {
            auto* button = FindButton(view, caption);
            REQUIRE(button);
            actions.push_back(button);
            const auto b = button->Bounds();
            REQUIRE(b.width > 0);
            REQUIRE(b.height > 0);
            REQUIRE(b.x >= 0);
            REQUIRE(b.x + b.width <= 480);
            REQUIRE(view.HitTest(b.x + b.width / 2, b.y + b.height / 2) == button);
        }
        for (size_t i = 0; i < actions.size(); ++i)
            for (size_t j = i + 1; j < actions.size(); ++j)
            {
                const auto a = actions[i]->Bounds(), b = actions[j]->Bounds();
                REQUIRE(
                    (a.x + a.width <= b.x || b.x + b.width <= a.x || a.y + a.height <= b.y || b.y + b.height <= a.y));
            }
    }
}
TEST_CASE("Retained views reschedule pending filtering after attachment", "[viewer][filter][lifetime]")
{
    TestPlatform platform;
    LWS::Window first(platform), second(platform);
    REQUIRE(first.Create({.clientSize = {640, 600}, .visible = true}) == LWS::Result::Success);
    REQUIRE(second.Create({.clientSize = {640, 600}, .visible = true}) == LWS::Result::Success);
    WaitForConfiguration(first);
    WaitForConfiguration(second);
    SettingsDocument document; // The document must outlive both hosts' retained views.
    LWSUI::UIHost firstHost(first), secondHost(second);
    REQUIRE(SettingsStorage::LoadTemplate(document, Template()));
    auto root = std::make_unique<LWSUI::Container>();
    auto* parent = root.get();
    auto& view = root->Emplace<SettingsView>(document);
    firstHost.SetRoot(std::move(root));
    firstHost.Update();
    view.SetFilter("count");
    auto retained = parent->Remove(view);
    secondHost.SetRoot(std::move(retained));
    firstHost.Update();
    secondHost.Update();
    auto* numeric = FindControl<LWSUI::NumericEdit<int64_t>>(view);
    REQUIRE(numeric);
    REQUIRE(numeric->Parent()->Visible());
    view.SetFilter("no such setting");
    secondHost.Update();
    REQUIRE_FALSE(numeric->Parent()->Visible());
    view.SetFilter("");
    secondHost.Update();
    REQUIRE(numeric->Parent()->Visible());
}
TEST_CASE("Save and Apply shortcuts validate relocated header drafts", "[viewer][header][validation]")
{
    TestPlatform platform;
    EditorConfig config;
    config.document.applyPolicy = ApplyPolicy::OnApply;
    int saves = 0;
    config.document.writeValues = [&](const std::filesystem::path&, const Values&) -> Status
    {
        ++saves;
        return {};
    };
    config.document.valuesFile = "unused.json";
    SettingsEditor editor(platform, config);
    REQUIRE(SettingsStorage::LoadTemplate(editor.Document(), Template()));
    REQUIRE(editor.Show());
    auto& view = *editor.View();
    auto& host = *view.Host();
    WaitForConfiguration(host.Window());
    auto& text = view.HeaderActions().Emplace<LWSUI::TextBox>("committed");
    host.Update();
    REQUIRE(host.Focus(&text));
    Draft(text, "invalid");
    text.SetValidation("Rejected");
    REQUIRE(editor.Document().SetValue("name", std::string{"pending"}));
    REQUIRE(host.Route({.kind = Kind::KeyDown, .key = Key::S, .control = true}));
    REQUIRE(host.Route({.kind = Kind::KeyDown, .key = Key::Enter, .control = true}));
    REQUIRE(saves == 0);
    REQUIRE(editor.Document().AppliedValue("name") == Value{std::string{"hello"}});
    REQUIRE(text.Text() == "invalid");
    text.SetValidation("");
    REQUIRE(host.Route({.kind = Kind::KeyDown, .key = Key::Enter, .control = true}));
    REQUIRE(editor.Document().AppliedValue("name") == Value{std::string{"pending"}});
}
