# CrossInk — Shared Agent Guide

This is the canonical repo instruction file.
`CLAUDE.md` should point here so Codex and Claude read the same guidance.

Project: Open-source e-reader firmware for ESP32-C3 and ESP32-S3 devices.

## Core Rules

- Role: Senior Embedded Systems Engineer for ESP-IDF / Arduino-ESP32 work.
- Support both constrained ESP32-C3 devices and PSRAM-equipped ESP32-S3 devices. Keep shared code safe for the C3 unless it is explicitly capability-gated; stability beats features.
- Cite file paths and line numbers before proposing non-trivial changes.
- Do not assume ESP-IDF or SDK API availability. Verify in `freeink-sdk/` or the live code.
- Do not claim performance or memory wins without explaining the mechanism, such as reduced heap churn, flash vs DRAM placement, or smaller stack use.
- Justify new heap allocations or explain why stack/static storage is not suitable.
- Explain fixes in plain language where possible, ideally in terms a Node / React developer would follow.
- After proposing or making a fix, say how to verify it on hardware.

## Persistent Context

- Read `.claude/CONTEXT.md` at session start for durable repo-specific gotchas.
- Keep `.claude/CONTEXT.md` short. Add only reusable findings, not turn-by-turn history.

## Repo Skills

- Do not read every `.claude/skills/*/SKILL.md` at session start.
- Use this section as an index. Read a local skill only when the task clearly matches its folder name or purpose.
- Current local skills:
  - `control-flow-clarity`: simplify confusing logic without behavior changes.
  - `refactor-for-review`: small refactors intended to reduce review risk.
  - `hal-and-abstractions`: HAL boundaries and platform abstraction work.
  - `heap-discipline`: memory allocation, lifetime, and fragmentation-sensitive work.
  - `scope-discipline`: keep changes narrow and avoid unrelated cleanup.
  - `custom-fonts`: font generation, conversion, and SD/built-in font work.
- Treat these as task-specific playbooks layered on top of this guide. If a skill conflicts with this file, prefer `AGENTS.md` and note the conflict.

## Hardware Constraints

- ESP32-C3 targets (Xteink X3/X4): single-core RISC-V at 160 MHz, no PSRAM, and about 380 KB usable internal RAM.
- ESP32-S3R8 targets (Seeed reTerminal Sticky/Xteink X4 Pro): dual-core Xtensa at up to 240 MHz with 8 MB PSRAM. PSRAM is slower than internal DRAM and is not suitable for every DMA, ISR, or latency-sensitive buffer.
- Current displays use an 800x480 1-bit e-ink framebuffer: `800 * 480 / 8 = 48000` bytes. Use runtime renderer dimensions because orientation and future device profiles may differ.
- Use one framebuffer only. C3 targets keep it in internal RAM; current S3 targets place it in PSRAM via `FREEINK_FB_PSRAM`.
- Storage is exposed through SdFat, but the transport is device-specific (SPI SD on X3/X4/Sticky and SDMMC on X4 Pro). On real hardware, only one reader can hold a file open at a time.

## Resource Rules

1. Keep local stack usage small. Anything meaningfully larger than 256 bytes should be justified.
2. Avoid repeated heap churn in loops. Allocate once in `onEnter()`, reuse, and free in `onExit()`.
3. Large constant tables should be `static const` so they live in flash, not DRAM.
4. Avoid `std::string` and Arduino `String` in hot paths. Prefer `string_view`, `char[]`, and `snprintf`.
5. All user-facing UI strings must use `tr(STR_*)`. Logs may be hardcoded.
6. Prefer `constexpr` for compile-time constants.
7. Reserve `std::vector` capacity before push loops.
8. Debounce persistent writes. Do not write progress on every page turn.
9. `new` is not nothrow on ESP32. With exceptions disabled, bare `new` calls `abort()` on allocation failure instead of returning `nullptr`. Use `new (std::nothrow)` or `makeUniqueNoThrow<T>()` from `lib/Memory/Memory.h` for fallible allocations.
10. Prefer `makeUniqueNoThrow<T>()` / `makeUniqueNoThrow<T[]>()` for owned heap allocations so cleanup is automatic on early returns.
11. Use raw `malloc` or `new (std::nothrow)` only when a C or SDK API takes ownership; add a short comment explaining that ownership transfer.
12. Treat PSRAM as a device capability, not a universal assumption. Keep shared paths within C3 limits or gate S3-only allocations behind the relevant board/capability macro, and handle PSRAM allocation failure.

## HAL And Platform Rules

- Use HAL classes, not SDK classes, in app code.
- File I/O uses `FsFile`, not Arduino `File`.
- Always close files explicitly.
- Use `MappedInputManager::Button::*` enums for button logic.

