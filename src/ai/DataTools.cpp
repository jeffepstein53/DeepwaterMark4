// Skill "read_data": the tools that let a local model read what Deepwater's pages show.
//
// The tools read the same services the pages use (no page needs to be open): quotes and candles (market::), calendar,
// news feed and squawk (news::, wire::), macro series and central banks (macro::), COT, retail and risk sentiment
// (positioning::), bank forecasts and research notes (banks::), sessions (global::), technical, liquidity and
// seasonality reads (analytics::, seasonality::), the fundamental scores (fundamental::), the user's track record
// (account::, without broker or login). Never the Sources and AI pages, keys, passwords or settings.
//
// Results are short plain-text tables, with the source and the time of the data, capped at kMaxChars; a tool says
// plainly when data is missing instead of leaving a gap the model could fill with an invented figure.
#include "ai/Tools.h"
#include "macrodata/MacroDataStore.h"
#include "technicaldata/Collector.h"
#include "technicaldata/TechnicalDataIndicators.h"
#include "technicaldata/TechnicalDataStore.h"
#include "technicaldata/TechnicalDataSpec.h"
#include "technicaldata/TechnicalDataBars.h"
#include "technicaldata/Dukascopy.h"
#include "account/Journal.h"
#include "account/Stats.h"
#include "analytics/Liquidity.h"
#include "analytics/Technical.h"
#include "banks/Forecasts.h"
#include "banks/Registry.h"
#include "banks/Research.h"
#include "fundamental/Fundamental.h"
#include "global/Sessions.h"
#include "macro/CentralBanks.h"
#include "macro/MacroStore.h"
#include "market/Candles.h"
#include "market/Instruments.h"
#include "market/QuoteHub.h"
#include "news/Calendar.h"
#include "news/Feed.h"
#include "positioning/Cot.h"
#include "positioning/Retail.h"
#include "positioning/RiskIndex.h"
#include "seasonality/Seasonality.h"
#include "wire/Wire.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QPointer>
#include <QTimer>
#include <QTimeZone>
#include <cmath>
#include <memory>
#include <tuple>

