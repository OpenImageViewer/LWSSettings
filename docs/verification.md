# Verification record

Reference: LWSSettings master `2e43ae8`. Implementation verification: 2026-09-19.

## Builds and existing checks

| Configuration | Result |
| --- | --- |
| Windows clang-cl Release, settings library and both demos | Built successfully |
| Windows settings checks | 37 cases, 1837 assertions passed |
| Wayland clang build, settings library and both demos | Built successfully |
| Wayland settings checks | 37 cases, 1826 assertions passed |
| Wayland LWS platform checks | 47 cases, 224 assertions passed |

The original 37 settings scenarios were adapted to the new public document and
control APIs. No additional test cases or private implementation-access hooks
were introduced. The Windows-only native-dialog scenario runs on Windows and
is not a portal interaction test on Wayland.

The checks cover typed defaults, atomic template/value replacement, validation,
exact int64 bounds, decimal steps, notifications and listener insertion during
dispatch, persistence and automatic-save failures, staged apply/cancel, UTF-8
editing, drafts, numeric holds and cancellation, deep nesting, filtering,
scrolling, popup priority/description ownership, default highlighting and color
conversion. The existing Windows F4 case opens the native file dialog and
cancels it without publishing a value change.

## Demo and visual verification

Both settings and controls-only demos ran with their smoke option on Windows
and Wayland. The Windows settings demo also ran with staged/autosave policies.
The OIViewer profile loaded and rendered; its two JSON files are unchanged from
the reference, preserving every setting path and value.

The settings demo's `--snapshot` option renders through the same public control
tree and backend as the window. Snapshots were inspected for the showcase and
OIViewer profile on Windows and for the showcase on Wayland. This caught and
corrected clipped Reset labels with the Linux font and an initial presentation
attempt before Wayland surface configuration. The host now retries that pending
frame at the next update. Demo error subscriptions report implicit UI failures.

Generated artifacts and logs are in the ignored build directories:

- `build-release/v1-build.log`, `v1-tests.log`, `snapshot.ppm` / `snapshot.png`
- `build-release/oiv/snapshot.ppm` / `snapshot.png`
- `build-linux/v1-build.log`, `v1-tests.log`, `v1-lws-tests.log`, `snapshot.ppm`

### Live desktop follow-up, 2026-09-19

The desktop connection was restored in a fresh session. The rebuilt Windows
settings demo and the reference executable built from `2e43ae8` were inspected
live. Their tree indentation, editor alignment, row spacing, descriptions and
footer layout closely match. This was a visual comparison, not a pixel-diff
assertion; loaded values differed between the demo working directories.

Live input verified:

- Checkbox editing and dropdown selection over covered numeric rows; console
  records identify the setting, old/new values, domain, origin and phase.
- Branch collapse/expansion, scrolling through three nested groups, and search
  retaining the matching setting's ancestors.
- RGB slider dragging updating the color preview and HSL values; Cancel restores
  the original color and emits preview/cancel records.
- Native Windows file-dialog opening and cancellation without a value edit.
- The controls-only demo's nested tree, numeric rocker preview/commit, and branch
  collapse event. Both demo error logs remained empty.
- OnApply with OnCommit saving in an isolated `build-release/live-staged` folder:
  edits emit Working-domain events without writing a file; Cancel restores the
  applied snapshot; Apply emits an Applied-domain event and saves the new value.

Live event logs are `build-release/live-settings.log`, `live-controls.log`, and
`live-staged/events.log`; matching error logs are empty. The existing Windows
build/check record is `build-release/restore-build.log` / `restore-tests.log`.
The interrupted Wayland build was resumed successfully; fresh results are
`build-linux/live-build.log`, `live-tests.log`, `live-settings-smoke.log`, and
`live-controls-smoke.log`. Both Wayland demo smoke runs exited successfully.

Physical multi-monitor DPI movement and interactive Wayland portal selection
remain unverified. Numeric hold acceleration, deep nesting beyond the showcase,
and color acceptance remain covered by existing checks rather than this live
input pass. No new automated test cases were added.

## Architecture refinement verification

The focused architecture follow-up retains the existing 37 test cases and extends
relevant scenarios. Windows passed 338 assertions; Wayland passed 327 assertions.
The checks now cover complete reset snapshots before notification, one persistence
notification/write per group or all-settings reset, silent value reloads retaining
control identity and collapsed state, cancellation of an old color popup during
reload, pointer caret placement after a text change without an intervening paint,
custom theme foreground colors, and callback-posted structural removal.

Both Windows demos rendered snapshots successfully; the generated settings and
controls images were inspected. Both Wayland demo smoke runs exited successfully.
Logs use `architecture-build.log`, `architecture-tests.log`, `architecture-demo.log`
and `architecture-controls.log` in the respective build directories. Windows
snapshots are `build-release/architecture-snapshot.png` and
`build-release/architecture-controls.png`.

The original Windows demo was open during linking. The Windows build therefore
uses `build-release/architecture-check` for its updated executables, leaving the
running original demo session intact. This is a local CMake cache setting, not a
change to the project's default output directory.

