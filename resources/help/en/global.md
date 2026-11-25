# Global

The market at a glance: a live quote tape, the real-time wire, risk appetite, currency strength, central banks, a recap of each trading session, and the four FX sessions on your own clock. None of the pages in this tab needs a key or an account.

## Overview

The Overview puts the whole market on one screen. A live quote tape runs along the top, with the session clocks on its right. In a wide window (1340 px and more) the panels sit in three columns: the Squawk on the left, then risk sentiment, currency strength and central banks in the middle, and the global map and asset registry on the right. In a narrower window the page uses two columns, and the map and registry sit side by side on a row below, which you reach by scrolling. In a very small window every panel takes the full width and the page scrolls.

Until its source answers, a panel shows a quiet **CONNECTING…** or **NO DATA**. Deepwater never invents a number, so a missing value shows as **—**.

### Quote tape

Eight markets scroll from right to left: EURUSD, GBPUSD, USDJPY, XAUUSD, WTI, S&P 500, NASDAQ and DOW. Each shows its last price and its change since the previous close, in green when up, red when down and grey when unchanged. The quotes come from Yahoo Finance and refresh every 20 seconds while the page is shown.

On the right, the **New York**, **London**, **Tokyo** and **Sydney** clocks say whether each session is **Open** or **Closed**. They use the same session hours as [Market Hours](help:global), and are hidden when the bar is narrower than 600 px.

### Squawk

This panel is a compact copy of the wire in [News › Squawk](help:news). Its header holds a **LIVE** tag, a description, an **EN / FR** button and the number of **ITEMS**. Just below, a scrolling line runs every item as a continuous ticker (time • text, with price moves in green or red).

Four filter tabs, each with its count, choose what the list shows: **ALL**, **DATA** (headlines, the default), **◇ MOVERS** (price moves) and **RELEASES** (economic data with their actual). Deepwater remembers the tab you pick. Each row gives the local time, the age (**12m ago**…), a type icon (a dot for a headline, a diamond for a price move, bars for a release) and the text. Hovering a row highlights it. The wheel scrolls the list, then the page once the list reaches an end. Unlike on the Squawk page, rows here do not open their source.

**EN / FR** shows the headlines machine-translated into French on this Mac (Apple Translation), with the **AUTO-TRANSLATED** label. Price moves are never translated. The button starts in the interface language and remembers your choice; hover it to see the translator's state. The footer, **Terminal Data · HH:MM:SS**, gives the time of the newest headline. Hover it for the list of feeds, the last fetch time and any feed error.

### Risk Sentiment

The gauge tells you whether the market is taking on risk or shedding it. It runs from 0 to 100: **RISK-OFF** below 40, **NEUTRAL** from 40 to 60 and **RISK-ON** above 60, with the zone's name under the value. The **1W** and **1M** chips give the change since a week and a month ago, with the old value as **(WAS n)**. Below them, **HISTORY · 1 YEAR** plots the daily values of the last 12 months, coloured by zone.

Hovering the title explains the method. The score is the average percentile, against the previous 252 trading days, of seven components computed from Yahoo Finance daily closes: Volatility (VIX), Equity trend (the S&P 500 against its 125-day average), Credit appetite (HYG/LQD), AUD/JPY, Copper / Gold, Stocks vs safe haven, and Crypto momentum. The tooltip lists each component's raw value and percentile, and when the inputs were fetched.

The gauge refreshes when the page opens, then every 5 minutes while it stays open. The full gauge, with its drivers, is in [Positioning › Risk Sentiment](help:positioning).

### Currency Strength

This chart shows which of the eight majors (USD, EUR, GBP, JPY, CHF, CAD, AUD and NZD) is gaining on the others. Under the title **CURRENCY STRENGTH · UNWEIGHTED**, each line is the unweighted mean of one currency's % moves against the seven others, measured from the first bar on screen. Every line therefore starts at 0 %, and the eight values add up to about zero.

Deepwater builds the chart itself from Yahoo Finance candles for the 28 pairs of the eight majors, and the universe dropdown offers that single choice, **FOREX · 8 majors, 28 pairs**. While the pairs load, the chart reads **LOADING n/28 PAIRS…**. If more than 4 pairs fail, it shows **n of 28 pairs unavailable** rather than a biased mean.

