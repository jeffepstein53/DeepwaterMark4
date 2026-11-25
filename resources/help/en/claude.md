# Claude

Deepwater can let Claude read what its pages show, so you can ask Claude about the markets, the calendar or the central banks while Deepwater is open. It is read only: Claude can look, never act. This chapter is not a tab of the header: it is reached from the **?**.

## Switching it on

### In the options

Open the gear, then the **CLAUDE** section. **Let Claude read Deepwater's data** is off until you switch it on. Then **ADD TO CLAUDE CODE** or **ADD TO CLAUDE DESKTOP** declares Deepwater to that program, and a note under the buttons tells you what to do next: restart the program, and keep Deepwater open. The buttons turn into **REMOVE FROM CLAUDE CODE** and **REMOVE FROM CLAUDE DESKTOP** once it is done.

For Claude Desktop, Deepwater adds its entry to every copy of Claude's configuration file it finds on the computer, and first saves each one next to itself under the same name ending in `.deepwater-backup`. A file it cannot read is left alone, with a message. Your other servers and settings stay as they are. Quit Claude Desktop completely and open it again afterwards; Deepwater cannot tell from outside which of those files a given version of Claude Desktop reads, so if Deepwater is not listed there, use the plug-in below.

**Include my trading journal** adds one more tool, for your track record. It is off by default.

### Deepwater must be open

Claude reaches Deepwater through the program that is running. When Deepwater is closed, or the switch is off, Claude gets a message that says so. Deepwater never starts by itself because Claude asked.

### The plug-in

The project's repository also holds a ready-made Claude plug-in, in the folder `plugin`. It finds the installed Deepwater by itself and gives Claude a short guide to the tools. It is another way in, for Claude Code and for the **Plug-ins** page of Claude Desktop, and it does not need the buttons.

## What Claude can read

### The tools

Claude has 24 reading tools: quotes, price history, the economic calendar, news, the live wire (Squawk), macro data, macro chains and facts, central banks, COT, retail sentiment, the risk index, bank forecasts and research, market hours, technical reads, liquidity pools, seasonality, fundamental scores, and the recorded data of the Data tab: technical coverage, technical data, footprint, order book and open interest. With the journal switch on there is a 25th, for the trading journal.

### How fresh the answers are

A tool that is asked about data older than its source allows asks the source again before it answers, and every answer says how old its data is: when the quotes were fetched, how long ago the newest news came, when the calendar's figures were last downloaded, when the central banks' odds were computed. If a source does not answer, the tool says so and gives the age of what it has, instead of presenting old figures as fresh.

## What it never does

### Safeguards

- **No order, ever.** Every tool only reads, and none can place, change or cancel a trade.
- **No key and no password.** Your API keys and the Myfxbook session never leave Deepwater.
- **Only this computer.** The connection stays on `127.0.0.1`, with a new secret key at every launch. A web page, or a program that does not hold the key, is refused.
- **At most 30 calls a minute**, and a log of the calls: the tool, its arguments, whether it worked and the size of the answer, never the data. It is in the `mcp` folder of your data.
- **Third parties' text is marked as data.** News headlines and the like reach Claude inside a marked block, with a reminder that they are not instructions.

### What goes out of your computer

What Claude reads is sent to Anthropic, as part of your conversation with Claude. Switch the option off when you do not want that.
