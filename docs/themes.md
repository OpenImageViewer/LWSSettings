# Themes and search highlights

The showcase reads the optional `theme.json` profile when present and applies its
fields over the selected built-in preset. Without a profile it uses built-in theme
defaults; it does not read a `theme-default.json` fallback or write a startup file.
The live editor builds its definitions directly from the shared field catalog,
including on first F6 or `--edit-theme`. No `theme-settings.json` is required.
Reopening reuses the editor. External profile edits require a restart; there is no
file watcher. Editor windows combine their icon, title, theme selector, and status with
Minimize/Close in one compact bar instead of a native caption and separate header. Native file dialogs retain operating-system styling.

Run `LWSSettingsDemo --theme other.json` to select a theme. Run
`LWSSettingsDemo --export-theme exported.json` to export the resolved theme without
opening a window. No implicit theme file means built-in defaults; malformed or
explicitly missing files print a warning and use those defaults in the demo.

The compact bar uses header colors and fits header fonts into the menu-bar height.
Its title uses the regular header font; the larger title font remains available to
standalone SettingsView headers. Title truncation preserves the original title,
and Saved/Unsaved reserve the same width to keep neighboring controls stable.

## Live editor

Press **F6** in the showcase, or launch `LWSSettingsDemo --edit-theme`. A separate
Theme editor window uses a stable built-in style while the showcase previews
changes. F6 reopens/reactivates that window after it is closed. Groups start
collapsed; use search to find a property, such as `description/colors/background`.

Each property occupies one named row. Editing it automatically creates an override.
The action column shows **Inherited** (disabled) while the value follows its base or
default, and **Use inherited** when an explicit override exists. Use inherited removes
that override and immediately restores the effective inherited value. Escape cancels
an edit and restores inheritance when the edit created the override.

Changing a base value updates sections whose corresponding properties remain
inherited. Explicit values are retained even when they equal defaults. Existing full
exported themes therefore initially show Use inherited for every supplied property;
they are not silently normalized into partial files. A file containing only
`{"version": 1}` starts fully inherited.

**Save** atomically writes only enabled overrides to the selected `--theme` path,
or the profile's `theme.json`. The settings values file remains separate. A failed
save retains edits and the previous saved baseline. **Revert** restores the last
loaded or successfully saved theme. **Reset all** clears overrides and previews
built-in defaults; it does not save. Closing the editor reverts outstanding edits.
The Use inherited row action returns that property to inheritance.

The editor's metadata, bounds, and defaults come from the private `ThemeFields`
catalog, also used for loading and validating themes. An optional exported template
uses the normal version-1 settings schema. Export it for inspection or explicit
library use with `ThemeEditor::Load(path)`:

```text
LWSSettingsDemo --export-theme-template <source-directory>/examples/theme-settings.json
```

The generated definitions contain one setting per catalog path, with matching types,
defaults, bounds and steps. Override flags are private document state and are not
in the template or control tree.
Labels and descriptions can be customized. The build output contains generated demo
assets; use `--theme` with a file outside the build directory to keep a personal theme
independent of rebuilding those assets.

## File structure

`version` is 1. `base` and each named `sections` entry accept these groups:

| Group | Contents |
| --- | --- |
| `typography` | `font`, `smallFont`, `headingFont`, `titleFont`, `glyphFont` |
| `colors` | Backgrounds, foregrounds, accents, muted/error/changed text, selection, row hover, popup and search colors |
| `metrics` | Logical-pixel padding, spacing, borders, control sizes, tree columns/indentation, scrolling and popup sizes |
| `interaction` | Numeric repeat delay, interval and acceleration time in milliseconds |

Every font has `family`, `size`, `weight` (1–999), and `italic`. Sizes are logical
pixels at 96 DPI, independent of geometry. Empty family selects the platform UI
font; unavailable families fall back through the platform font system. Colors use
`#RRGGBB` or `#RRGGBBAA` (alpha last).

Sections are `header`, `search`, `tree`, `description`, `footer`, `controls`, and
`popups`. Header titles use `titleFont`; group headings and description titles use
`headingFont`; reset/status text uses `smallFont`; numeric arrows use `glyphFont`.
Setting values use the `controls` section. The description body uses its own
`font` and `muted` color, with `accent` for its title. Editors use `surface` and
`line` for idle/focused backgrounds; pane backgrounds use `background`.

`sections/search/metrics/spacing` controls the gap between search and the value-filter
selector: 8 logical pixels by default, horizontally or vertically when stacked.
An explicit section override wins over base spacing; zero deliberately removes the gap.

