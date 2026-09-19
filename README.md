# LWSSettings

A C++26, JSON-driven settings window for Windows and Linux/Wayland. One `SettingsPage` owns its
definitions, values, edit state, and viewer. The model can be used and tested without creating a window.

The viewer uses a collapsible tree, a search field, typed setting editors, reset actions, and a description
pane. Controls share layout and interaction code; text and graphics use Direct2D/DirectWrite on Windows and
Cairo/Pango on Linux, presented through LWS.

## Build

Requires CMake 3.30+, a C++26 compiler, LWS with the accompanying settings-support changes, LLUtils
(already supplied by LWS), and nlohmann/json 3.12. The optional tests use Catch2 3.7.1.

Windows, from a Visual Studio developer shell with clang-cl available:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_C_COMPILER=clang-cl -DLWSSETTINGS_LWS_SOURCE_DIR=D:/dev/LWS -DLWSSETTINGS_JSON_SOURCE_DIR=D:/dev/oiv/External/json
cmake --build build
ctest --test-dir build --output-on-failure
cd build
.\LWSSettingsDemo.exe
```

Linux requires Wayland client/cursor development files, wayland-protocols, wayland-scanner,
Pango/Cairo, Fontconfig, libxkbcommon, and libdbus. A running Wayland compositor is required for GUI tests.
Native file browsing requires a session D-Bus and an `xdg-desktop-portal` backend supplied by the desktop.
Fonts are resolved through the platform font system; install fonts covering the scripts your application displays.

```sh
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang \
  -DLWSSETTINGS_LWS_SOURCE_DIR=/path/to/LWS \
  -DLWSSETTINGS_JSON_SOURCE_DIR=/path/to/json
cmake --build build
ctest --test-dir build --output-on-failure
cd build
./LWSSettingsDemo
```

The demo accepts `--staged`, `--autosave`, and `--smoke` (a short display smoke test).
It reads `settings.json` in its working directory and saves to `user-settings.json`.
Existing user settings take precedence; on first launch it loads `showcase-values.json`, which includes
non-default examples. CMake copies both showcase files into the build directory. The template demonstrates
all eight editors across three nested group levels, with settings at each level. Console notifications are
flushed JSON records with ID, old/new values, value type, kind, origin, and working/effective domain.

Embed with `add_subdirectory` and link `LWSSettings::LWSSettings`. Existing `LWSLib`,
`LLUtils::LLUtils`, and `nlohmann_json::nlohmann_json` targets are reused.
If no JSON source directory is supplied, CMake uses `find_package(nlohmann_json)`.
Disable the demo/tests using `LWSSETTINGS_BUILD_DEMO=OFF` and `LWSSETTINGS_BUILD_TESTS=OFF`.
Enable the dependency's regression tests separately with `LWS_BUILD_TESTS=ON`.

## OIViewer profile

[examples/oiv](examples/oiv/README.md) contains a template matching the actual OIViewer settings file and a
companion snapshot of its current values. Run the demo from the generated `oiv` directory beside its executable
to preview this profile. The original OIViewer settings file is unchanged.

## Using the library

```cpp
#include <LWSSettings/SettingsPage.hpp>
#include <LWS/Platform.hpp>

LWS::Platform::Session platform;
LWSSettings::SettingsConfig config;
config.title = "Application settings";
config.valuesFile = "settings-values.json";
config.workflow = LWSSettings::Workflow::ImmediateManualSave;

LWSSettings::SettingsPage settings(config);
auto loaded = settings.LoadTemplateFile("settings-template.json");
if (!loaded)
    throw std::runtime_error(loaded.error().message);

auto subscription = settings.OnChange([](const LWSSettings::Change& change) {
    if (change.domain == LWSSettings::ValueDomain::Effective) {
        // Apply change.newValue to the application.
        // Live previews and Confirmed changes are distinguishable through change.kind.
    }
});

auto shown = settings.Show();
if (!shown)
    throw std::runtime_error(shown.error().message);
// Continue the application's existing LWS event loop.
```

Check every returned `Status` (`std::expected<void, Error>`). Errors include a location and message.
`GetValue(id)` returns the current working value; `GetEffectiveValue(id)` returns the applied value.
Both return `std::optional<Value>`, with no value for unknown IDs or groups.

`Value` is a variant of `bool`, `int64_t`, `double`, UTF-8 `std::string`, and `LLUtils::Color`.
For example:

```cpp
auto updated = settings.SetValue("refresh", int64_t{120});
auto refresh = settings.GetEffectiveValue("refresh");
if (refresh)
    useRefreshRate(std::get<int64_t>(*refresh));
