# Launcher feature and scenario test plan

This plan covers every launcher feature that can be exercised on the PC harness. Tests use the real
menu stack, the real install/scan/catalogue code and a loopback HTTP server whose catalogue, NROs,
RomFS images, banners and launcher can be republished between steps.

Status legend: `[x]` automated, `[s]` screenshot only, `[ ]` not covered on PC.

## Harness

- `launcher_pc_tests`: catalogue, scan, install, verify, delete logic.
- `launcher_ui_tests`: menu state and navigation smoke tests.
- `launcher_network_tests`: HTTP transport, catalogue fetch, install/update over HTTP.
- `launcher_scenario_tests`: end-to-end menu flows against a live, mutable catalogue.
- `launcher_render`: real NanoVG screenshots of menus and dialogs.

Scenario tests rewrite `assets.json`/`assets.url` and republish NRO, RomFS, banner and launcher
files while the server is running. Every assertion also checks the filesystem, not just the UI.

## A. Catalogue acquisition and parsing

- [x] A1 `assets.local.json` wins over `assets.cache.json` and bundled `assets.json`.
- [x] A2 Invalid local catalogue falls back to the cache, then to the bundled catalogue.
- [x] A3 No catalogue at all returns failure without crashing.
- [x] A4 `FetchRemoteAssets` always uses the network and ignores local/cache copies.
- [x] A5 Successful fetch writes `assets.cache.json` atomically and leaves no `.tmp`.
- [x] A6 Failed fetch keeps the previous cache and leaves the previous `Assets` untouched.
- [x] A7 Invalid JSON, empty games, missing NRO, unsafe ids and object-less games are rejected.
- [x] A8 Catalogue larger than 4 MiB is rejected; a cancelled fetch writes no cache.
- [x] A9 Banners download, verify SHA, are reused without a second request and reject bad hashes.
- [x] A10 Launcher version, URL, SHA and size parse correctly.
- [x] A11 `assets.url` missing/blank falls back to the built-in URL; trimming is applied.
- [x] A12 New games, removed games, renamed games, new authors and launcher updates are picked up
      after a refresh without restarting the launcher.
- [x] A13 Catalogue entries with a `path` component install locally; `url` entries download.

## B. Game scanning and row state

- [x] B1 Not installed -> `Install`.
- [x] B2 Installed with matching catalogue version and size -> `Launch`.
- [x] B3 Higher or lower NRO version -> `Update` (downgrades are supported).
- [x] B4 NRO size mismatch (same version) -> `Update`.
- [x] B5 Invalid installed NRO -> `Update` when catalogued, invalid flag when manual.
- [x] B6 RomFS version or image size mismatch -> `Update`.
- [x] B7 Manual install (no `installed.json`) reads the NACP version and is flagged manual.
- [x] B8 Saves-only folder -> `Install` when catalogued, hidden when not catalogued.
- [x] B9 `.`-prefixed and `.removed` folders are ignored.
- [x] B10 Catalogue games without a folder are listed as `Install`.
- [x] B11 `installed.json` missing fields, string components and corrupt JSON are tolerated.
- [x] B12 A game removed from the catalogue stays installed and launchable.

## C. Main menu

- [x] C1 Rows: installed games, optional launcher update row, download row.
- [x] C2 Download count and row order (last used first, then name).
- [x] C3 Authors shown as subtitles; empty authors hide the subtitle.
- [x] C4 A launches a valid game, writes usage and quits.
- [x] C5 A on an invalid NRO shows an error and does not launch.
- [x] C6 A on an install row starts the install flow.
- [x] C7 A on a stale row starts the update flow.
- [x] C8 A on the launcher row starts the launcher update.
- [x] C9 A on the download row opens the download menu.
- [x] C10 X is `Game options` only on installed game rows.
- [x] C11 SELECT opens Settings on every row; B quits at the root; START quits.
- [x] C12 Missing base data shows the launch-anyway prompt; cancel and confirm both work.
- [x] C13 Missing base data adds a warning to install/update prompts.
- [x] C14 Boot prompts for missing or incomplete base data, replacement and auto-extraction.
- [s] C15 Row meta text: version, `Update`, `Install`, `Invalid NRO`, `Needs game data`,
      launcher version, download count.

## D. Download menu

- [x] D1 Fetches the catalogue on first focus and only once.
- [x] D2 Lists only not-installed games with name, author and size.
- [x] D3 Empty state when every catalogue game is installed.
- [x] D4 Install prompt shows download size, required/free space and base-data warning.
- [x] D5 Cancel leaves no files and no metadata.
- [x] D6 Install writes the NRO, `installed.json` and usage, reports and removes the row.
- [x] D7 Install with RomFS writes both components and records both versions.
- [x] D8 Install failure reports an error and leaves no partial files.
- [x] D9 Catalogue fetch failure shows an error and returns to the main menu.
- [x] D10 Cancelled catalogue fetch returns silently.
- [x] D11 B returns to the main menu and refreshes installed rows.
- [x] D12 A new remote catalogue game appears after reopening the page.

## E. Game options sidebar

