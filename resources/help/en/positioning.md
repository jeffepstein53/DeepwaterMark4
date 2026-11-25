# Positioning

The Positioning tab shows who holds what: how retail traders split between long and short, what the CFTC's weekly Commitments of Traders report says about futures traders, which way the banks' published forecasts point, and how much risk the markets are willing to take.

## Retail Sentiment

Retail Sentiment shows how retail traders are positioned on each instrument, from the Myfxbook community outlook. The point is to spot a lopsided crowd: the summary cards point you to the most one-sided instruments, the table lists them all, and each instrument opens a window comparing the crowd with the price.

### Connecting your account

The outlook is only available with your own Myfxbook account. You can connect in the page, on the **CONNECT MYFXBOOK** card (email, password, then **CONNECT**, or Enter in the password field), or in the [Sources](help:sources) tab. Once data is loaded, **CLOSE** hides the card. Login errors appear in the card.

Your password is sent once to Myfxbook over HTTPS to open a session and is never stored. Only the session token is kept, and it is cleared when you log out or when it expires.

The badge at the top right tells you where you stand. Connected, it reads **MYFXBOOK · NEXT UPDATE hh:mm · LOG OUT**, and a click logs you out. Otherwise it reads **MYFXBOOK NOT CONNECTED · CONNECT** or **MYFXBOOK SESSION EXPIRED · CONNECT**, and a click opens the login card.

### Summary cards

The cards give the overall picture before you look at single instruments; hover one for its definition. **CROWDED LONG** and **CROWDED SHORT** count the instruments where 65% or more of retail positions are long (or short), out of all listed instruments. **MOST CROWDED LONG** and **MOST CROWDED SHORT** name the instrument with the highest long (or short) share, and give that share.

**TRACKED POSITIONS** is the number of open positions in the outlook, all instruments together. **CROWD LEAN** is a bar computed by Deepwater: long positions as a share of all positions, over every instrument.

### Filtering and sorting

These controls narrow and order the table:

- **Filter…** keeps the symbols containing the text you type (a slash is ignored).
- **All**, **FX**, **Commodities** and **Crypto** pick an asset group.
- **Custom** opens a menu where you tick the instruments of your own list, with **Show my custom list only**.
- **Majors first**, **Most extreme** (furthest from 50%), **Most active** (most positions) and **A-Z** set the order inside each group.

Group, sort and custom list are remembered.

### The instrument table

Instruments are grouped by FX, Commodities, Crypto, Indices and Other, each group with its count. Each row gives the **SYMBOL**, a **POSITIONING** bar split between long and short, the **LONG** and **SHORT** shares, the **24H Δ LONG**, the number of **POSITIONS** and a **STATUS**, which reads the crowd by thresholds:

| Status | Rule |
|---|---|
| CROWDED LONG | Long ≥ 65% |
| LEANING LONG | Long > 50% |
| BALANCED | 50% |
| LEANING SHORT | Short > 50% |
| CROWDED SHORT | Short ≥ 65% |

The 24-hour column is the change, in points, of the long share since the app's own recording from about 24 hours earlier (give or take 3 hours). It reads **flat** when nothing moved and **—** as long as no such recording exists; hover it for both values and the time of the recording. Click a row to open the instrument window.

The footer gives the time of the last answer and a **DELAYED** badge. While the app's history covers less than 20 hours, it also shows how many snapshots have been recorded so far.

### The instrument window

The window opens on the pair, its full name and its status; ×, Esc or a click outside closes it. Three chips set the crowd against the market: the average entry of retail longs, the average entry of retail shorts, and the last price (the live quote, else the last 1-hour close).

The price chart uses delayed Yahoo Finance candles: 1-hour candles for **5D**, **2W** and **1M**, daily candles for **3M**, **6M** and **1Y**. Hover it for a crosshair and the OHLC values; the clock at the bottom right shows your local time and its UTC offset.

**LONG / SHORT DISTRIBUTION** is a pie of the two shares. **RETAIL POSITIONING STATISTICS** lists long, short and total positions, the average long and short prices, the long and short ratios, and the long and short volume in lots. Its last four lines are computed by Deepwater rather than reported by brokers (hover them): **Price vs Entry** is the live quote minus the average long entry, and the average short entry minus the quote, so a positive figure means that side is in profit on average; **Est. avg P&L** gives the same in %.

**LONG / SHORT HISTORY** plots the long share at each recorded snapshot, up to the last 150. **OPEN INTEREST** shows the CFTC weekly open interest of the matching futures contract over the last 26 weeks; only pairs against the USD, gold, silver and bitcoin have one, and crosses say why not. **VOLUME** and **POSITION** draw longs upward and shorts downward at each snapshot, each side scaled to its own range.

Under the charts, a written summary describes the state of the crowd, where each side stands against its average entry and what has changed since the app's first snapshot, followed by the sources.

### Where the data comes from

Myfxbook is asked at most once every 30 minutes, and only while a page showing this data is open. Every answer is recorded on this computer, and the 24-hour changes and the history charts are built from these recordings: they start empty and fill up as the app runs. The last 60 days are loaded at launch.

