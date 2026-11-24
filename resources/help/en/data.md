# Data

The Data tab shows what Deepwater records for itself, on this computer. **Macro data** keeps every economic fact the application reads, as linked chains; **Technical data** keeps the market bars of the pairs you choose, with their indicators and order flow.

## Macro data

This page answers a simple question: what did each indicator say, for which period, and was it revised since? The chains are listed on the left; the right side draws the ones you tick, in 2D by default or in one of two 3D views, with a details box under the 2D view.

### Facts, chains and revisions

A **fact** is one value of one indicator for one period, real or forecast. A **chain** links the facts of one indicator, of one kind and from one source, each to the one before. A revised value never overwrites the old one: the revision is a new fact, layered on the block it corrects, so you can always see what was known before.

Each chain shows its kind: **ACTUAL**, **FORECAST**, **CONSENSUS**, **MARKET-IMPLIED** or **PRICE**. The chains fall into six families, each with its own colour. The macro family covers every series of every economy shown in [Economy](help:economy).

| Family | What it records |
|---|---|
| RATES | Policy rates of every economy (whole history, one fact per change), the followed central banks' current rate, their decisions (taken from their statements, or inferred from the rate series: two separate chains) and the market-implied path |
| MACRO | Every macro series of every economy in Economy, whole history |
| CALENDAR | Calendar releases recorded week by week: actual values, Forex Factory consensus, and revisions read from the "previous" figure |
| PRICES | Daily open, high, low and close of every instrument, from Yahoo |
| FORECASTS | Banks' published forecasts by quarter (ING), linked to the market chain they predict |
| POSITIONING | CFTC COT since 1986 (open interest and net positions by category), retail sentiment recordings and the risk index |

### Import and refresh

The first import starts the first time you open this page. It downloads each family's history, and those facts are marked "imported from history". After that, a pass runs at launch for every family whose last pass is 6 hours old or more, then every 6 hours while Deepwater runs, and at once for each new actual value in the calendar. These later passes only add what is new or revised, and their facts are marked "seen live".

The header shows how far the current pass has got (`2 of 6 · Market prices…`), or **Up to date · next pass in …**, along with the totals of chains, facts and revisions. If a family's source is not ready, what was fetched so far is stored and the family is passed again later. Everything is kept in a database on this computer; nothing is sent anywhere.

### Choosing chains

Every word you type in the search field of the left panel must appear in a chain's country or market, indicator, kind, source or key. The family buttons, on two rows, narrow the list to one family, while **ALL** lists them all; each shows its number of chains, and one is active at a time. **✓ TICK ALL** and **✕ UNTICK ALL** tick or untick every chain that the current family and search let through, and a line under them tells how many chains are shown.

Each row gives the subject (a country, bank or market), the indicator and source, the kind, and the number of facts and revisions (`1,204 · 3R`). Clicking a row shows or hides its chain, and the ticks are remembered. When nothing matches, the list reads **No chain matches.**

### The 2D view

The buttons at the top switch between **2D**, the default, and the two 3D views. In 2D each shown chain gets a lane on a shared time axis; the dates at the top stay in place while the lanes scroll, and a dashed line marks today. **Range** sets the span, from 1M through 6M, 1Y and 5Y to 20Y, or ALL, which goes from the first to the last fact of the shown chains. The span is remembered, and every range ends a little after today to leave room for forecasts.

A block sits on its period, and its brightness places the value between the chain's lowest and highest. The blocks of a chain are joined by a line, with the value written inside when the block is wide enough. Revisions are thin orange layers stacked on the block, up to three drawn. Forecasts are dashed blocks on the period they predict, with a dotted line down to the actual value when that chain is shown too. A small green dot means the fact was seen live rather than imported from history.

The wheel zooms around the cursor, from 5 days to 200 years, and dragging moves through time. Hovering a block gives its chain, period, value and number of revisions; clicking it shows its details below. Lanes are read from the database as they scroll into view, so thousands of chains can be ticked. With nothing ticked, the view says **Tick chains on the left to show them here.**

### The details box

Without a selection, the box explains blocks, chains, revisions and forecasts. Once you click a block, it gives the chain and source, the period and value (for a forecast, the period it predicts, the value and the day it was made), when the fact was recorded and whether it was seen live or imported, when it was published or observed, and the source's note.

For a revised period it lists every value the period has had, oldest first, with the day each was recorded. It also shows the neighbours, that is the previous and next values with their periods, and the change. **◀ PREVIOUS** and **NEXT ▶** step through the chain's blocks, and the view follows.

