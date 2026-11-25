# News

What the markets read: the economic calendar with its actuals and release history, a visual news feed, the banks' own research notes, and a real-time wire with live TV. None of the pages in this tab needs a key or an account.

## Calendar

The calendar tells you what comes out this week, what the market expects, and what was actually published. Filters run along the top: a search box, a week navigator, country chips and impact segments. Below them, the table groups the week's events by day, and its column and day headers stay pinned while you scroll. Clicking a value slides the range panel in from the right. From 1300 px wide the table narrows to make room for it; in a narrower window the panel slides over the table. The footer, **Terminal Data · HH:MM:SS**, gives the time of the last calendar refresh.

### Filters

The search box, **Search event, country, or currency...**, matches the event name, the currency code or the country name. The **‹ ›** arrows around the week dates step to the previous or next week. A week runs from Sunday to Saturday, New York time.

**COUNTRIES** has one chip per major currency (USD, EUR, GBP, JPY, CHF, CAD, AUD and NZD), and clicking a chip shows or hides that currency. **More...** opens a menu of the week's other currencies to tick, such as CNY or **ALL — global events**. When some currencies are hidden the button reads **More (n)**, where n is the number of currencies still shown. Deepwater remembers the selection. **IMPACT** then narrows the table to **All**, **High**, **Medium** or **Low**, and each segment shows its count.

### Table

| Column | What it holds |
|---|---|
| **TIME** | Your local time, or **All Day** for holidays |
| **CURRENCY** | Flag and currency code |
| **EVENT** | The event's name, with impact dots before it (3 = high, 2 = medium, 1 = low) and a period tag after it |
| **ACTUAL** | The published figure, with an arrow showing whether it is up or down against the previous one |
| **FORECAST** | The consensus |
| **PREVIOUS** | The previous release |
| **SURPRISE** | Actual minus forecast, coloured by sign |
| **MIN** / **MAX** | The economists' range. No free source publishes it, so it shows **—** |

The period tags are **ANNUAL** (y/y), **MONTHLY** (m/m), **QUARTERLY** (q/q) and **3-MONTH**. When the table gets narrow it drops **MIN** / **MAX** first, then **SURPRISE**, and their values move to the row's tooltip.

Hovering a cell tells you more. On **ACTUAL**, the tooltip says where and when the figure was recorded. On **FORECAST**, it gives the source of the consensus. On a long name, it gives the full title and the impact. The wheel scrolls the table smoothly.

### Event details

Click an event's name to unfold it, and click again to fold it back. The first part explains what the indicator measures and how a reading above or below the forecast is usually read for the currency. Below that comes the release history, titled with the country's name followed by **· RELEASE HISTORY**. It is a chart of past releases with the latest value, the average, the low and the high.

You can draw the history as **BARS** or as a **LINE**, over **5Y**, **10Y**, **20Y** (the default) or **MAX**. Hover the chart to read the value of a period. Drag across it to zoom on that span, and double-click to reset the zoom.

Where an official series exists, the history uses it, dated by reference period. The official series are OECD consumer prices, unemployment and GDP, the Bank of Canada's core CPI measures, and the US BLS figures for CPI, PPI and payrolls. For other indicators, the chart shows the releases Deepwater has recorded itself.

### Range panel

Click any value cell, from **ACTUAL** to **MAX**, to open the panel. To close it, click the same cell again, click the ✕, or press Esc. The panel is built around the economists' range. **ECONOMISTS' RANGE** draws a scale with markers for the consensus, the previous figure and the actual. The range itself stays empty, because no free source publishes it. **BANKS' OWN FORECASTS** is in the same position: Deepwater collects no bank forecast for calendar events at present, so the section reads **No bank forecast found for this release**.

Further down, the **CONSENSUS**, **RANGE**, **PREVIOUS** and **ACTUAL** boxes repeat the figures, with a legend. **SOURCES** links to the ForexFactory calendar and to wherever the actual came from; clicking a link opens it in your browser. The wheel scrolls the panel.

### Where the data comes from

