# AI

This tab brings language models and machine learning to Deepwater's data. You download local models, or use Claude Code, load them into four isolated environments, and give each environment the skills it may use. You can also train your own models in a private Python environment.

## LLM

This sub-tab is where the models come from. Its bar switches between **LOCAL MODELS**, the models you download and run on this computer, and **INTEGRATION**, where Claude Code is set up; the choice is remembered.

### Finding a model on the Hub

Type a name in the search field, such as `qwen`, `llama`, `mistral` or `phi`: results come from the Hugging Face Hub as you type, and the first 50 are shown. By default the search keeps **GGUF ONLY**, the format the environments run; **ALL FORMATS** lifts that limit. The results can be sorted by **MOST DOWNLOADED**, **MOST LIKED** or **RECENTLY UPDATED**.

Each result gives the name, owner, licence, task, downloads (↓) and likes (♥), and reads **GATED** when the model asks you to accept its terms first. A status line says whether the search is running, how many models were found, or that the Hub is unreachable.

### Downloading a model

Click a result to see its licence, downloads and last update, with **OPEN ON HUGGING FACE** to visit its page. Read the licence before using a model: some forbid commercial use or require attribution. For a gated model, accept its terms on huggingface.co with your account, then save your token as described below.

The model's files are listed with their name (the tooltip gives the path and SHA-256), quantisation and size, and a **DOWNLOAD** button, or **✓ INSTALLED** once you have it. In GGUF view, small files such as the README or config are hidden. **Not enough space** appears when a file is larger than the free disk. When choosing among quantisations, Q4_K_M is a good trade-off between size and quality, and a model needs about its file size in free memory.

Downloads queue one after the other. A download shows **QUEUED**, then the amount received out of the total, the speed and the minutes left, with **PAUSE**, **RESUME** and **CANCEL**. At the end it shows **CHECKING SHA-256…**, or **FAILED: …** on an error. An interrupted download resumes where it stopped.

### Installed models and the token

Each installed model shows its file, repository, size and licence, and **SHA-256 verified** (or "no checksum published"). **SHOW** opens its folder and **DELETE** removes it, after asking. The header holds **OPEN FOLDER**, which opens the models folder, and gives the number of models and the space used and free.

The **TOKEN** field takes an optional Hugging Face access token, needed only for gated models. Press **SAVE TOKEN** (or Enter); saving it empty removes it. The token is kept on this computer, never shown back and sent to huggingface.co only. The same token can be saved in [Sources](help:sources).

### Claude Code

The **INTEGRATION** part lets you use Claude Code as a model. The left panel shows the **PROGRAM** (where Claude Code is installed, or "not found on this computer"), its **VERSION**, and the **ACCOUNT**, signed in or not; the e-mail is never shown. This is checked again each time the page opens, and **RECHECK** does it on demand.

If Claude Code is not installed, install it from claude.com/claude-code, sign in once by typing `claude` in Terminal, then press **RECHECK**. If it is installed but signed out, type `claude auth login` in Terminal.

**MODEL** chooses between Opus, Sonnet, Fable and Haiku, and **EFFORT** goes from low through medium, high and extra high to max. On both, **DEFAULT** keeps Claude Code's own choice, and a change applies from the next question. The right panel, **Load into an environment**, shows what each environment holds, with **LOAD**, **REPLACE** (which takes the current model out) or **EJECT**. You can also drag Claude Code from the list of **Agents** onto a cube.

### What Claude Code can and cannot do

In a cube, Claude Code answers in the cube's chat, one run per question, and the conversation carries on from question to question until you press **CLEAR**. Its own tools are turned off: it has no shell, no access to your files and no web search, and none of your own Claude Code settings, hooks or servers are used.

It reads Deepwater's data only when one of the cube's skill boxes is ticked. It then goes through Deepwater's tools, served on this computer only, with a key per cube, and makes at most 20 tool calls per question.

Unlike a local model, Claude Code runs on Anthropic's servers with your own Claude sign-in. Your questions, and what its tools read, are sent to Anthropic and count against your Claude plan or API usage.

## Agents