## C++ / Embedded Gotchas

- `string_view::data()` is not null-terminated. Do not pass it directly to C APIs.
- ISR handlers need `IRAM_ATTR`, and ISR-read data must be in DRAM, not flash-only storage.
- Never call `xSemaphoreTake()` from an ISR. Use ISR-safe give APIs.
- Do not cast unaligned `uint8_t*` data to wider pointer types. Use `memcpy`.
- No exceptions. No `abort()`. Log before returning failure.
- Avoid `std::function` in hot paths and library code; prefer function pointers or a small context/callback struct.
- Keep template use deliberate. If a template is needed in shared code, consider explicit instantiation in a `.cpp` file to avoid repeated binary growth.

## Error Handling

- Prefer `LOG_ERR(...)` plus `return false` for recoverable failures.
- Prefer `LOG_ERR(...)` plus a known fallback when the app can continue safely.
- Use `assert(false)` only for truly impossible fatal states.
- Use `ESP.restart()` only for intentional recovery flows, such as completing OTA.
- Always log before returning failure from allocation, file, parse, network, or hardware paths.

## Activity Lifecycle

- Activities are heap-allocated and deleted on exit.
- Allocate long-lived buffers and tasks in `onEnter()`.
- Free resources in reverse order in `onExit()`.
- Delete FreeRTOS tasks before the activity is destroyed.
- Close open file handles in `onExit()`.
- Typical task stacks:
  - 2048 bytes for simple rendering work
  - 4096 bytes for network or EPUB parsing work

## UI And Input

- Do not hardcode screen dimensions like `800` or `480`; use renderer dimensions and orientation helpers.
- Use `renderer.getOrientedViewableTRBL()` for layout that must stay inside usable bezel-safe bounds.
- Use logical `MappedInputManager::Button::*` values in activities; raw hardware button indices belong only in button-mapping code.
- Route UI drawing through `UITheme` / `GUI` where practical so fonts, spacing, and orientation behavior stay consistent.
- User-facing text must be translated with `tr(STR_*)`; logs can remain hardcoded.

## Build And Verification

- PlatformIO is the source of truth. Personal overrides belong in `platformio.local.ini`.
- Host environment may be macOS, Linux, WSL, or Windows Git Bash. Check `uname -s` before recommending platform-specific shell commands.
- Logging uses `LOG_INF`, `LOG_DBG`, and `LOG_ERR`.
- The simulator env in this repo is `simulator`.
- For simulator work, build from this firmware repo unless the change belongs in `crossink-simulator` itself.
- Common validation commands:
  - `pio run -e simulator` for simulator-facing UI/reader work.
  - `pio run -e default` for the ESP32-C3 X3/X4 firmware.
  - `pio run -e sticky` for the ESP32-S3 Sticky firmware.
  - `pio run -e x4-pro` for the ESP32-S3 X4 Pro firmware.
  - `pio check -e default --fail-on-defect low --fail-on-defect medium --fail-on-defect high` for static analysis.
  - `find src lib include test -name "*.cpp" -o -name "*.h" | xargs clang-format -i` for formatting touched C++ files.
- For crash debugging, check serial logs, internal heap with `ESP.getFreeHeap()` and `ESP.getMaxAllocHeap()`, task stack high-water marks, and whether cache files need clearing. On S3 targets, also inspect PSRAM free space and largest allocatable block; abundant PSRAM does not prove that internal-RAM or DMA-capable allocations can succeed.
- Hardware verification should mention the concrete device path to test, expected UI/log behavior, and any cache reset needed.

## Generated Files

- Do not edit generated files directly.
- Web portal headers under `src/network/html/*.generated.h` are built by `scripts/build_web.py` from sources in `web/`: pages compose `web/templates/base.html` (shared chrome) with `web/pages/<slug>.{html,css,js}`, plus shared assets `web/assets/style.css` (served at `/style.css`) and `web/assets/logo.png` (served at `/logo.png`). Edit the `web/` sources, never the generated headers.
- I18n generated files under `lib/I18n/` come from `lib/I18n/translations/*.yaml` via `scripts/gen_i18n.py`.

## Cache Format

- EPUB cache lives under `.crosspoint/epub_<hash>/`.
- If you change binary cache layouts, bump the format version first and document it in `docs/file-formats.md`.
- Cache identity is tied to the book path hash; moving or renaming a book creates a different cache.
- Clear the relevant `.crosspoint/epub_<hash>/` cache when testing EPUB parser, layout, image, or binary cache format changes that may otherwise reuse stale output.

## KoInsight Stats Sync