Events, impact, forecasts and previous figures come from the ForexFactory weekly calendar. Deepwater reloads it every 30 minutes while the page is shown. Actuals are recorded as they come out, from three sources:

- the Myfxbook calendar RSS, checked at most every 5 minutes while Deepwater runs; the check is placed about a minute after each medium or high-impact release;
- data headlines on the wire (FinancialJuice, investingLive), accepted only when the currency, the release time, the indicator's words, the unit and the stated forecast or prior all match;
- official series (OECD, BLS, Bank of Canada) for past releases.

The same calendar supplies the releases shown in [Squawk](help:news), in [Global › Session Recap](help:global) and on the Overview wire.

ForexFactory publishes only the current week and the next one, and the next week appears during the week before it. An earlier week exists only if Deepwater recorded it at the time; otherwise the table reads **No recording for this week**. If ForexFactory cannot be reached, you see **ForexFactory calendar unavailable (…)**. Deepwater records actuals only while it is running. An event released while the app was closed may therefore stay at **—**, unless an official series fills it in.

## News Feed

The News Feed is the visual side of the news: stories with their pictures, ranked by how likely they are to move markets. The **MARKET WIRE** header holds the source chips, the **LIVE** status and clock, the language chip and the story count, and a ticker of headlines runs under it. The main column opens with the **LEAD STORY**, followed by the search box, the topic chips and the story cards. The **LATEST HEADLINES** column sits on the right. The footer, **Terminal Data · HH:MM:SS**, gives the time of the last refresh. On small screens everything becomes one scrolling column: the lead, the first five latest headlines, the filters, then the cards.

### Header

The **SOURCES** chips decide which feeds you read. Clicking a chip shows or hides that source, and a hidden source is not downloaded at all. Chips that do not fit fold into a **+n** menu. Hover a chip for its site and its last error. The **LIVE** status turns to **STALE** when no feed has refreshed for 20 minutes, and **n STORIES** counts the stories that match the current filters.

The language chip switches between **EN** (the original) and **FR**, a French translation made on this Mac by Apple Translation. It reads **FR…** while translations are pending. Its caption says **AUTO-TRANSLATED**, **ORIGINAL** or **FR UNAVAILABLE**. The chip starts in the interface language and remembers your choice; hover it for the translator's state.

### Ticker, lead and cards

The ticker scrolls every story of the active sources from right to left, with its age. Hovering pauses it, and clicking a headline opens the story. The **LEAD STORY** is the recent story most likely to move markets, preferably one with an image. Central-bank and macro topics weigh most in that choice. Click it to open it.

To narrow the cards, type in **Search headlines...**. It matches words of the title, original or translated, and the source's name. You can also click a topic chip; click it again to clear the filter.

- **TOP IMPACT**: keeps the important stories and ranks the last 48 hours by impact
- **CENTRAL BANK**, **INFLATION**, **EMPLOYMENT**, **GDP**, **PMI**, **RETAIL SALES**, **EARNINGS**: one topic each

Each card shows the image, the topic tag when a filter is on, the title, the source and the age. Click a card to open the story, or click the **+** on its right to add the story to the reading queue. Personal-finance and lifestyle pieces are kept out of the lead and the lists.

The **LATEST HEADLINES** column keeps the important and high-impact stories of the last 36 hours. High-impact stories come first, then the rest, newest first. Each one is labelled **HIGH IMPACT** or **IMPORTANT**, with its source and age. Click one to open it. The wheel scrolls the column.

### Reader

Opening a story replaces the feed with the reader and adds the story to the reading queue. The top bar starts with **BACK TO FEED**, followed by the **QUEUE** chips. Click a chip to open its story, click its ✕ to remove it, or use **+n** to reach the rest of the queue.

The article shows the source and time, a language chip that switches between EN and FR, the title and the image. The text follows, as much as the feed carries: the full article or a summary. **READ ON** followed by the site's name and ↗ opens the original in your browser. Links inside the text open there too. **Find in this article...** highlights a word in the text.