## COT

COT reads the CFTC's weekly Commitments of Traders report, which tells you how each category of futures traders, from commercial hedgers to funds and small traders, is positioned. The **COT Advanced** header carries the asset class (currencies, commodities or indices), with the market buttons under it and, at the right, the range used by the split and net charts: **6M**, **1Y**, **2Y** or **3Y**.

The page reads in two rows. **SUMMARY** holds the price, the weekly split and the long:short donut; **ADVANCED** holds the net charts, the report tables, the sector donuts, the stats and the automated read. Asset class, market, range and split category are remembered.

### The markets

| Class | Markets | Second report |
|---|---|---|
| Currencies | USD (ICE dollar index), EUR, GBP, JPY, CHF, CAD, AUD, NZD | Traders in Financial Futures (TFF) |
| Commodities | Gold, silver, copper, WTI, natural gas, corn, wheat, soybeans | Disaggregated |
| Indices | S&P 500, Nasdaq 100, Dow, Russell 2000, Nikkei, VIX, bitcoin | TFF |

Every market also has the Legacy report, which splits traders into Commercials, Large Speculators and Small Traders.

### Price

The price panel shows one year of daily candles with the last close and its daily change; hover for a crosshair, or hover the panel title for the price source. The bottom strip plots open interest over five years, with its latest value and weekly change.

For currencies you can choose what to look at. **Unweighted** shows an index computed by Deepwater, the geometric mean of the currency against the seven other majors, set to 100 on the first day of the year; **Futures** shows the continuous future. Other markets always show the future.

### Long:short split and net positions

The weekly split tells you how one category of traders is leaning. Its buttons pick one of the four categories of the second report (Dealer, Asset Manager, Leveraged Funds and Other Reportables for TFF; Producer/Merchant, Swap Dealers, Managed Money and Other Reportables for Disaggregated) or **Nonreportable**. Each bar is one week and shows the long share of that category; hover for the date and the long and short percentages. The **LONG:SHORT** donut gives the same category in the latest report.

The net charts follow long minus short over the chosen range. **LEGACY NET** draws Commercials, Large Speculators and Small Traders; **TRADERS IN FINANCIAL FUTURES NET** or **DISAGGREGATED NET** draws the four categories of the second report; and **NONREPORTABLE (SMALL TRADERS) NET** draws the small traders.

### Report tables, sectors and stats

The two tables, **LEGACY REPORT COT** and **TFF / DISAGGREGATED REPORT COT**, give the figures behind the charts. Their title line holds the open interest, its weekly change and the total number of traders; then each category gets a row:

| Column | Meaning |
|---|---|
| Long, short | Positions held |
| CHG | Weekly change |
| %OI | Share of open interest |
| TRDRS | Number of traders |
| Spreading | Spreading positions |
| L-S | Net position |

**LONG:SHORT SECTOR** draws one donut per category for the latest week. **STATS** gives each category's (long − short) / (long + short), as a bar and in %: +100% would mean all long, −100% all short.

### The automated read

The automated read puts the report into words. It is written by fixed rules from the CFTC figures, with no AI involved, as a headline, a paragraph and three bullets. Its key notion is the extreme: a category is "at an extreme" when its net position sits in the top or bottom 10% of its 52-week range.

The headline names the categories at an extreme, or failing that the biggest weekly move. The paragraph covers the two most extreme categories (their net, their place in the range and their weekly move) and the change in open interest. The bullets give the most extreme category, the biggest weekly move and the long share of leveraged funds or managed money.

The right-hand column places each category's net position on a marker within its 52-week range, and highlights the crowded ones. **EN** / **FR** switch the language of the read only; it starts in the interface language and is not remembered.

### Where the data comes from

The data comes from the CFTC's public reporting API, futures only: about five years of weekly reports per market, kept for 6 hours. Once a report is due (Friday 15:30 New York time) and missing, it is asked again at once, then every 10 minutes for three hours and every hour after that. Positions are as of Tuesday and published on Friday at 15:30 New York time. The footer shows the release time; hover **OFFICIAL** for the report and release dates. Prices are Yahoo Finance daily candles, and no key is needed.

A new report shows by itself while the page is open. Coming back to the page reloads the market when its data is too old or the last load failed. Messages above the panels report a CFTC error, one of the two reports missing (some panels then stay empty), or **CFTC unreachable — showing the cached report**.

## Bank Positioning

Bank Positioning turns the banks' published forecasts into market calls, so you can see which way the institutions lean on each market. The header holds **FX BANK FORECASTS**, which opens the forecasts window, the two views **DESK** and **ALL CALLS**, and a badge with the number of bank notes archived (from [News](help:news) › Bank Research) and the date the forecast tables were last revised. The desk shows the market cards, then the **HOW TO READ** and **BANKS** notes; on very wide windows the full list of calls follows under the cards.

### What a call is

A call is computed by Deepwater, not taken from a bank. For each bank, Deepwater takes its published forecast for the nearest quarter end that has not passed and compares it with today's price. A forecast above spot by more than the class's threshold gives **LONG**, one below by more than it gives **SHORT**, and anything within the threshold gives **RANGE**.

