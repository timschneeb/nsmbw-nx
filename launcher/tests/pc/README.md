# Launcher PC tests

Headless Linux build of the launcher's portable logic. This is a test harness, not a shippable PC
launcher.

```
cmake -S launcher/tests/pc -B build-launcher-pc -G Ninja
cmake --build build-launcher-pc
cd build-launcher-pc && ctest --output-on-failure
```

Targets:

- `launcher_pc_tests`: catalogue, scan, install, verify and delete logic.
- `launcher_ui_tests`: menu state and navigation smoke tests.
- `launcher_network_tests`: HTTP transport, fetch, install and update over HTTP.
- `launcher_scenario_tests`: end-to-end menu flows against a live, mutable catalogue.
- `launcher_render`: real NanoVG screenshots of menus and dialogs.
- `launcher_viewer`: interactive PC window running the same menus (needs GLFW).

Covered:

- catalogue loading and `file://` fetching
- NRO header and NACP version parsing
- install from an SD path and from a URL
- SHA and size verification
- invalid NRO rejection
- RomFS decompression and hashing
- `installed.json` state and scan results
- delete game vs delete game and save data
- manual install detection
- HTTP 200/404/500, redirects and redirect loops
- partial, chunked, cancelled and oversized transfers
- remote catalogue fetch, cache retention on failure, local-file precedence
- banner download and checksum rejection
- URL installs, partial URL installs, invalid NROs and SHA mismatches
- RomFS downloads
- launcher update over HTTP and failed-update rollback
- main menu rows, authors and footer labels
- X hidden on non-game rows
- `Update` vs `Check for updates`
- Settings entries and removal of Base game data / cached catalogue
- About content and credits
- Update all and game update-check dialogs

The scenario tests rewrite the served catalogue and republish NRO, RomFS, banner and launcher files
while the launcher is running. They cover:

- new, renamed and removed catalogue games
- full and partial game updates (NRO only or RomFS only)
- update all, partial failure and retry
- delete, delete and save data, verify failures and reinstall
- launcher update success, failure, cancellation and the launcher row
- download page errors, cancellation and empty state
- settings, local catalogue clearing and every About base-data state
- missing base data prompts and stubbed extraction success/failure/cancel
- free-space failures, install cancellation and IO failures
- cleanup of `.part`, `.new`, `.old`, `.tmp` and `.removed` leftovers

Switch-only services are stubbed in `test_shim.cpp` with file-backed implementations where the test
needs them. `test_hooks.hpp` controls free space, base-data state and progress-box cancellation.
Rendering is replaced by NanoVG no-op stubs; the UI tests drive the real widget stack.

See `TEST_PLAN.md` for the full feature matrix.

## Interactive viewer

`launcher_viewer` opens a 1280x720 window with the real launcher menus. Build and run it from a
desktop session:

```
cmake --build build-launcher-pc --target launcher_viewer
./build-launcher-pc/launcher_viewer
```

Controls: Enter/Z/Space = A, Backspace = B, X = X, C = Y, Minus = Settings, Equals = Start,
arrows = D-pad, mouse = click rows and dialogs, Escape = close. `--seconds N` closes after N seconds.

The viewer uses fixtures under `/tmp/opencode/launcher-viewer` and does not touch the console or the
release catalogue.

## Screenshots

`launcher_render` uses EGL surfaceless OpenGL, NanoVG GL3 and a system Noto/DejaVu font to render the
real menus and dialogs. It writes:

```
/tmp/opencode/launcher-screenshots/main.png
/tmp/opencode/launcher-screenshots/game-options.png
/tmp/opencode/launcher-screenshots/game-options-current.png
/tmp/opencode/launcher-screenshots/update-prompt.png
/tmp/opencode/launcher-screenshots/install-confirm.png
/tmp/opencode/launcher-screenshots/update-all-confirm.png
/tmp/opencode/launcher-screenshots/settings.png
/tmp/opencode/launcher-screenshots/settings-local.png
/tmp/opencode/launcher-screenshots/about.png
/tmp/opencode/launcher-screenshots/download.png
/tmp/opencode/launcher-screenshots/download-empty.png
/tmp/opencode/launcher-screenshots/download-error.png
/tmp/opencode/launcher-screenshots/delete-confirm.png
/tmp/opencode/launcher-screenshots/launcher-dialog.png
```

It needs `libepoxy`, EGL, OpenGL and a Noto or DejaVu font installed.

Fixtures are created under `/tmp/opencode/launcher-pc-tests` and `/tmp/opencode/launcher-scenarios`.