Beside the text, the **AUTOMATED READ** panel sums up the story using fixed rules, with no language model. It lists the **KEY POINTS** and the banks and markets **MENTIONED**, with **LIVE QUOTES** for the markets named. Its **TONE** reads hawkish / strong, dovish / weak or neutral. It also gives the reading time and the word count.

### Where the data comes from

| Source | Checked every | Default |
|---|---|---|
| investingLive | 2 min | on |
| Yahoo Finance | 5 min | on |
| CNBC | 2 min | on |
| MarketWatch | 2 min | on |
| Central banks (Fed, ECB, Bank of England) | 5 min | on |
| FinancialJuice | 2 min | off |
| Nasdaq | 10 min | off |
| Investing.com | 5 min | off |

Feeds are polled while the page is shown, and in the background when **Options › Data** keeps data fresh (on by default). Stories are kept for 4 days, up to 900 of them, and images are cached for 7 days. Many feeds carry no image, and a generated tile takes its place.

## Bank Research

The research notes and PDFs of the major banks, their head offices on a globe, and the registry of systemic banks with their total assets. The left column holds the **RESEARCH**, **NEWS**, **DATA** and **FILINGS** tabs, the list and a footer on the sources. The globe fills the centre until you open a note, and then the PDF viewer takes its place. On the right is **BANK REGISTRY — SYSTEMIC INSTITUTIONS**, and the badge at the top counts the institutions and regions. On small screens the notes, the globe and the registry stack in one scrolling column, and an opened note covers them.

### Notes

**RESEARCH** lists every note, while **NEWS** keeps only the quick reads (snaps and opinions). Notes are grouped by day (**TODAY**, **YESTERDAY**, then dates). The header shows how many notes were published today and the size of the library. Each row gives the bank's logo and name, the category, the age, the title, and **PDF · OPEN IN TERMINAL** or **WEB · OPEN IN TERMINAL**. Click a row to open the note.

Two dropdowns narrow the list. **ALL BANKS** picks one bank; banks marked **(LINK)** have only a link. **ALL PUBLICATIONS** picks a kind: articles, snaps, opinions, reports, bundles or forecasts. If you pick a bank that Deepwater does not read, the list shows **NO NOTES FROM …** with the bank's name, the reason, and an **OPEN … ↗** link to the bank's research site.

### Data and Filings

The **DATA** tab explains which banks Deepwater reads and why. Each bank falls in one of three groups:

| Status | Meaning |
|---|---|
| **READ** | Notes cached and forecasts parsed: the bank's terms allow reuse with attribution |
| **LINK** | The terms forbid reproduction, so there is only a link |
| **NONE** | The terms forbid automated access, deep links or citing |

The tab then explains where the registry's assets and G-SIB flags come from. **FILINGS** lists the latest SEC EDGAR filings of the registry banks, with the date, form, bank and description, and each filing is a link.

### Note viewer

The viewer's header gives the category and date, the bank and the title. Next to them are three buttons: **⤓ PDF** saves a copy, **⧉ SOURCE** opens the note on the bank's site, and **✕** closes the viewer, as Esc does.

The toolbar has the usual controls. **≡** shows page thumbnails; click one to jump to its page. There is a page number field with the page count, **−** / **+** zoom buttons, fit to width, rotate, save and print. The **⋮** menu repeats save and print and adds a few more: open in the default PDF viewer, copy the PDF link, open the source, and the document's size. ⌘ + wheel (Ctrl + wheel on Windows) zooms from 25 % to 400 %, and the wheel alone scrolls. A note without a PDF shows a message pointing you to **⧉ SOURCE**.

### Globe and registry

The globe turns with a beacon on each bank's head office. Hover a beacon's label to see the bank and the city, and click it to select the bank. Each registry row gives the rank, logo, name, total assets and ticker. Hovering a row adds the legal name, the city, the G-SIB status, and the assets with the dates of the balance sheet and of the filing. Clicking a row selects the bank. On short windows the wheel scrolls the registry.

Selecting a bank, on the globe or in the registry, filters the notes to that bank. If you were on **DATA** or **FILINGS**, the column switches back to **RESEARCH**.