The timeframe buttons set the bar size and how much history is loaded, and Deepwater remembers your choice. Between reloads, live quotes move the last bar. Days and weeks follow the London calendar date.

| Button | Bars | History | Reloaded every |
|---|---|---|---|
| **5m** | 5 min | 5 days | 2 min |
| **15m** | 15 min | 1 month | 5 min |
| **1H** (default) | 1 hour | 3 months | 10 min |
| **4H** | 1-hour bars grouped by 4 | 6 months | 30 min |
| **D** | 1 day | 2 years | 30 min |
| **W** | 1 week | 10 years | 60 min |

Hovering the chart draws a crosshair with a dot on each line. The bar's time appears in a pill under the chart and in the subtitle, and the value under the cursor appears on the right axis. The wheel zooms around the hovered bar and re-bases every line to 0 % at the new first bar. A double-click resets the zoom. The ⓘ next to the title explains the method.

### Central Banks

The panel has eight rows: the Fed (USD), ECB (EUR), BoE (GBP), BoJ (JPY), RBA (AUD), RBNZ (NZD), BoC (CAD) and SNB (CHF). Each row gives the policy rate under **RATE** and the date under **NEXT MEETING**. Then come the market-implied probabilities of each possible decision at that meeting, under **CUT**, **HOLD** and **HIKE**, with the most likely outcome in bold. Hover the rate for its publisher and date, and hover the odds to see how they were computed and when they were priced.

Deepwater uses whatever free pricing exists for each bank:

| Bank | Odds from |
|---|---|
| Fed | CME fed funds futures (via Yahoo Finance) |
| ECB | €STR futures (via Yahoo Finance) |
| BoE | its OIS curve |
| BoJ | JPX TONA futures |
| SNB | Eurex SARON futures |
| BoC | Montréal Exchange CORRA futures |
| RBA, RBNZ | a FinancialJuice probability picture read by OCR |

Where there is no free pricing, the odds show **—** and the tooltip says why. [Economy › Rates](help:economy) gives the details meeting by meeting.

### Global Map and Asset Registry

The map is a dot-matrix globe that turns once every two minutes. The four FX-session centres are marked on it in their session colours. You cannot interact with it, and it stops turning when the page is hidden.

The asset registry lists countries by region under the tabs **★ G20**, **AMERICA**, **EUROPE**, **AFRICA**, **ASIA** and **OCEANIA**. Deepwater remembers the chosen tab, and its country count sits at the top right. Each row shows a flag, the country and a badge naming the institution whose policy rate Deepwater uses (**FED**, **ECB**, **BIS**…). Highlighted rows have a policy-rate series from a central bank or the BIS, while dim rows have only World Bank data. Hover a row for the institution's full name or the data note. The wheel scrolls the list.

## Session Recap

A post-mortem of each Tokyo, London and New York session, or of each week: what moved, which data came out and what the wire said. The **RECAP REGISTRY** on the left lists the windows, and the centre shows the file of the one you select. On the right, **RELEASES IN THE WINDOW** sits above **WIRE IN THE WINDOW**. Under 1366 px wide, those two boxes move under the registry. On small screens everything stacks in one scrolling column.

### Sessions and weeks

The recap works on fixed windows, Monday to Friday, in each centre's local time, with daylight saving taken into account. A week runs from Monday's Tokyo open to Friday's New York close.

| Session | Hours (local) |
|---|---|
| Tokyo | 09:00–15:00 |
| London | 08:00–16:30 |
| New York | 08:30–16:00 |

