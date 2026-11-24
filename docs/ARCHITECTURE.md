# Deepwater — architecture and conventions

Qt 6.12 (`~/Qt/6.12.0/macos`), C++20, CMake + Ninja, macOS first.

    cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$HOME/Qt/6.12.0/macos && cmake --build build

## Layout
- `src/main.cpp` — dev flags: `--page tab/sub`, `--size WxH`, `--screenshot f.png`, `--delay ms`, `--quit-after ms`.
- `src/app/` — `MainWindow` (header + stacked pages, lazy creation), `Header` (76 px: main tabs + sub-tabs + clock),
  `Pages.h` (navigation table, `DW_PAGE("tab/sub", Class)` self-registration).
- `src/core/` — `settings::` (QSettings, test isolation through `DW_PROFILE`), `paths::` (data/cache dirs).
- `src/net/` — `net::get` (one HTTP client: per-host pacing, disk cache with TTL, stale fallback), `sources::`
  registry (every outside source declares host, pace, access and terms next to its code with `sources::Registrar`).
- `src/ui/` — `theme::` (palette tokens, fonts JetBrains Mono + Barlow, style sheet), `ui::` kit (`Panel` with corner
  brackets, `PillBar`, `LiveBadge`, `StatusOverlay`, `flag()`), `fmt::` formats, `chart::` painting helpers.
- `src/market/` — `instruments()` catalogue (127 markets), `QuoteHub` (live quotes, Yahoo spark, 20 s, subscriptions
  per owner, polled only while the owner is visible), `candles()` (Yahoo chart history, cached).
- `src/<domain>/` — data services of one domain (`news/`, `macro/`, `positioning/`, `account/`, `analytics/` …).
- `src/pages/<tab>/` — one folder per header tab, one class per page (plus its private widgets).
- `third_party/quickjs-ng/` — QuickJS-ng v0.18.0 (MIT, see its SOURCES.md): the JavaScript engine of src/charts/core.
- `src/charts/` — C++ port of Quant Terminal (docs/charts/NOTICE.md): the indicators, footprints and Binance streams of
  Data › Technical data. The Charts and AI tabs are not part of Mark 3 (their code is in the repository's history).
- `src/ai/` — the former MCP server and its data tools, the base of Mark 3's own MCP: not built yet.
- `resources/` — embedded at `:/<path>` (fonts, flags, images); files named `SOURCES*` and `*.md` are not embedded
  and hold licences/origins of the assets.

## Rules
- Real data only. No invented numbers. If a value has no free source, show "—" and say why in a tooltip; values the
  app computes itself (scores, gauges) say so ("computed by Deepwater from …").
- New outside source = `sources::Registrar` entry first (with its terms), then code. Respect terms and robots; never
  bypass a block (403, Cloudflare challenge, captcha), never rotate user agents.
- Network only through `net::get` (pacing + cache). Never block the GUI thread: parse big payloads with
  `QtConcurrent::run` and return to the GUI thread before touching widgets.
- Colours from `theme::pal()`, fonts from `theme::mono/sans`. No literal colours except chart series palettes.
- Pages pause timers and animations in `hideEvent`, resume in `showEvent`; `QuoteHub::setActive(this, visible)`.
- Settings keys use dots: `<tab>.<page>.<name>`. API keys: `settings::apiKey("fred")` (stored as `keys.fred`);
  never print, log or screenshot a key.
- Durable user data under `paths::data("<domain>")`, re-downloadable data under `paths::cache(...)`.
- Tests: `tools/shot.sh <page> <out.png> [delay] [size]` runs with `DW_PROFILE` (isolated settings writes and data) so
  the user's preferences and journal are never touched.

## Traps
- A new header with `Q_OBJECT` is not seen by moc until CMake reconfigures: `cmake -S . -B build` (and if the link
  still misses `vtable`/signals, `rm -rf build/Deepwater_autogen`).
- `QCommandLineParser::parse` (not `process`): unknown flags are ignored, page hooks are env vars `DW_*`.
- Yahoo spark: max 20 symbols per request. Weekend: only crypto moves.
- Never wait with `until ! pgrep -f "<words>"`: the waiting shell's own command line contains the words, so the loop
  matches itself and never ends (three such shells ran for 40 min on 2026-11-14). Wait on the PID you started
  (`wait $pid`) or run the build in the foreground.
