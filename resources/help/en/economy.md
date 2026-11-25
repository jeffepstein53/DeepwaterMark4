# Economy

The Economy tab answers two questions: where each of the world's economies stands according to the official agencies (BIS, OECD, World Bank, central banks), and what the markets expect from the eight central banks of the FX majors. Everything on it is either a published value or a calculation made from published values; nothing is estimated or forecast.

## Macro

Macro puts a rotating globe on the left and the **ASSET REGISTRY**, the list of economies Deepwater follows, on the right, with a reset button and three filters under the globe. Clicking an economy replaces the globe with its **ASSET FILE**, the full macro record of that country. On a narrow window held upright, the globe moves to the top and the registry goes below it.

The pill in the top bar, **• N ECONOMIES · N MONTHLY · N LIVE FEEDS**, sums up the coverage. Hovering it spells out the three numbers: the economies in the registry, those with monthly official series, and those with a daily policy-rate feed from their central bank or the BIS (the euro area counts once).

### The globe

The globe turns slowly on its own. It stops while a country is hovered or highlighted, and for 2.5 s after any interaction, so that what you pointed at stays in view. Hovering a country outlines it and its name follows the pointer; dragging left or right rotates the globe.

Clicking a country of the registry opens its Asset File, while countries outside the registry do nothing. The circular arrow at the bottom left brings the globe back to its starting rotation and clears the highlight.

### The registry and its filters

The registry holds 82 economies in six groups: **★ G20** (23 economies), America, Europe, Africa, Asia and Oceania; click a group tab to switch. Each row gives the rank, the flag, the name and a badge telling you where the policy rate comes from: the central bank's code (FED, ECB, BOE, BOJ, SNB, BOC, RBA, RBNZ, with euro members showing ECB), **BIS** when the BIS publishes the rate, or **WB** when only World Bank annual data exists. Rows with a policy-rate feed, from a central bank or the BIS, are highlighted.

Hovering a row turns the globe to that economy and outlines it (for the euro area, every member is outlined), and clicking the row opens the Asset File. The mouse wheel, or a finger drag on a touch screen, scrolls a list longer than the panel.

The three filters under the globe each add their group of economies to the list or remove it:

- **• LIVE FEED**: economies with a policy-rate feed.
- **• MONTHLY**: the other economies whose inflation series is monthly.
- **• ANNUAL**: the rest, with World Bank annual data only. This button looks dim even when it is on.

### The Asset File

The file opens with the flag and the long name of the economy; **×** at the top right or Esc closes it. Its **POLICY RATE** block gives the current rate and two lines telling you how fresh it is: **PUBLIC FEED** (or **BIS FEED**) with the time the feed answered, and **AS OF** with the date of the value and its source. Deepwater uses the central bank's own feed where it follows the bank, and the BIS series otherwise. An economy with neither reads **NO POLICY-RATE SERIES (WORLD BANK DATA ONLY)**.

The **SOVEREIGN YIELD CURVE** draws one bar per maturity for the latest published day; hover a bar for its yield. The curves come from the US Treasury (par curve), the ECB (AAA euro area spot curve), Japan's Ministry of Finance, the Bank of Canada and the Bank of England (5, 10 and 20 years). Other economies show **SOVEREIGN RATES (OECD MONTHLY)** instead, the monthly averages of the 3-month and 10-year rates, or **No sovereign curve published for this economy** when there is nothing at all.

### The macro dashboard

The dashboard lays out the main indicators of the economy in four tabs:

| Tab | Indicators |
|---|---|
| MAIN INDICATOR | Interest rate, GDP q/q, inflation, unemployment, business confidence |
| PRICES | Inflation, core inflation, annual-average inflation, 3-month interbank rate, 10-year government yield |
| GROWTH | GDP q/q, GDP y/y, real GDP growth, retail sales y/y, business and consumer confidence |
| LABOUR | Unemployment rate, ILO unemployment (annual), population |

Each row shows a **TREND** sparkline of the last 24 values, the **LATEST** value with an arrow, the **PRIOR** value and the **PERIOD** the value refers to; for the policy rate, the period is the date of the last decision. The arrow points up when the latest value is above the average of the three before it, and down otherwise.

An **ANNUAL** tag warns you that the value is annual: either the indicator is annual by nature, or the monthly or quarterly series was missing or more than two years old and the World Bank annual series took its place. A row reading **no free official series** has nothing published for that economy. Click a row to open its detail view.