This is where models are put to work. The left panel, **Models & integrations**, lists your downloaded models and **Claude Code** (integration · Anthropic's servers); if it is empty, get a model in **LLM › Local models**. The right side holds four cubes, **ENVIRONMENT 1** to **4**, in a 2 × 2 grid or a row of four, whichever gives bigger cubes. The header counts the environments loaded (`2 / 4`) and the memory the loaded models need against this computer's RAM, with a warning tooltip once that passes three quarters of it.

### Loading and moving models

Drag a model from the list onto a cube and it loads there; the cube lights up. While you drag, the cube reads **DROP A MODEL HERE**, then **RELEASE TO LOAD**. Dragging a lit cube onto another moves its model, and **EJECT** turns the cube off and stops its model.

A cube's card shows the model name, then its quantisation, size and share of RAM (or "INTEGRATION · runs on Anthropic's servers" for Claude Code), and its state: **STANDBY**, **LOADING**, **READY**, **ANSWERING**, **FAILED** or **OFF**. The assignments are remembered: at launch the cubes come back on standby and load their model only when used. **CHAT**, or a double-click on the cube, opens its conversation and starts loading its model.

### Isolation and sandbox

Each cube has its own folder and, for a local model, its own process, with no internet, no access to your files and nothing shared with the other cubes. On macOS that process runs in a sandbox that only lets it read the app and its model file, its own folder, and use the GPU. On other systems it is not sandboxed; the chat says so, and you should use models you trust.

A local model is told that it is offline, and it sees no data unless a skill is ticked. Image models and other files the engine cannot run fail with a message in the chat.

### Skills: what a model can read

A model starts with no data at all. Tables and charts never reach it by being pasted into its prompt: it can only read them by calling the tools of a skill you grant. The skill boxes sit on each card and in the chat header. Ticking one grants the skill at once, and every tool call is checked against it again, so a skill unticked in the middle of an answer makes the next call refused.

**Read Deepwater's data** opens what the pages show: quotes and price history, the economic calendar, news and Squawk, Economy series, central banks, COT, retail and risk sentiment, bank forecasts and research, market hours, technical reads, liquidity pools, seasonality, fundamental scores, and your track record without broker or login. It also covers the chains and facts of **Data › Macro data** and the bars, footprint, order book and open interest of **Data › Technical data**. It never reaches the Sources and AI pages, nor any key or password.

**Machine learning** gives the **ML** sub-tab: the model can load data sets (prices, macro, COT, technical data), read them, train models of the five families in the same sandbox (one at a time, named after the environment), then read and compare the results, whose figures are computed by Deepwater. It cannot delete, stop or install anything, nor read files outside the data sets.

**Backtest strategies** gives the Strategy tester of [Charts](help:charts), with rule strategies or Pine strategies the model writes, run in a sandbox, and the quant checks, on Technical data's history or Charts' sources. It saves nothing and places no order. Its tries are counted, so the Sharpe ratio of the best one is deflated by their number.

A local model can make up to 5 tool calls per round and 6 rounds per question. Every call is shown in the chat and written to the environment's journal. Text from news, wires and research is passed to the model marked as third-party data, not as instructions.

### The chat

The chat header holds **← ENVIRONMENTS** to go back, then the cube, model, state, device (GPU or CPU), context size and speed. Next come **WEB**, the buttons **1** to **4** to switch environment, and **CLEAR**, which empties the conversation. Enter sends and Shift+Enter breaks the line; **SEND** becomes **STOP** while the model answers. If the engine fails, its error is shown with **RETRY**.

Reasoning blocks are dimmed under **REASONING**, which you can show in full or fold, and are not sent back to the model. Each tool call appears as a **TOOL** line with the number of characters read, or as **TOOL ✕** when it was refused or failed.

When an answer is written after tool results, a figures check follows it. It reads "✓ n figures checked" when every figure appears in what the tools returned; otherwise it lists, in orange, the figures it could not find. The answer details give tokens, tokens per second and seconds, and say when the answer was stopped, cut by the length limit or the context, or when older messages were left out. The conversation is kept in the environment's folder on this computer.

### The activity panel

The panel to the right of the conversation shows what the agent is doing; drag its edge to widen or narrow it, and the width is remembered. **NOW** gives the tool being called and for how long, or says the agent is thinking, writing, loading its model, in error, or waiting for your question.

**ML TRAININGS** has one tile per training this agent started: its data, progress and stage, and a live curve (score per fold, loss per epoch or return per episode), then the result with a small chart. **STOP** halts a training while it runs. The newest tile slides in on top, and **‹** and **›** (or ← and → after clicking the panel) move back and forth. **LAST ACTIONS** lists the agent's latest tool calls.

### The WEB view

Ticking **WEB** in the chat header puts the conversation in the middle, linked to three nodes. **COLLECTION** lists the data the agent read, newest first, with what each call asked for and the reads and refusals counted. **TRAINING** shows its trainings, as in the activity panel. **COMPARISON** holds its last comparison of trainings, redone by Deepwater, with each group's question, best setup and verdict. A link lights up and carries moving points while its work runs.

### Evaluation

How good is a model's market judgement, once you rule out what it simply remembers? **EVALUATION** on a loaded card opens the dialog that tests it.

With **Evaluation mode on** and a date (UTC), the environment's tools answer as of that moment: bars closed by then, order flow stored up to it, macro facts published by then, and backtests on the history before it. Later dates are refused, and tools that only know the present (prices, news, the live book, the pages) are off. The model starts afresh and is told the date, but its own memory of later events is not erased. **Blind** goes further, shifting the dates and masking the market's name and price level. The card then shows **EVALUATION · date UTC**, and the chat marks where the mode starts and ends.

The **Evaluation protocol** runs on BINANCE:BTCUSDT, with 1-hour bars and 15 bp of costs a side. At each moment between **From** and **To**, the model decides LONG, SHORT or FLAT for the **Horizon** (1, 4, 12 or 24 h), in a fresh conversation, with its tools. You set the **Decisions** (2 to 60) and **Repeats** (1 to 5), and choose **Trials** with the market named, blind, or both. Each decision is scored on the stored bars, after costs, against buy & hold, the trend rule, flat, and an ML model without LLM. The protocol needs **Read Deepwater's data** ticked, and a dozen decisions take several minutes.

**Probe its memory** first: then only the decisions after the last month the model remembers count (tier A). The **Forward test (tier C)** asks the same question in the present, then every 1 to 72 hours while Deepwater runs, and scores each decision once its horizon has passed; missed times count as missed. Keep the same model and skills throughout. The report appears in the chat.

## ML

This sub-tab trains your own models on market and macro data, from start to finish: you pick a family, load a data set, say what to predict and from what, choose an algorithm, then read and compare the runs.

### Python environment

Training runs on the Mac: each training runs inside a macOS sandbox that cuts it off from the network and from your files, and Windows has no such sandbox in Deepwater yet; an iPhone or iPad cannot start Python at all. On these, the header says so and nothing is installed. On the Mac, training needs Python 3.11 or later, from python.org or Homebrew. The header says whether Python was found and what state the environment is in. **INSTALL**, after asking, creates Deepwater's own Python environment in its data folder and downloads scikit-learn, pandas, NumPy, SciPy, LightGBM and XGBoost, as ready-built packages only (about 300 MB); **CANCEL** stops it while it installs.

**INSTALL PYTORCH** appears when a family needs it and adds PyTorch, processor only (about 700 MB). An algorithm that cannot run here is listed **unavailable** or **needs PyTorch**, with the reason in its tooltip.

### Families

| Family | What it learns |
|---|---|
| SUPERVISED | Predicts a column from the others, from labelled history (a class or a value) |
| UNSUPERVISED | Finds groups of similar periods, regimes, unusual days, or fewer dimensions; no target |
| SEMI-SUPERVISED | Learns from a few labelled rows and many unlabelled ones (classes only) |
| SELF-SUPERVISED | Learns from the data itself (hidden or next parts), then reuses what it learned (PyTorch) |
| REINFORCEMENT | An agent learns to be long, short or flat on a market's history, rewarded by its simulated profit and loss; no order is ever sent |

### Data

Five sources can feed a training. Changing the source, market or interval loads the data at once, and **LOAD** loads it again; the page then shows its title, rows, period and source.

| Source | What it loads |
|---|---|
| PRICES | One market's **1D**, **1W** or **1H** bars from Yahoo, with features from past bars only: returns, volatility, RSI, distance to moving averages, range. Daily and weekly since 1990, hourly over the last 729 days |
| MACRO | One economy's monthly table; quarterly values are carried over their months, and rows are dated by reference period, not release |
| COT | One CFTC market: nets per category, open interest and the future's close |
| CSV | Any CSV with a header row, picked with **CHOOSE FILE…**; a date column is found by its name |
| TECHNICAL DATA | A pair of Data › Technical data: a timeframe and its last 50,000 to 1,000,000 bars; **order flow** adds its footprint, depth and open interest columns, keeping only the bars they cover |

Technical data comes from the pairs you record in [Data](help:data). Data sets are kept under 5 GB in total; past that, the oldest are deleted when a new one is written.

### Target and features

**TARGET: WHAT TO PREDICT** takes a transform, a column and how many rows ahead to look (0 to 250). The transform can be the **value**, its **change** or **% change**, its **direction (up / down)**, the **realized volatility**, the **range (high − low)**, the **size of the move**, or a **big move (yes / no)**. A sentence below spells out what will be predicted. The label changes with the family: it becomes an optional outcome for unsupervised learning, classes for semi-supervised, an optional probe for self-supervised, and the price traded for reinforcement.

**FEATURES: WHAT IT LEARNS FROM** lists the columns to tick, with **ALL** and **NONE**. The target itself is marked, since a model reading it would simply read the answer. **Past values of each feature too** adds 0 to 10 lags.

### Model

**ALGORITHM** offers, per family, the methods below; each one's tooltip says what it does.

| Family | Algorithms |
|---|---|
| Supervised | Linear / logistic, Random forest, Gradient boosting, LightGBM, XGBoost, Nearest neighbours, Support vector machine, Neural network (MLP) |
| Unsupervised | k-means (with a scan of k), Gaussian mixture, Hierarchical (latest 8,000 rows), DBSCAN, Isolation forest, Local outlier factor, Principal components (PCA), t-SNE (latest 5,000 rows) |
| Semi-supervised | Self-training, Label spreading, Label propagation |
| Self-supervised | Masked autoencoder, Contrastive (SimCLR), Next step |
| Reinforcement | Q-learning (table), Deep Q-network (DQN), Actor-critic (A2C); costs in bp per position change, long-short or long-only |

**PARAMETERS** holds the algorithm's settings, within fixed ranges. For supervised learning, **VALIDATION** sets the **Test part (latest rows)** from 5 to 50 % (20 % by default), the **Time-series CV folds** from 0 to 10, the **Task** (automatic, classification or regression), and the **Test part predicted by** option: one cut, or walk-forward retrained 5 or 10 times. **NAME** is optional; left empty, the training is named after the algorithm and the data.

**TRAIN**, which becomes **STOP** while it runs, starts one training at a time; if it cannot start, the reason is shown. Trainings run in a sandbox, with no network and no access to your files or keys.

### Runs

Each training gets a row with its name, its state (a percentage while it runs), its date and its main score against no skill. Click a row to see its result. **FOLDER** opens its folder, which holds its copy of the data, its predictions and its model files, and **DELETE** removes it with its model, after asking, though not while it runs.

A supervised result gives the training and test periods and the metrics on **TEST**, **TRAINING**, **NO SKILL** and **PERSISTENCE** (what was last seen happens again), then the walk-forward or cross-validation scores, the latest row's prediction, a chart of the test period, what the model relies on, and the confusion matrix. For a direction or a price, the test predictions are traded after costs against buy & hold, a trend rule and constant exposure. Volatility targets are judged against HAR and EWMA.

An unsupervised result describes the groups (rows, how long they last, what followed, what sets them apart), with quality scores, the groups along time, the k scan, unusual rows and why, maps and PCA loadings. A semi-supervised result shows the labels kept and hidden, compared on the same test rows with the same model on the kept labels only and on every label. A self-supervised result gives the loss per epoch, the hardest windows, a map of the embeddings, and the optional probe against the raw features and no skill.

A reinforcement result gives the return, overall and per year, the Sharpe ratio, the maximum drawdown, the trades and time in the market, against buy & hold, flat and 50 random agents, and the agent's latest choice. These outputs are not advice.

### COMPARE

Is the best setup really better, or just the luckiest of many tries? Tick the box at the left of several rows (or ⌘-click them) and press **COMPARE (n)**. With nothing ticked, **COMPARE** takes the selected training and the ones like it: the same question on the same market, or the same setup on other markets.

**SAME MARKET** ranks the setups by their edge over no skill, with p-values adjusted for the number of setups tried, checks them against persistence too, compares the best with the second on their shared test rows, and gives a verdict in words. **SEVERAL MARKETS** takes one setup and measures each market against its own reference. Reinforcement runs are compared on test return, buy & hold, excess and random agents beaten; clustering on groups and silhouette, which stays descriptive.

Every figure is computed by Deepwater from the trainings' own files, the same ones the agents quote. Click a row to open that training, and **‹ BACK TO THE COMPARISON** to return.