This fork uploads per-page reading statistics to a self-hosted
[KoInsight](https://github.com/Ko-Insight/KoInsight) server through its
KOReader-plugin import API, so CrossInk reading time combines with KOReader in
the same dashboard. User-facing docs: `docs/koinsight-stats-sync.md`.

### Data flow

1. **Capture** — `EpubReaderActivity::queueKoInsightPageEvent()` records a page
dwell event in RAM whenever `recordCurrentPageReadingTime()` qualifies a page
(`currentPageReadingSecondsForStats()` passes; see Filters below).
2. **Flush** — the RAM buffer is appended to the per-book queue file
(`<cachePath>/koinsight_pending.bin`) at `KOINSIGHT_SESSION_FLUSH_THRESHOLD`
(256, keeps RAM ~4KB) and again in `EpubReaderActivity::onExit()`.
3. **Upload** — `KOReaderSyncActivity::uploadKoInsightStats()` runs after a
successful progress sync while WiFi is up (all 4 success paths: upload,
smart-apply, manual apply, already-synced). `KoInsightUpload::syncBookStats()`
registers the device, then drains the queue in batches (≤64 events/request,
≤8 batches/run); events are consumed locally only after the server returns 200.
4. **Reset** — `BookReadingStats::remove()` also deletes the pending queue so a
stats reset doesn't resurrect deleted history server-side.

### Module map (`lib/KoInsightSync/`)

- `KoInsightSettings` — persisted store at `/.crosspoint/koinsight.json`
  (`enabled`, `serverUrl`; empty URL falls back to the KOReader sync base URL,
  because KoInsight also serves the kosync API). `KOINSIGHT_STORE` macro.
- `KoInsightEventLog` — append-only-in-spirit queue. Records are 16-byte LE
  (`startTime u32, duration u32, page u32, totalPages u32`) behind an 8-byte
  header (`KIPQ` + version + reserved). `MAX_EVENTS = 2000` (32KB); on overflow
  the oldest events are dropped. Codec is header-inline so host tests can build
  it without HalStorage/Logging.
- `KoInsightClient` — HTTP via `freeink::SecureHttpClient` + ArduinoJson.
  `PLUGIN_VERSION` **must equal KoInsight's `REQUIRED_PLUGIN_VERSION`
  (currently `0.3.0`)** or the server rejects every request with 400 — this is
  the single most likely breakage when KoInsight upgrades. Device id is
  `crossink-<mac-hex>` from `esp_efuse_mac_get_default`; never reuse the static
  kosync `"crossink-device"` id. TLS heap-gated like the KOSync client.
- `KoInsightUpload` — orchestrator; per-batch `pages` comes from the newest
  event's `totalPages`. Failures never affect progress sync; the queue survives
  for the next attempt (server upserts are idempotent).

### Invariants (do not break)

- Books are always keyed by the **binary partial MD5**
  (`KOReaderDocumentId::calculate`), independent of the kosync document-matching
  setting, so CrossInk and KOReader rows unify into one KoInsight book. Row
  uniqueness is per-device: `page_stat (book_md5, device_id, page, start_time)`,
  `book_device (book_md5, device_id)` — CrossInk rows can never clobber
  KOReader's.
- `total_pages` must be the **whole-book** page count, never a spine count:
  KoInsight's pages-read math is `(1/total_pages) * reference_pages` per event.
- Uploads are idempotent; consume queue only after a 200. If local consume
  fails after a successful POST, re-sending is harmless.

### Pagination logic (`queueKoInsightPageEvent`)

- Indexed books (`epub->hasStablePageNumbers()`): `resolveReferencePage()` gives
  KOReader-style word-count pages — real whole-book `page` + `total_pages`.
- Unindexed fallback: `total_pages` estimated from the spine's share of the book
  (`spinePages / (calculateProgress(spine,1) − calculateProgress(spine,0))`) and
  `page` derived in whole-book coordinates from the same progress fraction
  (clamped 1..total_pages), so page numbers don't restart per chapter.
- Both must stay whole-book; never send `section->currentPage + 1` /
  `section->estimatedTotalPages()` (spine-relative) as the event values.

### Filters

- Dwell `< MIN_KOINSIGHT_PAGE_SECONDS` (10s) is dropped in
  `queueKoInsightPageEvent` (mirrors the stats panel's "under 10s doesn't count").
- Dwell `> SETTINGS.getReadingIdleTimeThresholdSeconds()` is already rejected by
  `currentPageReadingSecondsForStats()` before capture (keep that behavior).
- Events with an unset clock (`time(nullptr) < 946684800`) are dropped.
- Only EPUB reading is captured (XTC/TXT readers have their own dwell paths and
  no sync hook; adding them means recording + a flush path, plus the sync
  activity currently only accepts `epubPath`).

### Settings UI

- `SettingsList.h` (web settings) + `KOReaderSettingsActivity` (device screen):
  "KoInsight Stats Sync" toggle + "KoInsight Server URL".
- The device screen is an index-based menu (`MENU_ITEMS`, `menuNames[]`,
  `handleSelection`, `buildListScreen`) — when adding/removing rows, keep the
  indices in sync across all four places.
- New `STR_*` keys go in `lib/I18n/translations/english.yaml` only;
  `I18nKeys.h` is regenerated by `scripts/gen_i18n.py` at build time.
- Settings load at boot in `main.cpp` (both the normal and network-resume
  paths), alongside `KOREADER_STORE.loadFromFile()`.
- Pre-provision on the SD card: `/.crosspoint/koinsight.json`
  (`{"enabled": true, "serverUrl": "http://<host>:3005"}`). A gitignored local
  copy lives at `provision/sdcard/.crosspoint/koinsight.json` (never commit it).

### Storage gotchas

- `Storage.openFileForWrite` truncates (`O_TRUNC`); there is no append mode, so
  the queue is read-modify-written. Keep it small and cap reads in
  `KoInsightEventLog` (`readAll` rejects files > 4x MAX_EVENTS).
- Per-page SD writes are avoided by buffering in RAM and flushing at the
  threshold + on exit.

### Verification

- Host unit tests (codec only, no storage): `test/koinsight_event_log/`
  (gtest, wired into `test/CMakeLists.txt`; run via cmake build of
  `test/koinsight_event_log/KoInsightEventLogTest`).
- Live API check: `scripts/koinsight_api_test.sh` posts device registration +
  import + idempotency + KOReader-unification + version-gate checks to
  `KOINSIGHT_URL` (default `http://zai:3005`). It writes labeled test rows; the
  user deletes them.
- On-device: read past a chapter boundary, run KOReader Sync on the book, check
  KoInsight for the `crossink-<mac>` device and per-page rows. Serial logs under
  tag `KNS` show capture, flush, request/response, and drain progress.

### Releases

- `main` on the fork is the release branch. Push a `v*` tag and
  `.github/workflows/release-fork.yml` builds both firmware variants and
  attaches them to a release with auto-generated changelog notes, using only
  the default `GITHUB_TOKEN` (no secrets).
- Upstream's `.github/workflows/release.yml` (manual dispatch + secret-gated)
  will not run on the fork; leave it untouched to avoid merge conflicts.
- Keep the firmware's `PLUGIN_VERSION` in lockstep with the KoInsight server's
  required version when bumping releases.

## Git Workflow

- Check `git status --short` before edits and before reporting results. Preserve unrelated user changes.
- When resolving merge, rebase, or cherry-pick conflicts, inspect the relevant commit messages for upstream PR references such as `#2608`. Open the PR in its source repository and read its description and changed files before resolving the conflict so the intended behavior is understood.
- Do not resolve conflicts by automatically keeping CrossInk's current implementation or by discarding the upstream change wholesale. Preserve or adapt the upstream intent unless it is already fully implemented, would introduce a regression, or would substantially and unjustifiably change CrossInk's UX or behavior. When rejecting an upstream change, state the concrete reason.
- If a referenced PR cannot be accessed, inspect the source commit diff and nearby history, then report that the PR intent could not be verified instead of guessing.
- Do not commit unless the user explicitly asks or committing is part of the skill utilized.
- Before staging, ensure ignored/generated/local files such as `.pio/`, `*.generated.h`, `compile_commands.json`, and `platformio.local.ini` are not included.
- Branch names should use repo-style prefixes such as `feat/`, `fix/`, `docs/`, `refactor/`, `test/`, or `chore/`.
- Suggested commit messages should follow `<type>: <short summary>`, using types like `feat`, `fix`, `docs`, `refactor`, `test`, `chore`, or `perf`.

## Changelog

When new features are added or issues are fixed, make sure to add an entry to `CHANGELOG.md` with the user-facing description of the change. Types of changes should have their own section.

### Changelog Guiding Principles

- Changelogs are _for humans_, not machines.
- There should be an entry for every single version.
- The same types of changes should be grouped.
- Versions and sections should be linkable.
- The latest version comes first.
- The release date of each version is displayed.

### Types of Changelog Changes

- Added - for new features.
- Changed - for changes in existing functionality.
- Deprecated - for soon-to-be removed features.
- Removed - for now removed features.
- Fixed - for any bug fixes.
- Security - in case of vulnerabilities.