Deferred optimization decisions are documented at their implementation sites:
UIHost scheduling/layout, ScrollView traversal, Canvas cache eviction and Windows
brush allocation, SettingsView binding lookup, and settings CMake target granularity.
Each comment explains why the simpler implementation remains and what requirement
or measurement would justify the extra machinery.

## Demo startup packaging correction

Launching the Windows executable from `build-release/architecture-check` exposed
an exit-code-3 startup failure: `showcase-values.json` was missing there. The
architecture smoke runs above used the parent build directory and did not exercise
that launch location. CMake now generates both default-profile JSON files beside
the executable, using the same target-relative output convention as the OIViewer
profile. Immediate and staged smoke launches from the executable directory, and
an OIViewer smoke launch from its generated profile directory, all exited with 0.
Windows logs are `startup-build.log`, `startup-demo.log`, `startup-staged.log`, and
`startup-oiv.log` in `build-release`.

## JSON themes and search highlighting

Windows and Wayland builds passed after the theme/highlight changes. The same 37
settings cases now pass 375 assertions on Windows and 364 on Wayland. Extended
scenarios cover theme export/load equivalence, partial overrides, invalid fields,
CRLF export, repeated/overlapping/UTF-8 matches, current values and choice labels,
result stability during edits, wrapped highlight pixels, clearing cached text
attributes, per-section fonts/colors and larger-font layout. A document-lifetime
mistake in the added viewer scenario was corrected before the final passing runs.

Live Windows input verified current-value and dropdown highlights for `dark`,
matching label/editor/breadcrumb/description occurrences for `profile`, selection
colors taking precedence, a nonmatching draft retaining its row, Escape restoring
the original value, and clearing search removing all highlights. The temporary
edit emitted preview/cancel events and was not saved.

A second live window loaded a partial theme with larger Arial text, Georgia in the
description body, separate header/description/footer backgrounds, magenta search
highlights, and an enlarged purple color popup. The fonts, backgrounds, highlights,
RGB/HSL layout, preview swatches and Cancel action were inspected successfully.
The temporary theme window was closed; the default showcase was left open with
search cleared. Both live error logs are empty.

The showcase loads its bundled `theme.json` from the executable/profile directory.
Launch checks passed with absent, malformed, explicitly missing and custom themes,
and with the generated OIViewer profile. Exported resolved JSON matches the bundled
source theme. Both platform demo runs exited successfully; Windows default/custom
snapshots were also inspected.

Artifacts: `theme-build.log`, `theme-tests.log`, `theme-demo.log` and
`theme-controls.log` in the platform build folders; Windows `theme-live.log`,
`theme-live-contrast.log` and their empty error logs; startup fallback checks in
`build-release/theme-fallback-check`. The sample verification override is
`build-release/architecture-check/theme-contrast.json`. The editable bundled theme
is `examples/theme.json`; usage and inheritance rules are in `docs/themes.md`.

## Live theme editor

The runtime-theme/editor follow-up builds on Windows and Wayland. The existing
37 test cases pass 450 assertions on Windows and 439 on Wayland. Extended scenarios
cover catalog/template equality, sparse override persistence, base inheritance,
section overrides, automatic override creation and cancellation, no-op edit
confirmation, Reset All, Revert, save failure recovery, coalesced runtime styles,
retained control identity/focus/drafts, popup cancellation, and editing a theme color
through public control input while the other editor updates.

`--edit-theme` and the OIViewer profile launch successfully from their actual output
directories on both platforms. The controls-only Windows smoke run also passes.
The exported theme-settings template is generated from the same typed catalog as
validation and decoding. The Windows runtime output is now
`build-release/live-theme-check`; the previously open showcase binary was preserved.
Logs are `live-theme-build.log`, `live-theme-tests.log`, `live-theme-smoke.log`,
`live-theme-oiv.log` and Windows `live-theme-controls.log` in the build folders.

The demo's public render path produced `snapshot.ppm` and `theme-editor.ppm`;
`theme-editor.png` was inspected on Windows for the initial collapsed groups and
Reset all/Revert/Save footer. Physical live desktop verification of the new editor
was initially blocked: fresh window selection and one retry returned
`GetCursorPos failed: Access is denied (0x80070005)` and
`IGraphicsCaptureItemInterop.CreateForMonitor ... (0x80070057)`. No further input was
sent through that unavailable desktop. Public-API interaction checks and rendered
snapshots are distinguished from live mouse/keyboard checks here.

Monitor discovery also reported zero DPI before window creation during this pass.
The UI scaling helper now treats unknown DPI as 96, avoiding tiny zero-client-height
startup windows; the existing DPI scenario verifies that fallback.

## Live theme editor desktop follow-up

After desktop access was restored, the Windows theme editor was verified using
`build-release/live-theme-check/live-edit-test.json`, initially only `version: 1`.
Typing `#163832ff` for `sections/description/colors/background/value` automatically
enabled its override and changed the showcase description pane immediately, while
the theme editor retained its stable styling. The file remained unchanged until Save.

Save wrote only the single explicit background override. Disabling it immediately
restored the inherited background. Revert restored the saved green background and
Override state. Closing the editor with an unsaved disable also restored that saved
baseline. F6 reopened the editor with its filter and saved value intact.
`build-release/live-theme-verified-errors.log` is empty. Both test windows were left
open; the normal theme files and ordinary settings values were not modified.