### The advanced cards

Ten cards give the long view: GDP, GDP per capita, population, real GDP growth, inflation, unemployment, current account (% of GDP), government debt / GDP, FX reserves and the 10-year government yield. Each shows the latest value and its date, a sparkline, and the change from the prior value, in % for GDP, GDP per capita, population and reserves and in percentage points for the others.

The change is coloured by whether the move is good or bad for that indicator, so a rise in unemployment shows as bad news. **5Y**, **10Y**, **20Y** and **MAX** set the window of the sparklines, and a card reading **not published for this economy** has no series.

### The detail view

The detail view is where you study one indicator. Its header repeats the indicator, the trend arrow, the latest and prior values and the period; the next release date is not known and shows as —. **← BACK TO DASHBOARD** or Esc takes you back.

**3Y**, **5Y**, **10Y** and **MAX** set the chart's window, and **BAR** / **AREA** its style. **STANDARD DEVIATIONS** adds the mean (μ) and lines at ±1σ and ±2σ, computed over the visible window: a value beyond ±2σ is unusual for that period. **FORECAST** stays disabled, because there is no free source of consensus forecasts.

To set another economy against this one, type any part of its name in **Compare with…**, then pick it or press Enter: its series is drawn as a line over the same periods. Ctrl K (⌘K on a Mac) puts the cursor in the box, and clearing the box removes the comparison.

Drag across the chart to zoom into a period, as many times as you like, and double-click to return to the full window. Hovering the chart gives the period and the value, along with the compared economy's value. The caption under the chart sums up the latest value, its direction versus the prior one, and the range and average over the visible window; hover it for the full text with the source and data set. Finally, **RECORDED HISTORY** lists the last 8 values with their change, in % for level series and as a difference otherwise.

### Where the data comes from