### Where the data comes from

Notes, PDFs and forecast tables come from ING THINK, through its RSS feed and article listings. ING is the only bank whose terms allow reuse with attribution. Deepwater re-reads the listings at most once an hour and the forecast tables every 6 hours. A PDF downloads when you open it, and stays cached. Of the other banks, 19 are link-only and 9 are excluded by their terms; the **DATA** tab gives the reason for each.

Total assets are the latest *Assets* fact in each bank's SEC EDGAR XBRL filings, in the filing currency, cached for one day. The G-SIB flags follow the FSB list of November 2024. ING's forecasts also feed [Positioning › Bank Positioning](help:positioning).

## Squawk

The real-time wire: headlines, data releases and price moves, timestamped to the second, next to live TV and a compact calendar. The wire fills the left side, with its header, a price-move ticker, the filter tabs, the list and a footer. On the right, **TERMINAL TV** sits above the compact economic calendar. When the channel has no live stream, the TV shrinks to a message band and the calendar takes the height. In small or very short windows, the blocks scroll as one page.

### Wire

The header reads **SQUAWK LIVE**, with a description, the **EN** / **FR** translate button and the number of **ITEMS**. Under it, the ticker scrolls from right to left through the markets that are currently beyond their move threshold.

The tabs **ALL**, **DATA** (headlines, the default), **MOVERS** and **RELEASES** each show their count, and Deepwater remembers the one you pick. Each row gives the time to the second, the age, a source badge and the headline. Hover a row for its source, date and time, and for the original English text when the headline is translated. Click a row to open its source in your browser. The wheel scrolls smoothly, and new items arriving at the top do not move your reading position.

**EN** / **FR** machine-translates the headlines into French on this Mac, until you close the app. It starts in the interface language. Hover the button to find out whether translation is available. The footer, **Terminal Data · HH:MM:SS**, gives the time of the last fetch, next to a **LIVE** badge.

### What the wire contains

The wire mixes three kinds of item. Headlines come from FinancialJuice (checked every minute), investingLive (every 2 minutes), and the Fed, ECB and Bank of England press releases (every 10 minutes). While no wire page is open, these checks slow down to every 10 to 30 minutes. Releases are the [Calendar](help:news) events whose actual has been published.

Price moves are computed by Deepwater from Yahoo Finance quotes, as the change against the previous close. An item is added each time a market crosses its threshold, and again at each further multiple of it:

| Class | Threshold |
|---|---|
| Indices (except VIX) | 1 % |
| Stocks | 2 % |
| Commodities | 2 % |
| Crypto | 3 % |
| FX majors | 0.5 % |

The list shows the last 24 hours, and always at least the 150 newest items. Headlines are archived for 70 days for [Global › Session Recap](help:global).

### Terminal TV

The TV plays **Bloomberg Television**, **Yahoo Finance** or **CNBC Television**: each channel's own YouTube live stream in the official player, muted at start. Deepwater remembers the channel you chose. **Open on YouTube** opens the channel's live page in your browser.

If a channel is not broadcasting, the band reads **… has no live stream on YouTube right now**, with the channel's name; try another channel. The player stops when you leave the page. It needs the system web view, which for now means macOS.

### Compact calendar

The compact calendar shows the same events as the [Calendar](help:news), from Sunday to Saturday. Once the week is over it moves on to the next one, unless you have navigated yourself.

Its controls mirror the full page: a search box, **‹ ›** to change week, chips for the 8 majors, and impact chips **All**, **High**, **Medium** and **Low**. **More...** offers **All currencies**, **The 8 majors only**, or other currencies to tick. Deepwater remembers the countries and the impact you choose.

Each row gives the time, the flag and currency, the impact, the event, **ACTUAL**, **FORECAST** and **PREVIOUS**. Upcoming events count down to the second (HH:MM:SS, then days and hours beyond 100 hours), and today's day header carries **TODAY**. Click a row to unfold its details (impact, local time and frequency), or hover it for the full event and its values.
