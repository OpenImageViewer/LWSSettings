# LWSSettings v1

LWSSettings is a C++26 settings document and editor for LWS on Windows and Wayland.
The editor is assembled from the independent `LWSUI::LWSUI` control library in LWS.
There are no compatibility adapters for the reference `SettingsPage` API.

## Build

Use one stable output per platform/configuration. From a Visual Studio developer
shell on Windows (with clang-cl on PATH), or from WSL/Linux, choose the matching preset:

```sh
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

Use `wayland-release` for Linux, or `wayland-asan` for Debug with ASAN/UBSAN.
Each preset rebuilds in `build/<preset-name>`; do not copy executables into alternate
“latest” folders. `LWSSettingsDemo` is at the build root and `LWSUIDemo` is under
`dependencies/LWS`. The presets use the adjacent LWS checkout and its existing JSON
and Catch2 source caches; override these cache paths when using another checkout.

Alternatively provide existing `LWSUI::LWSUI`, `LWSLib`, and
`nlohmann_json::nlohmann_json` targets. Windows uses DirectWrite/Direct2D/WIC;
Wayland uses Pango/Cairo and LWS bitmap presentation. LWS handles native file
dialogs, clipboard, timers and DPI notifications.

Run `LWSSettingsDemo` from its output directory. `--staged` selects OnApply;
`--autosave` selects OnCommit. They may be combined. `--smoke` exits after a short
run. `--snapshot` also renders the current view to `snapshot.ppm` for visual QA.
Run from the generated `oiv` directory to use the OIViewer profile.

When Python is available at configuration time, CTest also registers
LWSSettingsDemoStartup, which launches isolated settings-only profiles and checks
optional overrides, failure handling, and the built-in theme editor.

`settings.json` is the only required data file. Its template supplies initial values.
If `user-settings.json` exists, the demo loads it over those defaults; otherwise it
starts from the template alone. Save writes all applied setting values, including
unchanged defaults, to `user-settings.json`. Starting the demo does not create it.
An invalid saved-values file is reported as an error rather than silently ignored.

The optional `theme.json` profile customizes the built-in theme. If it is absent,
the demo uses built-in defaults; `--theme <path>` selects a different optional profile.
Invalid themes and explicitly missing paths produce a warning and use defaults.
`--export-theme <path>` exports the resolved theme without opening a window. The
legacy `showcase-values.json` and `theme-default.json` files are not read or shipped.
See [Themes](docs/themes.md) for colors, fonts, search highlights, and section styles.

Press **F6** or launch with `--edit-theme` to open the live theme editor. Its editing
definitions come from the built-in catalog, so no `theme-settings.json` is needed.
Its own style stays fixed while changes preview in the showcase. Editing a property
creates an override; Use inherited removes it. Save writes the theme profile,
Revert restores its baseline, and closing without saving reverts the preview.
Reopening reuses the editor. `--export-theme-template <path>` remains available for
explicit template exports. `--edit-theme --snapshot` also writes `theme-editor.ppm`.

Settings and theme-editor windows merge their header into the captionless menu bar:
cog icon, compact title, theme selector, Saved/Unsaved, Minimize, and Close. The
menu-item list stays empty, and search/filter controls start directly below this
single row. The icon, title, Saved/Unsaved text, and unoccupied gaps move the window
through LWS; menu items, interactive controls (including disabled ones), and window
buttons do not. Close retains validation/hide/revert behavior. Minimize is disabled
on Wayland. The title shortens first at narrow widths; required controls determine
the minimum window width. No additional guaranteed drag strip is imposed yet.

Standalone `SettingsView` retains its embedded header. `HeaderActions()` continues
to return the real action panel even when the header is hosted in a menu bar; it
throws `std::logic_error` if that content has been removed externally. `SetIcon()`
updates the embedded icon or menu-bar icon as appropriate. Presentation state is
shared separately from controls, and references to relocated controls are checked.

The compact double-down and double-up chevrons expand or collapse every nonempty
group, including nested groups. Pointer hover shows “Expand all” or “Collapse all”.
These mouse actions never take focus, show no focus underline, and are skipped by Tab;
individual groups retain their keyboard navigation. Both icons are gray and disabled
while search or Changed/Default filtering is active, and until any queued filter update
completes. Each action also disables when no group needs it; the toolbar wraps on narrow windows.

Search includes current values and choice labels as well as metadata. Every literal
occurrence is highlighted in visible settings text, including wrapped descriptions.
Matching is ASCII case-insensitive and otherwise exact UTF-8. Highlights update with
text edits; result membership remains stable until the query changes or values reload.
The search input and action captions are excluded, and text selection takes priority.

`LWSUIDemo`, under the LWS build output, constructs controls directly in C++ and
links only `LWSUI::LWSUI`. Its nested tree logs control edits and group expansion
to the console. Pass `--smoke` for a short run or `--snapshot` for `controls.ppm`.

## Ownership and responsibilities

| Component | Responsibility |
| --- | --- |
| `SettingsDocument` | Typed definitions, validated values, working/applied/saved snapshots, edit transactions and notifications |
| `SettingsStorage` | JSON decoding/encoding and atomic replacement of a values file |
| `SettingsView` | Search, nested groups, breadcrumbs, descriptions, default highlighting, editor bindings and actions |
| `SettingsEditor` | Own one document, window, host and view |
| `LWSUI::Control` | Bounds, visibility, enabled state, styling, measure/arrange/render/input wrappers |
| `LWSUI::Container` | Own children with `unique_ptr` |
| `LWSUI::UIHost` | Attach a control tree to a window, route input, focus/capture/popups and coalesce redraws |

`SettingsDocument.hpp` includes neither LWS window APIs nor JSON. Numeric and
choice definitions have typed specifications. Groups have only metadata and
children. JSON belongs in `SettingsStorage.hpp`; typed updates never round-trip
through JSON. Template replacement validates the entire candidate first.

Controls never know setting IDs, defaults, JSON or saving policies. A binding
translates typed editor events into `SettingsDocument::SetValue`. Model refreshes
use silent control setters, so they cannot feed edits back into the model.
Painting neither rebuilds the view nor updates settings.

All objects are confined to the UI thread. The caller's initialized PlatformContext
must outlive its editors. Editors are independent top-level windows; SettingsView can be embedded. `UIHost` owns its root and popup; a separately supplied window must outlive
its host. Controls and native pointer input use logical coordinates. The host renders using
the paired framebuffer size and per-axis scale from ClientAreaMetrics. Layout stores the bounds used by both paint
and hit testing. Focus, capture, popup owners and asynchronous clipboard callbacks
use checked non-owning handles. Detachment invalidates outstanding handles.

LWSUI event connections are move-only, disconnect on destruction, and can
outlive the publisher. Listener callables remain stable during dispatch. A
listener connected during a notification starts with the next notification;
a disconnected listener is skipped. Document callbacks may update values but
must not destroy the document or replace its definitions/values while it is
notifying. Snapshot changes finish before notifications are drained.

## Minimal editor

```cpp
#include <LWSSettings/SettingsEditor.hpp>
#include <LWSSettings/SettingsStorage.hpp>
#include <LWS/Platform.hpp>