## Single-row theme properties and sparse startup

The override-checkbox removal builds on Windows and Wayland. The existing 37 cases
pass 1837 assertions on Windows and 1826 on Wayland, including catalog-wide visible
field coverage. Tests verify that the generated template contains one setting per
catalog property, internal override state creates no checkbox controls, editing
promotes an override, Escape restores inheritance, equal-valued explicit overrides
can be removed, and row captions/enabled state update correctly.

The bundled `examples/theme.json` is now a 22-byte CRLF file containing only version
1. The editor template is 342091 bytes (previously 897151). Normal startup was tested
in `build-release/single-row-startup-check` with `theme-settings.json` absent: it
completed successfully with no template error. Explicit `--edit-theme` in the same
folder requested the missing file and reported it. Both packaged profiles contain
the matching generated single-row template and sparse theme.

Live Windows checks used `single-row-live.json`, leaving normal theme files alone.
The description background appeared as one named row with a disabled Inherited
indicator. Editing it to `#294c43ff` changed the action to Use inherited and updated
the showcase immediately. Save wrote only that color override. Use inherited
restored the effective default; Revert restored the saved explicit value. Removing
it again and saving returned the file to version 1 only. F6 activated the existing
editor with its filter and row state intact. `single-row-live-errors.log` is empty.

Build/test logs are `single-row-build.log` and `single-row-tests.log` in each build
folder. The latest Windows executable remains in `build-release/live-theme-check`.
CRLF and whitespace checks passed; no new test cases were added.

## Preservation and hygiene

Existing LWS platform changes were retained. The shared LLUtils event change is
confined to `Include/LLUtils/Event.h`; the unrelated `EnumClassBitwise.h` edits
were preserved. No commits, repository resets or history rewrites were made.
New and modified source files use CRLF. Whitespace checks pass for authored
changes; existing whitespace in the unrelated LLUtils file was left untouched.

## Single-template startup and captionless editors, 2026-09-29

The demo now requires only settings.json. Saved values and optional theme profiles
are loaded when present; no showcase-values.json, theme-default.json, or external
theme-settings.json is used at startup or when opening the live theme editor.
Full applied-value snapshots and the existing manual/autosave/staged policies are
preserved. SettingsEditor uses a captionless resize border and an empty draggable
Minimize/Close menu bar. Close still respects validation and revert-on-close.

Windows and Wayland each passed 46 settings cases. Isolated executable checks passed
17 scenarios on Windows (including F6) and 16 on Wayland: settings-only startup,
no startup writes, built-in theme-editor creation, ignored malformed legacy files,
saved values, invalid/missing templates, invalid saved values, OIViewer defaults,
optional/explicit/missing/malformed themes, and export commands. The new caption
case checks native styles, menu configuration, validation-blocked close, hide/reopen,
and retained menu identity after a theme update.

Default settings and theme-editor snapshots were inspected on Windows. Snapshots
now include the entire client area and custom menu bar. Twelve obsolete generated
asset copies were removed only after matching their source-file hashes; existing
user-settings.json and theme.json files were preserved. Historical verification
records above describe their original packaging and are not current requirements.

## Compact menu-bar headers, 2026-09-30

SettingsEditor now hosts the settings header inside the generic LWSUI menu-bar
content slot. Icon, menu items, header content, and window buttons retain that
order; settings editors have no menu items. The standalone SettingsView keeps its
embedded header. Only unoccupied gaps initiate native dragging. The decision to
reserve a guaranteed drag strip remains a TODO.

Windows release passed all 47 settings cases (4620 assertions) and 133 LWS cases
(2509 assertions). Wayland release passed all 47 settings cases (5210 assertions)
and 104 LWS cases (2545 assertions). The isolated demo startup checks passed all
17 Windows and 16 Wayland scenarios, including the Windows F6 editor shortcut.

Focused coverage exercises icon/menu/content ordering, action clicks, disabled and
passive occupied regions, blank-gap native dragging, capture cancellation, Tab and
context menus, minimum-width resizing, relocated header identity, replacement
invalidation, standalone layout, theme-selector interaction, and window recreation.
Wayland ASan/UBSan runs passed the header case (47 assertions) and content case
(32 assertions), with leak detection disabled. Windows settings and theme-editor
snapshots were visually checked for the single-row header and adjacent search row.
Both repositories pass whitespace checks; modified text files retain CRLF endings.

### Passive header drag regions, 2026-09-30

The follow-up extends native window dragging to the menu-bar icon and compact
settings title and Saved/Unsaved text. Interactive and disabled controls, menu
items, and window buttons remain excluded. Generic labels and standalone settings
headers retain their existing hit-testing behavior. No native backend changes or
new public API were needed; the guaranteed empty drag-strip TODO remains deferred.

Windows regression tests intercept the native caption-move request to verify
icon/title/status dragging without moving the test windows. Portable checks cover
hit testing and disabled dragging, including vertical bars. The full Windows and
Wayland settings and LWS suites passed after this change.
