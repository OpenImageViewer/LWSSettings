# OIViewer settings profile

This LWSSettings template matches the 14 setting paths in
`D:/dev/oiv/Clients/OIViewer/Resources/Configuration/Settings.json`.

- `settings.json`: UI definitions, labels, descriptions, and defaults based on the supplied NetSettings template.
- `values.json`: a snapshot of the actual OIViewer values, converted to LWSSettings' values format.
- IDs use OIViewer's slash-separated paths, for example `viewsettings/imagemargins/x`.

The source file contains System, File system, View settings (including Image margins), and Auto scroll.
The supplied template's Display settings and Files groups are absent from that file and are omitted here.

Defaults remain separate from current values: `system/allowdynamicsettings` defaults to false but is currently
true; `filesystem/deletedfileremovalmode` defaults to internally but is currently externally. These two settings
therefore appear in the non-default color.

Numeric editors follow the current `OIVAppCore/Source/Src/AppSettingsPolicy.cpp`: `minimagesize` is a number,
while `speedfactorrange` is an integer. Integral fields written as whole-valued floats in the source file
(such as `2000.0`) are represented as integers in the values snapshot. No additional numeric bounds are imposed.

## Preview

CMake places the profile in an `oiv` directory beside the demo executable, naming the current-value snapshot
`showcase-values.json` for the existing demo loader. Run the demo with that directory as its working directory.

For the current Windows build:

```powershell
cd D:\dev\LWSSettings\build-release\oiv
..\LWSSettingsDemo.exe
```

Add `--staged` for OnApply and `--autosave` for OnCommit; combine them to save after Apply. Saved edits go to `user-settings.json` in the
profile directory. Existing saved edits take precedence over the supplied snapshot.

## Application integration

```cpp
LWSSettings::SettingsEditor editor(platform); // Borrow the application PlatformContext.
auto& document = editor.Document();
auto schema = LWSSettings::SettingsStorage::LoadTemplateFile(document, "examples/oiv/settings.json");
auto values = LWSSettings::SettingsStorage::LoadValuesFile(document, "examples/oiv/values.json");
```

Check both returned statuses. To write back to OIViewer, map the slash-separated IDs into its nested JSON
object; the demo's saved format is the LWSSettings flat values envelope. The source OIViewer file is unchanged,
and this profile is a snapshot rather than a live binding.

There is an existing key mismatch in OIViewer: the settings file contains `system/allowdynamicsettings`,
while `AppSettingsPolicy::ParseAction` handles `system/reloadsettingsfileifchanged`. This profile preserves the
actual file's key. Resolve that mismatch separately when connecting events to OIViewer's runtime policy.