// Use the application's initialized platform context. On Windows, call
// LWS::Win32::BootstrapProcess before initializing it.
LWS::PlatformContext& platform = applicationPlatform;
LWSSettings::EditorConfig config;
config.document.valuesFile = "user-settings.json";
LWSSettings::SettingsEditor editor(platform, config);
auto& document = editor.Document();
auto loaded = LWSSettings::SettingsStorage::LoadTemplateFile(document, "settings.json");
if (!loaded) /* report loaded.error().location and .message */;
auto changes = document.OnChange.Connect([](const LWSSettings::Change& change) {
    // React to Applied domain events for runtime behavior.
});
auto errors = document.OnError.Connect([](const LWSSettings::Error& error) {
    // Implicit save or UI failures. Accepted values remain available.
});
auto shown = editor.Show();
if (!shown) /* report shown.error() */;
```

Keep subscriptions in the scope where notifications are wanted. Data operations
work without initializing a backend or displaying a window. `Show` requires a usable
context. The application pumps that context while the editor is visible.

## Values, edits and persistence

- `WorkingValue(id)` is the value shown in the editor. `AppliedValue(id)` is the
  value adopted by the application. Both return `optional<Value>`.
- `SetValue(id, value)` validates a programmatic commit. Invalid updates return
  `unexpected<Error>` and leave the document intact. No-op commits do not notify,
  but a successful external assignment silently dismisses an existing draft.
- User controls send Preview, Commit and Cancel phases. The document remembers
  the first preview's value and uses it as the old value of the edit-wide commit.
  A rocker hold or color panel is one edit. Invalid text remains a control draft;
  it never enters the document. Escape restores the edit start.
- `Reset(id)` resets a setting or group; `ResetAll()` restores all defaults.
  Highlighting compares with template defaults, independently of saved state.
- `IsDirty()` compares working with saved values. `HasPendingChanges()` compares
  working with applied values. Neither is a validation or persistence error.

| Apply policy | Save policy | Behavior |
| --- | --- | --- |
| Immediate (default) | Manual (default) | Valid edits immediately update applied values; explicitly save when wanted |
| Immediate | OnCommit | Previews update applied values; persistence follows edit confirmation |
| OnApply | Manual | Working edits wait for Apply; save the applied snapshot explicitly |
| OnApply | OnCommit | Apply adopts the complete working snapshot, then persists it |

`RevertSaved()` restores the loaded or last successfully saved snapshot in either
mode. An optional `DocumentConfig::writeValues` callback supplies another file
format while retaining the normal saved-state and error handling.

`Cancel()` restores applied values in OnApply mode; in immediate mode it restores
active edit starts. `Save()` persists **applied** values and does not implicitly
apply staged work. The view's Save action confirms drafts, applies, then saves.
Document clients must explicitly finish their own edits before applying/saving.

Synchronous failures return `Status`. Automatic saving reports failures through
`OnError` and `Persistence()`; accepted working/applied values are retained and
the saved snapshot advances only after a successful write. UI validation,
operation errors and informational status are separate. File writes use a
flushed temporary file in the destination directory and atomic replacement;
JSON text uses CRLF. The parent directory must already exist.

`Change` contains `id`, `oldValue`, `newValue`, `phase`, `origin`, and `domain`.
The demo prints JSON records with these fields and `valueType` (see
`examples/EventLogging.hpp`). Preview/commit/cancel timing is independent of
painting. OnApply edit commits remain in the Working domain; Apply publishes
Applied-domain commits for changed values.

## Version 1 template schema

There is one template envelope: `{"version": 1, "children": [...]}`. Every node
has a unique nonempty `id`, a nonempty `label`, and optional `description`.
IDs are stable independent of nesting, so regrouping does not change saved paths.
Groups use `type: "group"` and a `children` array. A setting requires `default`.
Nesting is limited to 64 groups; search includes labels, descriptions, IDs and
ancestor breadcrumbs, and temporarily reveals collapsed groups.

| Type | Default/value | Additional fields |
| --- | --- | --- |
| `text` | UTF-8 string | â€” |
| `file` | UTF-8 path string | Editable text, browse action and F4 native picker |
| `checkbox` | Boolean | â€” |
| `integer` | Signed 64-bit integer | Optional `min`, `max`, positive integer `step` (default 1) |
| `number` | Finite number | Optional `min`, `max`, positive finite `step` (default 0.1) |
| `combo`, `radio` | Choice value string | Nonempty `options` array of unique `{ "label", "value" }` entries |
| `color` | `#RRGGBB` or `#RRGGBBAA` | RGB/alpha and HSL editing, checkerboard alpha preview |

