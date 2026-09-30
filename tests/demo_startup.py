"""Exercise demo startup in isolated folders; never read or overwrite user profiles."""
import ctypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

exe, examples, scratch = map(lambda value: Path(value).resolve(), sys.argv[1:4])
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
        assert result.returncode == expected, (directory.name, result.returncode, result.stdout, result.stderr)
        assert ("Theme warning:" in result.stderr) == warning, (directory.name, result.stderr)
        checks += 1
        return result

    clean = case("settings-only")
    assert {path.name for path in clean.iterdir()} == {exe.name, "settings.json"}
    run(clean, "--smoke")
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
    assert light_theme != baseline
    (light / "warm.json").write_bytes(b'{"version":1,"preset":"warm"}\r\n')
    run(light, "--theme", "warm.json", "--export-theme", "resolved.json")
    assert json.loads((light / "resolved.json").read_text()) not in (baseline, light_theme)
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
        # Deliver F6 only to a window owned by this test's child process; no global input injection.
        from ctypes import wintypes
        user32 = ctypes.WinDLL("user32", use_last_error=True)
        callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
        user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
        user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
        user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        hotkey = case("f6-without-template")
        process = subprocess.Popen([str(hotkey / exe.name), "--smoke"], cwd=hotkey,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        sent = False
        opened = False
        try:
            deadline = time.monotonic() + 10
            while process.poll() is None and time.monotonic() < deadline:
                windows = []

                @callback_type
                def collect(hwnd, unused):
                    pid = wintypes.DWORD()
                    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                    if pid.value == process.pid:
                        title = ctypes.create_unicode_buffer(256)
                        user32.GetWindowTextW(hwnd, title, len(title))
                        windows.append((hwnd, title.value))
                    return True

                user32.EnumWindows(collect, 0)
                for hwnd, title in windows:
                    if title == "Settings" and not sent:
                        assert user32.PostMessageW(hwnd, 0x100, 0x75, 1)  # WM_KEYDOWN / F6
                        assert user32.PostMessageW(hwnd, 0x101, 0x75, 0xC0000001)
                        sent = True
                    if title == "Theme editor":
                        opened = True
                time.sleep(.02)
            stdout, stderr = process.communicate(timeout=10)
            assert process.returncode == 0 and sent and opened, (sent, opened, stdout, stderr)
            assert not (hotkey / "theme.json").exists()
            checks += 1
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()

print(f"Passed {checks} isolated demo startup checks.")
