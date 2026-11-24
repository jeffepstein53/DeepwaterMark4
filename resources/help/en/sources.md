# Sources

This tab lists every outside source Deepwater reads: whether it answers, what it provides, which pages use it, its terms and the key or account it needs, with a switch to stop it. A second view draws where the data comes from and which tabs use it.

## Data Sources

The page opens on **DATA SOURCES**, followed by the totals: sources, active, failing and disabled. The search field matches a source's publisher, id, host, data or the pages using it, ignoring case. The filters beside it, each with its count, run from **ALL** to **KEY OR ACCOUNT**, for the sources that need a key or an account, by way of the active, failing, disabled and not yet contacted ones.

### Status

A source's status comes from Deepwater's own requests and is kept across launches. It is **ACTIVE** when the source answered, and **FAILING** when it failed twice in a row, or never answered and failed at least once. A source you switched off here is **DISABLED**, and Deepwater no longer contacts it. **NOT CONTACTED** simply means it has not been read yet: a source is read when a page that uses it opens.

Under each card you will find its last answer, its last failure with the error, and the number of answers and failures recorded. Answers served from the cache are not requests and change nothing.

### A source card

A card starts with the publisher, then the source's id, its hosts and its pace, for instance `1 request / 2 s`: Deepwater never asks one host faster than that. The access is **FREE**, **API KEY** or **ACCOUNT**. Then come what Deepwater takes from the source, **Used by** for the pages that read it, and **Terms** for why and how it may be used.

The switch on the right stops the source: once it is off, Deepwater refuses every request to that source's hosts. Sources that share a host are stopped too, and the card says which. A source that needs a key and has none reads **No key saved: the data it provides stays empty.**

### Keys and passwords

One source takes a key here: e-Stat. Paste the key and press **SAVE KEY** (or Enter); **REMOVE** deletes it, and the card shows **● KEY SAVED** or **○ NO KEY**. A key is stored on this computer only, never shown back, and sent only to its own source. It is used from the next refresh of the pages that read it.

Myfxbook, which provides retail sentiment, asks for your e-mail and password instead. **CONNECT** signs in and **LOG OUT** signs out; the state reads **● CONNECTED**, **… CONNECTING**, **○ NOT CONNECTED** or **○ SESSION EXPIRED — CONNECT AGAIN**, or shows the error. The password is sent once to Myfxbook over HTTPS and never stored; only the session token is kept, until you log out or it expires.

### The sources

| Source | What it provides | Key |
|---|---|---|
| Yahoo Finance | Quotes and candles (FX, indices, futures, stocks, crypto), symbol search, headline RSS | No |
| Binance spot (and its public mirror) | Crypto candles, tickers, best bid/ask, live bars; bars for Technical data | No |
| Binance USD-M futures | Perpetual candles, bid/ask, open interest, long/short ratio, funding, live liquidations | No |
| Binance public data archives | Daily trade files (footprint history), perpetual book depth and open interest | No |
| Dukascopy | Forex bid bars since 2003 (Technical data) | No |
| Forex Factory | Weekly economic calendar | No |
| Myfxbook calendar RSS | Actual values of recent releases | No |
| Myfxbook community outlook | Retail long/short %, positions, average prices | Account |
| FinancialJuice, investingLive | Headline wires | No |
| Investing.com, CNBC, MarketWatch, Nasdaq | News RSS (and the stories' images) | No |
| YouTube | Embedded live streams on Squawk | No |
| Federal Reserve Board | FOMC calendar, statements, votes, press releases | No |
| New York Fed | Effective fed funds rate, target range | No |
| European Central Bank | Key rates, €STR, yield curve, meetings, press | No |
| Bank of England | Bank Rate, SONIA, gilt yields, MPC dates and votes, news | No |
| Bank of Canada | Overnight rate, CORRA, yields, schedule, core CPI | No |
| Swiss National Bank | Policy rate, SARON, schedule | No |
| Bank of Japan | Meeting schedule, overnight call rate (TONA) | No |
| JPX, Eurex, Montréal Exchange | Rate futures settlements (TONA, SARON, CORRA) | No |
| Polymarket | Central-bank decision markets (probe only) | No |
| BIS | Policy rates, consumer prices | No |
| OECD | GDP growth, unemployment, confidence, rates; release histories | No |
| World Bank | Annual GDP, population, debt, reserves… | No |
| Eurostat | Euro-area core inflation | No |
| e-Stat (Japan) | Japanese core CPI | Yes |
| US Treasury | Daily par yield curve | No |
| Ministry of Finance Japan | JGB yields | No |
| US Bureau of Labor Statistics | Nonfarm payrolls | No |
| CFTC | Commitments of Traders | No |
| SEC EDGAR | Total assets of the systemic banks | No |
| ING THINK | Research notes and forecast tables | No |
| Banks' research pages | Links only, nothing downloaded | No |
| GitHub (Deepwater Mark 4's releases) | New versions of Deepwater: the update check and its download | No |
| ntfy.sh | A message when a new version is published, so the update icon lights up at once | No |

## Sources graph

This view answers the question "where does this page get its data?" at a glance. The header gives the number of sources, sub-tabs and links drawn between them.

### Reading the diagram

The left column holds every source, grouped under **MAINLY FOR** the tab it feeds first, each with a status dot. Below them, **LOCAL, NOT FROM THE NETWORK** gathers what comes from this computer: your broker's exports (MT4, MT5, cTrader) and the computer's clock. The several feeds of one institution share a single box; that is the case for the Federal Reserve, the ECB, the Bank of England, the Bank of Canada and Yahoo.

The right column lists the main tabs and their sub-tabs in the navigation's order, one colour per tab. A curve joins a source to each sub-tab it feeds. The number on a source counts the sub-tabs it feeds, and the number on a sub-tab counts the sources it reads. The legend gives the dot colours: **ACTIVE**, **FAILING**, **WAITING**, **DISABLED** and **LOCAL**.

Some sources name pages that are not in the navigation; those pages are listed under the diagram.

### Exploring it

Hover a source, a sub-tab or a tab and its links light up. The tooltip of a source gives its publisher, data, access, status (feed by feed for a merged box), terms and the pages using it; on a sub-tab, it lists the sources the sub-tab reads. On a touch screen, tap a box to light it and show its tooltip, and tap it again, or tap the background, to clear it.

To find a source, type in **Search a source…**. Every word must match a source's name, publisher, id or data; case and accents are ignored, and French words also match their English form, so "banque" finds "bank". The matches light up, the view scrolls to the first one, and Enter moves to the next; the number of matches is shown beside the field.