Top-level `layout` controls the main section heights, horizontal padding, gaps,
and footer action widths. `descriptionHeight` includes its title, body and status.
`descriptionMaxFraction` caps its share of the space below search (default 0.75),
leaving room for the tree. An empty status message gives its space to the body. Font measurement establishes
minimum control heights, and overflowing details/popups scroll within the window.
Geometry does not implicitly scale when changing font sizes.

Missing fields retain defaults. Explicit `base` fields also apply to sections
unless overridden there. Export includes every resolved section field, so edits
in that complete file should target the desired section; its explicit section
values override corresponding base fields. Unused groups for a section may be
omitted in hand-written partial files. Unknown names and invalid values reject
the entire file rather than applying half a theme.

For example, this small file inherits all other values:

```json
{
  "version": 1,
  "base": {
    "typography": { "font": { "family": "Arial", "size": 18 } },
    "colors": { "searchBackground": "#ffd166ff", "searchForeground": "#171c25ff" }
  },
  "sections": {
    "description": {
      "colors": { "background": "#163832ff", "muted": "#c8f4e4ff" },
      "typography": { "font": { "family": "Georgia", "size": 20 } }
    },
    "controls": { "metrics": { "rockerWidth": 36 } }
  },
  "layout": { "descriptionHeight": 220 }
}
```

## Search

Search matches setting/group labels, descriptions, IDs, ancestors, current values
(including displayed Enabled/Disabled states), and choice labels. It is literal,
ASCII case-insensitive, and otherwise byte-exact UTF-8; it is not regex or Unicode
case folding. Matching IDs can reveal a setting without a visible highlighted ID.

All occurrences in visible settings text are highlighted, including choice menus,
editor text, breadcrumbs, and wrapped descriptions. Overlapping matches form one
highlighted range. The search input, action captions and decorative arrows are
excluded. Selection colors take precedence over search colors; other non-default
and row styling remains outside the matched ranges.

Editing updates highlights immediately but keeps the current result rows stable.
Changing the query or reloading values recomputes membership. Clearing the query
removes highlighting and restores the user's collapsed groups.

## C++ integration

`SettingsTheme` contains resolved typed styles. Pass it through `EditorConfig.theme`
or the third argument of `SettingsView`. `ThemeStorage::Decode` and `LoadFile`
return `std::expected<SettingsTheme, Error>`; `Encode` returns JSON and `SaveFile`
returns `Status`. Callers decide whether a load error should fall back or abort.
No JSON or settings dependency was added to LWSUI.

LWSUI controls inherit `Theme` and a literal highlight pattern from their parent.
`FontSpec` is shared by rendering, measurement and caret positioning. Canvas text
spans use UTF-8 byte ranges and are applied to the complete native shaped layout,
preserving wrapping and bidirectional glyph placement. The host/control caches
continue to use the simple bounded policy documented in their source files.

`SettingsEditor::SetTheme` validates typed styles immediately and coalesces their
application into the next host update. Existing controls, focus, drafts, search and
expansion state survive; scroll offsets clamp to the new layout. Showcase popups
cancel before the style change. `ThemeEditor::OnPreview` can feed this API; the
preview subscription and target editor must outlive the editing session's use.
The adapter tracks typed overrides without serializing/parsing JSON per edit.
`SettingsDocument::OnEditFinished` includes no-op confirmations so companion
override edits finish correctly. `RevertSaved()` restores the saved snapshot;
`showRevert` and `revertOnClose` opt ordinary editors into the same behavior.

`SettingDefinition::visibleInEditor` defaults to true. C++ adapters can mark internal
entries false to retain normal document snapshots and dirty tracking without creating
controls or search entries. `SettingsView::SetRowResetPresentation` updates action
captions/enabled state without changing ordinary Reset behavior. Theme value IDs are
the catalog paths themselves, with no `/value` or `/override` UI suffixes.

## Color picker

The picker combines a fixed-hue saturation/lightness field, a vertical hue spectrum,
and R/G/B/A/H/S/L sliders with editable values. Saturation increases rightward;
lightness increases upward. RGB/Alpha use integers 0–255; HSL accepts decimal
degrees or percentages. The title hex field accepts #RRGGBB and #RRGGBBAA.
Valid edits preview immediately; invalid drafts preserve the last valid color and
block Accept. Cancel or Escape restores the opening color. Original/New swatches
share a checkerboard and an edge for comparison.

Default popup dimensions are 720 × 600 logical pixels. Existing popup metrics
control layout, with stacking and scrolling in smaller windows. Explicit theme
overrides remain authoritative; old exported template size defaults migrate.

When no theme profile exists, the built-in preset establishes the baseline without
creating a file. Theme Save writes the optional `theme.json`, or the explicit
`--theme` path. CMake never generates or overwrites these user-owned profiles.