### LANDSCAPE 3D

The landscape lays time across, one box per month, and the shown chains from front to back, grouped in plateaus by family; a forecast chain stands just behind the chain it predicts. Height and brightness give the month's rank in its own chain's history, from 0 for the lowest ever to 100 for the highest. Chains in different units can therefore be compared, and moves that happen together show up as ridges.

Revised months carry a thin orange slab, forecasts are see-through, and translucent planes mark today and the slice date. The **Slice** slider places that date plane, **✕** removes it and **RECENTRE** resets the camera. Drag to turn, right-drag (or Shift + drag) to move, and use the wheel to zoom. Hovering shows the value, a click keeps a block's details, and a double-click opens that chain in 2D around that month, ticking it if it was not.

The landscape follows the ticks. With nothing ticked, it reads **Tick chains on the left (or TICK ALL) to raise the landscape.**

### MACRO CUBE 3D

The cube crosses countries, indicators and time. It is built from every macro chain, whatever is ticked, with one cell per month (or quarter, or year). Its colour is the value's rank in its own series: blue for the lowest ever, grey for the middle, orange for the highest.

**Slice by** cuts the cube by date, country or indicator, and the slider then picks which one, its name shown beside it; the rest of the cube stays as a faint ghost. **RECENTRE** turns the camera to face the slice. The mouse works as in the landscape: drag to turn, right-drag (or Shift + drag) to move, wheel to zoom, hover a cell for its value and rank, double-click to open it in 2D.

Until the import has filled a macro chain, the cube says **No macro chain yet: the import fills them.** Both 3D views are created only when first opened, and that first opening reads the monthly cells (**Reading the monthly cells…**).

## Technical data

This page records the closed market bars of the pairs you choose and shows where their collection stands. The left panel follows the collection of the selected pair, the right panel shows its last bars as stored, and an order flow panel appears when the pair records order flow.

### What is recorded

Each pair keeps its closed bars on the timeframes you choose among 1m, 5m, 15m, 1h, 4h, 1D and 1W, from its first day or from a day you pick, with the indicator columns you select. Those are computed by Deepwater's indicator engine on each series' whole history. BTCUSDT is recorded by default, on all seven timeframes, over its whole history and with 23 indicator columns; if you remove it, it stays removed.

Crypto pairs come from Binance spot, public data that needs no key. They are caught up at launch and kept up every minute; a hole is asked for once more, and whatever is still missing after that is the exchange's own outage. Forex comes from Dukascopy bid prices, also without a key: the seven majors (EURUSD, GBPUSD, USDJPY, USDCHF, AUDUSD, USDCAD, NZDUSD), with history from 2003. The forex trading day runs from 17:00 to 17:00 New York time and is published once it is over, so the newest bars are yesterday's, and there are no bars at weekends.

Binance pairs can also record order flow: footprint history (spot, USD-M perpetual or both), the perpetual's book depth and open interest, and a live book kept in memory only. Everything written to disk goes into one database on this computer, whose size is shown at the top right.

### Following the collection

The pills at the top of the left panel pick the pair shown, and the choice is remembered. Under them come the market and source, the first day, and the number of timeframes and indicator columns.

The table then has one row per timeframe. **TF**, **BARS** and **FROM → TO** give the timeframe, its number of bars and its first and last bar. **INDICATORS** counts the rows computed and turns green when every bar has them. **HOLES** gives the number of gaps and missing bars, in orange while some have not been asked for again yet. **STATE** says where the timeframe stands, and a bar shows how far the history has come, green once it is complete.

The state can be downloading history, updating, waiting, complete (kept up to date, or daily for forex), paused, cancelled, not started, nothing yet, history incomplete, an error, or "Dukascopy refused …: asked again in 10 min".

When the pair records order flow, a second table lists its **ARCHIVES** (footprint spot, footprint perp., depth perp., open interest), with the **DAYS** stored out of those wanted, **FROM → TO**, the days **MISSING**, the **SIZE** per day and the **STATE**. Below the tables, a line says what is downloading or computing right now, with a note when the collection is paused, cancelled, not started yet, or held back by Dukascopy's slow pace.

The collection starts 20 seconds after Deepwater opens, unless Binance or Dukascopy is turned off in [Sources](help:sources), and the pairs take turns.

### Pausing, cancelling and adding pairs

**⏸ PAUSE ALL**, at the top, stops every pair, crypto and forex, until you press **▶ RESUME ALL**; the choice is kept across launches. **⏸ PAUSE** and **▶ RESUME** do the same for this pair only: what is stored stays, and the collection resumes where it stopped.