```

All operations and subscriptions belong to the UI thread. Initialize LWS before `Show()` and keep its
session alive until the page is destroyed. The optional owner window must also outlive the page.
Destroy subscriptions before their page; callbacks must not destroy the page. Callbacks may update values
or subscriptions, but must not replace the template or bulk-load values during notification.

## Construction configuration

| Field | Meaning |
| --- | --- |
| `workflow` | Immediate manual saving (default), immediate autosave, or staged Apply/Cancel |
| `valuesFile` | Explicit save destination; required to save or autosave |
| `title` | UTF-8 window title |
| `owner` | Optional parent/owner LWS window |
| `size` | Preferred window dimensions before appearance scaling |
| `appearance.scale` | Additional user scale, 0.5–4.0; default 1.0 |
| `appearance.fontSize` | Body font size at 96 DPI, 8–48 logical pixels; default 16 |
| `appearance.nonDefaultForeground` | Label/value color for settings differing from template defaults; default amber |
| `appearance.background/foreground/accent` | Colors for the shared theme |

The final body font size is `fontSize × appearance.scale × windowDpiY / 96`. Layout, caret measurements,
mouse hit testing, and popup geometry share the same scaling metrics. DPI changes follow the current window
without committing edits or changing values. Windows uses per-monitor DPI; Wayland uses the entered outputs'
integer buffer scale and retains logical pointer coordinates.

Non-default highlighting compares **working values against template defaults**, independently of saved or
applied snapshots. It remains after saving and clears when a value returns to its default.

### Immediate manual saving

Valid edits change both working and effective values immediately. Save finishes the active edit and writes
values to disk. Closing the window hides it and finishes a valid edit; it does not save automatically.
Show reopens the same page with its current state.

### Immediate autosave

Values still change immediately. A confirmed edit, programmatic change, or reset writes the values file.
Typing and color previews do not write on every intermediate change. Save can retry a failed autosave.
A failed autosave retains effective values, keeps the page dirty, and records `LastError()`; accepting a
value and persisting it are separate operations, so a successful `SetValue()` does not guarantee a disk write.

### Staged Apply/Cancel

Working values change as the user edits. Effective values remain at the last applied snapshot.
Apply publishes the changed effective values; it does not write the file.
Save applies and then persists. Cancel restores the last applied values.
Closing hides the page and preserves unapplied working values; it does not implicitly Apply or Cancel.

Snapshots are owned by the single model; there are no separate bindable data providers.
`HasPendingChanges()` compares working with applied values.
`IsDirty()` compares valid working values with the last loaded/saved snapshot.
An incomplete invalid editor draft is held outside the typed value store.

## Template format

See [examples/settings.json](examples/settings.json) for all controls.

```json
{
  "version": 1,
  "children": [
    {
      "type": "group",
      "id": "appearance",
      "label": "Appearance",
      "children": [
        {
          "type": "checkbox",
          "id": "fullscreen",
          "label": "Fullscreen",
          "description": "Use the whole display.",
          "default": false
        }
      ]
    }
  ]
}
```

Every node has a globally unique, case-sensitive, nonempty `id`, a nonempty `label`, and a `type`.
The optional `description` is plain text. Groups have a `children` array and may be nested up to 64 levels.
Visual indentation is capped to retain readable labels; the scrollable description pane shows the full
breadcrumb. The three-level showcase is an example, not a hierarchy-depth restriction.
Settings require a typed `default`. IDs are independent of tree placement and are preserved when groups move.

| Type | Value and additional fields |
| --- | --- |
| `text` | UTF-8 string, edited in a single-line text box |
| `integer` | Signed 64-bit integer; optional integer `min`, `max`, and positive `step` (default 1) |
| `number` | Finite double; optional `min`, `max`, and positive finite `step` (default 0.1) |
| `checkbox` | Boolean |
| `combo`, `radio` | String choice; nonempty `options` array of `{"label": "...", "value": "..."}` |
| `color` | `#RRGGBB` or `#RRGGBBAA`; saved as `#RRGGBBAA` |
| `file` | UTF-8 path; an editable field with an Open File action |
| `group` | No value; contains `children` |

Radio options together form one setting. Choice values must be unique and the default must be a member.
Templates and bulk value loads are validated before replacing current state. Unknown control types,
duplicate IDs, invalid defaults, out-of-range values, and invalid UTF-8 are rejected.

Saved values have a separate format:

```json
{
  "version": 1,
  "values": {
    "fullscreen": true,
    "accent": "#66b7ffff"
  }
}
```

Missing known values use template defaults; unknown saved IDs are ignored.
Template/value loading is silent and establishes a clean snapshot. To notify the application of subsequent
changes, use `SetValue()` or reset operations rather than bulk loading.
`ExportValues()` exports valid working values without committing an active editor draft.
Writes use a temporary file beside the destination and replace the destination after the write succeeds.

## Events and interaction

Changes include `id`, `oldValue`, `newValue`, `kind`, `origin`, and `domain`.

- Immediate editing emits `Live` for valid intermediate values and one `Confirmed` for a changed edit on
  Enter, focus loss, or picker acceptance. Confirmation compares against the edit's starting value.
- Checkbox, choice, file-dialog acceptance, programmatic updates, and resets confirm immediately.
- Escape/color cancellation restores the starting value with a `Live` event and `Cancel` origin.
- Staged editing emits only working-domain `Live` notifications; Apply emits effective-domain
  `Confirmed` notifications. Applications can ignore the working domain or use it for previews.