```json
{
  "version": 1,
  "children": [{
    "type": "group", "id": "view", "label": "View",
    "children": [{
      "type": "integer", "id": "view/zoom", "label": "Zoom",
      "description": "Initial zoom percentage.",
      "default": 100, "min": 10, "max": 800, "step": 10
    }]
  }]
}
```

Values use `{"version": 1, "values": {"view/zoom": 125}}`. Missing setting values
use defaults; unknown IDs are ignored so a values file can survive profile
changes. Invalid known values reject the entire load. Colors export as eight
hex digits. Loading does not emit user edits. `OnReload` reports definition changes;
`OnValuesReload` refreshes existing controls while preserving tree state.
`OnPersistenceChanged` reports the updated save status, including automatic saves.
Group resets update the complete snapshot before notifying and save once.
The demo runs the blocking context message loop. `SettingsEditor::OnHidden` lets
its application decide when to quit; reusable editors do not quit the context themselves.
UI updates are posted only when needed, and numeric repeat timers run only during holds.

When invoked from UI callbacks, definition replacement must use `UIHost::Post`
because it rebuilds the attached control tree.

## Extending and checking

Implement a new reusable control by overriding its small measure, arrange,
render and input hooks. Use `Container::Emplace` for owned components. Add a
settings binding in the editor factory within `SettingsView.cpp` when needed;
no host rendering/input switch needs modification. Theme colors, font size,
spacing and repeat timings are centralized in `LWSUI::Theme`.

The reference and verification records are in `docs/reference.md` and
`docs/verification.md`. Existing checks use public APIs; there is no friend
access to an implementation object. No new automated test cases were added.