- [x] E1 Current managed game: `Check for updates`, `Reinstall`, `Verify files`, both deletes.
- [x] E2 Stale managed game: `Update`, `Reinstall`, `Verify files`, both deletes.
- [x] E3 Unmanaged/manual game: `Verify files`, both deletes only.
- [x] E4 `Update` downloads only stale components, reports and refreshes the row.
- [x] E5 `Check for updates` reports up to date, offers an update, or reports network failure.
- [x] E6 A game removed from the catalogue during the check reports `no longer in the catalogue`.
- [x] E7 `Reinstall` re-downloads current components even when nothing is stale.
- [x] E8 `Verify files` reports success, missing files, size/checksum mismatch and missing metadata.
- [x] E9 Refreshing a manual install fills in its checksums.
- [x] E10 Delete confirmation cancel changes nothing.
- [x] E11 `Delete game` removes managed files, keeps saves/config and removes usage.
- [x] E12 `Delete game and save data` removes the whole folder.
- [x] E13 B backs out of the sidebar.

## F. Install engine

- [x] F1 Local copy install; a component already at the destination is verified in place.
- [x] F2 URL install; partial downloads, 404 and truncated bodies leave no file.
- [x] F3 Force install re-downloads both components.
- [x] F4 Missing SHA, bad SHA, wrong size, invalid NRO and missing NRO are rejected.
- [x] F5 RomFS compressed hash, image hash and image size are verified.
- [x] F6 Missing or bad RomFS checksums are rejected.
- [x] F7 Free-space read failure and insufficient space abort before writing.
- [x] F8 Cancellation at download/copy/verify leaves no final or `.part` file.
- [x] F9 A failed first install removes the empty game folder; a failed update keeps the old files.
- [x] F10 NRO succeeds and RomFS fails: NRO metadata is kept and a retry only fetches RomFS.
- [x] F11 A retry after any failure succeeds.
- [x] F12 `InstallTransferSize`, `InstallRequiredSpace` and prompt text differ for install, update,
      force and in-place components.
- [x] F13 Write failure on `installed.json` is reported and does not leave a half install.
- [x] F14 Catalogue cache write failure does not fail the fetch.

## G. Update flows

- [x] G1 Update from the row (A).
- [x] G2 Update from Game options.
- [x] G3 `Update all` updates multiple stale games and reports once.
- [x] G4 `Update all` with nothing stale reports `Everything is up to date.`
- [x] G5 `Update all` stops on the first failure and keeps updated games updated.
- [x] G6 Update-all retry only downloads the remaining game.
- [x] G7 NRO-only update leaves RomFS untouched and does not request it.
- [x] G8 RomFS-only update leaves the NRO untouched and does not request it.
- [x] G9 Republished NRO with a new version but same content still counts as an update.
- [x] G10 Same version + different size counts as an update; same version + same size does not
      (Verify files then detects the checksum change).
- [x] G11 Interrupted/failed update keeps the previous component usable.
- [x] G12 Catalogue refresh from an update check adds/removes rows without a restart.

## H. Launcher update

- [x] H1 No launcher entry -> no row and `Launcher is up to date.`
- [x] H2 Newer launcher -> row and Settings check dialog with version.
- [x] H3 Confirm downloads, validates NRO/version, replaces the launcher and restarts it.
- [x] H4 A on the launcher row starts the same flow.
- [x] H5 Bad SHA, wrong version, invalid NRO and missing URL keep the old launcher.
- [x] H6 Cancelled download keeps the old launcher and leaves no staging files.
- [x] H7 `CleanupPendingInstalls` removes stale launcher/game/banner/cache artifacts.

## I. Settings and About

- [x] I1 Settings without a local catalogue has `Update all`, `Check launcher update`, `About`.
- [x] I2 With `assets.local.json`, `Clear local catalogue` appears; cancel and confirm both work.
- [x] I3 Clearing reloads the cache/bundled catalogue and updates rows immediately.
- [x] I4 About reports version, installed/catalogue counts, base data state, root, build and credits.
- [x] I5 About distinguishes `Ready`, `Ready (dvd_root)`, `Missing` and `Incomplete (problem)`.

## J. Base data UI (stubbed extraction)

- [x] J1 Missing base data with no image explains where to put the image.
- [x] J2 Incomplete base data with no usable image shows the problem.
- [x] J3 Incomplete base data with an image offers re-extraction; cancel and confirm both work.
- [x] J4 Missing base data with an image extracts automatically on boot.
- [x] J5 Extraction failure is reported; cancellation is silent and keeps the state.
- [ ] J6 Actual Wii disc parsing/extraction (needs `wii_disc`; console build only).

## K. Persistence, cleanup and robustness

- [x] K1 `usage.json` is written atomically, tolerates invalid JSON and escapes ids.
- [x] K2 `installed.json` is written atomically and tolerates corrupt/legacy content.
- [x] K3 `CleanupPendingInstalls` removes `.part`, `.new`, `.old`, `.tmp`, `.removed` leftovers.
- [x] K4 Root paths with spaces work.
- [x] K5 Retry after a metadata/IO failure succeeds.
- [x] K6 No test leaves a `.part`, `.new`, `.old` or `.importing` file behind.

## L. Screenshots

- [s] L1 Main menu with installed, update, launcher and download rows.
- [s] L2 Download list with authors and sizes.
- [s] L3 Install confirmation.
- [s] L4 Game update confirmation with partial download size.
- [s] L5 Update-all confirmation.
- [s] L6 Game options for current and stale games.
- [s] L7 Settings with and without a local catalogue.
- [s] L8 About ready and missing states.
- [s] L9 Delete confirmation.
- [s] L10 Catalogue error dialog and empty download page.
- [s] L11 Launcher update dialog.

Screenshots are written to `/tmp/opencode/launcher-screenshots/` by `launcher_render`.
