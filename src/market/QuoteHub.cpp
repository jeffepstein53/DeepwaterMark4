#include "market/QuoteHub.h"
#include "market/Instruments.h"
#include "net/Http.h"
#include "net/Sources.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QTimeZone>
#include <memory>

namespace market {
static const sources::Registrar kYahoo({"yahoo", "Yahoo Finance",
  {"query1.finance.yahoo.com", "query2.finance.yahoo.com"}, sources::Access::Keyless, 350,
  "Quotes (spark), candles (chart) for FX, indices, futures, stocks, crypto",
  "Public endpoints without key, personal non-commercial display; data delayed for some exchanges",
  {"market/markets-watch", "global/overview", "global/session-recap", "news/news-feed", "news/squawk", "economy/rates",
   "market/seasonality", "market/technical-heatmap", "market/liquidity", "positioning/risk-sentiment", "positioning/cot",
   "positioning/retail-sentiment", "positioning/bank-positioning", "account/track-record", "account/risk-calculator"}});

QuoteHub& QuoteHub::instance() { static QuoteHub* h = new QuoteHub; return *h; }

QuoteHub::QuoteHub() {
  m_timer.setInterval(20000);
  connect(&m_timer, &QTimer::timeout, this, &QuoteHub::poll);
  m_timer.start();
}

static QString appSymbol(const QString& s) { const auto* i = find(s); return i ? i->symbol : s.toUpper(); }
static QString yahooOf(const QString& s) { const auto* i = find(s); return i ? i->yahoo : s; }

void QuoteHub::subscribe(QObject* owner, const QStringList& symbols, bool active, bool urgent) {
  // once per owner: unsubscribe() keeps the owner watched, so re-subscribing doesn't add another connection
  if (!m_watched.contains(owner)) {
    m_watched.insert(owner);
    connect(owner, &QObject::destroyed, this, [this, owner] { m_subs.remove(owner); m_watched.remove(owner); });
  }
  auto& s = m_subs[owner];
  QStringList fresh;
  for (const auto& x : symbols) { const QString a = appSymbol(x); if (!s.symbols.contains(a)) { s.symbols.insert(a); if (!m_quotes.contains(a)) fresh << yahooOf(a); } }
  s.active = active;
  s.urgent = urgent;
  if (active && !fresh.isEmpty()) request(fresh, false, urgent);
}
void QuoteHub::setActive(QObject* owner, bool active) {
  auto it = m_subs.find(owner);
  if (it == m_subs.end() || it->active == active) return;
  it->active = active;
  if (active) {   // a page coming back: refresh what is older than one interval
    // (Quote::time is the last trade, old for a closed market: the age that matters is when we last asked.)
    // Never quoted yet: a first load (an urgent owner's goes ahead of the queue: the Overview's tape subscribes
    // inactive and activates on show), not a refresh.
    QStringList stale, fresh;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const auto& a : it->symbols) {
      if (!m_quotes.contains(a)) fresh << yahooOf(a);
      else if (now - m_fetched.value(a, 0) > m_timer.interval()) stale << yahooOf(a);
    }
    if (!fresh.isEmpty()) request(fresh, false, it->urgent);
    if (!stale.isEmpty()) request(stale, true);
  }
}
void QuoteHub::unsubscribe(QObject* owner) { m_subs.remove(owner); }
Quote QuoteHub::quote(const QString& s) const { return m_quotes.value(appSymbol(s)); }
void QuoteHub::refreshNow() { poll(); }

qint64 QuoteHub::ageMs(const QString& s) const {
  const qint64 t = m_fetched.value(appSymbol(s), 0);
  return t ? QDateTime::currentMSecsSinceEpoch() - t : -1;
}

int QuoteHub::refreshStale(const QStringList& symbols, qint64 maxAgeMs, bool priority) {
  QStringList stale;
  for (const auto& s : symbols) {
    const qint64 age = ageMs(s);
    if (age < 0 || age > maxAgeMs) stale << yahooOf(s);
  }
  if (!stale.isEmpty()) request(stale, true, priority);   // forced (no cache); ahead of the queue when someone is waiting for it
  return int(stale.size());
}

void QuoteHub::poll() {
  QSet<QString> want;
  for (const auto& s : m_subs) if (s.active) want.unite(s.symbols);
  QStringList y; for (const auto& a : want) y << yahooOf(a);
  if (!y.isEmpty()) request(y, true);
}