**CANCEL** goes further. It stops the pair and deletes everything it downloaded, after asking you to choose between **Stop and delete** and **Keep**. The pair stays listed, paused and empty, and **▶ START AGAIN** collects it again from its first day.

**SETTINGS** opens the pair's dialog, described below, and **+ PAIR** adds a new one. With no pair at all, the page reads **No pair recorded: add one with + PAIR.**

### The pair dialog

When you add a pair, you first choose **CRYPTO · BINANCE** or **FOREX · DUKASCOPY**. For crypto, type a Binance spot symbol in **Pair**, for instance `ETHUSDT`. Binance is asked at once and answers whether the pair still trades or is history only, and when its first bar is, or the dialog says "Binance has no spot pair …". For forex, pick one of the majors not yet recorded. A pair already recorded is refused.

**History** starts from the pair's first day or, with **From**, at a date you set. Under **Timeframes**, tick the ones you want. For forex, 4h, 1D and 1W are built from 1h, which is kept with them, while 1m, 5m and 15m need one minute file per day: expect days of downloading.

The **Footprint** section (crypto only) records **Spot**, **Perpetual USD-M** or both, from the first archive (spot 2017-08-17, perpetual 2019-12-31) or from a date, three months back by default. It stores buyers and sellers at every price level, minute by minute, on levels of one basis point (0.01 % of the price), for about 130 KB a day per market.

**Order book and positions** (crypto only) offers three recordings. **Book depth** takes the perpetual's book out to ±0.2, 1, 2, 3, 4 and 5 % every 30 seconds, from 2023. **Open interest** comes with the long/short ratios every 5 minutes, from 2020. **Live book (memory)** keeps the spot and perpetual book and trades in memory while Deepwater runs, never writing them to disk.

**Indicators** lists the columns, each removable with ✕. **DEFAULT LIST** restores the 23 default columns (averages 20, 50 and 200, Bollinger, RSI 14, MACD, ATR, DMI, Stochastic, OBV, VWAP), and **NONE** keeps bars only. With **ADD AN INDICATOR** you pick any indicator of Deepwater's library and set it, and Deepwater checks it on test bars: it is accepted only if it never looks ahead and if recomputing it on new bars gives the values computed over the whole history. Outputs that look ahead are refused, and their button says why. **ADD** puts the indicator in the list; duplicates are refused.

**Size** estimates the bars and disk space for each timeframe. The dialog closes with **ADD THE PAIR** (or **SAVE**), with **CANCEL**, or with **REMOVE THE PAIR**, which deletes every bar and indicator stored for it after asking. Any change that would delete stored data, such as a dropped timeframe, a later first day or a shortened footprint, lists what would go and asks **Delete and save**.

### The last bars

The right panel shows the newest 300 bars as stored, newest first, on the timeframe picked by its pills, which are the pair's own (1h by default). The columns are **time (UTC)**, **open**, **high**, **low**, **close** (green or red), **volume**, **trades** and **taker buy %**, the share of the volume bought by market orders, followed by every indicator column. Forex has no trade count or taker share, shown as —.

Greyed indicator values are still in their warm-up: among the first bars, long averages have not settled yet, as the tooltip explains. A `…` means the indicators have not been computed for that bar yet.

### The order flow panel

This panel appears when the pair records any order flow, with one tab for each kind: **Footprint**, **Book depth**, **Open interest** and **Live book**. It opens on the newest stored day and follows new ones; **‹** and **›** move to the day before or after. If no day is stored yet, wait a little: the collection downloads the newest days first.

The footprint tab switches between **Spot** and **Perpetual USD-M** and shows one row per minute: time, OHLC, volume, bought, sold, delta, trades, levels, POC, value area, buy and sell imbalances, and stacks, each explained by its column tooltip. Click a minute to see its levels on the right. Imbalances use a ratio of 3.

The book depth tab lists the day's snapshots: the bids' share out to ±0.2, ±1 and ±5 %, the cumulative depth by distance (bids −, asks +), the mid price and how far the snapshot reached. The open interest tab has 5-minute rows: contracts open, change, the top traders' long/short ratios (by accounts and by positions), the ratio for all accounts, and the taker buy/sell ratio.

The live book shows spot or perpetual, refreshed every second: the last 10 minutes on screen and 24 hours kept in memory, never on disk. It starts with the collection and shows nothing while the pair is paused.