namespace ai {
namespace {
constexpr int kMaxChars = 6000;

QString num(double v, int dec = 2) { return std::isfinite(v) ? QString::number(v, 'f', dec) : QStringLiteral("n/a"); }
QString pct(double v, int dec = 2) { return std::isfinite(v) ? (v > 0 ? "+" : "") + QString::number(v, 'f', dec) + "%" : QStringLiteral("n/a"); }
QString utc(const QDateTime& t) { return t.isValid() ? t.toUTC().toString("yyyy-MM-dd HH:mm") + " UTC" : QStringLiteral("n/a"); }
// "12 s ago", "7 min ago", "2.5 h ago": how old a datum is, so a tool never hands over an old figure as a fresh one
QString ageText(qint64 ms) {
  const qint64 s = qMax<qint64>(0, ms) / 1000;
  if (s < 90) return QString::number(s) + " s ago";
  if (s < 5400) return QString::number((s + 30) / 60) + " min ago";
  return QString::number(double(s) / 3600.0, 'f', 1) + " h ago";
}
QString ageText(const QDateTime& t) { return t.isValid() ? ageText(t.msecsTo(QDateTime::currentDateTimeUtc())) : QStringLiteral("unknown age"); }
// `tail`: what must be said even when the list before it is cut (how old the data is, what is not available)
QString cap(QString s, const QString& tail = {}) {
  const qsizetype room = kMaxChars - tail.size();
  if (s.size() > room)
    s = s.left(room) + "\n[cut: result longer than " + QString::number(kMaxChars) + " characters; ask for less]" + (tail.isEmpty() ? "" : "\n");
  return s + tail;
}
ToolResult okText(const QString& s, const QString& tail = {}) { return {true, cap(s, tail)}; }
ToolResult fail(const QString& s) { return {false, s}; }
int intArg(const QJsonObject& a, const char* k, int def, int lo, int hi) {
  const QJsonValue v = a.value(k);
  const int n = v.isDouble() ? v.toInt() : v.isString() ? v.toString().toInt() : def;
  return qBound(lo, n == 0 && !v.isDouble() && !v.isString() ? def : n, hi);
}
QString strArg(const QJsonObject& a, const char* k) { return a.value(k).isString() ? a.value(k).toString().trimmed() : QString(); }
QStringList listArg(const QJsonObject& a, const char* k) {
  QStringList out;
  const QJsonValue v = a.value(k);
  if (v.isArray()) for (const auto& x : v.toArray()) out << x.toString().trimmed();
  else if (v.isString()) out = v.toString().split(QRegularExpression("[,;]"), Qt::SkipEmptyParts);
  for (auto& s : out) s = s.trimmed();
  out.removeAll(QString());
  return out;
}

// "EURUSD", "EUR/USD", "eur usd", "gold", "S&P 500", "BTC" → the app's instrument
const market::Instrument* resolve(const QString& q0) {
  const QString q = q0.trimmed();
  if (q.isEmpty()) return nullptr;
  if (auto* i = market::find(q)) return i;
  static const QRegularExpression notAlnum("[^A-Z0-9]");   // compiled once (was one per instrument per call)
  QString compact = q.toUpper(); compact.remove(notAlnum);
  for (const auto& i : market::instruments()) {
    QString s = i.symbol.toUpper(); s.remove(notAlnum);
    if (s == compact) return &i;
  }
  for (const auto& i : market::instruments()) if (i.name.compare(q, Qt::CaseInsensitive) == 0) return &i;
  for (const auto& i : market::instruments()) if (i.name.contains(q, Qt::CaseInsensitive)) return &i;
  if (compact.size() == 3)   // a currency or crypto code alone
    for (const auto& i : market::instruments()) if (i.base == compact || i.symbol.startsWith(compact)) return &i;
  return nullptr;
}
QString unknownInstrument(const QString& q) {
  QStringList some;
  for (const auto& i : market::instruments()) { if (some.size() >= 40) break; some << i.symbol; }
  return "error: no instrument matches \"" + q + "\". Known symbols include: " + some.join(", ") + " …";
}

// Waits for `signal` of `sender` (or `ms` milliseconds), then calls `then` once. The context dies with it.
template <typename Sender, typename Signal>
void waitSignal(Sender* sender, Signal signal, int ms, std::function<void()> then) {
  auto* ctx = new QObject(qApp);
  auto once = std::make_shared<bool>(false);
  auto fire = [ctx, once, then] { if (*once) return; *once = true; ctx->deleteLater(); then(); };
  QObject::connect(sender, signal, ctx, [fire] { fire(); });
  QTimer::singleShot(ms, ctx, [fire] { fire(); });
}

// Waits until `ready()` holds, re-checking at each `signal` (data sets arrive one by one), or `ms` milliseconds.
template <typename Sender, typename Signal>
void waitUntil(Sender* sender, Signal signal, std::function<bool()> ready, int ms, std::function<void()> then) {
  if (ready()) { then(); return; }
  auto* ctx = new QObject(qApp);
  auto once = std::make_shared<bool>(false);
  auto fire = [ctx, once, then] { if (*once) return; *once = true; ctx->deleteLater(); then(); };
  QObject::connect(sender, signal, ctx, [fire, ready] { if (ready()) fire(); });
  QTimer::singleShot(ms, ctx, [fire] { fire(); });
}
QString impactText(news::Impact i) {   // English for the model, whatever the interface language
  switch (i) { case news::Impact::High: return "high"; case news::Impact::Medium: return "medium";
               case news::Impact::Low: return "low"; case news::Impact::Holiday: return "holiday"; }
  return {};
}

// country argument: ISO2 ("US"), currency ("USD") or English name ("Japan")
QString iso2Of(const QString& q0) {
  const QString q = q0.trimmed();
  if (q.size() == 2) return q.toUpper();
  if (q.size() == 3) {
    const QString up = q.toUpper();
    if (up == "EUR") return "EZ";
    const macro::CurrencyMacro cm = macro::MacroStore::instance().forCurrency(up);
    if (!cm.iso2.isEmpty()) return cm.iso2.toUpper();
  }
  if (q.compare("euro area", Qt::CaseInsensitive) == 0 || q.compare("eurozone", Qt::CaseInsensitive) == 0) return "EZ";
  for (int t = 1; t <= int(QLocale::LastTerritory); ++t) {
    const auto terr = QLocale::Territory(t);
    if (QLocale::territoryToString(terr).compare(q, Qt::CaseInsensitive) == 0) return QLocale::territoryToCode(terr);
  }
  return q.toUpper();
}

// ------------------------------------------------------------------------------------------------ quotes
void quotes(const QJsonObject& a, ToolDone done) {
  QStringList asked = listArg(a, "symbols");
  QList<const market::Instrument*> ins;
  QStringList unknown;
  for (const auto& s : asked) { if (auto* i = resolve(s)) ins << i; else unknown << s; }
  if (asked.isEmpty()) {   // no symbol: the majors of each class
    for (const char* s : {"EURUSD", "GBPUSD", "USDJPY", "AUDUSD", "USDCAD", "USDCHF", "XAUUSD", "CL", "SPX", "NDX", "BTCUSD", "US10Y"})
      if (auto* i = resolve(s)) ins << i;
  }
  if (ins.isEmpty()) { done(fail(unknownInstrument(unknown.join(", ")))); return; }
  if (ins.size() > 20) ins = ins.mid(0, 20);
  QStringList syms; for (auto* i : ins) syms << i->symbol;
  auto* owner = new QObject(qApp);
  auto& hub = market::QuoteHub::instance();
  auto answer = [owner, ins, unknown, done] {
    auto& h = market::QuoteHub::instance();
    QString out = "Live quotes (Yahoo Finance, delayed for some exchanges)\nsymbol | name | price | change | change % | day low | day high | time\n";
    for (auto* i : ins) {
      const auto q = h.quote(i->symbol);
      if (!q.valid()) { out += i->symbol + " | " + i->name + " | not available now\n"; continue; }
      out += QString("%1 | %2 | %3 | %4 | %5 | %6 | %7 | %8\n").arg(i->symbol, i->name, num(q.price, i->decimals), num(q.change, i->decimals),
                                                                pct(q.changePct), num(q.dayLow, i->decimals), num(q.dayHigh, i->decimals),
                                                                utc(q.time) + (q.time.secsTo(QDateTime::currentDateTimeUtc()) > 3600
                                                                    ? " (last trade, not now: market closed or no recent trade)" : ""));
    }
    if (!unknown.isEmpty()) out += "Unknown symbols (not read): " + unknown.join(", ") + "\n";
    // how fresh the answer is: the "time" column is the last trade (old for a closed market), this is when Deepwater asked
    qint64 oldest = -1; bool unknownAge = false;
    for (auto* i : ins) {
      if (!h.quote(i->symbol).valid()) continue;
      const qint64 age = h.ageMs(i->symbol);
      if (age < 0) unknownAge = true; else oldest = qMax(oldest, age);
    }
    if (unknownAge || oldest > 60000)
      out += "WARNING: the refresh did not complete (Yahoo did not answer in time): " +
             (unknownAge ? QString("some quotes are cached copies of unknown age") : "the oldest quote was fetched " + ageText(oldest)) + ".\n";
    else if (oldest >= 0)
      out += "Fetched from Yahoo at " + QDateTime::currentDateTimeUtc().addMSecs(-oldest).toString("HH:mm:ss") + " UTC (" + ageText(oldest) + ").\n";
    market::QuoteHub::instance().unsubscribe(owner);
    owner->deleteLater();
    done(okText(out));
  };
  // A quote nothing has refreshed for more than 15 s is asked again before answering: pages poll only while shown, so
  // the memory can hold a quote of half an hour ago (measured 2026-11-24: 35 min, labelled "Live quotes").
  hub.subscribe(owner, syms, false);
  if (hub.refreshStale(syms, 15000) == 0) { answer(); return; }
  waitUntil(&hub, &market::QuoteHub::updated, [syms] {
    auto& h = market::QuoteHub::instance();
    for (const auto& s : syms) { const qint64 age = h.ageMs(s); if (age < 0 || age > 20000) return false; }
    return true;
  }, 8000, answer);
}

// ------------------------------------------------------------------------------------------------ history
void history(const QJsonObject& a, ToolDone done) {
  const QString q = strArg(a, "symbol");
  auto* i = resolve(q);
  if (!i) { done(fail(unknownInstrument(q))); return; }
  QString interval = strArg(a, "interval").toLower();
  if (!QStringList{"1h", "1d", "1wk", "1mo"}.contains(interval)) interval = "1d";
  const int bars = intArg(a, "bars", 30, 5, 120);
  const QString range = interval == "1h" ? "1mo" : interval == "1d" ? "1y" : interval == "1wk" ? "5y" : "10y";
  const QString sym = i->symbol, name = i->name; const int dec = i->decimals;
  market::candles(sym, interval, range, qApp, [=](const market::Series& s) {
    if (!s.ok()) { done(fail("error: no price history for " + sym + (s.error.isEmpty() ? "" : " (" + s.error + ")"))); return; }
    const int from = qMax(0, int(s.bars.size()) - bars);
    double hi = -INFINITY, lo = INFINITY;
    for (int k = from; k < s.bars.size(); ++k) { hi = qMax(hi, s.bars[k].h); lo = qMin(lo, s.bars[k].l); }
    const double first = s.bars[from].o, last = s.bars.last().c;
    QString out = QString("%1 (%2) %3 bars, Yahoo Finance%4\nperiod: %5 → %6 | change %7 | high %8 | low %9\ntime (UTC) | open | high | low | close\n")
                      .arg(name, sym, interval, s.stale ? " (cached copy, network failed)" : "",
                           QDateTime::fromSecsSinceEpoch(s.bars[from].t, QTimeZone::UTC).toString("yyyy-MM-dd HH:mm"),
                           QDateTime::fromSecsSinceEpoch(s.bars.last().t, QTimeZone::UTC).toString("yyyy-MM-dd HH:mm"),
                           pct((last / first - 1) * 100), num(hi, dec), num(lo, dec));
    for (int k = from; k < s.bars.size(); ++k) {
      const auto& b = s.bars[k];
      out += QDateTime::fromSecsSinceEpoch(b.t, QTimeZone::UTC).toString(interval == "1h" ? "yyyy-MM-dd HH:mm" : "yyyy-MM-dd") +
             " | " + num(b.o, dec) + " | " + num(b.h, dec) + " | " + num(b.l, dec) + " | " + num(b.c, dec) + "\n";
    }
    done(okText(out));
  }, 60);   // cache 60 s (the pages keep 30 min for a day bar, 5 min for an hour): the last bar must be this minute's
}

// ------------------------------------------------------------------------------------------------ calendar
void calendar(const QJsonObject& a, ToolDone done) {
  const int ahead = intArg(a, "days_ahead", 7, 0, 13), back = intArg(a, "days_back", 0, 0, 13);
  QStringList ccys = listArg(a, "currency");   // "USD" or "EUR,USD" or ["EUR","USD"]
  for (auto& c : ccys) c = c.toUpper();
  const QString ccy = ccys.join(",");
  const QString minImp = strArg(a, "min_impact").toLower();
  const int minI = minImp == "high" ? 3 : minImp == "medium" ? 2 : 1;
  const QDateTime from = QDateTime::currentDateTimeUtc().addDays(-back).addSecs(back == 0 ? -6 * 3600 : 0);
  const QDateTime to = QDateTime::currentDateTimeUtc().addDays(ahead);
  auto& cal = news::Calendar::instance();
  QList<QDate> weeks;
  for (QDate w = news::Calendar::weekStart(from.date()); w <= to.date(); w = w.addDays(7)) weeks << w;
  auto answer = [=] {
    auto& c = news::Calendar::instance();
    QString out = "Economic calendar (ForexFactory schedule and forecasts; actuals from Myfxbook / headlines / official series)\n"
                  "from " + utc(from) + " to " + utc(to) + (ccy.isEmpty() ? "" : ", currency " + ccy) + "\n"
                  "Grouped by day (UTC). Each line: time | currency | impact | event | actual | forecast | previous\n";
    int n = 0;
    QDate day;
    QStringList notes;
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();   // past events without an actual: released, just not read
    for (const QDate& w : weeks) {
      if (!c.hasData(w)) { notes << "week of " + w.toString(Qt::ISODate) + ": " + (c.weekNote(w).isEmpty() ? "no data" : c.weekNote(w)); continue; }
      for (const auto& e : c.events(w)) {
        if (e.time < from || e.time > to) continue;
        if (!ccys.isEmpty() && !ccys.contains(e.currency)) continue;
        if (int(e.impact) < minI) continue;
        const QDate d = e.time.toUTC().date();
        if (d != day) { day = d; out += "\n== " + QLocale(QLocale::English).toString(d, "dddd d MMMM yyyy") + " ==\n"; }
        out += (e.allDay ? QString("all day") : e.time.toUTC().toString("HH:mm") + " UTC") + " | " + e.currency + " | " +
               impactText(e.impact) + " | " + e.title + " | " + (e.actual.has() ? e.actual.text : e.time > nowUtc ? "not released yet" : "not read (released, no source gave the figure)") + " | " +
               (e.forecast.has() ? e.forecast.text : "none") + " | " + (e.previous.has() ? e.previous.text : "none") + "\n";
        ++n;
      }
    }
    if (n == 0) out += "(no event matches)\n";
    done(okText(out, (notes.isEmpty() ? QString() : "Not available: " + notes.join("; ") + "\n") +
                     "Actuals feed last downloaded " + ageText(c.lastActuals()) + " (asked at most every 5 min: the source's own limit).\n"));
  };
  auto proceed = [=] {
    auto& c = news::Calendar::instance();
    QList<QDate> missing;
    for (const QDate& w : weeks) if (!c.hasData(w)) missing << w;
    if (missing.isEmpty()) { answer(); return; }
    for (const QDate& w : missing) c.load(w);
    waitSignal(&c, &news::Calendar::weekLoaded, 6000, answer);
  };
  // The actuals (figures just released) come from a feed polled only while a page is shown: ask it once more now.
  // A copy younger than 5 min is served without a request (the source's own limit, news/ActualsPace.h).
  cal.refreshActuals();
  waitSignal(&cal, &news::Calendar::actualsPolled, 6000, proceed);
}

// ------------------------------------------------------------------------------------------------ news
void newsFeed(const QJsonObject& a, ToolDone done) {
  const QString q = strArg(a, "query");
  const int max = intArg(a, "max", 10, 1, 25);
  auto answer = [=] {
    QString out = "News feed (public RSS feeds, newest first)" + (q.isEmpty() ? QString() : ", matching \"" + q + "\"") + "\n";
    int n = 0;
    for (const auto& s : news::Feed::instance().stories()) {
      if (s.lifestyle) continue;
      if (!q.isEmpty() && !s.title.contains(q, Qt::CaseInsensitive) && !s.summary.contains(q, Qt::CaseInsensitive)) continue;
      out += "- " + utc(s.time) + " | " + s.source + " | " + s.title + (s.summary.isEmpty() ? "" : "\n  " + s.summary.left(280)) + "\n";
      if (++n >= max) break;
    }
    if (n == 0) out += "(no story matches)\n";
    done(okText(out, "Newest feed download " + ageText(news::Feed::instance().lastRefresh()) + " (each source is asked at its own pace, 2 to 10 min).\n"));
  };
  // Every source whose own pause has passed is asked again, and the answer waits for all of them. It used to answer at
  // the first one back and to trust a 10-minute-old download: on 2026-11-24 the first call after a pause gave stories
  // 54 minutes old, the second (three minutes later) the fresh ones.
  auto& feed = news::Feed::instance();
  auto* owner = new QObject(qApp);
  feed.setActive(owner, true);
  auto finish = [answer, owner] { news::Feed::instance().setActive(owner, false); owner->deleteLater(); answer(); };
  if (!feed.busy()) { finish(); return; }
  waitSignal(&feed, &news::Feed::settled, 12000, finish);
}

void squawk(const QJsonObject& a, ToolDone done) {
  const int max = intArg(a, "max", 20, 1, 50);
  auto answer = [max, done] {
    auto& w = wire::Wire::instance();
    const auto items = w.items(wire::Filter::All, max);
    QString out = "Squawk / live wire (FinancialJuice, investingLive, central banks; movers detected by Deepwater), newest first\n";
    for (const auto& i : items) {
      out += utc(i.time) + " | " + (i.kind == wire::Kind::Mover ? "MOVER" : i.kind == wire::Kind::Release ? "RELEASE" : "HEADLINE") +
             " | " + i.source + " | " + i.text + "\n";
    }
    if (items.isEmpty()) out += "(the wire has no item yet)\n";
    done(okText(out, "Feeds last polled " + ageText(w.lastFetch()) + " (each feed at the pace its publisher asks: 1 to 10 min)." +
                     (w.feedErrors().isEmpty() ? QString() : " Feed errors: " + w.feedErrors().join("; ") + ".") + "\n"));
  };
  // Feeds follow their idle pace (10 to 30 min) while no page is shown: poll the ones past their active pace now
  auto& w = wire::Wire::instance();
  w.refreshNow();
  if (!w.busy()) { answer(); return; }
  waitSignal(&w, &wire::Wire::settled, 10000, answer);
}

// ------------------------------------------------------------------------------------------------ macro
void macroData(const QJsonObject& a, ToolDone done) {
  const QString country = strArg(a, "country");
  if (country.isEmpty()) { done(fail("error: give a country (ISO code like US, GB, JP, EZ, a currency like USD, or a name)")); return; }
  const QString iso = iso2Of(country);
  const QString indArg = strArg(a, "indicator").toLower();
  auto& store = macro::MacroStore::instance();
  store.ensureLoaded();
  auto answer = [=] {
    auto& st = macro::MacroStore::instance();
    QString out = "Macro data for " + iso + " (official series: BIS, OECD, World Bank, central banks)\n";
    int found = 0;
    for (int k = 0; k < int(macro::Ind::Count); ++k) {
      const auto ind = macro::Ind(k);
      const auto& inf = macro::info(ind);
      if (!indArg.isEmpty() && !inf.name.toLower().contains(indArg) && !inf.shortName.toLower().contains(indArg)) continue;
      const macro::Series s = st.series(iso, ind);
      if (!s.ok()) continue;
      ++found;
      if (indArg.isEmpty()) {   // overview: latest value of each indicator
        out += inf.name + ": " + macro::formatValue(ind, s.last().value) + " (" + macro::formatPeriod(s.last().date, s.freq) +
               ", previous " + macro::formatValue(ind, s.prior().value) + ", source " + s.source + ")\n";
      } else {                  // one indicator: its recent history
        out += inf.name + " — source " + s.source + " " + s.dataset + "\nperiod | value\n";
        for (int p = qMax(0, int(s.pts.size()) - 24); p < s.pts.size(); ++p)
          out += macro::formatPeriod(s.pts[p].date, s.freq) + " | " + macro::formatValue(ind, s.pts[p].value) + "\n";
      }
    }
    const auto pr = st.policyRate(iso);
    if (pr.valid() && (indArg.isEmpty() || indArg.contains("policy") || indArg.contains("rate")))
      out += "Policy rate (latest): " + num(pr.rate) + "% as of " + pr.asOf.toString(Qt::ISODate) + " (" + pr.sourceName + ")\n";
    if (found == 0) out += st.loading() ? "(data still loading: ask again in a moment)\n" : "(no series for this country / indicator)\n";
    done(okText(out));
  };
  waitUntil(&store, &macro::MacroStore::updated, [iso] {
    auto& st = macro::MacroStore::instance();
    return !st.loading() || (st.series(iso, macro::Ind::Inflation).ok() && st.series(iso, macro::Ind::Unemployment).ok());
  }, 20000, answer);
}

void centralBanks(const QJsonObject& a, ToolDone done) {
  const QString key = strArg(a, "bank");
  auto& cb = macro::CentralBanks::instance();
  cb.refreshOdds(300);   // the Fed's and the ECB's, past 5 min: asked now, the answer waits for the Fed's (below)
  const QDateTime asked = QDateTime::currentDateTime();
  auto answer = [=] {
    auto& c = macro::CentralBanks::instance();
    QString out = "Central banks (rates from official feeds; odds computed by Deepwater from futures where priced)\n";
    auto line = [&](const macro::Bank& b, bool full) {
      const auto next = b.nextMeeting();
      const auto o = b.nextOdds();
      out += b.name + " (" + b.code + ", " + b.currency + "): rate " + (!b.rateText.isEmpty() ? b.rateText : std::isfinite(b.rate) ? num(b.rate) + "%" : QString("not loaded")) +
             (b.rateAsOf.isValid() ? " as of " + b.rateAsOf.toString(Qt::ISODate) : QString()) + " | next meeting " + (next.decision.isValid() ? next.decision.toString(Qt::ISODate) : "n/a");
      if (o.valid()) out += " | odds cut " + num(o.cut() * 100, 0) + "% hold " + num(o.hold * 100, 0) + "% hike " + num(o.hike() * 100, 0) +
                            "% | implied " + num(b.impliedBp, 1) + " bp | bias " + b.bias() +
                             (b.pricedAt.isValid() ? " | odds computed " + ageText(b.pricedAt.toUTC()) : QString());
      else out += " | no market pricing" + (b.pricingNote.isEmpty() ? QString() : " (" + b.pricingNote + ")");
      out += "\n";
      if (!full) return;
      if (!b.path.isEmpty()) {
        out += "  implied path: ";
        QStringList p; for (const auto& pt : b.path) p << pt.meeting.decision.toString("yyyy-MM-dd") + " " + num(pt.rate, 2) + "%";
        out += p.join(", ") + " (" + b.pricingSource + ")\n";
      }
      for (int k = 0; k < qMin(4, int(b.decisions.size())); ++k)
        out += "  decision " + b.decisions[k].date.toString(Qt::ISODate) + ": " + b.decisions[k].text +
               (b.decisions[k].vote.isEmpty() ? "" : " (vote " + b.decisions[k].vote + ")") + "\n";
    };
    if (!key.isEmpty()) {
      const macro::Bank* b = c.bank(key);
      if (!b) { done(fail("error: unknown central bank \"" + key + "\" (FED ECB BOE BOJ SNB RBA RBNZ BOC, or their currency)")); return; }
      line(*b, true);
    } else {
      for (const auto& b : c.banks()) line(b, false);
    }
    if (c.banks().isEmpty()) out += "(still loading: ask again in a moment)\n";
    done(okText(out));
  };
  waitUntil(&cb, &macro::CentralBanks::updated, [asked] {
    int rates = 0;
    for (const auto& b : macro::CentralBanks::instance().banks()) if (std::isfinite(b.rate)) ++rates;
    const macro::Bank* fed = macro::CentralBanks::instance().bank("FED");
    const bool oddsIn = !fed || (fed->priced && fed->pricedAt.isValid() && fed->pricedAt.secsTo(asked) < 300);
    return rates >= 6 && oddsIn;
  }, 15000, answer);
}

// ------------------------------------------------------------------------------------------------ positioning
void cot(const QJsonObject& a, ToolDone done) {
  const QString q = strArg(a, "market");
  const positioning::CotMarket* m = positioning::Cot::market(q.toUpper());
  if (!m) m = positioning::Cot::marketForSymbol(q);
  if (!m) {
    QStringList ids; for (const auto& x : positioning::Cot::markets()) ids << x.id;
    done(fail("error: unknown COT market \"" + q + "\". Markets: " + ids.join(", ")));
    return;
  }
  const int weeks = intArg(a, "weeks", 6, 1, 26);
  positioning::Cot::instance().history(m->id, qApp, [=](const positioning::CotHistory& h) {
    if (!h.ok()) { done(fail("error: COT data not available (" + h.error + ")")); return; }
    QString out = "CFTC Commitments of Traders — " + h.market.contract + " (positions as of each report's Tuesday)" +
                  (h.stale ? " [cached copy, network failed]" : "") + "\n";
    const int from = qMax(0, int(h.weeks.size()) - weeks);
    QStringList head{"date", "open interest"};
    for (const auto& c : h.weeks.last().legacy) head << c.name + " net";
    for (const auto& c : h.weeks.last().second) head << c.name + " net";
    out += head.join(" | ") + "\n";
    for (int k = from; k < h.weeks.size(); ++k) {
      const auto& w = h.weeks[k];
      QStringList row{w.date.toString(Qt::ISODate), num(w.oi, 0)};
      for (const auto& c : w.legacy) row << num(c.net(), 0);
      for (const auto& c : w.second) row << num(c.net(), 0);
      out += row.join(" | ") + "\n";
    }
    done(okText(out));
  });
}

void retail(const QJsonObject& a, ToolDone done) {
  auto& r = positioning::Retail::instance();
  const auto& snap = r.latest();
  if (snap.isEmpty()) {
    done(fail(r.state() == positioning::Retail::State::Connected ? "retail sentiment: no answer received yet"
                                                                  : "retail sentiment needs the user's Myfxbook login (Positioning › Retail Sentiment); not connected, nothing read"));
    return;
  }
  const QString q = strArg(a, "symbol").toUpper().remove('/');
  QString out = "Retail trader positioning (Myfxbook community outlook, received " + utc(snap.time) + ")\nsymbol | long % | short % | positions | avg long price | avg short price\n";
  int n = 0;
  for (const auto& s : snap.symbols) {
    if (!q.isEmpty() && s.name != q) continue;
    out += s.display() + " | " + num(s.longPct, 0) + " | " + num(s.shortPct, 0) + " | " + num(s.totalPositions, 0) + " | " +
           num(s.avgLong, 5) + " | " + num(s.avgShort, 5) + "\n";
    if (++n >= 40) break;
  }
  if (n == 0) out += "(no symbol " + q + " in the outlook)\n";
  done(okText(out));
}

void risk(const QJsonObject&, ToolDone done) {
  auto& ri = positioning::RiskIndex::instance();
  auto answer = [done] {
    const auto& d = positioning::RiskIndex::instance().data();
    if (!d.ok()) { done(fail("risk sentiment not available" + (d.error.isEmpty() ? QString() : ": " + d.error))); return; }
    QString out = "Risk sentiment index (computed by Deepwater, 0 = extreme risk-off, 100 = extreme risk-on), inputs fetched " +
                  d.updated.toUTC().toString("yyyy-MM-dd HH:mm") + " UTC (" + ageText(d.updated.msecsTo(QDateTime::currentDateTime())) + ")\n";
    out += "composite " + num(d.value, 0) + " (" + positioning::RiskIndex::zoneLabel(d.value) + "), week ago " + num(d.weekAgo, 0) +
           ", month ago " + num(d.monthAgo, 0) + ", components available " + QString::number(d.available) + "/7\n";
    out += "component | reading | percentile\n";
    for (const auto& c : d.components) out += c.name + " | " + (c.rawText.isEmpty() ? "n/a" : c.rawText) + " | " + num(c.percentile, 0) + "\n";
    done(okText(out));
  };
  // inputs older than 5 min are fetched again (the page reloads every 30 min and only while it is shown)
  const bool old = ri.data().ok() && ri.data().updated.isValid() && ri.data().updated.secsTo(QDateTime::currentDateTime()) > 300;
  if (!ri.loading() && (!ri.data().ok() || old)) ri.load(300);
  if (!ri.data().ok() || ri.loading()) waitSignal(&ri, &positioning::RiskIndex::updated, 15000, answer); else answer();
}

// ------------------------------------------------------------------------------------------------ banks
void bankForecasts(const QJsonObject& a, ToolDone done) {
  auto& f = banks::Forecasts::instance();
  const QString q = strArg(a, "market").toUpper().remove('/');
  auto answer = [=] {
    auto& fc = banks::Forecasts::instance();
    QList<banks::Forecast> all = fc.forecasts(q);
    if (all.isEmpty()) {
      QStringList ids; for (const auto& m : banks::markets()) ids << m.id;
      done(fail(fc.error().isEmpty() ? "no bank forecast for \"" + q + "\". Markets: " + ids.join(", ") : "bank forecasts not available: " + fc.error()));
      return;
    }
    QString out = "Bank forecasts (published tables; only ING publishes free machine-readable ones)\nbank | market | quarter | forecast | revised\n";
    for (const auto& x : all) {
      const auto* m = banks::market(x.marketId);
      const auto* b = banks::bank(x.bankId);
      out += (b ? b->shortName : x.bankId) + " | " + (m ? m->display : x.marketId) + " | " + QString("Q%1 %2").arg(x.quarter.q).arg(x.quarter.year) + " | " +
             num(x.value, m ? m->decimals : 4) + (m && !m->unit.isEmpty() ? " " + m->unit : "") + " | " + x.revised.toString(Qt::ISODate) + "\n";
    }
    done(okText(out));
  };
  if (!f.loaded()) { f.refresh(); waitSignal(&f, &banks::Forecasts::updated, 10000, answer); } else answer();
}

void bankResearch(const QJsonObject& a, ToolDone done) {
  auto& r = banks::Research::instance();
  const QString bank = strArg(a, "bank").toLower();
  const int max = intArg(a, "max", 10, 1, 25);
  auto answer = [=] {
    QString out = "Bank research notes (public research feeds), newest first\n";
    int n = 0;
    for (const auto& note : banks::Research::instance().notes()) {
      const auto* b = banks::bank(note.bankId);
      if (!bank.isEmpty() && note.bankId != bank && !(b && b->shortName.toLower() == bank)) continue;
      out += "- " + utc(note.published) + " | " + (b ? b->shortName : note.bankId) + " | " + note.type + " | " + note.title +
             (note.summary.isEmpty() ? "" : "\n  " + note.summary.left(280)) + "\n";
      if (++n >= max) break;
    }
    if (n == 0) out += "(no note)\n";
    done(okText(out));
  };
  if (r.notes().isEmpty()) { r.refresh(); waitSignal(&r, &banks::Research::updated, 10000, answer); } else answer();
}

// ------------------------------------------------------------------------------------------------ sessions
void marketHours(const QJsonObject&, ToolDone done) {
  const QDateTime now = global::nowUtc();
  QString out = "Forex sessions now (" + utc(now) + ")\nsession | state | opens / closes (UTC)\n";
  for (const auto& s : global::sessions()) {
    if (global::isOpen(s, now)) {
      const auto sp = global::currentSpan(s, now);
      out += s.name + " | OPEN | closes " + utc(sp.close) + "\n";
    } else {
      const auto sp = global::nextSpan(s, now);
      out += s.name + " | closed | opens " + utc(sp.open) + "\n";
    }
  }
  done(okText(out));
}

// ------------------------------------------------------------------------------------------------ analytics
void technical(const QJsonObject& a, ToolDone done) {
  const QString q = strArg(a, "symbol");
  auto* i = resolve(q);
  if (!i) { done(fail(unknownInstrument(q))); return; }
  const QString sym = i->symbol, name = i->name; const int dec = i->decimals;
  const bool crypto = i->category == market::Category::Crypto;
  market::candles(sym, "1d", "1y", qApp, [=](const market::Series& s) {
    if (!s.ok()) { done(fail("error: no daily history for " + sym)); return; }
    const auto r = analytics::technicalRead(s.bars, {}, crypto, QDateTime::currentSecsSinceEpoch() / 86400);
    if (!r.ok) { done(fail("technical read not available: " + r.error)); return; }
    auto lean = [](analytics::Lean l) { return l == analytics::Lean::Bull ? "bullish" : l == analytics::Lean::Bear ? "bearish" : "neutral"; };
    QString out = "Technical read of " + name + " (" + sym + "), computed by Deepwater from daily Yahoo closes, last close " +
                  QDateTime::fromSecsSinceEpoch(r.lastTime, QTimeZone::UTC).toString("yyyy-MM-dd") + "\n";
    out += "last " + num(r.last, dec) + " | EMA20 " + num(r.ema20, dec) + " | EMA50 " + num(r.ema50, dec) + " | trend " + lean(r.trend) +
           " (" + pct(r.trendPct) + ")\n";
    out += "RSI(14) " + num(r.rsi, 1) + " | ROC(10) " + pct(r.roc10) + " | momentum " +
           (r.momentum == analytics::Momentum::Overbought ? "overbought" : r.momentum == analytics::Momentum::Oversold ? "oversold" : "neutral") + "\n";
    out += "ATR " + num(r.atrPct, 2) + "% of price (percentile " + num(r.atrPctile, 0) + ") | annualised volatility " + num(r.volAnn, 1) +
           "% | structure " + lean(r.structure) + "\n";
    done(okText(out));
  }, 60);
}

void liquidity(const QJsonObject& a, ToolDone done) {
  const QString q = strArg(a, "symbol");
  auto* i = resolve(q);
  if (!i) { done(fail(unknownInstrument(q))); return; }
  const QString sym = i->symbol, name = i->name; const int dec = i->decimals;
  market::candles(sym, "1h", "2y", qApp, [=](const market::Series& s) {
    if (!s.ok()) { done(fail("error: no hourly history for " + sym)); return; }
    const auto pools = analytics::findPools(s.bars);
    const double last = s.bars.last().c;
    QList<analytics::Pool> open;
    for (const auto& p : pools) if (p.state == analytics::PoolState::Untested) open << p;
    std::sort(open.begin(), open.end(), [last](const auto& x, const auto& y) { return std::abs(x.level - last) < std::abs(y.level - last); });
    QString out = "Liquidity pools of " + name + " (" + sym + "), equal highs / lows found by Deepwater on 1h Yahoo bars; last " +
                  num(last, dec) + "\nside | level | distance | touches\n";
    for (int k = 0; k < qMin(10, int(open.size())); ++k) {
      const auto& p = open[k];
      out += QString(p.side == analytics::PoolSide::BuySide ? "buy-side (stops above equal highs)" : "sell-side (stops below equal lows)") +
             " | " + num(p.level, dec) + " | " + pct((p.level / last - 1) * 100) + " | " + QString::number(p.touches) + "\n";
    }
    if (open.isEmpty()) out += "(no untested pool)\n";
    done(okText(out));
  }, 60);
}

void seasonal(const QJsonObject& a, ToolDone done) {
  const QString q = strArg(a, "symbol");
  auto* i = resolve(q);
  if (!i) { done(fail(unknownInstrument(q))); return; }
  const int years = intArg(a, "years", 10, 5, 20);
  const QString sym = i->symbol, name = i->name;
  seasonality::loadHistory(sym, years, qApp, [=](const QVector<seasonality::DailyClose>& closes, const QString& err, bool) {
    if (closes.isEmpty()) { done(fail("error: no history for " + sym + (err.isEmpty() ? "" : " (" + err + ")"))); return; }
    const auto ds = seasonality::build(closes, years, QDate::currentDate(), sym);
    if (!ds.ok()) { done(fail("seasonality not available for " + sym)); return; }
    QString out = "Seasonality of " + name + " (" + sym + "), " + QString::number(ds.years.size()) + " years of daily Yahoo closes, " +
                  "average return per calendar month (computed by Deepwater)\nmonth | average % | years\n";
    for (const auto& m : seasonality::monthlyAverages(ds))
      out += QLocale(QLocale::English).monthName(m.month) + " | " + pct(m.mean) + " | " + QString::number(m.n) + "\n";
    done(okText(out));
  });
}

void fundamentals(const QJsonObject&, ToolDone done) {
  auto* src = fundamental::defaultSource(qApp);
  auto answer = [src, done] {
    if (!src->ready()) { src->deleteLater(); done(fail("fundamental scores still loading" + (src->status().isEmpty() ? QString() : ": " + src->status()))); return; }
    const auto scores = fundamental::scoreAll(src->inputs());
    QString out = "Fundamental heatmap (scores computed by Deepwater from central-bank and macro series; rank 1 = strongest)\n"
                  "rank | currency | score | bias | rates | inflation | growth | labour | activity | sentiment\n";
    for (const auto& s : scores) {
      QStringList row{QString::number(s.rank), s.ccy, num(s.score, 0), s.bias};
      for (int c = 0; c < fundamental::kColumns; ++c) row << s.sig[c].word + (s.sig[c].sub.isEmpty() ? "" : " (" + s.sig[c].sub + ")");
      out += row.join(" | ") + "\n";
    }
    src->deleteLater();
    done(okText(out));
  };
  src->activate();
  waitUntil(src, &fundamental::Source::changed, [src] { return src->ready(); }, 25000, answer);
}

// ------------------------------------------------------------------------------------------------ account
void trackRecord(const QJsonObject& a, ToolDone done) {
  const auto* acc = account::Journal::instance().current();
  if (!acc) { done(fail("no trading account in Account › Track Record")); return; }
  const int max = intArg(a, "trades", 15, 0, 50);
  const auto st = account::compute(acc->trades, acc->balance);
  QString out = "Track record of the user's account \"" + acc->name + "\" (" + acc->platform + ", " + acc->currency + ")" +
                (acc->demo ? " — SAMPLE DATA, not real trades" : "") + "\n";
  out += "closed trades " + QString::number(st.n) + " | net " + num(st.net) + " " + acc->currency + " | win rate " + num(st.winRate * 100, 1) +
         "% | profit factor " + num(st.profitFactor) + " | average win " + num(st.avgWin) + " | average loss " + num(st.avgLoss) +
         " | max drawdown " + num(st.maxDD) + " (" + num(st.maxDDPct * 100, 1) + "%)\n";
  if (max > 0) {
    out += "last trades: close time | symbol | side | lots | entry | exit | net\n";
    for (int k = int(acc->trades.size()) - 1, n = 0; k >= 0 && n < max; --k, ++n) {
      const auto& t = acc->trades[k];
      out += utc(t.close) + " | " + t.symbol + " | " + (t.side == account::Side::Long ? "long" : "short") + " | " + num(t.lots) + " | " +
             num(t.entry, 5) + " | " + num(t.exit, 5) + " | " + num(t.net()) + "\n";
    }
  }
  done(okText(out));
}

// ------------------------------------------------------------------------------------------------ Data › Technical data
// technical-data.sqlite (src/technicaldata): the pairs Deepwater records (Binance spot crypto, Dukascopy forex), and the
// indicators of Deepwater's Charts engine computed on them.
QString marketOf(const QString& q) {
  QString m = q.trimmed().toUpper().remove('/').remove('-');
  if (m.isEmpty()) m = "BTCUSDT";
  if (m == "BTCUSD" || m == "BINANCE:BTCUSD") m = "BTCUSDT";
  return technicaldata::storedMarket(m);
}
int tfOf(const QString& q) {
  for (const auto& t : technicaldata::timeframes()) if (t.name == q) return t.seconds;
  for (const auto& t : technicaldata::timeframes()) if (t.name.compare(q, Qt::CaseInsensitive) == 0 && q != "1M") return t.seconds;
  return 0;
}
QString tfNames() {
  QStringList n;
  for (const auto& t : technicaldata::timeframes()) n << t.name;
  return n.join(", ");
}
QString when(qint64 t, int tf) {
  // a day or a week at midnight UTC (crypto): the date; a forex one starts the evening before at 21:00 or 22:00: the time too
  return t <= 0 ? QStringLiteral("none") : QDateTime::fromSecsSinceEpoch(t, QTimeZone::UTC).toString(tf >= 86400 && t % 86400 == 0 ? "yyyy-MM-dd" : "yyyy-MM-dd HH:mm");
}

void marketCoverage(const QJsonObject&, ToolDone done) {
  const qint64 asOf = asOfFor("get_technical_coverage");   // evaluation mode: what was stored and closed by then
  auto& st = technicaldata::Store::instance();
  const QStringList markets = st.pairs();
  if (markets.isEmpty()) { done(fail("Data › Technical data records no pair (pairs are added in Data › Technical data)")); return; }
  QString out = "Data › Technical data (technical-data.sqlite): bars stored by Deepwater for the pairs the user chose, closed bars "
                "only, each pair's timeframes from its first day; indicators computed by Deepwater's Charts engine on each series' "
                "whole history, each pair its own. Crypto (BINANCE:…): Binance spot, kept up to date every minute while Deepwater "
                "runs. Forex (DUKASCOPY:…): Dukascopy's bid prices, up to the last whole UTC day (Dukascopy publishes a day once "
                "it is over); a trading day runs from 17:00 to 17:00 New York, so a daily bar starts at 21:00 or 22:00 UTC the "
                "evening before, and 4h bars start from that 17:00; no forex bar on weekends (not counted as holes); volume is "
                "Dukascopy's own, with no trade count or taker buys. Times are bar open times, UTC.\n";
  for (const QString& m : markets) {
    const technicaldata::PairSpec spec = technicaldata::specFromJson(st.pairSpec(m));
    out += "\nmarket " + m + " (" + technicaldata::sourceOf(m) + "; history asked from " + (spec.start ? when(spec.start, 86400) : QStringLiteral("the source's first bar")) +
           ")\ntimeframe | bars | first | last | whole history | indicators up to | holes (bars missing: the exchange's own outages)\n";
    for (const auto& t : technicaldata::timeframes()) {
      if (!spec.timeframes.contains(t.seconds)) continue;
      auto s = st.state(m, t.seconds);
      auto i = st.indState(m, t.seconds);
      qint64 cut = 0;   // evaluation mode: the newest bar closed by then
      if (asOf > 0 && s.count > 0) {
        cut = asOf - t.seconds;
        if (s.last > cut) {
          s.count = s.first <= cut ? st.count(m, t.seconds, s.first, cut) : 0;
          const auto lastBars = st.before(m, t.seconds, cut + 1, 1);
          s.last = lastBars.isEmpty() ? 0 : lastBars.last().t;
          s.historyDone = s.historyDone && s.count > 0;
        }
        i.last = std::min(i.last, s.last);
      }
      if (s.count == 0) { out += t.name + " | 0 | none yet\n"; continue; }
      qint64 holes = 0, missing = 0;
      for (const auto& g : st.gaps(m, t.seconds)) {
        if (cut && g.to > cut) continue;   // a hole known only once the bar after it came
        ++holes; missing += g.missing;
      }
      out += t.name + " | " + QString::number(s.count) + " | " + when(s.first, t.seconds) + " | " + when(s.last, t.seconds) + " | " +
             (s.historyDone ? "yes" : "not yet") + " | " + when(i.last, t.seconds) + " | " +
             (holes ? QString::number(holes) + " (" + QString::number(missing) + " bars)" : QStringLiteral("none")) + "\n";
    }
    QStringList d;
    for (const auto& c : technicaldata::columnsOf(spec)) {
      QString onTf;
      if (c.intradayOnly) onTf = " [intraday only]";
      else if (!c.tfs.isEmpty()) {
        QStringList n;
        for (const auto& t : technicaldata::timeframes()) if (c.tfs.contains(t.seconds)) n << t.name;
        onTf = " [only " + n.join(", ") + "]";
      }
      d << c.name + " = " + c.id + "(" + QString::fromUtf8(QJsonDocument(technicaldata::fullParams(c.id, c.params)).toJson(QJsonDocument::Compact)) +
               ")." + c.output + onTf;
    }
    out += "indicator columns (Charts indicator, settings, output): " + (d.isEmpty() ? QStringLiteral("none") : d.join("; ")) +
           ". Warm-up: the first " + QString::number(technicaldata::warmupOf(spec)) + " bars of each series (long averages not settled yet).\n";
    if (m.startsWith("BINANCE:")) {   // the order flow it records (get_footprint, get_order_book, get_open_interest)
      QStringList of;
      auto span = [](qint64 a, qint64 b, int n) { return QString::number(n) + " days, " + when(a, 86400) + " to " + when(b, 86400); };
      // evaluation mode: the whole days over by then (a day's archive holds all of it)
      auto upTo = [asOf](auto days) {
        if (asOf > 0) days.erase(std::remove_if(days.begin(), days.end(), [asOf](const auto& d) { return d.day + 86400 > asOf; }), days.end());
        return days;
      };
      for (const QString& v : spec.footprint) {
        const auto days = upTo(st.footprintDays(m, v));
        of << "footprint " + v + ": " + (days.isEmpty() ? QStringLiteral("collecting, none stored yet") : span(days.first().day, days.last().day, int(days.size())));
      }
      for (const auto& [on, kind, name] : {std::tuple{spec.depth, "depth", "perpetual book depth"}, std::tuple{spec.openInterest, "oi", "open interest"}}) {
        if (!on) continue;
        const auto days = upTo(st.archiveDays(m, kind));
        of << QString(name) + ": " + (days.isEmpty() ? QStringLiteral("collecting, none stored yet") : span(days.first().day, days.last().day, int(days.size())));
      }
      if (spec.liveBook) of << (asOf > 0 ? QStringLiteral("live book and trades: off in evaluation mode") : QStringLiteral("live book and trades (memory, spot and perpetual): on"));
      out += "order flow: " + (of.isEmpty() ? QStringLiteral("none recorded") : of.join("; ")) + "\n";
    }
  }
  out += "\nRead bars with get_technical_data; the order flow with get_footprint, get_order_book and get_open_interest.\n";
  if (asOf > 0) out = "EVALUATION MODE: as of " + asOfStamp(asOf) + " (what was stored and over by then).\n" + out;
  done(okText(out));
}

void marketBars(const QJsonObject& a, ToolDone done) {
  auto& st = technicaldata::Store::instance();
  const QString m = marketOf(strArg(a, "market"));
  if (m.isEmpty() || !st.pairs().contains(m)) {
    done(fail("error: no stored market \"" + strArg(a, "market") + "\". Stored: " + (st.pairs().isEmpty() ? QStringLiteral("none yet") : st.pairs().join(", "))));
    return;
  }
  const technicaldata::PairSpec spec = technicaldata::specFromJson(st.pairSpec(m));
  const QString tfName = strArg(a, "timeframe").isEmpty() ? QStringLiteral("1h") : strArg(a, "timeframe");
  const int tf = tfOf(tfName);
  if (!tf) { done(fail("error: timeframe must be one of " + tfNames())); return; }
  const auto s = st.state(m, tf);
  if (!spec.timeframes.contains(tf)) {
    QStringList n;
    for (const auto& t : technicaldata::timeframes()) if (spec.timeframes.contains(t.seconds)) n << t.name;
    done(fail("error: " + m + " is not recorded on " + tfName + " (its timeframes: " + n.join(", ") + ")"));
    return;
  }
  if (s.count == 0) { done(fail("error: no " + tfName + " bar of " + m + " stored yet")); return; }
  const int want = intArg(a, "bars", 20, 1, 200);
  const qint64 asOf = asOfFor("get_technical_data");   // evaluation mode: only the bars closed by then
  qint64 end = asOf > 0 ? std::min(s.last, asOf - tf) : s.last;
  const QString endArg = strArg(a, "end");
  if (!endArg.isEmpty()) {
    QDateTime e;
    for (const char* f : {"yyyy-MM-dd HH:mm", "yyyy-MM-ddTHH:mm", "yyyy-MM-dd HH:mm:ss", "yyyy-MM-ddTHH:mm:ss", "yyyy-MM-dd"}) {
      e = QDateTime::fromString(endArg.left(QString(f).size()), f);
      if (e.isValid()) break;
    }
    if (!e.isValid()) { done(fail("error: end must be a UTC date or time like 2024-03-01 or 2024-03-01 14:00")); return; }
    e.setTimeZone(QTimeZone::UTC);
    end = e.toSecsSinceEpoch();
    if (asOf > 0 && end > asOf) { done(fail(afterClock(asOf, "end " + endArg))); return; }
    if (asOf > 0) end = std::min(end, asOf - tf);   // the bar holding that time had not closed yet
    if (end < s.first) { done(fail("error: the stored " + tfName + " bars of " + m + " start at " + when(s.first, tf))); return; }
  }
  if (asOf > 0 && end < s.first) { done(fail("error: no " + tfName + " bar of " + m + " had closed by " + asOfStamp(asOf))); return; }
  // columns: the indicators asked for (or a key set), always after the bar itself
  const QStringList all = st.indicatorColumns(m);
  QStringList cols = listArg(a, "columns");
  for (auto& c : cols) c = c.toLower();
  for (const auto& c : cols)
    if (!all.contains(c)) { done(fail("error: no indicator column \"" + c + "\". Columns: " + all.join(", "))); return; }
  if (cols.isEmpty()) {   // a key set when the pair has it, else its first ten
    for (const QString& c : {"rsi14", "macd_hist", "bb_pb", "bb_width", "atr14", "adx14", "ema20", "ema50", "ema200", "vwap"})
      if (all.contains(c) && (tf < 86400 || QString(c) != "vwap")) cols << c;
    if (cols.isEmpty()) cols = all.mid(0, 10);
  }
  QList<int> idx;
  for (const auto& c : cols) idx << int(all.indexOf(c));
  const QVector<technicaldata::Kline> bars = st.before(m, tf, end + 1, want + 1);   // one more: the hole before the first
  if (bars.size() < 2 && bars.size() < want) { if (bars.isEmpty()) { done(fail("error: no bar at or before " + endArg)); return; } }
  QHash<qint64, technicaldata::IndRow> ind;
  for (const auto& r : st.indicators(m, tf, bars.first().t, bars.last().t)) ind.insert(r.t, r);
  const auto dec = [](const QString& c) { return c == "bb_pb" ? 4 : c == "obv" ? 1 : 2; };
  const bool fx = technicaldata::dukascopy::isForex(m);
  // forex: a price level (an average, a band) needs five decimals; an oscillator keeps two
  const auto decOf = [dec, fx](const QString& c, double v) { return fx && std::isfinite(v) && std::fabs(v) < 50 && c != "bb_pb" && !c.startsWith("rsi") && !c.startsWith("stoch") && !c.startsWith("adx") && !c.startsWith("di_") ? 5 : dec(c); };
  QString head = m + " " + tfName + " bars (" + technicaldata::sourceOf(m) + ", closed bars; indicators by Deepwater's Charts engine), oldest first, times UTC (bar open)" +
                 (fx ? QStringLiteral("; forex volume is Dukascopy's own, no taker buys") : QString()) + "\n";
  if (asOf > 0) head = "EVALUATION MODE: as of " + asOfStamp(asOf) + " (the bars closed by then).\n" + head;
  const QString colsLine = "time | open | high | low | close | volume | taker buy % | " + cols.join(" | ") + "\n";
  QStringList rows;
  int holes = 0, warm = 0;
  int budget = kMaxChars - int(head.size() + colsLine.size()) - 300;
  for (int i = int(bars.size()) - 1; i >= 1 && rows.size() < want; --i) {   // newest first, until the size limit
    const auto& k = bars[i];
    const qint64 gap = fx ? (k.t > technicaldata::dukascopy::nextExpected(bars[i - 1].t, tf) ? std::max<qint64>(1, (k.t - technicaldata::dukascopy::nextExpected(bars[i - 1].t, tf)) / tf) : 0)
                          : (k.t - bars[i - 1].t) / tf - 1;   // forex: the weekend is no hole
    const auto r = ind.constFind(k.t);
    QStringList cells{when(k.t, tf) + (gap > 0 ? " [" + QString::number(gap) + " bars missing before]" : QString()),
                      num(k.o, fx ? 5 : 2), num(k.h, fx ? 5 : 2), num(k.l, fx ? 5 : 2), num(k.c, fx ? 5 : 2), num(k.v, 3),
                      fx ? QStringLiteral("n/a") : num(k.v > 0 ? k.tbv / k.v * 100 : NAN, 1)};
    for (int j = 0; j < idx.size(); ++j) {
      const double v = r == ind.cend() ? NAN : r->v.value(idx[j], NAN);
      cells << (r == ind.cend() ? QStringLiteral("not computed yet") : num(v, decOf(cols[j], v)));
    }
    QString line = cells.join(" | ");
    if (r != ind.cend() && r->warm) { line += " (warm-up)"; ++warm; }
    if (budget - line.size() - 1 < 0) break;
    budget -= int(line.size()) + 1;
    if (gap > 0) ++holes;
    rows.prepend(line);
  }
  if (bars.size() == 1) rows << when(bars[0].t, tf) + " | " + num(bars[0].o) + " | " + num(bars[0].h) + " | " + num(bars[0].l) + " | " + num(bars[0].c);
  QString out = head + colsLine + rows.join("\n") + "\n";
  if (rows.size() < want && rows.size() < bars.size() - 1)
    out += "[" + QString::number(rows.size()) + " of the " + QString::number(want) + " bars asked fit in one answer: ask for fewer columns or bars]\n";
  if (holes) out += "Holes: bars the exchange never published (its outages), not filled in.\n";
  if (warm) out += "warm-up: among the first " + QString::number(technicaldata::warmupOf(spec)) + " bars of the series, long averages not settled.\n";
  done(okText(out));
}

// ------------------------------------------------------------------------------------------------ Data › Macro data
// macro-data.sqlite (src/macrodata): every economic fact Deepwater recorded, in chains (one indicator, one kind, one
// source). Revisions are new facts marked as revising the earlier one, nothing is overwritten.
QString factNum(double v) {
  if (!std::isfinite(v)) return QStringLiteral("n/a");
  QString t = QString::number(v, 'f', std::fabs(v) >= 1000 ? 2 : 4);
  if (t.contains('.')) { while (t.endsWith('0')) t.chop(1); if (t.endsWith('.')) t.chop(1); }
  return t;
}
// ms may be negative (series start before 1970) or 0: 1970-01-01 is a real period (the 1970 point of annual chains);
// a fact's target and publication time use 0 for "none", and their callers test that themselves
QString periodText(qint64 ms, const QString& freq) {
  const QDateTime t = QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC);
  if (freq == "A") return t.toString("yyyy");
  if (freq == "Q") return t.toString("yyyy") + "-Q" + QString::number((t.date().month() - 1) / 3 + 1);
  if (freq == "M") return t.toString("yyyy-MM");
  if (t.time() != QTime(0, 0)) return t.toString("yyyy-MM-dd HH:mm");
  return t.toString("yyyy-MM-dd");
}
QString stamp(qint64 ms) { return ms > 0 ? QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC).toString("yyyy-MM-dd HH:mm") : QStringLiteral("—"); }
qint64 dateArg(const QString& v, bool endOfDay) {   // ms, 0 = none, -1 = unreadable
  if (v.isEmpty()) return 0;
  for (const char* f : {"yyyy-MM-dd HH:mm", "yyyy-MM-ddTHH:mm", "yyyy-MM-dd", "yyyy-MM", "yyyy"}) {
    QDateTime t = QDateTime::fromString(v.left(QString(f).size()), f);
    if (!t.isValid()) continue;
    t.setTimeZone(QTimeZone::UTC);
    if (endOfDay && QString(f).size() <= 10) {
      if (QString(f) == "yyyy") t = t.addYears(1).addMSecs(-1);
      else if (QString(f) == "yyyy-MM") t = t.addMonths(1).addMSecs(-1);
      else t = t.addDays(1).addMSecs(-1);
    }
    return t.toMSecsSinceEpoch();
  }
  return -1;
}