The settings view retains the reference's indented tree labels, aligned editor
column, selection and hover rows, compact numeric rockers, radio markers, details
panel and footer actions. Search reveals matching descendants without losing
collapsed state. Up/Down and Tab navigate tree rows; Left/Right collapse or expand
groups and change choices. Ctrl+F searches, Ctrl+S saves, Ctrl+Enter applies, and
Ctrl+R / Ctrl+Shift+R reset a setting / all settings. Color editing uses a centered
panel with original/current previews and explicit Accept/Cancel actions.

## Default-value filter and window icons

The **All / Changed / Default** buttons combine with text search. Changed compares
working values with template defaults, independently of save/apply status. In the
theme editor it compares effective values with catalog defaults, not override flags.
Rows remain visible during previews and are refiltered after editing ends. Filtering
preserves remembered group expansion; selection survives hide/show but is not saved.

```cpp
LWSSettings::EditorConfig config;
config.iconPath = "my-settings.png"; // Empty uses the embedded cog wheels.
LWSSettings::SettingsEditor editor(platform, config);
editor.SetValueFilter(LWSSettings::SettingValueFilter::Changed);
```

The same PNG appears in the header and supported native window surfaces. Relative
paths use the working directory. PNGs may contain alpha; nonsquare images are padded
transparently. Missing/invalid images report an icon error and use the cog fallback.
Use `ThemeEditor(platform, file, iconPath)` to customize the theme editor too. The showcase
accepts `--icon path.png` and passes it to both windows.

Windows sets small and large native icons. Wayland uses optional
`xdg-toplevel-icon-v1`; the compositor controls native placement and may not support
it. The header icon always remains visible. No desktop entries or icon-theme files
are installed. No settings/theme JSON changes are required.

When search contains text, the Ã— button clears only the query and returns focus to
search. Escape while search is focused does the same. All / Changed / Default
is retained, and clearing removes the search highlights.

Reset spacing is controlled independently: `sections/tree/metrics/resetRightMargin`
(default about 23 logical pixels, matching the formâ€™s outer inset) moves the whole button left; `resetWidth` specifies the
button width, expanded to fit its caption. Tree `padding` controls column gaps,
not Reset's width. Excessive margins/gaps are clamped on narrow rows. Older partial
JSON files inherit the margin default; older full editor templates receive only
these new margin fields automatically, preserving their existing customization.

## Numeric presentations

Numeric settings keep their data type and may opt into a different presentation:

```json
{"type":"number","id":"scale","label":"Content scale","default":1,
 "min":0.5,"max":3,"step":0.1,"presentation":"slider"}
```

Omitting `presentation` (or using `"numeric"`) retains the existing numeric editor.
`"slider"` combines dragging with an editable numeric field. Dragging snaps to steps
anchored at `min`; both endpoints remain reachable. Typing may supply valid off-step
values. Escape restores the exact pre-drag value. Arrows step the value; Shift uses
ten steps. Reset, staged Apply/Cancel, autosave and Changed/Default filtering share
the existing document lifecycle. Values files still contain ordinary numbers.

Sliders require finite `min < max`, a positive step and a finite range/step count.
Integer slider bounds and span must fit within the exact double integer range
(+/- (2^53 - 1)); use numeric presentation for larger integer ranges. Unsupported
presentations fail template loading instead of silently selecting another control.

The slider/value composite is private to LWSSettings. It reuses LWSUI controls;
public extraction is deferred until another consumer needs the same behavior.

### Color-picker keyboard workflow

Ctrl+Up/Down adjusts the vertical slider from any picker field. Alt+Left/Right adjusts
the color area's horizontal axis; Alt+Down/Up adjusts its vertical axis. Shift uses larger
steps. Focus stays on the current control; invalid drafts block adjustments. Ctrl+Enter
accepts valid edits, and Escape restores the original color. Ctrl+Alt is not a picker-wide
shortcut. Plain arrows still operate the focused control.

The editable hex value sits beside New; Original shows the fixed opening value as a
read-only selectable #RRGGBBAA text. Double-click the Original preview to restore its
color and alpha, including replacing an invalid draft, without closing the picker.
New/original previews share one cached composed image with an opaque-color fast path.
The picker keeps all seven channels in compact rows and has no scrollbar. It measures
its height, reducing the color area and preview heights when space is tight while
keeping fonts readable. If even the minimum layout cannot fit, it reports that a larger
window or smaller popup fonts are needed. Horizontal padding remains symmetric.

