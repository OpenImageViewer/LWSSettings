"""Exercise demo startup in isolated folders; never read or overwrite user profiles."""
import ctypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import struct
import sys
import tempfile
import time

exe, examples, scratch = map(lambda value: Path(value).resolve(), sys.argv[1:4])
event_debugger = len(sys.argv) > 4 and sys.argv[4] == "1"
if os.name == "nt":
    image = exe.read_bytes()
    pe = struct.unpack_from("<I", image, 0x3c)[0]
    assert struct.unpack_from("<H", image, pe + 24 + 68)[0] == 2, "Expected Windows GUI subsystem"
    assert (b"LWSSettings Events" in image) == event_debugger
scratch.mkdir(parents=True, exist_ok=True)
checks = 0

with tempfile.TemporaryDirectory(prefix="run-", dir=scratch) as temporary:
    root = Path(temporary).resolve()
    assert root.parent == scratch  # Cleanup is confined to this generated test directory.

    def case(name, files=None, template=True):
        directory = root / name
        directory.mkdir()
        shutil.copy2(exe, directory / exe.name)
        if template:
            shutil.copy2(examples / "settings.json", directory / "settings.json")
        for filename, value in (files or {}).items():
            (directory / filename).write_bytes(value.encode("utf-8") if isinstance(value, str) else value)
        return directory

    def run(directory, *arguments, expected=0, warning=False):
        global checks
        result = subprocess.run([str(directory / exe.name), *arguments], cwd=directory,
                                capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=25)
        assert not result.stdout, (directory.name, result.stdout)
        assert result.returncode == expected, (directory.name, result.returncode, result.stdout, result.stderr)
        assert ("Theme warning:" in result.stderr) == warning, (directory.name, result.stderr)
        checks += 1
        return result

    clean = case("settings-only")
    assert {path.name for path in clean.iterdir()} == {exe.name, "settings.json"}
    run(clean, "--smoke")
    run(clean, "--export-theme", expected=2)
    run(clean, "--smoke", "--theme", expected=2)
    assert not (clean / "user-settings.json").exists()
    assert not (clean / "theme.json").exists()
    run(case("deferred-resize"), "--no-live-resize", "--edit-theme", "--snapshot")
    run(clean, "--edit-theme", "--snapshot")
    assert (clean / "theme-editor.ppm").exists()
    assert not (clean / "theme.json").exists()
    assert not (clean / "user-settings.json").exists()
    run(case("autosave-defaults"), "--autosave", "--smoke")
    assert not (root / "autosave-defaults" / "user-settings.json").exists()
    run(case("staged-defaults"), "--staged", "--autosave", "--smoke")
    assert not (root / "staged-defaults" / "user-settings.json").exists()

    legacy = case("ignored-legacy", {name: "not JSON" for name in
                  ("showcase-values.json", "theme-default.json", "theme-settings.json")})
    run(legacy, "--edit-theme", "--snapshot")
    assert (legacy / "theme-editor.ppm").exists()
    assert not (legacy / "theme.json").exists()

    saved_bytes = b'{"version":1,"values":{"name":"Saved startup value"}}\r\n'
    saved = case("saved-values", {"user-settings.json": saved_bytes})
    run(saved, "--smoke")
    assert (saved / "user-settings.json").read_bytes() == saved_bytes
    invalid = case("invalid-values", {"user-settings.json": "invalid saved data"})
    run(invalid, "--smoke", expected=3)
    assert (invalid / "user-settings.json").read_text() == "invalid saved data"
    run(case("missing-template", template=False), "--smoke", expected=2)
    run(case("invalid-template", {"settings.json": "{}"}), "--smoke", expected=2)
    oiv = case("oiv-defaults", {"settings.json": (examples / "oiv" / "settings.json").read_bytes()})
    run(oiv, "--smoke")
    assert not (oiv / "user-settings.json").exists()

    run(clean, "--export-theme", "resolved.json")
    baseline = json.loads((clean / "resolved.json").read_text())
    light = case("optional-theme", {"theme.json": '{"version":1,"preset":"light"}'})
    run(light, "--export-theme", "resolved.json")
    light_theme = json.loads((light / "resolved.json").read_text())
    assert light_theme == baseline
    (light / "dark.json").write_bytes(b'{"version":1,"preset":"dark"}\r\n')
    run(light, "--theme", "dark.json", "--export-theme", "resolved.json")
    dark_theme = json.loads((light / "resolved.json").read_text())
    assert dark_theme != baseline
    (light / "warm.json").write_bytes(b'{"version":1,"preset":"warm"}\r\n')
    run(light, "--theme", "warm.json", "--export-theme", "resolved.json")
    assert json.loads((light / "resolved.json").read_text()) not in (baseline, dark_theme)
    run(light, "--theme", "absent.json", "--export-theme", "resolved.json", warning=True)
    assert json.loads((light / "resolved.json").read_text()) == baseline
    malformed = case("malformed-theme", {"theme.json": "invalid theme"})
    run(malformed, "--export-theme", "resolved.json", warning=True)
    assert json.loads((malformed / "resolved.json").read_text()) == baseline
    assert (malformed / "theme.json").read_text() == "invalid theme"
    run(clean, "--export-theme-template", "exported-template.json")
    exported = json.loads((clean / "exported-template.json").read_text())
    assert exported["version"] == 1 and exported["children"]

    if os.name == "nt":
        from ctypes import wintypes
        user32 = ctypes.WinDLL("user32", use_last_error=True)
        callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
        user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
        user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
        user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        user32.IsWindowVisible.argtypes = [wintypes.HWND]
        hotkey = case("local-window-hotkeys")
        process = subprocess.Popen([str(hotkey / exe.name)], cwd=hotkey,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)

        def windows():
            found = {}

            @callback_type
            def collect(hwnd, unused):
                pid = wintypes.DWORD()
                user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                if pid.value == process.pid:
                    title = ctypes.create_unicode_buffer(256)
                    user32.GetWindowTextW(hwnd, title, len(title))
                    found[title.value] = hwnd
                return True

            user32.EnumWindows(collect, 0)
            return found

        def wait_for(predicate):
            deadline = time.monotonic() + 8
            while process.poll() is None and time.monotonic() < deadline:
                found = windows()
                if predicate(found):
                    return found
                time.sleep(.02)
            raise AssertionError(("Hotkey state not reached", process.poll(), windows()))

        def key(hwnd, code, repeat=False):
            # Messages target only windows belonging to this test process.
            assert hwnd in windows().values()
            assert user32.PostMessageW(hwnd, 0x100, code, 0x40000001 if repeat else 1)
            assert user32.PostMessageW(hwnd, 0x101, code, 0xC0000001)

        def event_visible(found):
            hwnd = found.get("LWSSettings Events")
            return bool(hwnd and user32.IsWindowVisible(hwnd))

        try:
            found = wait_for(lambda found: "Settings" in found)
            main_window = found["Settings"]
            key(main_window, 0x75)  # F6
            found = wait_for(lambda found: "Theme editor" in found)
            theme_window = found["Theme editor"]
            key(main_window, 0x77)  # F8
            if event_debugger:
                found = wait_for(event_visible)
                event_window = found["LWSSettings Events"]
                key(event_window, 0x77, repeat=True)
                time.sleep(.1)
                assert event_visible(windows())
                key(event_window, 0x77)
                wait_for(lambda found: not event_visible(found))
                key(theme_window, 0x77)
                found = wait_for(event_visible)
                assert found["LWSSettings Events"] == event_window
                assert user32.PostMessageW(event_window, 0x10, 0, 0)  # WM_CLOSE hides
                wait_for(lambda found: not event_visible(found))
                key(main_window, 0x77)
                wait_for(event_visible)
            else:
                key(theme_window, 0x77)
                time.sleep(.2)
                assert "LWSSettings Events" not in windows()
            assert user32.PostMessageW(main_window, 0x10, 0, 0)
            stdout, stderr = process.communicate(timeout=10)
            assert process.returncode == 0 and not stdout and not stderr, (process.returncode, stdout, stderr)
            assert not (hotkey / "theme.json").exists()
            saved_settings = json.loads((hotkey / "user-settings.json").read_text())
            assert saved_settings["version"] == 1 and isinstance(saved_settings["values"], dict)
            checks += 1
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()

print(f"Passed {checks} isolated demo startup checks.")