| Class | Threshold |
|---|---|
| FX | 1% |
| Asia & EM FX | 1.5% |
| Commodities, indices | 3% |
| Crypto | 5% |
| Rates | 0.15 yield point |

Rates are read as bond calls: a forecast yield more than 0.15 point below today's yield is LONG, since bond prices rise when yields fall, and one more than 0.15 point above is SHORT.

### Market cards

There are six cards, one per class: FX, Asia & EM FX, Commodities, Indices, Rates and Crypto. Each counts the LONG, SHORT and RANGE calls, shows them as a long / range / short proportion bar, and ends with **positions · banks · markets covered**, where markets covered means the markets with at least one call, out of the class's markets.

Hovering a card lifts it, and its tooltip gives the rule and threshold. Clicking it opens **ALL CALLS** at that class.

### All calls

All calls is the full list, grouped by class:

| Column | Meaning |
|---|---|
| MARKET | The market called |
| BANK | The bank whose forecast it is |
| CALL | LONG, SHORT or RANGE |
| HORIZON | Quarter of the forecast |
| FORECAST | The bank's forecast |
| SPOT | Today's price |
| TO TARGET | % change to the forecast, or yield points for rates |
| TABLE REVISED | Date the forecast table was revised |

A class without calls reads **— No forecast from the banks Deepwater may read for these markets**. The mouse wheel scrolls the list smoothly.

### The FX bank forecasts window

This window sets the banks' FX forecasts against the price. Pair pills along the top list the pairs, the most covered first; when they do not all fit, a **+N** pill opens a menu with the others.

On the left, a table gives each bank (**INSTITUTION**, with its **LAST REVISED** date) across six quarters: the current one, the next four, and Q4 two years ahead. On the right, one year of daily candles shows the pair with its live price, and one dashed line per quarter marks the average of the banks' forecasts; quarters with the same value share one line.

Click a bank row to see that bank's own forecasts as lines, and click it again to go back to the averages. The quarter pills show or hide each quarter's line, and **× CLOSE** or Esc closes the window.

### Where the data comes from

The forecast tables are read from ING THINK, which covers FX, commodities and US Treasuries and allows reuse with attribution. TD, MUFG, KBC, Scotiabank and Commerzbank are not read, because their terms forbid reuse or automated access.

The forecasts are asked again when the page opens, at most every 6 hours; if ING THINK cannot be reached, the reason is appended to the **BANKS** note. Spot prices come from the app's live quotes and update the calls while the page is shown.

## Risk Sentiment

Risk Sentiment measures how much risk the markets are willing to take, as a single score that Deepwater computes from seven markets, shown with its one-year history next to the S&P 500 and the VIX.

### The composite gauge

**COMPOSITE RISK APPETITE** runs from 0 to 100: low values mean the markets are seeking safety, high values that they are reaching for risk.

| Zone | Score |
|---|---|
| EXTREME OFF | Below 20 |
| RISK-OFF | 20–39 |
| NEUTRAL | 40–59 |
| RISK-ON | 60–79 |
| EXTREME ON | 80 and above |

The needle sits at today's value, and two marks on the scale, **1W** and **1M**, show where it stood 5 and 21 trading days ago. The pills give the change over one week and over one month, in points, with the earlier value (**WAS n**).

### The components

Each component's reading of the day is ranked against its own previous 252 trading days, as a percentile from 0 to 100, oriented so that higher always means more risk appetite. A percentile of 90 means the reading is more risk-on than on 90% of those days. The composite is the plain average of the components available that day.

| Component | Reading |
|---|---|
| Volatility (VIX) | VIX level, inverted (lower volatility = more appetite) |
| Equity trend | S&P 500 versus its 125-day average, % |
| Credit appetite | High-yield / investment-grade bond ETFs (HYG/LQD), 20-day change of the ratio |
| AUD/JPY | 20-day change |
| Copper / Gold | 20-day change of the ratio |
| Stocks vs safe haven | S&P 500 20-day return minus gold's |
| Crypto momentum | Bitcoin 20-day change |

Each row shows the raw reading, the percentile and a bar, and hovering a row highlights it. A component whose last reading is more than 10 days old is left out and shows **—**.

### The charts

**COMPOSITE HISTORY · 1 YEAR** plots the composite on the S&P 500's trading days over zone bands: risk-off below 40, neutral from 40 to 60, risk-on above 60. Hover for the date and value. **S&P 500 · 1 YEAR** and **VIX · VOLATILITY INDEX · 1 YEAR** show daily closes with the last value and its one-year change; the VIX chart marks 20 (elevated) and 30 (stress).

### Where the data comes from

All readings are Yahoo Finance daily closes: VIX, S&P 500, HYG, LQD, AUD/JPY, copper and gold futures, and bitcoin. They are asked again every 5 minutes while the page is shown, and the footer gives the update time. If Yahoo Finance cannot be reached, the panels read **Yahoo Finance daily closes unavailable**. No key is needed.