The registry's **SESSIONS** / **WEEKS** tabs switch between two lists. One holds the sessions of the last 62 days, grouped by day. The other holds the last 9 weeks, grouped by month. Both are newest first. Each row gives the session's name (or **WEEK n**), its hours in your time (or the week's dates), and the window's top mover with its change. The newest window opens first. Click any row to open its file.

### Session file

The file's title reads **SESSION FILE · TKY / LDN / NYC / WEEK**, with a flag and a name. Under it, a line gives the date and hours in your time, the **Breadth** (markets up ↑ / down ↓), the number of releases and headlines, and a **LIVE** or **CLOSED** pill.

The **AUTOMATED READ** comes next: a headline, a lead and one paragraph per market group. Fixed rules write it from the figures in the file, with no language model involved. It reads **UPDATED LIVE** while the window is open and **COMPUTED BY DEEPWATER** once the window has closed. It starts in the interface language, and its **EN** / **FR** buttons switch the language for as long as the app stays open.

The **WINDOW TIMELINE** traces one market across the window. By default it traces the biggest mover; click any of the six mover chips above it to trace another. The open is a dashed line labelled **OPEN**, and the window's high and low are marked on the right. Hovering the chart shows a crosshair and a box with the time, the price and the change since the open. Two lanes run under the price. The **RELEASES** lane puts a diamond at each release time: gold for high impact, grey for medium, with a number when several releases share the time. The **WIRE** lane has one bar per 15 minutes (per 2 hours for a week). A bar grows taller with more headlines and shows their count from 2 upward.

**MOVES OF THE WINDOW** ends the file with 12 cards in a 4 × 3 grid: S&P 500, Gold, EUR/USD, Bitcoin, Nasdaq 100, Silver, USD/JPY, Ethereum, Dow Jones, WTI Crude, EUR/GBP and Natural Gas. Each card shows the change from open to close, a sparkline and the **O** / **C** prices. Click a card to trace it in the timeline.

### Releases and wire

**RELEASES IN THE WINDOW** lists the economic calendar's medium- and high-impact releases. Each row has a time, a flag, an impact diamond and the event, then **A** (actual), **F** (forecast) and **P** (previous). Hover a row for the full name and values.

**WIRE IN THE WINDOW** lists the headlines archived during the window. A headline that names a tracked market carries a tag for it (**WTI**, **GOLD**, **BTC**…). Hover a headline for its source and time, or click it to open the source in your browser. Each column scrolls with the wheel.

### Where the data comes from

Prices are 15-minute bars from Yahoo Finance. Deepwater uses index futures, so Tokyo and London have index bars too. Windows that are not over yet reload every 2 minutes, until 20 minutes after their close. Releases come from the economic calendar in [News › Calendar](help:news). Headlines come from Deepwater's own wire archive (FinancialJuice, investingLive, the Fed, the ECB and the Bank of England), which keeps them for 70 days.

Each source has a limit. Yahoo keeps only 60 days of 15-minute bars, so an older window, or one on a market holiday, shows **No 15-minute bars for this window**. The wire archive holds only what Deepwater received while it was running with a wire page open. Earlier windows have no headlines, and the box says so. Calendar weeks from before you installed Deepwater are not available either (see [News › Calendar](help:news)).

## Market Hours

The four FX sessions shown live on your own clock, with their overlaps and a countdown to every open and close. Everything is computed on your computer from the time-zone database, so nothing is downloaded. The page has no controls: it is there to be read.

### Sessions

| Session | Hours (centre's local time) |
|---|---|
| Sydney | 07:00–16:00 |
| Tokyo | 09:00–18:00 |
| London | 08:00–17:00 |
| New York | 08:00–17:00 |

The sessions run Monday to Friday and follow daylight saving. These are each centre's conventional business hours, not exchange hours, and bank holidays are not taken into account. Your time zone is the computer's. The badge at the top right sums it all up: **n/4 SESSIONS LIVE · YOUR CLOCK UTC±h**.

### Your clock

Under the title **YOUR CLOCK**, followed by your time zone, an analogue clock turns on a 24-hour dial with a radar sweep. Coloured arcs on the dial show each centre's current or next session in your time. Open sessions are bright; when every session is closed, the next one to open is bright instead.

Under the dial you find the digital time, the date with your UTC offset, and a status line. The status line names the live session (**… SESSION LIVE**) or the two centres that overlap (**… · … OVERLAP**). When no centre is open it reads **FX BETWEEN SESSIONS**, and from Friday 17:00 New York time to the Monday open it reads **WEEKEND · FX MARKETS CLOSED**.

### 24-hour timeline

**24-HOUR TIMELINE · YOUR TIME** lays out your local day from 00:00 to 24:00. Each session has its own row with today's bars and their hours. An open session glows, and a vertical line marks the present moment. The **OVERLAPS** row underneath shows when two or more centres trade together, labelled with the centres and the hours (for example **LDN · NY**). On a day without an overlap it reads **NO OVERLAP TODAY**.

### Session cards

Each session gets a card with its name, an **OPEN** / **CLOSED** pill and the centre's local time to the second. A **CLOSES IN** or **OPENS IN** countdown follows, with a progress bar while the session is open. The bottom line gives the hours in the centre's local time and the same session in your time.