| Indicator | Source |
|---|---|
| Policy rate | BIS policy-rate series (monthly history, daily latest); the central bank's feed where followed |
| Inflation (monthly) | BIS long consumer price series |
| GDP q/q and y/y | OECD quarterly national accounts |
| Unemployment (monthly) | OECD infra-annual labour statistics |
| Business and consumer confidence | OECD, amplitude adjusted (100 = long-term average) |
| 10-year yield, 3-month rate | OECD financial statistics, monthly averages |
| Core inflation | OECD; national series where fresher (ECB/Eurostat, Bank of Canada, SNB, Japan's e-Stat) |
| Retail sales y/y | Computed by Deepwater from the OECD volume index |
| Annual figures | World Bank (WDI) |

History starts in 2000 where the agency has it. The data sets load in the background when the page opens and are asked again once stale: daily BIS rates and yield curves every 3 hours, the other BIS and OECD sets every 12 hours, the World Bank every 24 hours. A failed request may be asked again 5 minutes later, the next time the page opens. While a set is loading, its cells read **loading…**.

Japan's core inflation from e-Stat needs an e-Stat key, entered in the [Sources](help:sources) tab; without it, the OECD series is used. No PMI is shown, because the S&P Global and ISM PMIs have no free source; OECD business confidence is the closest official series.

## Rates

Rates shows what the markets expect from eight central banks: the Fed, ECB, BoE, BoJ, SNB, RBA, RBNZ and BoC. Everything sits in one framed panel, **INTEREST RATE PROBABILITY**, which scrolls down through the summary cards, the bank table, the **TERMINAL DATA** charts, **POLYMARKET** and **COMMITTEES & VOTES**. In its header, the gear's tooltip lists the sources and the chevron collapses or expands the panel; pin, lock, float and close are not available on this page, as their tooltips say.

### Summary cards

The first three cards count the banks priced to hike, to hold and to cut (**PRICED TO HIKE**, **PRICED TO HOLD**, **PRICED TO CUT**), that is, the banks whose most likely outcome at the next meeting is a hike, a hold or a cut, out of the banks that have market pricing. **MEETING THIS WEEK** counts the banks meeting within 7 days, out of 8. The line under the cards names the market used for each bank's odds; hover it for the full text.

### The bank table

The table has one row per bank:

| Column | What it shows |
|---|---|
| BANK | Flag, code, name, and a pill with the most likely outcome: Hike, Hold or Cut |
| RATE | Current policy rate (for the Fed, the upper bound of the target range), with **since** and the date of the last change, or **as of** and the date of the observation |
| NEXT MEETING | First day of the next meeting and a countdown (**in Nd**, **today**) |
| IMPL BP | Implied change at the next meeting in basis points, and its change over one week (**1w**) |
| CUT, HOLD, HIKE | Probabilities of the next-meeting outcomes |
| BIAS | Hawkish, dovish or neutral, with the probability behind it |

A **\*** next to a meeting date means the date was entered by hand from a secondary source; hover it for the bank's page, where you can check it. The bias reads **HAWKISH n** when the hike probability is 10% or more and not below the cut probability, **DOVISH n** when the cut probability is 10% or more, and **NEUTRAL n** otherwise, n being the probability in question, in %.

A row reading **NO FEED** has no usable market pricing; hover it for the reason. Hovering the implied columns gives the pricing source and its caveats. Clicking a row selects the bank for every chart below (the Fed by default) and highlights it in the committees table.

### How the odds are computed

Deepwater computes the odds itself from futures, with the standard method: the expected overnight rate is held constant between meetings and fitted to each contract's average, outcomes move in 25 bp steps, and each meeting is treated independently. In plain words, if the pricing implies a rate 12.5 bp lower after the next meeting, that reads as a 50% chance of a 25 bp cut and a 50% chance of a hold.

| Bank | Market used |
|---|---|
| Fed | CME 30-day fed funds futures (via Yahoo) |
| ECB | CME 3-month €STR futures |
| BoE | Bank of England OIS forward curve |
| BoJ | JPX 3-month TONA futures |
| SNB | Eurex 3-month SARON futures |
| BoC | Montréal Exchange 3-month CORRA futures |
| RBA, RBNZ | FinancialJuice probability pictures read by OCR |

The RBA and RBNZ figures are kept only when the table checks out, that is, when the moves add up to the totals. The ECB futures market is thin, so its odds are less reliable than the others.

### Terminal data charts

The charts follow the bank selected in the table, and each panel has an **i** icon whose tooltip gives its definition. **IMPLIED INTEREST RATE CURVE** starts at today's rate (**Now**) and plots the implied rate after each upcoming meeting, up to 10 meetings: today's rate plus the cumulative implied change. **IMPLIED POINTS** shows that change meeting by meeting, in bp. **NEXT MEETING PROBABILITY** spreads the next meeting over five bars, -50+bps, -25bps, No change, +25bps and +50+bps; hover for a crosshair and the probability.

**ODDS EVOLUTION (TERMINAL)** follows the five outcome probabilities over the last month, which shows how the market's view has shifted. The meeting chips choose which upcoming meeting is charted, and **KEY DATA OFF / ON** marks the decision days of the 8 banks inside the window. Hovering gives a crosshair with the date, the time and the five probabilities. The history comes from hourly futures closes for the Fed, from daily settlements or curves for the others, or from snapshots recorded by the app, in which case it starts empty and builds up.

When a bank has no pricing, the reason appears in place of the charts, with **Loading futures…** while the data loads.

### Committees and votes

This section tells you who decides and how they voted. Each bank has a block with its name, its committee, a note on how it publishes votes, and its head (title and name). Then come up to two of its latest decisions, each with the **MEETING** date, the **DECISION** text, the **VOTE** (**unanimous** when it was, **Not published** for the ECB), the **DISSENT**, and an **Official ↗** link that opens the statement in your browser.

Fed and BoE decisions are read from their official statements. For the other banks they are inferred from the policy-rate series, as a change between before and after the meeting date. A bank with nothing available reads **No decision read yet**.

### Where the data comes from

Policy rates come from the BIS daily series for all eight banks, replaced by the bank's own feed for the Fed (NY Fed), ECB, BoE, BoC and SNB. No key or account is needed. While the page is shown, each part is refreshed once it is older than its limit:

| Part | Refreshed after |
|---|---|
| Policy rates | 30 min |
| Fed and ECB odds | 5 min (15 min for the Fed and 1 h for the ECB when no page shows them) |
| BoC odds | 1 h |
| BoE, BoJ and SNB odds | 2 h |
| RBA and RBNZ odds | 30 min |
| Decisions | 6 h |

Polymarket odds are not shown in this version, and its two panels are placeholders. Deepwater only probes Polymarket, every 6 hours: the panels read **POLYMARKET MARKETS NOT WIRED** when it answers and **POLYMARKET UNREACHABLE** when it does not. The odds above never depend on it.