// When a fact was known: its publication time, else (seen live) when Deepwater recorded it, else (imported history,
// no publication time) an estimate: the end of its period plus a typical release lag. *estimated tells which.
// monthStarts: every period of the chain is the 1st of a month at 00:00 (e.g. BIS monthly policy rates, whose value is the
// rate at the month's end, stored in "changes" mode): known at the end of that month, not on its first day.
qint64 knownAt(const macrodata::Fact& f, const QString& freq, bool monthStarts, bool* estimated) {
  *estimated = false;
  if (f.at > 0) return f.at;
  if (f.origin != "import") return f.recorded;
  *estimated = true;
  const QDateTime p = QDateTime::fromMSecsSinceEpoch(f.period, QTimeZone::UTC);
  if (freq == "M") return p.addMonths(1).addDays(15).toMSecsSinceEpoch();
  if (freq == "Q") return p.addMonths(3).addDays(45).toMSecsSinceEpoch();
  if (freq == "A") return p.addYears(1).addDays(120).toMSecsSinceEpoch();
  if (monthStarts) return p.addMonths(1).toMSecsSinceEpoch();
  return p.addDays(1).toMSecsSinceEpoch();   // D, W, E: rates and prices are known the same day
}

void macroChains(const QJsonObject& a, ToolDone done) {
  const QList<macrodata::Chain> all = macrodata::Store::instance().chains();
  if (all.isEmpty()) { done(fail("Data › Macro data holds no chain yet (it fills from Deepwater's sources once the Macro data page has been opened)")); return; }
  const QStringList words = strArg(a, "query").toLower().split(' ', Qt::SkipEmptyParts);
  const QString family = strArg(a, "family").toLower(), kind = strArg(a, "kind").toLower();
  const int max = intArg(a, "max", 30, 1, 100);
  QMap<QString, int> families;
  QList<macrodata::Chain> hits;
  for (const auto& c : all) {
    families[c.def.family]++;
    if (!family.isEmpty() && c.def.family != family) continue;
    if (!kind.isEmpty() && c.def.kind != kind) continue;
    const QString hay = (c.def.key + " " + c.def.subject + " " + c.def.indicator + " " + c.def.source).toLower();
    bool ok = true;
    for (const auto& w : words) if (!hay.contains(w)) { ok = false; break; }
    if (ok) hits << c;
  }
  QString out = "Data › Macro data (macro-data.sqlite): every economic fact Deepwater has recorded, real or forecast, in chains: one "
                "indicator of one subject, one kind (actual, forecast, consensus, implied, price), one source. " +
                QString::number(all.size()) + " chains; by family: ";
  QStringList fam;
  for (auto it = families.cbegin(); it != families.cend(); ++it) fam << it.key() + " " + QString::number(it.value());
  out += fam.join(", ") + ".\n";
  if (words.isEmpty() && family.isEmpty() && kind.isEmpty()) {
    out += "Give a query (e.g. \"united states inflation\", \"fed\", \"EURUSD\"), a family or a kind to list chains.\n";
    done(okText(out));
    return;
  }
  std::sort(hits.begin(), hits.end(), [](const macrodata::Chain& x, const macrodata::Chain& y) {
    return x.def.subject != y.def.subject ? x.def.subject < y.def.subject : x.def.indicator < y.def.indicator;
  });
  out += QString("%1 chain(s) match%2.\nkey | subject | indicator | kind | source | unit | freq | facts | revisions | first → last period\n")
             .arg(hits.size()).arg(hits.size() > max ? QString(", the first %1 shown").arg(max) : QString());
  for (int i = 0; i < std::min<int>(max, int(hits.size())); ++i) {
    const auto& c = hits[i];
    out += c.def.key + " | " + c.def.subject + " | " + c.def.indicator + " | " + c.def.kind + " | " + c.def.source + " | " +
           (c.def.unit.isEmpty() ? "—" : c.def.unit) + " | " + c.def.freq + " | " + QString::number(c.count) + " | " +
           QString::number(c.revisions) + " | " + periodText(c.first, c.def.freq) + " → " + periodText(c.last, c.def.freq) + "\n";
  }
  out += "Read a chain's facts with get_macro_facts(chain: <key>).\n";
  done(okText(out));
}

