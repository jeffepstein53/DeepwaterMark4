# Deepwater

Deepwater is a market terminal built entirely on public sources. It brings together macro data, news, positioning, live markets, charts with order flow, a trading journal and AI tools. This first chapter covers what every page has in common; each of the following chapters is devoted to one tab of the header.

## Interface

### The header

The first row holds the Deepwater logo, the main tabs, a clock, the update icon, the **?** that opens this documentation, and the gear that opens the options. The second row lists the sub-tabs of the tab you are in, with a one-line description of the current page on the right.

The clock shows your local day, date and time, and adds UTC when there is room. When the window gets narrow, the header gives up the UTC time first, then the date, then the Deepwater name, and tightens the tabs, so that every tab remains visible.

### Moving around

Clicking a main tab takes you back to the sub-tab you last used there, so you can switch between, say, a chart and the calendar without losing your place. At the next launch Deepwater reopens the page you left.

### How pages behave

A page is built the first time you visit it and is then kept as it is: coming back is instant, and your selection, scroll position and filters are still there. While a page is hidden it pauses its timers and animations, and picks them up again when you return.

Some pages need dozens of requests before they are full. To spare you the wait, Deepwater builds them in the background, one after another, starting about four seconds after launch: **Overview**, **Technical Heatmap**, **Rates**, **Macro**, **Fundamental Heatmap**, **Session Recap**, **Markets Watch** and **Bank Research**. If you open one of them before its turn, its requests jump the queue.

Changing the language or the theme rebuilds every page, then reopens the one you were on.

### Small screens

On a computer the window can shrink to 1,024 × 600, and the pages rearrange their panels to fit. On a phone the header is reduced to a single row: **☰** opens a menu listing every tab and sub-tab, followed by the name of the current page, the time, **?** and the gear.

## Keyboard shortcuts

A few shortcuts work everywhere in the application; the pages that have their own, such as [Charts](help:charts), list them in their chapter. On Windows, use Ctrl wherever this documentation says ⌘.

| Keys | What it does |
|---|---|
| ⌘1 … ⌘9 | Opens main tab 1 to 9, on its first sub-tab (⌘0 is the zoom's actual size; on an iPad it opens the tenth tab) |
| ⌘] and ⌘[ | Next and previous sub-tab of the current tab |
| ⌘, | Options |
| F1 or ⌘? | This documentation, open at the chapter of the current tab |

## Options

The gear opens the options; so do ⌘, and, on macOS, **Deepwater › Settings…** in the menu bar. Every change applies at once and is saved, and Esc closes the window.

### Language

The interface, this documentation included, is available in English and French. Whatever the language, numbers keep the 1,234.56 format traders are used to; only dates and the names of days and months follow your choice.

### Theme

Ten themes recolour the whole application. Each card shows a preview of its colours, and hovering it gives a short description.

| Theme | Kind | Look |
|---|---|---|
| Deepwater | Default | Warm black, amber accents, cyan up and coral down |
| Ember | Dark | Warm charcoal with gold accents |
| Abyss | Dark | Deep-sea blue with aqua accents |
| Jade | Dark | Dark green with jade accents |
| Oxide | Dark | Rust browns with a red-orange accent |
| Nova | Dark | Night violet with lavender accents |
| Bloomberg | Dark | Pure black, amber text and white accents |
| Glacier | Light | Ice white with blue accents |
| Dune | Light | Sand with burnt-orange accents |
| Sakura | Light | Blush pink with rose accents |

## Updates

Deepwater looks for a new version on its own: once at launch, then by staying connected to a notification service that announces each new release, so a version published while the app is open shows up within seconds. It never checks on a timer. The versions come from the project's releases on GitHub, built for every platform at once.

When one is available, the update icon in the header (an arrow into a tray, left of the **?**) turns amber with a dot. A click opens the update window: the version you have, the new one, and what changed in it. **Check now** asks again at any time.

On a Mac, **Install and restart** downloads the new version, checks it, puts it in place of the one you are running and starts it again; if Deepwater runs from a build folder, the new version goes into Applications instead. On Windows, the same button downloads the installer, which runs once Deepwater has closed: Windows asks for your permission, then Deepwater starts again. A download goes on if you close the window, and the icon shows its progress.

An iPhone or iPad app cannot install a new version of itself. There, the window tells you a version is available and **Open the release page** leads to it, to install it again with Sideloadly or AltStore; through TestFlight, updates arrive by themselves.

The update check uses two sources listed in the [Sources](help:sources) tab, GitHub and ntfy.sh; switching them off there stops it.

## Reading the screens

In the default theme, cyan means up, long, positive or risk-on, and coral red means down, short, negative or risk-off. Amber marks titles, the active tab and live data.

A **● LIVE** badge means the data is streaming or fresh. When it turns grey and reads **STALE** or **OFFLINE**, the source has not answered for a while and the page is showing the last data it received.

Times follow your computer's clock unless a page says otherwise, for instance when it shows UTC or the local time of an exchange. An **ⓘ** after a panel title explains the panel when you hover it.

## Data and keys

Deepwater reads public sources: central banks, statistics offices, exchanges and news feeds. Most of them need nothing from you. A few ask for a free key or an account, which you enter in the [Sources](help:sources) tab; that tab also shows the state of every source and lets you turn any of them off.

Everything Deepwater records, from your journal and market archives to economic facts, models and settings, stays on this computer.

A closed market simply shows its last price. Weekends, holidays and night sessions are not errors.

## Using this documentation

The chapters are listed on the left, the text sits in the middle, and the right-hand column shows the sections of the chapter, highlighting the one you are reading; click a section to jump to it. Links in the text take you to other chapters.

To search, type in the field at the top left or press ⌘F. Case and accents are ignored, and the chapter list shows how many matches each chapter holds. Enter or ↓ moves to the next match, carrying on into the next chapter when this one is exhausted; Shift+Enter or ↑ goes back.

The window follows the main window when you move or resize it, and you can drag it by its title row. Esc or × closes it.

## The chapters

[Global](help:global) gives the overall picture: a market overview, session recaps and market hours. [News](help:news) covers the economic calendar, the news feed, bank research and the Squawk wire. [Economy](help:economy) holds the macro globe and central bank rates, and [Positioning](help:positioning) shows how retail traders, the COT report and the banks are placed, along with risk sentiment.

[Market](help:market) follows live prices and reads them: markets watch, seasonality, fundamental and technical heatmaps, liquidity. [Charts](help:charts) is the charting terminal, with indicators, order flow, the strategy tester and paper trading. [Account](help:account) holds your track record and the risk calculator.

Finally, [Data](help:data) shows the economic facts and market bars that Deepwater records, [AI](help:ai) brings together language models, agents and machine learning, and [Sources](help:sources) lists every data source with its state and keys.