Reusable TextBox controls support independent SetReadOnly and SetBorderless options.
Read-only text retains selection, navigation, and copying; typing, deletion, cut, and
paste are blocked. Enabling read-only cancels any active draft and invalidates pending
paste. Programmatic SetText remains available. Borderless suppresses the input background
and frame while keeping the caret and selection visible. Ordinary labels stay lightweight.

### File-picker validation

A user-edited nonempty path must identify an existing regular file. Invalid drafts stay
visible with an error and cannot be committed or saved. Empty values remain allowed.
Browse/F4 can discard an invalid draft to choose a replacement; cancellation retains the
previous value. A missing or deleted browse result is rejected without replacing that value.
Loaded/default/programmatic values are not filesystem-validated, so missing external files
do not prevent opening a profile. Escape can restore such a previously loaded value.

Text fields support right-click and Shift+F10 context menus with Cut, Copy, Paste, and
Select All. Read-only fields, including the Original color hex, retain Copy and Select
All. Opening a menu preserves selection and drafts, and Escape closes the menu before
closing its containing color picker.

### Built-in themes and live switching

Dark preserves the original palette. Light uses neutral light surfaces and blue accents;
Warm uses cream surfaces and brown/amber accents. Fonts and layout defaults are shared.
Use the header's Theme button to switch immediately. A star marks unsaved theme changes.
Choosing any preset, including the current one, clears custom overrides. Save theme
persists the choice; Revert theme restores the last saved profile. Application settings
and their Save button remain separate from theme persistence.

A theme profile is sparse: `{"version":1,"preset":"warm"}` is a complete valid theme.
Optional `base`, `sections`, and `layout` properties override only the supplied fields.
Omitting `preset` keeps legacy Dark behavior. Unknown presets fail without changing the
active session. Use ThemeStorage's profile APIs to retain the preset; the override-only
helpers describe patches over Dark. Full-theme export writes all resolved values.

ThemeSession owns current/saved profiles and live notifications without loading editor
definitions. ThemeSelector is an optional UI adapter. ThemeEditor accepts a shared session
or owns one for standalone use; the full template is loaded only when editing is opened.
Each time F6 opens a hidden editor, it copies the current main appearance and holds that
style until closed. Previews affect the main window. Closing F6 reverts unsaved changes.
A field's reset inherits from the selected preset, including inherited base overrides.

SettingsView::HeaderActions() is an optional horizontal container. In SettingsEditor,
it shares the compact menu-bar row with the title and status; the title truncates first
and the window minimum width protects the actions. A standalone SettingsView retains
its embedded header and wraps actions below the title on narrow views. Theme controls
reuse the ordinary LWSUI context menu.

Non-default setting names use bold weight (at least 700). Dark uses amber gold
`#FBBF24` for the name and displayed value; Light and Warm retain normal text colors.
Value controls keep their configured font weights. This indicates a difference from
the default, including saved values. Reset removes the emphasis. Explicit `changed`
color overrides remain supported, and custom name weights above 700 remain intact.

When no theme profile exists, the built-in preset establishes the baseline without
creating a file. Theme Save writes the optional `theme.json`, or the explicit
`--theme` path. CMake never generates or overwrites these user-owned profiles.

ThemeProfile.hpp contains public sparse-profile types without JSON dependencies.
ThemeSession and theme-editor headers do not include the codec. ThemeStorage.hpp uses
JSON forward declarations; include `<nlohmann/json.hpp>` when manipulating JSON values.
The shared field descriptor catalog is private to the implementation.

ThemeEditor::Editor() provides read-only editor/document inspection. Use Session() or
an externally supplied ThemeSession for validated profile changes, and subscribe to
ThemeEditor::OnError for errors. The editor's hidden metadata cannot be replaced through
the public document API. Clean session pulls perform no synthetic field commits; dirty
pulls apply only differences after establishing the saved baseline.

## Live resize redraw

SettingsEditor redraws during resizing by default. Set `EditorConfig::redrawOnResize`
to false when constructing an expensive view or targeting a slower machine. The
runtime `SettingsEditor::SetRedrawOnResize(bool)` setter preserves the choice across
hide/show and native-window recreation; `RedrawOnResize()` reports it. ThemeEditor
has the same setter/getter and enabled default. This is an application policy,
not a JSON theme or settings property.

The demo accepts `--no-live-resize` to use deferred redraw for both the settings
window and its live theme editor. Menu-bar double-click behavior is a separate
LWSUI opt-in and is not enabled automatically by this resize policy.