void QuoteHub::request(const QStringList& symbols, bool force, bool priority) {
  // Sorted and deduplicated: the chunk URLs are the HTTP cache keys, and a QSet's iteration order changes at each
  // launch, so unsorted chunks were new cache files every time.
  QStringList yahoo = symbols;
  yahoo.sort();
  yahoo.removeDuplicates();
  // A poll refreshes quotes already shown, in chunks of 20 answered ~350 ms apart (Yahoo's pace): one `updated` for
  // the whole poll, so listeners (Markets Watch rebuilds its board, the risk calculator recomputes) run once per poll,
  // not once per chunk. First loads (force = false) still announce each chunk as it lands.
  struct Batch { int left = 0; QStringList changed; };
  std::shared_ptr<Batch> batch;
  if (force && yahoo.size() > 20) { batch = std::make_shared<Batch>(); batch->left = int((yahoo.size() + 19) / 20); }
  for (int i = 0; i < yahoo.size(); i += 20) {
    const QStringList chunk = yahoo.mid(i, 20);
    QUrl u("https://query1.finance.yahoo.com/v7/finance/spark");
    QUrlQuery q; q.addQueryItem("symbols", chunk.join(',')); q.addQueryItem("range", "1d"); q.addQueryItem("interval", "5m");
    u.setQuery(q);
    // A poll (force) is not written to the disk cache: a stale answer only fills symbols that have no quote yet, so
    // rewriting every chunk each 20 s only fed the cache. First loads, and chunks with a symbol still unquoted, keep
    // the stale fallback.
    bool unquoted = !force;
    for (const auto& y : chunk) if (!m_quotes.contains(appSymbol(y))) { unquoted = true; break; }
    net::Request r; r.url = u; r.ttlSec = force ? 0 : 15; r.staleOnError = unquoted;
    r.priority = priority;
    net::get(r, this, [this, batch](const net::Reply& rep) {
      auto announce = [this, &batch](const QStringList& changed) {
        if (!batch) { if (!changed.isEmpty()) emit updated(changed); return; }
        batch->changed += changed;
        if (--batch->left == 0 && !batch->changed.isEmpty()) emit updated(batch->changed);
      };
      if (!rep.ok()) { announce({}); return; }
      // rep.stale: the poll failed and Http served the last cached answer (possibly days old). It never replaces a
      // quote already shown; it only fills a symbol that has none yet, marked stale.
      const bool stale = rep.stale;
      const auto res = QJsonDocument::fromJson(rep.body).object()["spark"].toObject()["result"].toArray();
      QStringList changed;
      for (const auto& v : res) {
        const auto o = v.toObject();
        const auto resp = o["response"].toArray();
        if (resp.isEmpty()) continue;
        const auto r0 = resp[0].toObject();
        const auto m = r0["meta"].toObject();
        Quote qt;
        qt.symbol = appSymbol(o["symbol"].toString());
        if (stale && m_quotes.contains(qt.symbol)) continue;
        qt.stale = stale;
        auto num = [&](const char* k) { return m.contains(k) && !m[k].isNull() ? m[k].toDouble() : NAN; };
        qt.price = num("regularMarketPrice");
        qt.prevClose = num("chartPreviousClose");
        if (!std::isfinite(qt.prevClose)) qt.prevClose = num("previousClose");
        qt.changePct = num("regularMarketChangePercent");
        if (std::isfinite(qt.price) && std::isfinite(qt.prevClose) && qt.prevClose != 0) {
          qt.change = qt.price - qt.prevClose;
          if (!std::isfinite(qt.changePct)) qt.changePct = qt.change / qt.prevClose * 100.0;
        }
        if (std::isfinite(qt.changePct) && !std::isfinite(qt.change) && std::isfinite(qt.price))
          qt.change = qt.price - qt.price / (1 + qt.changePct / 100.0);
        qt.dayHigh = num("regularMarketDayHigh"); qt.dayLow = num("regularMarketDayLow");
        qt.volume = num("regularMarketVolume"); qt.high52 = num("fiftyTwoWeekHigh"); qt.low52 = num("fiftyTwoWeekLow");
        qt.time = QDateTime::fromSecsSinceEpoch(qint64(m["regularMarketTime"].toDouble()), QTimeZone::UTC);
        qt.currency = m["currency"].toString(); qt.exchangeTz = m["exchangeTimezoneName"].toString();
        const auto ts = r0["timestamp"].toArray();
        const auto cl = r0["indicators"].toObject()["quote"].toArray().first().toObject()["close"].toArray();
        for (int k = 0; k < ts.size() && k < cl.size(); ++k) {
          if (cl[k].isNull()) continue;
          qt.sparkT << qint64(ts[k].toDouble()); qt.spark << cl[k].toDouble();
        }
        if (!qt.valid()) continue;
        m_quotes.insert(qt.symbol, qt);
        if (!stale) m_fetched.insert(qt.symbol, QDateTime::currentMSecsSinceEpoch());
        changed << qt.symbol;
      }
      announce(changed);
    });
  }
}
}
