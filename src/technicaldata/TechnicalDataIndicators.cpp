// Key indicators of the stored bars, computed by Charts' engine. See TechnicalDataIndicators.h.
#include "technicaldata/TechnicalDataIndicators.h"
#include "charts/core/Indicators.h"
#include "technicaldata/Collector.h"
#include "technicaldata/TechnicalDataSpec.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QSet>
#include <QPointer>
#include <QThread>
#include <QTimeZone>
#include <cmath>

namespace technicaldata {

const QVector<IndColumn>& indicatorColumns() {
  static const QVector<IndColumn> c = {
      {"sma20", "sma", {{"length", 20}}, "value"},
      {"sma50", "sma", {{"length", 50}}, "value"},
      {"sma200", "sma", {{"length", 200}}, "value"},
      {"ema20", "ema", {{"length", 20}}, "value"},
      {"ema50", "ema", {{"length", 50}}, "value"},
      {"ema200", "ema", {{"length", 200}}, "value"},
      {"bb_upper", "bb", {{"length", 20}, {"mult", 2}}, "upper"},
      {"bb_basis", "bb", {{"length", 20}, {"mult", 2}}, "basis"},
      {"bb_lower", "bb", {{"length", 20}, {"mult", 2}}, "lower"},
      {"bb_pb", "bbpb", {{"length", 20}, {"mult", 2}}, "value"},
      {"bb_width", "bbw", {{"length", 20}, {"mult", 2}}, "value"},
      {"rsi14", "rsi", {{"length", 14}}, "value"},
      {"macd", "macd", {{"fast", 12}, {"slow", 26}, {"signal", 9}}, "macd"},
      {"macd_signal", "macd", {{"fast", 12}, {"slow", 26}, {"signal", 9}}, "signal"},
      {"macd_hist", "macd", {{"fast", 12}, {"slow", 26}, {"signal", 9}}, "hist"},
      {"atr14", "atr", {{"length", 14}}, "value"},
      {"di_plus", "dmi", {{"diLength", 14}, {"adxSmoothing", 14}}, "plus"},
      {"di_minus", "dmi", {{"diLength", 14}, {"adxSmoothing", 14}}, "minus"},
      {"adx14", "dmi", {{"diLength", 14}, {"adxSmoothing", 14}}, "adx"},
      {"stoch_k", "stoch", {{"k", 14}, {"smoothK", 1}, {"d", 3}}, "k"},
      {"stoch_d", "stoch", {{"k", 14}, {"smoothK", 1}, {"d", 3}}, "d"},
      {"obv", "obv", {}, "value"},
      {"vwap", "vwap", {{"source", "hlc3"}, {"anchor", "day"}}, "value", true},
  };
  return c;
}

QStringList indicatorColumnNames() {
  QStringList n;
  for (const auto& c : indicatorColumns()) n << c.name;
  return n;
}

QString indicatorVersion() {
  static const QString v = [] {
    QJsonArray a;
    for (const auto& c : indicatorColumns())
      a << QJsonObject{{"name", c.name}, {"id", c.id}, {"params", c.params}, {"output", c.output}, {"intraday", c.intradayOnly}};
    // the engine's own changes that move values: stdev became Pine's two-pass formula on 2026-11-18
    a << QJsonObject{{"engine", "charts::ind 2026-11-18 stdev two-pass"}, {"overlap", OVERLAP}, {"warmup", WARMUP}};
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(a).toJson(QJsonDocument::Compact), QCryptographicHash::Sha1).toHex().left(12));
  }();
  return v;
}

namespace {
// computeRows runs on the computing thread, on every piece: one log line per indicator and settings, not per piece
void reportFailure(const QString& key, const QString& why) {
  static QMutex mutex;
  static QSet<QString> told;
  {
    const QMutexLocker lock(&mutex);
    if (told.contains(key)) return;
    told.insert(key);
  }
  qWarning("technicaldata: indicator %s failed, its columns are stored empty: %s", qPrintable(key), qPrintable(why));
}
}  // namespace