- Invalid intermediate numeric/color text produces no typed value update. It blocks confirmation/saving
  until corrected or cancelled. No-op value assignments produce no events.
- A valid programmatic assignment supersedes an active draft for that setting. Invalid assignments preserve it.

Hover overrides the selected row's description. Leaving the row restores the selected description; an empty
description clears the pane. Long descriptions scroll independently. Search matches labels, IDs, and
descriptions, retains ancestors, and temporarily reveals matching descendants. Clearing search restores
the previous expansion state. ASCII matching is case-insensitive; other Unicode characters match literally.

Numeric text boxes have an up/down rocker. A press steps immediately; holding repeats after 400 ms, then
every 80 ms. Mouse holds accelerate exponentially: the step multiplier doubles every half-second after the
initial delay, capped at 1,000,000× the base step. Releasing resets acceleration; individual clicks and keyboard
steps retain the configured base step. The showcase's Large-range number (0–1,000,000, step 1) demonstrates this. Up/Down performs the same operation while the editor is focused. Values clamp at their bounds;
an unchanged value emits no notification. Each hold is one edit: live updates while stepping, one confirmation
on release, and one autosave at confirmation. Escape restores its starting value. Invalid text must first be
corrected or cancelled; programmatic replacement, reset, template/value loading, and Cancel stop repetition.

The tree scrollbar has a 16-unit-wide track and a draggable thumb. Dragging preserves the grab position,
clamps to the content bounds, and uses native mouse capture on Windows (Wayland supplies implicit capture).
Clicking the track pages up/down. Release, capture loss, focus loss, and closing end the drag.

An open combo popup owns hover over its items before the underlying tree is hit-tested. Hover highlights
an option without changing its value or emitting events; the description stays with the combo setting.
Hover is recomputed after scrolling, resizing, and DPI changes.

The color picker shows Original and Current swatches over checkerboards, with RGB/Alpha and HSL sliders
side by side. Editing RGB recomputes HSL; editing HSL updates RGB. Hue is in degrees (0–360), saturation and
lightness are percentages, and alpha remains independent. Hue is retained for gray colors so increasing
saturation restores the selected hue. Settings and events continue to use the same RGBA value and hexadecimal
serialization. Original is the value when the picker opened; Cancel restores it.

Keyboard controls: arrows/Home/End navigate rows, Left/Right collapse/expand groups or change choices,
Tab moves to the next row/editor, Enter/Space activates a control, Escape cancels an edit, Ctrl+F focuses search,
Ctrl+S saves, Ctrl+Enter applies staged edits, Ctrl+R resets the selected setting, and Ctrl+Shift+R resets all.
F4 opens a selected file setting's picker. In the color picker, Up/Down/Tab choose a channel,
Left/Right change it (Shift changes by 10), Enter accepts, and Escape cancels.
Text supports selection, mouse dragging, cut/copy/paste, select-all, Home/End, and UTF-8-safe cursor movement.

## Platform additions and limits

The accompanying LWS changes provide per-window DPI/framebuffer metrics, DPI-change events,
committed UTF-8 text events, pointer-leave events, Windows bitmap
presentation, cancellable Windows close events, text clipboard helpers, and Wayland portal file dialogs.
`LWS::SetClipboardText` and `RequestClipboardText` use UTF-8 on both platforms; reads may complete
immediately or asynchronously. Destroying an owner window cancels its pending reads.
`Platform::Feature::TextClipboard` advertises text-transfer support separately from raw clipboard formats.

Windows supports UTF-16 character input including surrogate pairs. Wayland uses the compositor's XKB keymap,
modifier state, dead-key composition, and repeat settings. Full IME/preedit composition is deferred.
Cursor movement follows Unicode scalar boundaries; full grapheme-cluster and visual bidirectional editing
are not provided in this first version. Native accessibility-provider integration is also not included.

Linux portal dialogs require a desktop portal at runtime. This implementation does not export a Wayland
parent handle to the portal; the settings page suspends interaction while the picker is open.
Portal unavailability leaves the path field editable and displays an error. The initial native Linux picker
does not apply the optional initial-directory, default-extension, or filter-index hints.

macOS/X11 viewers, embedded hosting, multiple viewers, and legacy NetSettings template compatibility are deferred.

## Validation

The test suite covers typed/default values, atomic load validation, persistence, all three workflows,
live/confirmed sequences, cancellation, invalid drafts, Unicode editing, filtering, callbacks, and rendering.
The viewer test writes optional PPM screenshots when `LWSSETTINGS_TEST_ARTIFACTS` names an output directory.
LWS regression tests also cover Windows bitmap presentation, handled close events, and UTF-8 event delivery.

Rendering tests require a real Windows session or Wayland compositor. Native Windows dialog tests open and cancel a real picker, accept Unicode filenames, and verify F4 from
the focused file editor. DPI tests cover Windows notifications and Wayland scaled/legacy buffers. Actual
Linux portal interaction requires a desktop with a running portal and must be checked there separately.