void macroFacts(const QJsonObject& a, ToolDone done) {
  auto& st = macrodata::Store::instance();
  const QString key = strArg(a, "chain");
  if (key.isEmpty()) { done(fail("error: give a chain key (find it with get_macro_chains)")); return; }
  const macrodata::Chain c = st.chain(key);
  if (!c.id) { done(fail("error: no chain \"" + key + "\" in Data › Macro data; find keys with get_macro_chains")); return; }
  const qint64 from = dateArg(strArg(a, "from"), false), to = dateArg(strArg(a, "to"), true);
  qint64 asOf = dateArg(strArg(a, "as_of"), true);
  if (from < 0 || to < 0 || asOf < 0) { done(fail("error: dates look like 2024-03-01, 2024-03 or 2024")); return; }
  // evaluation mode: as_of is the environment's moment (an earlier one may be asked, never a later one)
  const qint64 clock = asOfFor("get_macro_facts");
  if (clock > 0) {
    if (asOf > clock * 1000) { done(fail(afterClock(clock, "as_of " + strArg(a, "as_of")))); return; }
    if (asOf <= 0) asOf = clock * 1000;
  }
  const bool withRevisions = !a.contains("revisions") || a.value("revisions").toBool(true);
  const int last = intArg(a, "last", 24, 1, 200);
  const bool forecast = c.def.kind == "forecast" || c.def.kind == "consensus" || c.def.kind == "implied";
  QVector<macrodata::Fact> facts = st.facts(c.id);
  QHash<qint64, const macrodata::Fact*> byId;
  for (const auto& f : facts) byId.insert(f.id, &f);
  // as_of: what was known then — a fact counts from its publication time, else when Deepwater saw it, else an estimate
  int estimatedTimes = 0;
  bool monthStarts = facts.size() > 2;
  for (const auto& f : facts) {
    const QDateTime p = QDateTime::fromMSecsSinceEpoch(f.period, QTimeZone::UTC);
    if (p.date().day() != 1 || p.time() != QTime(0, 0)) { monthStarts = false; break; }
  }
  QVector<const macrodata::Fact*> kept;
  if (asOf > 0) {
    QHash<QString, const macrodata::Fact*> latest;   // period (+ target) → the newest known by then
    for (const auto& f : facts) {
      bool est = false;
      const qint64 known = knownAt(f, c.def.freq, monthStarts, &est);
      if (known > asOf) continue;
      if (est) ++estimatedTimes;
      const QString slot = QString::number(f.period) + "|" + QString::number(f.target);
      const auto* prev = latest.value(slot);
      bool e2 = false;
      if (!prev || knownAt(*prev, c.def.freq, monthStarts, &e2) <= known) latest.insert(slot, &f);
    }
    for (const auto& f : facts) if (latest.value(QString::number(f.period) + "|" + QString::number(f.target)) == &f) kept << &f;
  } else {
    for (const auto& f : facts) if (withRevisions || !f.revises) kept << &f;
    if (!withRevisions) {   // without revisions: the latest value of each period only
      QHash<QString, const macrodata::Fact*> newest;
      for (const auto* f : kept) newest.insert(QString::number(f->period) + "|" + QString::number(f->target), f);
      QVector<const macrodata::Fact*> k2;
      for (const auto* f : kept) if (newest.value(QString::number(f->period) + "|" + QString::number(f->target)) == f) k2 << f;
      kept = k2;
    }
  }
  QVector<const macrodata::Fact*> shown;
  for (const auto* f : kept) {
    if (from > 0 && f->period < from) continue;
    if (to > 0 && f->period > to) continue;
    shown << f;
  }
  const int total = int(shown.size());
  if (shown.size() > last) shown = shown.mid(shown.size() - last);
  QString out = "Data › Macro data chain " + c.def.key + ": " + c.def.subject + " · " + c.def.indicator + " · " + c.def.kind + " · source " +
                c.def.source + (c.def.unit.isEmpty() ? QString() : " · unit " + c.def.unit) + " · frequency " + c.def.freq + " · " +
                (clock > 0 ? QString::number(kept.size()) + " periods known by then\n"   // the chain's totals include later facts
                           : QString::number(c.count) + " facts, " + QString::number(c.revisions) + " revisions\n");
  if (clock > 0) out = "EVALUATION MODE: as of " + asOfStamp(clock) + ".\n" + out;
  if (!c.def.targetChain.isEmpty()) out += "predicts the chain " + c.def.targetChain + "\n";
  if (asOf > 0) out += "as of " + stamp(asOf) + " UTC: the latest value of each period known by then\n";
  out += QString("%1 of %2 fact(s), oldest first.\nperiod | %3value | published | recorded by Deepwater | origin%4\n")
             .arg(shown.size()).arg(total).arg(forecast ? "target | " : "", withRevisions && asOf <= 0 ? " | revision of" : "");
  for (const auto* f : shown) {
    QString line = periodText(f->period, c.def.freq) + " | ";
    if (forecast) line += (f->target ? periodText(f->target, c.def.freq == "E" ? "D" : c.def.freq) : QStringLiteral("—")) + " | ";
    line += (f->text.isEmpty() ? factNum(f->value) : (std::isfinite(f->value) ? factNum(f->value) + " " : QString()) + f->text) + " | " +
            stamp(f->at) + " | " + (clock > 0 && f->recorded > clock * 1000 ? QStringLiteral("—") : stamp(f->recorded)) + " | " + f->origin;
    if (withRevisions && asOf <= 0 && f->revises) {
      const auto* old = byId.value(f->revises);
      line += " | revises " + (old ? factNum(old->value) : QStringLiteral("an earlier value"));
    }
    if (!f->note.isEmpty()) line += " (" + f->note.left(80) + ")";
    out += line + "\n";
  }
  out += "origin import = taken from the source's history when Deepwater first imported it (its value then, revisions included, "
         "recorded at the import); live = seen as it was published.";
  if (estimatedTimes)
    out += QString(" as_of: %1 imported fact(s) have no publication time, so they count from an estimate (end of the period plus "
                   "a typical lag: same day for daily rates and prices, the month's end for monthly values dated on the 1st, 15 days for monthly, 45 for quarterly, 120 for annual figures), "
                   "and their values are the source's latest (later revisions included).").arg(estimatedTimes);
  done(okText(out + "\n"));
}

