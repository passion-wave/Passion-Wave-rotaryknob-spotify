#!/usr/bin/env python3
"""Run actual picker functions against simulated LVGL/provider boundaries.

This regression tests delayed discovery, touch/click identity binding and session
invalidation. It does not test rendering, the LVGL input engine or real hardware.
"""
import os
from pathlib import Path
import subprocess
import tempfile

COMPONENT = Path(__file__).resolve().parents[1]
COMPONENTS = COMPONENT.parent


def function(source, declaration):
    # Production functions close in column zero. Fail closed on source changes;
    # never keep a second hand-maintained copy of the picker implementation.
    if source.count(declaration) != 1:
        raise ValueError(f"Expected exactly one function: {declaration}")
    start = source.index(declaration)
    end = source.index("\n}", start) + 2
    return source[start:end] + "\n"


def main():
    source = (COMPONENT / "pw_ui.c").read_text()
    # Ensure the production's 500 ms snapshot refresh calls the tested policy.
    music = function(source, "static void refresh_music(void) {")
    assert "pw_spotify_get_snapshot(&spotify_view);" in music
    assert "service_picker_refresh();" in music
    with tempfile.TemporaryDirectory(prefix="pw-picker-native-") as directory:
        work = Path(directory)
        functions = (
            "static void close_picker(void) {",
            "static void picker_page_clicked(lv_event_t *event) {",
            "static void picker_row_clicked(lv_event_t *event) {",
            "static void refresh_picker(void) {",
            "static void service_picker_refresh(void) {",
        )
        (work / "production.inc").write_text("\n".join(function(source, name) for name in functions))
        binary = work / "picker-test"
        subprocess.run([
            os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g", "-I", str(work),
            "-I", str(COMPONENTS / "pw_spotify/tests/stubs"),
            "-I", str(COMPONENTS / "pw_spotify/include"),
            "-I", str(COMPONENTS / "pw_app/include"),
            str(Path(__file__).with_name("test_picker.c")), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True,
                       env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