QVector<IndRow> computeRows(const QVector<Kline>& bars, int from, int tf, qint64 seriesFirst, const QVector<IndColumn>& cols,
                            const QVector<double>& carried, int warmup) {
  QVector<IndRow> out;
  const int n = int(bars.size());
  if (from >= n) return out;
  charts::Bars cb;
  cb.reserve(n);
  for (const Kline& k : bars) cb << charts::Bar{k.t, k.o, k.h, k.l, k.c, k.v};
  // one computation per indicator and settings (bb gives three columns)
  QHash<QString, charts::ind::Result> done;
  QVector<charts::Series> series(cols.size());   // empty = no value (implicitly shared copies of the results)
  for (int i = 0; i < cols.size(); ++i) {
    if (!cols[i].appliesTo(tf)) continue;
    const QString key = cols[i].id + QString::fromUtf8(QJsonDocument(cols[i].params).toJson(QJsonDocument::Compact));
    if (!done.contains(key)) {
      charts::ind::Result r;
      // no value, but said once per indicator and settings: a silent catch stored empty columns with no trace of why
      try { r = charts::ind::compute(cols[i].id, cb, cols[i].params); }
      catch (const std::exception& e) { reportFailure(key, QString::fromUtf8(e.what())); }
      catch (...) { reportFailure(key, QStringLiteral("unknown exception")); }
      done.insert(key, r);
    }
    series[i] = done.value(key).value(cols[i].output);
  }
  // a running sum (OBV) starts from the first bar computed: carried over from the bar before this piece
  QVector<double> shift(cols.size(), 0.0);
  for (int i = 0; i < cols.size(); ++i)
    if (cols[i].running && from > 0 && i < carried.size() && std::isfinite(carried[i]) && series[i].size() >= from &&
        std::isfinite(series[i][from - 1]))
      shift[i] = carried[i] - series[i][from - 1];
  out.reserve(n - from);
  const qint64 warmEnd = seriesFirst + qint64(warmup) * tf;
  for (int b = from; b < n; ++b) {
    IndRow r;
    r.t = bars[b].t;
    r.warm = r.t < warmEnd;
    r.v.resize(cols.size());
    for (int i = 0; i < cols.size(); ++i) {
      const double x = b < series[i].size() ? series[i][b] : NAN;
      r.v[i] = std::isfinite(x) ? x + shift[i] : x;
    }
    out << r;
  }
  return out;
}

// ------------------------------------------------------------------------------------------------ engine
Indicators& Indicators::instance() {
  static Indicators* i = new Indicators(&Store::instance());
  return *i;
}

Indicators::Indicators(Store* store, QObject* parent) : QObject(parent), m_store(store) {
  for (const auto& t : timeframes()) m_tfs << t.seconds;
  m_debounce.setSingleShot(true);
  m_debounce.setInterval(2000);   // the collector writes a page every ~0.3 s: one computation for a few pages
  connect(&m_debounce, &QTimer::timeout, this, [this] {
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it) {
      const QString market = it.key().section('|', 0, 0);
      const int tf = it.key().section('|', 1).toInt();
      bool merged = false;
      for (Job& j : m_queue)
        if (j.market == market && j.tf == tf) { if (it.value() > 0) j.from = j.from > 0 ? std::min(j.from, it.value()) : it.value(); merged = true; }
      if (!merged) m_queue << Job{market, tf, it.value()};
    }
    m_pending.clear();
    emit progressChanged();
    next();
  });
}

QStringList Indicators::markets() const { return m_markets.isEmpty() ? m_store->pairs() : m_markets; }

QVector<int> Indicators::timeframesOf(const QString& market) const {
  QVector<int> out;
  const PairSpec spec = specFromJson(m_store->pairSpec(market));
  for (int tf : spec.timeframes) if (m_tfs.contains(tf)) out << tf;
  return out;
}

void Indicators::start() {
  if (m_started) return;
  m_started = true;
  connect(m_store, &Store::changed, this, [this](const QString& market, int tf, qint64 from) {
    // a first day moved earlier, its bars still coming page after page: each page is in the past and would have the
    // whole later history computed again (a full BTCUSDT 1m series per page); kept until the backfill ends
    if (m_store->backfilling(market, tf)) {
      qint64& held = m_held[market + "|" + QString::number(tf)];
      held = held > 0 ? std::min(held, from) : from;
      return;
    }
    // appended bars: computed after the last one; a hole filled in the past: from there
    const IndState s = m_store->indState(market, tf);
    schedule(market, tf, s.count > 0 && from <= s.last ? from : 0);
  });
  // the backfill is over: once, from the earliest bar it added
  connect(m_store, &Store::backfillDone, this, [this](const QString& market, int tf) {
    const qint64 from = m_held.take(market + "|" + QString::number(tf));
    if (from <= 0) return;
    const IndState s = m_store->indState(market, tf);
    schedule(market, tf, s.count > 0 && from <= s.last ? from : 0);
  });
  // a pair's settings changed: its version says what to compute again
  connect(m_store, &Store::pairsChanged, this, [this](const QString& market) {
    for (int tf : timeframesOf(market)) schedule(market, tf, 0);
  });
  for (const QString& m : markets())
    for (int tf : timeframesOf(m)) schedule(m, tf, 0);
}

void Indicators::schedule(const QString& market, int tf, qint64 from) {
  const QString k = market + "|" + QString::number(tf);
  const auto it = m_pending.constFind(k);
  if (it == m_pending.cend()) m_pending.insert(k, from);
  else if (from > 0) m_pending[k] = it.value() > 0 ? std::min(it.value(), from) : from;
  // 2 s after the first change, not after the last: the collector writes a page every ~0.3 s for half an hour
  if (!m_debounce.isActive()) m_debounce.start();
}