QJsonObject schema(std::initializer_list<std::pair<const char*, QJsonObject>> props, QStringList required = {}) {
  QJsonObject p;
  for (const auto& [k, v] : props) p[k] = v;
  QJsonObject o{{"type", "object"}, {"properties", p}};
  if (!required.isEmpty()) o["required"] = QJsonArray::fromStringList(required);
  return o;
}
QJsonObject str(const char* d) { return {{"type", "string"}, {"description", d}}; }
QJsonObject integer(const char* d) { return {{"type", "integer"}, {"description", d}}; }
}  // namespace

void registerDataTools(Tools& t) {
  const QString S = "read_data";
  t.add({"get_quotes", S, "Live prices of instruments (forex, indices, commodities, stocks, crypto, bonds): price, change, day range.",
         schema({{"symbols", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}},
                                         {"description", "e.g. [\"EURUSD\", \"XAUUSD\", \"SPX\", \"BTCUSD\"]; empty = main markets"}}}}), quotes});
  t.add({"get_price_history", S, "Past prices (candles) of one instrument.",
         schema({{"symbol", str("e.g. EURUSD, XAUUSD, SPX")}, {"interval", str("1h, 1d (default), 1wk or 1mo")},
                 {"bars", integer("number of bars, default 30, max 120")}}, {"symbol"}), history});
  t.add({"get_economic_calendar", S, "Economic calendar: scheduled releases and speeches with actual, forecast and previous values.",
         schema({{"days_ahead", integer("days after now, default 7")}, {"days_back", integer("days before now, default 0")},
                 {"currency", str("filter: one currency (USD) or several (EUR,USD)")}, {"min_impact", str("low, medium or high")}}), calendar});
  t.add({"get_news", S, "Latest market news stories (title, source, time, summary).",
         schema({{"query", str("optional word to filter, e.g. Fed, oil")}, {"max", integer("default 10")}}), newsFeed});
  t.add({"get_squawk", S, "Live wire: breaking headlines, data releases and big market moves, newest first.",
         schema({{"max", integer("default 20")}}), squawk});
  t.add({"get_macro_data", S, "Economy tab: the official macro series of a country as the Economy tab shows them now (policy rate, GDP, "
                              "inflation, core inflation, unemployment, confidence, yields, retail sales, debt…). Without indicator: "
                              "latest value of each; with one: its recent history. Not Data › Macro data's fact chains (revisions, "
                              "forecasts, consensus): those are get_macro_chains and get_macro_facts.",
         schema({{"country", str("ISO code (US, GB, JP, EZ, DE…), currency (USD) or name")},
                 {"indicator", str("optional: inflation, gdp, unemployment, policy rate, core, retail, confidence, yield…")}}, {"country"}), macroData});
  t.add({"get_central_banks", S, "Central banks: policy rate, next meeting, market odds of cut/hold/hike, implied path, recent decisions.",
         schema({{"bank", str("optional: FED, ECB, BOE, BOJ, SNB, RBA, RBNZ, BOC (or currency); empty = all")}}), centralBanks});
  t.add({"get_cot", S, "CFTC Commitments of Traders: weekly net positions of each trader category in a futures market.",
         schema({{"market", str("e.g. EUR, USD, JPY, GOLD, OIL, ES, or an instrument like EURUSD")},
                 {"weeks", integer("default 6")}}, {"market"}), cot});
  t.add({"get_retail_sentiment", S, "Retail traders' long/short percentages per symbol (Myfxbook).",
         schema({{"symbol", str("optional, e.g. EURUSD")}}), retail});
  t.add({"get_risk_sentiment", S, "Risk-on / risk-off index (0-100) and its seven components.", schema({}), risk});
  t.add({"get_bank_forecasts", S, "Banks' published forecasts by quarter for FX, commodities, rates.",
         schema({{"market", str("optional, e.g. EURUSD, XAUUSD, US10Y")}}), bankForecasts});
  t.add({"get_bank_research", S, "Latest research notes published by banks (title, type, summary).",
         schema({{"bank", str("optional bank id, e.g. ing")}, {"max", integer("default 10")}}), bankResearch});
  t.add({"get_market_hours", S, "Which forex sessions (Sydney, Tokyo, London, New York) are open now and when they open or close.",
         schema({}), marketHours});
  t.add({"get_technical_read", S, "Technical read of an instrument: trend (EMA20/50), RSI, momentum, ATR volatility, structure.",
         schema({{"symbol", str("e.g. EURUSD")}}, {"symbol"}), technical});
  t.add({"get_liquidity_pools", S, "Liquidity pools (equal highs / lows where stops sit) nearest to the price of an instrument.",
         schema({{"symbol", str("e.g. EURUSD")}}, {"symbol"}), liquidity});
  t.add({"get_seasonality", S, "Average return per calendar month of an instrument over past years.",
         schema({{"symbol", str("e.g. XAUUSD")}, {"years", integer("5 to 20, default 10")}}, {"symbol"}), seasonal});
  t.add({"get_fundamental_scores", S, "Fundamental strength ranking of the major currencies (rates, inflation, growth, labour, activity, sentiment).",
         schema({}), fundamentals});
  t.add({"get_macro_chains", S, "Data › Macro data: find chains of economic facts (one indicator of one subject, one kind — actual, "
                                "forecast, consensus, implied, price — one source) by words, family or kind; their size, revisions and period.",
         schema({{"query", str("words that must all match the key, subject, indicator or source, e.g. \"united states inflation\", \"fed\", \"EURUSD\"")},
                 {"family", str("optional: rates, macro, calendar, prices, forecasts or positioning")},
                 {"kind", str("optional: actual, forecast, consensus, implied or price")},
                 {"max", integer("chains to list, default 30, max 100")}}), macroChains});
  t.add({"get_macro_facts", S, "Data › Macro data: the facts of one chain, oldest first, with when each was published and recorded, its "
                               "origin, and the revisions (a revised value is a new fact, nothing overwritten). as_of gives what was "
                               "known at a past date, for tests without hindsight.",
         schema({{"chain", str("chain key from get_macro_chains, e.g. rate:us or macro:us:inflation")},
                 {"last", integer("newest facts to show, default 24, max 200")},
                 {"from", str("optional: periods from this date, e.g. 2020-01")}, {"to", str("optional: periods up to this date")},
                 {"as_of", str("optional: only what was known at this UTC date (latest value of each period published, or recorded, by then)")},
                 {"revisions", QJsonObject{{"type", "boolean"}, {"description", "show each revision as its own row (default true); false = the latest value of each period"}}}},
                {"chain"}), macroFacts});
  t.add({"get_technical_coverage", S, "Data › Technical data: which pairs Deepwater records (Binance spot crypto, Dukascopy forex majors; each "
                                   "pair with its own timeframes among 1m to 1W, from its first day), their period, holes, and each pair's "
                                   "indicator columns and how they are defined.", schema({}), marketCoverage});
  t.add({"get_technical_data", S, "Data › Technical data: stored bars of one pair (crypto or forex) and timeframe (1m 5m 15m 1h 4h 1D 1W), with volume, taker-buy "
                               "share and the indicators computed by Deepwater's Charts engine; the newest, or up to a past date.",
         schema({{"market", str("a recorded pair, e.g. BINANCE:BTCUSDT (default), ETHUSDT, EURUSD (see get_technical_coverage)")},
                 {"timeframe", str("1m, 5m, 15m, 1h (default), 4h, 1D or 1W, among the pair's")},
                 {"bars", integer("how many bars, default 20, max 200 (fewer when many columns: the answer is capped)")},
                 {"end", str("optional: last bar at or before this UTC date or time, e.g. 2024-03-01 or 2024-03-01 14:00; empty = the newest")},
                 {"columns", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}},
                                         {"description", "indicator columns of that pair to show (see get_technical_coverage); empty = rsi14, macd_hist, bb_pb, bb_width, atr14, adx14, ema20, ema50, ema200, vwap when the pair has them, else its first ten"}}}}),
         marketBars});
  t.add({"get_track_record", S, "The user's own trading journal: statistics and last closed trades.",
         schema({{"trades", integer("number of last trades, default 15")}}), trackRecord});
}
}  // namespace ai