void Indicators::shutdown(int waitMs) {
  m_stopping = true;
  m_debounce.stop();
  m_queue.clear();
  if (m_thread && m_thread->isRunning()) m_thread->wait(QDeadlineTimer(waitMs));
}

void Indicators::next() {
  if (m_running || m_stopping) return;
  if (m_queue.isEmpty()) {
    m_status.clear();
    emit progressChanged();
    // changes still waiting for their 2 s are work to come (the timer calls next() again): not idle yet
    if (!m_debounce.isActive()) emit idle();
    return;
  }
  const Job j = m_queue.takeFirst();
  m_running = true;
  QPointer<Indicators> self(this);
  QThread* t = QThread::create([this, j] { run(j); });
  m_thread = t;
  t->setObjectName("market-indicators");
  connect(t, &QThread::finished, t, &QObject::deleteLater);
  connect(t, &QThread::finished, this, [this] { m_running = false; emit progressChanged(); next(); });
  t->start(QThread::LowPriority);
}

void Indicators::run(const Job& j) {
  // the generation first: a cancel or new settings after this point drops what is computed from what is read here
  const int gen = m_store->generation(j.market);
  m_store->wait();   // the bars and indicator rows queued so far are written (this worker thread waits, not the window)
  const PairSpec spec = specFromJson(m_store->pairSpec(j.market));
  if (!spec.timeframes.contains(j.tf)) return;   // not a pair, or not one of its timeframes (any more)
  const SyncState bars = m_store->state(j.market, j.tf);
  if (bars.count == 0) return;
  const IndState ind = m_store->indState(j.market, j.tf);
  const QVector<IndColumn> cols = columnsOf(spec);
  if (cols.isEmpty()) {   // no indicator kept: the old rows go
    if (ind.count > 0) m_store->clearIndicators(j.market, j.tf);
    m_store->wait();
    return;
  }
  const QString version = versionOf(spec);
  const int warmup = warmupOf(spec);
  bool replace = ind.version != version || ind.count == 0;
  qint64 start = replace ? bars.first : ind.last + j.tf;
  if (!replace && j.from > 0 && j.from <= ind.last) start = j.from;   // a hole filled in the past
  if (start > bars.last) return;
  QStringList names;
  bool anyRunning = false;
  for (const IndColumn& c : cols) { names << c.name; anyRunning |= c.running; }
  QVector<double> carried;
  qint64 carriedAt = 0;
  QString tfName;
  for (const auto& t : timeframes()) if (t.seconds == j.tf) tfName = t.name;
  for (;;) {
    if (m_stopping) break;   // Deepwater closing: the next run goes on from the last piece written
    const QVector<Kline> after = m_store->range(j.market, j.tf, start, LLONG_MAX, m_piece);
    if (after.isEmpty()) break;
    const QVector<Kline> before = m_store->before(j.market, j.tf, start, m_overlap);
    QVector<Kline> all = before;
    all += after;
    QVector<double> prev(cols.size(), NAN);
    if (!before.isEmpty() && anyRunning) {
      const qint64 prevT = before.last().t;
      if (carriedAt == prevT) prev = carried;
      else if (!replace) {   // the stored row, of the same definitions
        const auto r = m_store->indicators(j.market, j.tf, prevT, prevT);
        if (!r.isEmpty()) prev = r.first().v;
      }
    }
    const QVector<IndRow> rows = computeRows(all, int(before.size()), j.tf, bars.first, cols, prev, warmup);
    if (!rows.isEmpty()) { carried = rows.last().v; carriedAt = rows.last().t; }
    m_store->addIndicators(j.market, j.tf, names, rows, version, replace, nullptr, {}, gen);
    // one piece in flight at most: without it a full recompute queues every piece (200,000 rows each, up to ~1 GB for
    // BTCUSDT 1m) to the writer faster than it writes, and quitting then blocks the window until all are written
    m_store->wait();
    if (m_store->generation(j.market) != gen) break;   // changed meanwhile: computed again from the new state
    replace = false;
    const QString status = QCoreApplication::translate("data", "Indicators %1 %2: up to %3")
                               .arg(j.market, tfName, QDateTime::fromSecsSinceEpoch(after.last().t, QTimeZone::UTC).toString("yyyy-MM-dd HH:mm"));
    QMetaObject::invokeMethod(this, [this, status] { m_status = status; emit progressChanged(); }, Qt::QueuedConnection);
    if (after.size() < m_piece) break;
    start = after.last().t + j.tf;
  }
  m_store->wait();
}

}  // namespace technicaldata
