// technical-data.sqlite (MarketStore) and the Binance collector against a fake Binance: paging from the first bar, closed
// bars only, a hole the exchange fills when asked again, a hole it never fills (its own outage), the mirror when the
// main host fails, catching up after time passes.
#include "Check.h"
#include "technicaldata/Collector.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrlQuery>
#include <memory>
#include <QDateTime>
#include <QSet>
#include <QTimeZone>
#include <QJsonArray>

using namespace technicaldata;

namespace {
constexpr qint64 T0 = 1502928000;   // 2017-08-17 00:00 UTC, BTCUSDT's first day on Binance
constexpr int TF = 60;

// A fake exchange: 1m bars from T0 up to the clock (the last one still forming), without `outage` (never served)
// and, on the first pass only, without `flaky` (served when asked for that range).
struct FakeBinance {
  qint64 nowMs = 0;
  QPair<qint64, qint64> outage{-1, -1}, flaky{-1, -1};
  bool flakyServed = false, mainDown = false;
  int requests = 0, mirrorRequests = 0;
  int delayMs = 0;   // each answer this late (a real page takes ~0.3 s)
  QJsonArray bar(qint64 t) const {
    const double p = 4000 + double((t - T0) / TF % 997);
    return QJsonArray{double(t * 1000), QString::number(p), QString::number(p + 5), QString::number(p - 5), QString::number(p + 1),
                      "1.5", double((t + TF) * 1000 - 1), "6000.5", 42, "0.7", "2800.2", "0"};
  }
  void serve(const QUrl& url, Collector::Reply reply) {
    ++requests;
    const bool mirror = url.host() == "data-api.binance.vision";
    if (mirror) ++mirrorRequests;
    if (mainDown && !mirror) { QTimer::singleShot(0, [reply] { reply(451, "{\"msg\":\"restricted\"}", {}); }); return; }
    const QUrlQuery q(url);
    const qint64 start = q.queryItemValue("startTime").toLongLong() / 1000;
    const qint64 end = q.hasQueryItem("endTime") ? q.queryItemValue("endTime").toLongLong() / 1000 : LLONG_MAX;
    const int limit = q.queryItemValue("limit").toInt();
    const bool askingFlaky = q.hasQueryItem("endTime") && start <= flaky.second && end >= flaky.first;   // a hole asked again
    QJsonArray out;
    for (qint64 t = std::max(T0, (start + TF - 1) / TF * TF); t <= end && t * 1000 < nowMs && out.size() < limit; t += TF) {
      if (t >= outage.first && t <= outage.second) continue;
      if (t >= flaky.first && t <= flaky.second && !askingFlaky) continue;
      out << bar(t);
    }
    const QByteArray body = QJsonDocument(out).toJson(QJsonDocument::Compact);
    QTimer::singleShot(delayMs, [reply, body] { reply(200, body, {}); });
  }
};

struct Rig {
  QTemporaryDir dir;
  std::unique_ptr<Store> store;
  std::unique_ptr<Collector> col;
  std::shared_ptr<FakeBinance> fake = std::make_shared<FakeBinance>();
  Rig() {
    store = std::make_unique<Store>(dir.filePath("technical-data.sqlite"));
    col = std::make_unique<Collector>(store.get());
    col->setTimeframes({{"1m", TF, "1m"}});
    col->setPageSize(100);
    auto f = fake;
    col->setFetch([f](const QUrl& u, QObject*, Collector::Reply r) { f->serve(u, std::move(r)); });
    col->setClock([f] { return f->nowMs; });
  }
  bool runUntilIdle(int ms = 20000) {
    QEventLoop loop;
    bool idle = false;
    QObject::connect(col.get(), &Collector::idle, &loop, [&] { idle = true; loop.quit(); });
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
    store->wait();
    return idle;
  }
};
}  // namespace

TEST("market: Binance klines are read, the bar still forming is left out") {
  FakeBinance f;
  f.nowMs = (T0 + 3 * TF + 30) * 1000;   // bars 0, 1, 2 closed; bar 3 forming
  QJsonArray a{f.bar(T0), f.bar(T0 + TF), f.bar(T0 + 2 * TF), f.bar(T0 + 3 * TF)};
  bool forming = false;
  const auto rows = parseKlines(QJsonDocument(a).toJson(), f.nowMs, &forming);
  CHECK_EQ(rows.size(), 3);
  CHECK(forming);
  CHECK_EQ(rows[0].t, T0);
  CHECK_EQ(rows[1].c, 4001.0 + 1);
  CHECK_EQ(rows[0].n, qint64(42));
  CHECK_NEAR(rows[0].tbqv, 2800.2, 1e-9);
  QString err;
  CHECK(parseKlines("{\"code\":-1121,\"msg\":\"Invalid symbol.\"}", f.nowMs, nullptr, &err).isEmpty());
  CHECK_EQ(err, QString("Invalid symbol."));
}

TEST("market: the whole history from the first bar, page by page; a hole filled on asking again, an outage kept") {
  Rig r;
  const qint64 n = 1234;
  r.fake->nowMs = (T0 + n * TF + 20) * 1000;            // bars 0 … n-1 closed, bar n forming
  r.fake->outage = {T0 + 500 * TF, T0 + 509 * TF};        // 10 bars the exchange never had
  r.fake->flaky = {T0 + 800 * TF, T0 + 804 * TF};         // 5 bars missing from the first pass only
  r.col->start();
  CHECK(r.runUntilIdle());
  const SyncState s = r.store->state("BINANCE:BTCUSDT", TF);
  CHECK(s.historyDone);
  CHECK_EQ(s.first, T0);
  CHECK_EQ(s.last, T0 + (n - 1) * TF);                    // never the forming bar
  CHECK_EQ(s.count, n - 10);                              // the flaky hole was filled
  const auto gaps = r.store->gaps("BINANCE:BTCUSDT", TF);
  CHECK_EQ(gaps.size(), 1);
  if (!gaps.isEmpty()) {
    CHECK_EQ(gaps[0].from, T0 + 499 * TF);
    CHECK_EQ(gaps[0].to, T0 + 510 * TF);
    CHECK_EQ(gaps[0].missing, qint64(10));
    CHECK(gaps[0].checked);
  }
  const auto bars = r.store->range("BINANCE:BTCUSDT", TF, T0 + 800 * TF, T0 + 804 * TF);
  CHECK_EQ(bars.size(), 5);
  std::printf("    %d requests for %lld bars (pages of 100)\n", r.fake->requests, static_cast<long long>(s.count));
}

TEST("market: after time passes only the new closed bars are fetched; a minute's check asks nothing too early") {
  Rig r;
  r.fake->nowMs = (T0 + 300 * TF + 10) * 1000;
  r.col->start();
  CHECK(r.runUntilIdle());
  CHECK_EQ(r.store->state("BINANCE:BTCUSDT", TF).count, 300);
  const int before = r.fake->requests;
  r.col->syncNow();   // no newer closed bar: nothing to ask
  QCoreApplication::processEvents();
  CHECK_EQ(r.fake->requests, before);
  r.fake->nowMs += 7 * TF * 1000;   // seven more minutes
  r.col->syncNow();
  CHECK(r.runUntilIdle());
  const SyncState s = r.store->state("BINANCE:BTCUSDT", TF);
  CHECK_EQ(s.count, 307);
  CHECK_EQ(s.last, T0 + 306 * TF);
  CHECK(r.fake->requests - before <= 2);
}

TEST("market: when the main host refuses, the mirror serves; the stored bars carry every Binance field") {
  Rig r;
  r.fake->mainDown = true;
  r.fake->nowMs = (T0 + 150 * TF + 5) * 1000;
  r.col->start();
  CHECK(r.runUntilIdle());
  CHECK_EQ(r.store->state("BINANCE:BTCUSDT", TF).count, 150);
  CHECK(r.fake->mirrorRequests > 0);
  for (const auto& p : r.col->progress()) CHECK(p.error.isEmpty());
  const auto last = r.store->last("BINANCE:BTCUSDT", TF, 2);
  CHECK_EQ(last.size(), 2);
  if (last.size() == 2) {
    CHECK_EQ(last[1].t, T0 + 149 * TF);
    CHECK_EQ(last[1].v, 1.5);
    CHECK_EQ(last[1].qv, 6000.5);
    CHECK_EQ(last[1].tbv, 0.7);
  }
}

TEST("market: storing the same bars twice keeps one copy and counts them once") {
  QTemporaryDir dir;
  Store st(dir.filePath("m.sqlite"));
  QVector<Kline> rows;
  for (int i = 0; i < 10; ++i) rows << Kline{T0 + i * TF, 1, 2, 0.5, 1.5, 10, 15, 3, 4, 6};
  st.add("BINANCE:BTCUSDT", TF, rows);
  st.add("BINANCE:BTCUSDT", TF, rows.mid(5));
  st.wait();
  CHECK_EQ(st.state("BINANCE:BTCUSDT", TF).count, 10);
  CHECK_EQ(st.range("BINANCE:BTCUSDT", TF, 0, LLONG_MAX).size(), 10);
  CHECK_EQ(st.markets().value(0), QString("BINANCE:BTCUSDT"));   // then the seven forex majors
  CHECK_EQ(st.pairs().size(), 8);
}

// ------------------------------------------------------------------------------------------------ indicators
#include "charts/core/Indicators.h"
#include "technicaldata/TechnicalDataIndicators.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>

namespace {
QVector<Kline> minuteFixture() {
  QVector<Kline> out;
  QFile f(QFileInfo(QString::fromUtf8(__FILE__)).dir().absoluteFilePath("fixtures/binance-btcusdt-1m.csv"));
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
  while (!f.atEnd()) {
    const QByteArray line = f.readLine().trimmed();
    if (line.isEmpty() || line.startsWith('#') || line.startsWith("time")) continue;
    const QList<QByteArray> c = line.split(',');
    Kline k;
    k.t = c[0].toLongLong(); k.o = c[1].toDouble(); k.h = c[2].toDouble(); k.l = c[3].toDouble(); k.c = c[4].toDouble(); k.v = c[5].toDouble();
    out << k;
  }
  return out;
}
// every column of `rows` against Charts computed once on all of `bars`
int compareWithWhole(const QVector<IndRow>& rows, const QVector<Kline>& bars, int tf, double* worst) {
  charts::Bars cb;
  for (const Kline& k : bars) cb << charts::Bar{k.t, k.o, k.h, k.l, k.c, k.v};
  QHash<qint64, int> at;
  for (int i = 0; i < bars.size(); ++i) at.insert(bars[i].t, i);
  const auto& cols = indicatorColumns();
  int bad = 0;
  *worst = 0;
  for (int c = 0; c < cols.size(); ++c) {
    const charts::Series whole = cols[c].intradayOnly && tf >= 86400 ? charts::Series() : charts::ind::compute(cols[c].id, cb, cols[c].params).value(cols[c].output);
    for (const IndRow& r : rows) {
      const double a = r.v.value(c, NAN), b = whole.value(at.value(r.t, -1), NAN);
      if (std::isnan(a) != std::isnan(b)) { if (bad++ < 3) std::printf("    %s at %lld: %g vs %g\n", qPrintable(cols[c].name), static_cast<long long>(r.t), a, b); continue; }
      if (std::isnan(a)) continue;
      const double rel = std::fabs(a - b) / std::max(1.0, std::fabs(b));
      *worst = std::max(*worst, rel);
      if (rel > 1e-9 && bad++ < 3) std::printf("    %s at %lld: %.17g vs %.17g\n", qPrintable(cols[c].name), static_cast<long long>(r.t), a, b);
    }
  }
  return bad;
}
bool waitIdle(Indicators& ind, Store& st, int ms = 60000) {
  QEventLoop loop;
  bool idle = false;
  QObject::connect(&ind, &Indicators::idle, &loop, [&] { idle = true; loop.quit(); });
  QTimer::singleShot(ms, &loop, &QEventLoop::quit);
  loop.exec();
  st.wait();
  return idle;
}
}  // namespace

TEST("market indicators: computed in pieces they equal Charts computed once on the whole series") {
  const QVector<Kline> bars = minuteFixture();
  CHECK_EQ(bars.size(), 5000);
  QTemporaryDir dir;
  Store st(dir.filePath("m.sqlite"));
  st.add("BINANCE:BTCUSDT", 60, bars);
  st.wait();
  Indicators ind(&st);
  ind.setTimeframes({60});
  ind.setPiece(700);
  ind.setOverlap(3000);   // the real overlap is 5000; 3000 still forgets an EMA 200's seed to ~1e-13
  ind.start();
  CHECK(waitIdle(ind, st));
  const auto rows = st.indicators("BINANCE:BTCUSDT", 60, 0, LLONG_MAX);
  CHECK_EQ(rows.size(), 5000);
  CHECK_EQ(st.indicatorColumns("BINANCE:BTCUSDT"), indicatorColumnNames());
  const IndState s = st.indState("BINANCE:BTCUSDT", 60);
  CHECK_EQ(s.count, 5000);
  CHECK_EQ(s.version, indicatorVersion());
  double worst = 0;
  CHECK_EQ(compareWithWhole(rows, bars, 60, &worst), 0);
  CHECK(rows[599].warm && !rows[600].warm);
  CHECK(std::isfinite(rows.last().v[indicatorColumnNames().indexOf("vwap")]));
  std::printf("    5000 bars in pieces of 700, %d columns, largest relative difference %.2g\n", int(indicatorColumns().size()), worst);
}

TEST("market indicators: new bars are computed after the last one; a hole filled in the past is computed again") {
  QVector<Kline> bars = minuteFixture();
  QTemporaryDir dir;
  Store st(dir.filePath("m.sqlite"));
  // the first pass lacks bars 2000 … 2099 (a hole the collector fills later) and the last 50
  QVector<Kline> first = bars.mid(0, 2000) + bars.mid(2100, 2850);
  st.add("BINANCE:BTCUSDT", 60, first);
  st.wait();
  Indicators ind(&st);
  ind.setTimeframes({60});
  ind.setPiece(1500);
  ind.start();
  CHECK(waitIdle(ind, st));
  CHECK_EQ(st.indState("BINANCE:BTCUSDT", 60).count, 4850);
  // the hole is filled, then 50 new bars come
  st.add("BINANCE:BTCUSDT", 60, bars.mid(2000, 100));
  st.add("BINANCE:BTCUSDT", 60, bars.mid(4950));
  CHECK(waitIdle(ind, st));
  const auto rows = st.indicators("BINANCE:BTCUSDT", 60, 0, LLONG_MAX);
  CHECK_EQ(rows.size(), 5000);
  CHECK_EQ(st.indState("BINANCE:BTCUSDT", 60).count, 5000);
  double worst = 0;
  CHECK_EQ(compareWithWhole(rows, bars, 60, &worst), 0);
}

TEST("market indicators: no VWAP on daily bars; changed definitions mean computing again") {
  QVector<Kline> days;
  for (int i = 0; i < 300; ++i) days << Kline{T0 + i * 86400, 100.0 + i, 102.0 + i, 99.0 + i, 101.0 + i, 10, 1000, 5, 4, 400};
  QTemporaryDir dir;
  Store st(dir.filePath("m.sqlite"));
  st.add("BINANCE:BTCUSDT", 86400, days);
  st.wait();
  // as written by older definitions
  QVector<IndRow> old{IndRow{T0, true, {1, 2}}};
  st.addIndicators("BINANCE:BTCUSDT", 86400, {"a", "b"}, old, "old-version");
  st.wait();
  Indicators ind(&st);
  ind.setTimeframes({86400});
  ind.start();
  CHECK(waitIdle(ind, st));
  CHECK_EQ(st.indicatorColumns("BINANCE:BTCUSDT"), indicatorColumnNames());
  const auto rows = st.indicators("BINANCE:BTCUSDT", 86400, 0, LLONG_MAX);
  CHECK_EQ(rows.size(), 300);
  CHECK_EQ(st.indState("BINANCE:BTCUSDT", 86400).version, indicatorVersion());
  const int vw = int(indicatorColumnNames().indexOf("vwap"));
  for (const auto& r : rows) CHECK(std::isnan(r.v[vw]));
  CHECK(std::isfinite(rows.last().v[indicatorColumnNames().indexOf("sma200")]));
}

TEST("market indicators: bars written without a pause (the collector's pages) still get computed") {
  const QVector<Kline> bars = minuteFixture();
  QTemporaryDir dir;
  Store st(dir.filePath("m.sqlite"));
  Indicators ind(&st);
  ind.setTimeframes({60});
  ind.start();
  // a page every 300 ms for 4.5 s, as the collector writes them
  QEventLoop loop;
  int k = 0;
  QTimer pages;
  QObject::connect(&pages, &QTimer::timeout, &loop, [&] {
    if (k >= 15) { pages.stop(); loop.quit(); return; }
    st.add("BINANCE:BTCUSDT", 60, bars.mid(k * 300, 300));
    ++k;
  });
  pages.start(300);
  loop.exec();
  CHECK(st.indState("BINANCE:BTCUSDT", 60).count > 0);   // computed while the pages were still coming
  CHECK(waitIdle(ind, st));
  CHECK_EQ(st.indState("BINANCE:BTCUSDT", 60).count, 4500);
}

// ------------------------------------------------------------------------------------------------ pairs
#include "technicaldata/TechnicalDataSpec.h"

TEST("pairs: BTCUSDT's default settings give its columns, names and version as before (nothing computed again)") {
  const PairSpec d = defaultSpec();
  CHECK_EQ(d.timeframes.size(), 7);
  CHECK_EQ(columnNames(d), indicatorColumnNames());
  CHECK_EQ(versionOf(d), indicatorVersion());
  CHECK_EQ(warmupOf(d), WARMUP);
  CHECK_EQ(specToJson(specFromJson(specToJson(d))), specToJson(d));
  // the same indicator written with all its settings is the same column
  CHECK_EQ(columnNames(PairSpec{{60}, 0, {IndSpec{"sma", fullParams("sma", {})}}}), QStringList{"sma20"});
  CHECK_EQ(columnNames(PairSpec{{60}, 0, {IndSpec{"bb", QJsonObject{{"length", 20}, {"mult", 2}, {"source", "close"}}}}}),
           (QStringList{"bb_upper", "bb_basis", "bb_lower"}));
  // others: named from their settings
  CHECK_EQ(columnNames(PairSpec{{60}, 0, {IndSpec{"rsi", QJsonObject{{"length", 7}}}}}), (QStringList{"rsi7", "rsi7_ma"}));
  IndSpec rsiOnly{"rsi", QJsonObject{{"length", 7}}};
  rsiOnly.outputs = {"value"};
  CHECK_EQ(columnNames(PairSpec{{60}, 0, {rsiOnly}}), QStringList{"rsi7"});
  CHECK_EQ(columnNames(PairSpec{{60}, 0, {IndSpec{"ema", QJsonObject{{"length", 100}, {"source", "hl2"}}}}}), QStringList{"ema100_hl2"});
  const QStringList bb = columnNames(PairSpec{{60}, 0, {IndSpec{"bb", QJsonObject{{"length", 20}, {"mult", 2.5}}}}});
  CHECK_EQ(bb.size(), 3);
  CHECK(bb.value(0).startsWith("bb20_mult2p5_"));
  IndSpec upper{"bb", QJsonObject{{"length", 20}, {"mult", 2}}};
  upper.outputs = {"upper"};
  CHECK_EQ(columnNames(PairSpec{{60}, 0, {upper}}), QStringList{"bb_upper"});
  CHECK_EQ(warmupOf(PairSpec{{60}, 0, {IndSpec{"ema", QJsonObject{{"length", 400}}}}}), 1200);
  // the first day is in the version (warm-up and running sums start there), the timeframes are not
  PairSpec later = d;
  later.start = 1600000000;
  CHECK(versionOf(later) != versionOf(d));
  later = d;
  later.timeframes = {3600};
  CHECK_EQ(versionOf(later), versionOf(d));
}

TEST("pairs: every Charts indicator checked by computing it: what looks ahead or needs the whole history is refused") {
  QVector<int> tfs;
  for (const auto& t : timeframes()) tfs << t.seconds;
  int ok = 0, running = 0, partial = 0, refused = 0;
  QStringList ahead, whole, other;
  for (const auto& d : charts::ind::all()) {
    if (!storable(d.id)) { other << d.id + "(not offered)"; continue; }
    const IndCheck c = checkIndicator(d.id, {}, tfs);
    if (!c.ok) {
      ++refused;
      (c.reason.contains("looks ahead") ? ahead : c.reason.contains("depends on more") ? whole : other) << d.id;
      continue;
    }
    if (!c.ahead.isEmpty()) ahead << d.id + "(" + c.ahead.join(",") + " only)";
    ++ok;
    running += c.running;
    if (c.tfs.size() < tfs.size()) { ++partial; other << d.id + "(" + c.reason + ")"; }
  }
  std::printf("    %d accepted (%d running sums, %d not on every timeframe), %d refused\n", ok, running, partial, refused);
  std::printf("    looks ahead: %s\n    whole history: %s\n    other: %s\n", qPrintable(ahead.join(" ")), qPrintable(whole.join(" ")),
              qPrintable(other.join(" | ")));
  // the defaults pass, the OBV as a running sum
  for (const IndSpec& i : defaultSpec().indicators) {
    const IndCheck c = checkIndicator(i.id, i.params, tfs);
    CHECK(c.ok);
    if (!c.ok) std::printf("    %s: %s\n", qPrintable(i.id), qPrintable(c.reason));
    CHECK_EQ(c.running, i.id == "obv");
  }
  CHECK(ahead.contains("zigzag"));
  const IndCheck ichimoku = checkIndicator("ichimoku", {}, {3600});
  CHECK(ichimoku.ok && !ichimoku.ahead.isEmpty() && !ichimoku.outputs.isEmpty());   // the lagging span only
  // a window longer than what is computed again before new bars
  const IndCheck longEma = checkIndicator("ema", QJsonObject{{"length", 3000}}, {3600});
  CHECK(!longEma.ok);
}

namespace {
// every column of a pair against Charts computed once on the pair's stored bars
int compareSpec(Store& st, const QString& market, int tf, const PairSpec& spec) {
  const auto bars = st.range(market, tf, 0, LLONG_MAX);
  charts::Bars cb;
  for (const Kline& k : bars) cb << charts::Bar{k.t, k.o, k.h, k.l, k.c, k.v};
  const auto rows = st.indicators(market, tf, 0, LLONG_MAX);
  const QVector<IndColumn> cols = columnsOf(spec);
  int bad = 0;
  if (rows.size() != bars.size()) { std::printf("    %d rows for %d bars\n", int(rows.size()), int(bars.size())); return 1; }
  for (int c = 0; c < cols.size(); ++c) {
    const charts::Series whole = charts::ind::compute(cols[c].id, cb, cols[c].params).value(cols[c].output);
    for (int i = 0; i < rows.size(); ++i) {
      const double a = rows[i].v.value(c, NAN), b = whole.value(i, NAN);
      if (std::isnan(a) != std::isnan(b) || (!std::isnan(a) && std::fabs(a - b) > 1e-9 * std::max(1.0, std::fabs(b)))) {
        if (bad++ < 3) std::printf("    %s at %d: %.12g vs %.12g\n", qPrintable(cols[c].name), i, a, b);
      }
    }
  }
  return bad;
}
}  // namespace

TEST("pairs: a second pair from its own first day, with its own indicators; first day moved earlier, then later; removed") {
  Rig r;
  const qint64 n = 1500;
  r.fake->nowMs = (T0 + n * TF + 20) * 1000;
  const QString eth = "BINANCE:ETHUSDT";
  IndSpec rsi{"rsi", QJsonObject{{"length", 7}}};
  rsi.outputs = {"value"};
  PairSpec spec{{TF}, T0 + 200 * TF, {rsi, IndSpec{"ema", QJsonObject{{"length", 100}}}, IndSpec{"obv", {}, true}}};
  r.store->setPairSpec(eth, specToJson(spec));
  r.store->wait();
  CHECK(r.store->pairs().contains("BINANCE:BTCUSDT") && r.store->pairs().last() == eth);
  Indicators ind(r.store.get());
  ind.setTimeframes({TF});
  ind.start();
  r.col->start();
  CHECK(r.runUntilIdle());
  SyncState s = r.store->state(eth, TF);
  CHECK_EQ(s.first, T0 + 200 * TF);
  CHECK_EQ(s.count, n - 200);
  CHECK(s.historyDone);
  CHECK_EQ(r.store->state("BINANCE:BTCUSDT", TF).first, T0);
  CHECK(waitIdle(ind, *r.store));
  CHECK_EQ(r.store->indicatorColumns(eth), (QStringList{"rsi7", "ema100", "obv"}));
  CHECK_EQ(compareSpec(*r.store, eth, TF, spec), 0);
  CHECK_EQ(r.store->indState(eth, TF).version, versionOf(spec));
  CHECK_EQ(compareSpec(*r.store, "BINANCE:BTCUSDT", TF, defaultSpec()), 0);   // BTCUSDT's 23 columns, unchanged

  // earlier: only the bars before the first stored one are asked for
  const int before = r.fake->requests;
  spec.start = T0 + 50 * TF;
  r.store->setPairSpec(eth, specToJson(spec));
  CHECK(r.runUntilIdle());
  s = r.store->state(eth, TF);
  CHECK_EQ(s.first, T0 + 50 * TF);
  CHECK_EQ(s.count, n - 50);
  CHECK_EQ(s.startReq, T0 + 50 * TF);
  CHECK(r.fake->requests - before <= 5);
  CHECK(waitIdle(ind, *r.store));
  CHECK_EQ(compareSpec(*r.store, eth, TF, spec), 0);   // computed again from the new first bar (warm-up, OBV)
  const auto rows = r.store->indicators(eth, TF, 0, LLONG_MAX);
  CHECK(rows.value(0).warm);

  // later: the bars before it are deleted, the rest computed again
  spec.start = T0 + 400 * TF;
  r.store->setPairSpec(eth, specToJson(spec));
  r.store->wait();
  QCoreApplication::processEvents();
  s = r.store->state(eth, TF);
  CHECK_EQ(s.first, T0 + 400 * TF);
  CHECK_EQ(s.count, n - 400);
  CHECK(waitIdle(ind, *r.store));
  CHECK_EQ(compareSpec(*r.store, eth, TF, spec), 0);

  // another indicator: a new column, computed for that pair only
  spec.indicators << IndSpec{"atr", QJsonObject{{"length", 5}}};
  r.store->setPairSpec(eth, specToJson(spec));
  CHECK(waitIdle(ind, *r.store));
  CHECK_EQ(r.store->indicatorColumns(eth), (QStringList{"rsi7", "ema100", "obv", "atr5"}));
  CHECK_EQ(compareSpec(*r.store, eth, TF, spec), 0);
  CHECK(!r.store->indicatorColumns("BINANCE:BTCUSDT").contains("atr5"));

  // removed: everything it held goes, and a page arriving afterwards is dropped
  r.store->removePair("BINANCE:BTCUSDT");
  r.store->wait();
  CHECK(r.store->pairs().contains(eth) && !r.store->pairs().contains("BINANCE:BTCUSDT"));
  CHECK_EQ(r.store->state("BINANCE:BTCUSDT", TF).count, 0);
  CHECK(r.store->range("BINANCE:BTCUSDT", TF, 0, LLONG_MAX).isEmpty());
  r.store->add("BINANCE:BTCUSDT", TF, {Kline{T0, 1, 2, 0.5, 1.5, 10, 15, 3, 4, 6}});
  r.store->add(eth, 3600, {Kline{T0, 1, 2, 0.5, 1.5, 10, 15, 3, 4, 6}});   // not one of its timeframes
  r.store->wait();
  CHECK(r.store->range("BINANCE:BTCUSDT", TF, 0, LLONG_MAX).isEmpty());
  CHECK_EQ(r.store->state(eth, 3600).count, 0);
  CHECK(!r.store->markets().contains("BINANCE:BTCUSDT"));
}

TEST("pairs: a first day moved earlier computes the indicators once at the end of the backfill, not after every page") {
  Rig r;
  const qint64 n = 3000;
  r.fake->nowMs = (T0 + n * TF + 20) * 1000;
  const QString eth = "BINANCE:ETHUSDT";
  PairSpec spec{{TF}, T0 + 2000 * TF, {IndSpec{"ema", QJsonObject{{"length", 20}}}, IndSpec{"obv", {}, true}}};
  r.store->setPairSpec(eth, specToJson(spec));
  r.store->wait();
  Indicators ind(r.store.get());
  ind.setTimeframes({TF});
  ind.setMarkets({eth});
  ind.setDebounce(20);   // shorter than a page: without the hold every page would start a computation of its own
  ind.start();
  r.col->setMarkets({eth});
  r.col->start();
  CHECK(r.runUntilIdle());
  auto settle = [&] {
    QElapsedTimer e;
    e.start();
    while ((ind.busy() || r.col->busy()) && e.elapsed() < 30000) {
      QEventLoop l;
      QTimer::singleShot(10, &l, &QEventLoop::quit);
      l.exec();
    }
    r.store->wait();
    QCoreApplication::processEvents();
  };
  settle();
  CHECK_EQ(compareSpec(*r.store, eth, TF, spec), 0);
  // 18 pages of 100 bars before the first stored one, 60 ms apart
  int computed = 0;
  QObject::connect(r.store.get(), &Store::indicatorsChanged, [&](const QString& m, int) { if (m == eth) ++computed; });
  r.fake->delayMs = 60;
  spec.start = T0 + 200 * TF;
  r.store->setPairSpec(eth, specToJson(spec));
  CHECK(r.runUntilIdle());
  settle();
  CHECK_EQ(r.store->state(eth, TF).first, T0 + 200 * TF);
  CHECK_EQ(compareSpec(*r.store, eth, TF, spec), 0);
  // the settings saved (new version) and the end of the backfill; before, about one per page
  std::printf("    %d computations for 18 backfill pages\n", computed);
  CHECK(computed >= 1 && computed <= 3);
}

// ------------------------------------------------------------------------------------------------ forex (Dukascopy)
#include "technicaldata/Dukascopy.h"
#include "technicaldata/ForexCollector.h"

namespace {
using namespace technicaldata::dukascopy;
qint64 utc(int y, int mo, int d, int h = 0, int mi = 0) { return QDateTime(QDate(y, mo, d), QTime(h, mi), QTimeZone::UTC).toSecsSinceEpoch(); }
QString fixtures() { return QFileInfo(QString::fromUtf8(__FILE__)).dir().absoluteFilePath("fixtures/dukascopy"); }
QByteArray fixture(const QString& rel) {
  QFile f(fixtures() + "/" + rel);
  return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
// A fake datafeed: the synthetic files of fixtures/ (make_dukascopy.py), 404 for any other, one 503 when asked
struct FakeDukascopy {
  int requests = 0;
  QSet<QString> failOnce;
  QStringList asked;
  void serve(const QUrl& url, Collector::Reply reply) {
    ++requests;
    const QString rel = url.path().section("/datafeed/", 1);
    asked << rel;
    if (failOnce.remove(rel)) { QTimer::singleShot(0, [reply] { reply(503, "No server is available", "HTTP 503"); }); return; }
    const QByteArray body = fixture(rel);
    QTimer::singleShot(0, [reply, body] { body.isEmpty() ? reply(404, "", "HTTP 404") : reply(200, body, {}); });
  }
};
struct FxRig {
  QTemporaryDir dir;
  std::unique_ptr<Store> store;
  std::unique_ptr<ForexCollector> col;
  std::shared_ptr<FakeDukascopy> fake = std::make_shared<FakeDukascopy>();
  const QString m = "DUKASCOPY:EURUSD";
  FxRig(const PairSpec& spec) {
    store = std::make_unique<Store>(dir.filePath("technical-data.sqlite"));
    store->setPairSpec(m, specToJson(spec));
    store->wait();
    QCoreApplication::processEvents();   // the save's answer: the pair is no longer busy
    col = std::make_unique<ForexCollector>(store.get());
    col->setMarkets({m});
    col->setRetryDelays({5, 5, 5});
    auto f = fake;
    col->setFetch([f](const QUrl& u, QObject*, Collector::Reply r) { f->serve(u, std::move(r)); });
    col->setClock([] { return utc(2024, 3, 6, 12) * 1000; });
  }
  bool runUntilIdle(int ms = 20000) {
    QEventLoop loop;
    bool idle = false;
    QObject::connect(col.get(), &ForexCollector::idle, &loop, [&] { idle = true; loop.quit(); });
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
    store->wait();
    QCoreApplication::processEvents();
    store->wait();
    return idle;
  }
};
}  // namespace

TEST("forex: Dukascopy's files are read; the week is cut at 17:00 New York, summer time followed") {
  QString err;
  const auto jan = parse(fixture("EURUSD/2024/00/BID_candles_hour_1.bi5"), utc(2024, 1, 1), 1e-5, &err);
  CHECK(err.isEmpty());
  int open = 0;
  for (qint64 t = utc(2024, 1, 1); t < utc(2024, 2, 1); t += 3600) open += !closed(t);
  CHECK_EQ(int(jan.size()), open);   // the closed market's rows left out
  if (!jan.isEmpty()) {
    CHECK_EQ(jan.first().t, utc(2024, 1, 1));   // Monday 00:00 UTC: Sunday 19:00 in New York, open
    CHECK_NEAR(jan.first().o, 1.10000, 1e-12);
    CHECK(jan.first().h >= std::max(jan.first().o, jan.first().c) && jan.first().l <= std::min(jan.first().o, jan.first().c));
  }
  CHECK(parse("not lzma", 0, 1e-5, &err).isEmpty() && !err.isEmpty());
  CHECK(parse(QByteArray(), 0, 1e-5).isEmpty());
  // 17:00 New York is 22:00 UTC in winter, 21:00 from 2024-03-10
  CHECK_EQ(sessionStart(utc(2024, 3, 8, 12)), utc(2024, 3, 7, 22));
  CHECK_EQ(sessionStart(utc(2024, 3, 11, 12)), utc(2024, 3, 10, 21));
  CHECK_EQ(bucketStart(utc(2024, 3, 11, 12), 86400), utc(2024, 3, 10, 21));
  CHECK_EQ(bucketEnd(utc(2024, 3, 10, 21), 86400), utc(2024, 3, 11, 21));
  CHECK_EQ(bucketStart(utc(2024, 3, 11, 2), 14400), utc(2024, 3, 11, 1));   // 21:00, 01:00, 05:00 …
  CHECK_EQ(bucketStart(utc(2024, 3, 13, 12), 604800), utc(2024, 3, 10, 21));   // the week from Sunday 17:00
  CHECK_EQ(bucketEnd(utc(2024, 3, 10, 21), 604800), utc(2024, 3, 15, 21));     // to Friday 17:00
  CHECK(closed(utc(2024, 3, 9, 12)) && closed(utc(2024, 3, 8, 22, 30)) && !closed(utc(2024, 3, 10, 21, 30)) && !closed(utc(2024, 3, 8, 20)));
  // after Friday's last hour comes Sunday 17:00; after Friday's day, Monday's (from Sunday 17:00)
  CHECK_EQ(nextExpected(utc(2024, 3, 8, 21), 3600), utc(2024, 3, 10, 21));
  CHECK_EQ(nextExpected(utc(2024, 3, 7, 22), 86400), utc(2024, 3, 10, 21));
  CHECK_EQ(nextExpected(utc(2024, 3, 8, 20, 55), 300), utc(2024, 3, 8, 21));
}

TEST("forex: hours, days and weeks from the monthly files, recent days from minute files; a weekend is no hole") {
  FxRig r(PairSpec{{3600, 86400, 604800}, utc(2024, 1, 1), {IndSpec{"rsi", QJsonObject{{"length", 14}}}}});
  r.fake->failOnce.insert("EURUSD/2024/00/BID_candles_hour_1.bi5");   // asked again
  CHECK(r.store->pairSpec(r.m).value("timeframes").toArray().size() == 3);
  r.col->start();
  CHECK(r.runUntilIdle());
  // 2 months + 1 asked again + Mar 1, 3, 4, 5 (Saturday 2 skipped)
  CHECK_EQ(r.fake->requests, 7);
  if (r.fake->requests != 7) std::printf("    asked: %s\n", qPrintable(r.fake->asked.join(" ")));
  const SyncState h = r.store->state(r.m, 3600);
  CHECK_EQ(h.first, utc(2024, 1, 1));
  CHECK_EQ(h.last, utc(2024, 3, 5, 23));
  CHECK(h.historyDone);
  // the hole of 2024-02-14 10:00–12:00, not the weekends
  const auto gaps = r.store->gaps(r.m, 3600);
  CHECK_EQ(gaps.size(), 1);
  if (!gaps.isEmpty()) {
    CHECK_EQ(gaps[0].from, utc(2024, 2, 14, 9));
    CHECK_EQ(gaps[0].to, utc(2024, 2, 14, 12));
    CHECK_EQ(gaps[0].missing, qint64(2));
    CHECK(gaps[0].checked);
  }
  // days: Tuesday 2 January (from Monday 22:00 UTC) to Tuesday 5 March, 46 trading days
  const auto days = r.store->range(r.m, 86400, 0, LLONG_MAX);
  CHECK_EQ(days.size(), 46);
  if (!days.isEmpty()) { CHECK_EQ(days.first().t, utc(2024, 1, 1, 22)); CHECK_EQ(days.last().t, utc(2024, 3, 4, 22)); }
  CHECK(r.store->gaps(r.m, 86400).isEmpty());
  // a day = its hours from 17:00 to 17:00 New York
  if (days.size() > 10) {
    const auto hrs = r.store->range(r.m, 3600, days[10].t, days[10].t + 86400 - 1);
    double hi = -1, lo = 1e9;
    for (const auto& k : hrs) { hi = std::max(hi, k.h); lo = std::min(lo, k.l); }
    CHECK_NEAR(days[10].h, hi, 1e-12);
    CHECK_NEAR(days[10].l, lo, 1e-12);
    CHECK_NEAR(days[10].o, hrs.first().o, 1e-12);
    CHECK_NEAR(days[10].c, hrs.last().c, 1e-12);
  }
  // weeks from Sunday 22:00 UTC: 7 January to 25 February (the week of 3 March is not over)
  const auto weeks = r.store->range(r.m, 604800, 0, LLONG_MAX);
  CHECK_EQ(weeks.size(), 8);
  if (!weeks.isEmpty()) CHECK_EQ(weeks.first().t, utc(2024, 1, 7, 22));
  // the next check asks only what is new: nothing yet
  const int before = r.fake->requests;
  r.col->syncNow();
  CHECK(r.runUntilIdle());
  CHECK_EQ(r.fake->requests, before);
}

TEST("forex: 5m and 15m from each day's minutes, in order; a timeframe added later comes from what is stored") {
  FxRig r(PairSpec{{300, 900, 3600}, utc(2024, 3, 1), {}});
  r.col->start();
  CHECK(r.runUntilIdle());
  // hours from the recent days (Mar 1, 3, 4, 5), then the same four days for 5m and 15m
  CHECK_EQ(r.fake->requests, 8);
  const auto m5 = r.store->range(r.m, 300, 0, LLONG_MAX), m15 = r.store->range(r.m, 900, 0, LLONG_MAX);
  CHECK(m5.size() > 800 && m15.size() > 250);
  CHECK(r.store->state(r.m, 300).historyDone);
  // a 15m bar = its three 5m bars
  if (m15.size() > 40) {
    const auto& q = m15[40];
    const auto parts = r.store->range(r.m, 300, q.t, q.t + 899);
    CHECK_EQ(parts.size(), 3);
    if (parts.size() == 3) {
      CHECK_NEAR(q.o, parts[0].o, 1e-12);
      CHECK_NEAR(q.c, parts[2].c, 1e-12);
      CHECK_NEAR(q.h, std::max({parts[0].h, parts[1].h, parts[2].h}), 1e-12);
      CHECK_NEAR(q.v, parts[0].v + parts[1].v + parts[2].v, 1e-6);
    }
  }
  CHECK(r.store->gaps(r.m, 300).isEmpty());   // Friday evening to Sunday evening is the weekend
  // 1D added: made from the stored hours, nothing downloaded
  const int before = r.fake->requests;
  r.store->setPairSpec(r.m, specToJson(PairSpec{{300, 900, 3600, 86400}, utc(2024, 3, 1), {}}));
  CHECK(r.runUntilIdle());
  CHECK_EQ(r.fake->requests, before);
  CHECK_EQ(r.store->range(r.m, 86400, 0, LLONG_MAX).size(), 2);   // Monday 4 and Tuesday 5 (Friday's began before the 1st)
  // 1W asks for 1h, which it is made from
  r.store->setPairSpec(r.m, specToJson(PairSpec{{300, 604800}, utc(2024, 3, 1), {}}));
  r.store->wait();
  CHECK(specFromJson(r.store->pairSpec(r.m)).timeframes.contains(3600));
  // removing the pair forgets where its downloads stood
  r.store->removePair(r.m);
  r.store->wait();
  CHECK(r.store->meta("fx.minutes:" + r.m).isEmpty());
}

TEST("forex: a recent day Dukascopy has not published yet ends the turn; the next check asks for it again") {
  FxRig r(PairSpec{{300, 3600}, utc(2024, 3, 1), {}});
  r.col->setClock([] { return utc(2024, 3, 7, 12) * 1000; });   // yesterday = the 6th, which the fake does not have
  r.col->start();
  CHECK(r.runUntilIdle(5000));
  CHECK_EQ(r.fake->requests, 10);   // Mar 1, 3, 4, 5, 6 for the hours, then again for 5m
  CHECK(!r.store->state(r.m, 300).historyDone);
  CHECK_EQ(r.store->meta("fx.minutes:" + r.m), QString("2024-03-05"));
  r.col->syncNow();
  CHECK(r.runUntilIdle(5000));
  CHECK_EQ(r.fake->requests, 12);   // only the 6th, twice
}

// ------------------------------------------------------------------------------------------------ pause, cancel, restart
namespace {
// runs the event loop until cond() or ms
bool until(std::function<bool()> cond, int ms = 10000) {
  QElapsedTimer t;
  t.start();
  while (!cond() && t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  return cond();
}
}  // namespace

TEST("pause: a crypto pair paused mid-history asks nothing more and writes nothing more; resumed, it ends as if never paused") {
  Rig r;
  const qint64 n = 2000;
  r.fake->nowMs = (T0 + n * TF + 20) * 1000;
  r.col->start();
  CHECK(until([&] { return r.store->state("BINANCE:BTCUSDT", TF).count >= 500; }));
  r.store->setPaused("BINANCE:BTCUSDT", true);
  CHECK(r.store->isPaused("BINANCE:BTCUSDT"));
  until([] { return false; }, 400);   // the page on its way comes back
  r.store->wait();
  const qint64 at = r.store->state("BINANCE:BTCUSDT", TF).count;
  const int asked = r.fake->requests;
  until([] { return false; }, 600);
  r.col->syncNow();
  until([] { return false; }, 300);
  r.store->wait();
  CHECK_EQ(r.fake->requests, asked);
  CHECK_EQ(r.store->state("BINANCE:BTCUSDT", TF).count, at);
  CHECK(at < n);
  bool rowPaused = false;
  for (const auto& p : r.col->progress()) if (p.market == "BINANCE:BTCUSDT") rowPaused = p.paused;
  CHECK(rowPaused);
  r.store->setPaused("BINANCE:BTCUSDT", false);
  CHECK(r.runUntilIdle());
  const SyncState s = r.store->state("BINANCE:BTCUSDT", TF);
  CHECK_EQ(s.count, n);
  CHECK(s.historyDone);
  CHECK(r.store->gaps("BINANCE:BTCUSDT", TF).isEmpty());
}

TEST("pause: everything paused, kept when the database is opened again; a pair's pause too") {
  QTemporaryDir dir;
  {
    Store st(dir.filePath("t.sqlite"));
    st.setAllPaused(true);
    st.setPaused("DUKASCOPY:EURUSD", true);
    st.wait();
    // saving the pair's settings keeps the pause
    st.setPairSpec("DUKASCOPY:EURUSD", specToJson(defaultForexSpec()));
    st.wait();
  }
  Store st(dir.filePath("t.sqlite"));
  CHECK(st.allPaused());
  CHECK(st.pairPaused("DUKASCOPY:EURUSD"));
  CHECK(!st.pairPaused("BINANCE:BTCUSDT"));
  CHECK(st.isPaused("BINANCE:BTCUSDT"));
  // the writer drops bars of a paused pair
  st.add("BINANCE:BTCUSDT", TF, {Kline{T0, 1, 2, 0.5, 1.5, 10, 15, 3, 4, 6}});
  st.wait();
  CHECK_EQ(st.state("BINANCE:BTCUSDT", TF).count, 0);
  st.setAllPaused(false);
  st.add("BINANCE:BTCUSDT", TF, {Kline{T0, 1, 2, 0.5, 1.5, 10, 15, 3, 4, 6}});
  st.wait();
  CHECK_EQ(st.state("BINANCE:BTCUSDT", TF).count, 1);
}

TEST("restart: Deepwater closed mid-history and opened again ends with the same bars as one run") {
  QTemporaryDir dir;
  auto fake = std::make_shared<FakeBinance>();
  const qint64 n = 1800;
  fake->nowMs = (T0 + n * TF + 20) * 1000;
  fake->outage = {T0 + 900 * TF, T0 + 904 * TF};
  auto make = [&](Store* st) {
    auto c = std::make_unique<Collector>(st);
    c->setTimeframes({{"1m", TF, "1m"}});
    c->setPageSize(100);
    c->setFetch([fake](const QUrl& u, QObject*, Collector::Reply rep) { fake->serve(u, std::move(rep)); });
    c->setClock([fake] { return fake->nowMs; });
    return c;
  };
  {
    Store st(dir.filePath("t.sqlite"));
    auto c = make(&st);
    c->start();
    CHECK(until([&] { return st.state("BINANCE:BTCUSDT", TF).count >= 600; }));
    c->stop();   // as at closing: an answer still coming writes nothing
    until([] { return false; }, 300);
    st.wait();
  }
  Store st(dir.filePath("t.sqlite"));
  const qint64 before = st.state("BINANCE:BTCUSDT", TF).count;
  CHECK(before >= 600 && before < n);
  auto c = make(&st);
  QEventLoop loop;
  QObject::connect(c.get(), &Collector::idle, &loop, &QEventLoop::quit);
  QTimer::singleShot(20000, &loop, &QEventLoop::quit);
  c->start();
  loop.exec();
  st.wait();
  const SyncState s = st.state("BINANCE:BTCUSDT", TF);
  CHECK_EQ(s.count, n - 5);
  CHECK(s.historyDone);
  CHECK_EQ(st.range("BINANCE:BTCUSDT", TF, 0, LLONG_MAX).size(), n - 5);   // no bar twice
  CHECK_EQ(st.gaps("BINANCE:BTCUSDT", TF).size(), 1);                     // only the outage
}

TEST("forex pause and cancel: paused mid-turn nothing more is written; resumed it ends whole; cancelled it is empty and paused") {
  FxRig r(PairSpec{{3600, 86400, 604800}, utc(2024, 1, 1), {}});
  r.col->start();
  CHECK(until([&] { return r.store->state(r.m, 3600).count > 0; }));
  r.store->setPaused(r.m, true);
  until([] { return false; }, 300);
  r.store->wait();
  const QString cursor = r.store->meta("fx.hours:" + r.m);
  const qint64 hours = r.store->state(r.m, 3600).count;
  const int asked = r.fake->requests;
  r.col->syncNow();
  until([] { return false; }, 300);
  CHECK_EQ(r.fake->requests, asked);
  CHECK_EQ(r.store->state(r.m, 3600).count, hours);
  CHECK_EQ(r.store->meta("fx.hours:" + r.m), cursor);
  r.store->setPaused(r.m, false);
  CHECK(r.runUntilIdle());
  CHECK_EQ(r.store->range(r.m, 86400, 0, LLONG_MAX).size(), 46);   // as in one run
  CHECK_EQ(r.store->range(r.m, 604800, 0, LLONG_MAX).size(), 8);
  CHECK(r.store->state(r.m, 3600).historyDone);
  // cancel: everything it downloaded goes, the pair stays, paused
  bool done = false;
  r.store->cancelPair(r.m, nullptr, [&] { done = true; });
  CHECK(r.store->pairPaused(r.m));
  CHECK(until([&] { return done; }));
  for (int tf : {3600, 86400, 604800}) CHECK_EQ(r.store->state(r.m, tf).count, 0);
  CHECK(r.store->range(r.m, 3600, 0, LLONG_MAX).isEmpty());
  CHECK(r.store->meta("fx.hours:" + r.m).isEmpty());
  CHECK(r.store->pairs().contains(r.m));
  // resumed: from the first day again
  const int before = r.fake->requests;
  r.store->setPaused(r.m, false);
  CHECK(r.runUntilIdle());
  CHECK(r.fake->requests > before);
  CHECK_EQ(r.store->range(r.m, 86400, 0, LLONG_MAX).size(), 46);
}

TEST("forex: paused or stopped while files are on the wire (a slow server) ends the turn cleanly; nothing late is written") {
  FxRig r(PairSpec{{3600, 86400}, utc(2024, 1, 1), {}});
  // answers 150 ms late, as a slow Dukascopy
  auto f = r.fake;
  auto sent = std::make_shared<int>(0);
  r.col->setFetch([f, sent](const QUrl& u, QObject* ctx, Collector::Reply rep) {
    ++*sent;
    QTimer::singleShot(150, ctx, [f, u, rep] { f->serve(u, rep); });
  });
  r.col->start();
  CHECK(until([&] { return *sent >= 1; }));
  r.store->setPaused(r.m, true);   // the first months are on the wire
  until([] { return false; }, 600);
  r.store->wait();
  CHECK_EQ(r.store->state(r.m, 3600).count, 0);
  CHECK(!r.col->busy());
  r.store->setPaused(r.m, false);
  CHECK(until([&] { return *sent >= 4 && r.col->busy(); }));
  r.col->stop();   // Deepwater closing mid-turn
  const qint64 at = r.store->state(r.m, 3600).count;
  until([] { return false; }, 600);
  r.store->wait();
  CHECK_EQ(r.store->state(r.m, 3600).count, at);
  CHECK(!r.col->busy());
}

// ------------------------------------------------------------------------------------------------ actions while things run
TEST("interleaving: cancelled while its indicators are being computed, no indicator row is left behind") {
  const QVector<Kline> bars = minuteFixture();
  QTemporaryDir dir;
  Store st(dir.filePath("m.sqlite"));
  st.add("BINANCE:BTCUSDT", 60, bars);
  st.wait();
  Indicators ind(&st);
  ind.setTimeframes({60});
  ind.setPiece(500);   // ten pieces: the cancel lands between two
  ind.start();
  CHECK(until([&] { return st.indState("BINANCE:BTCUSDT", 60).count > 0; }, 20000));
  bool done = false;
  st.cancelPair("BINANCE:BTCUSDT", nullptr, [&] { done = true; });
  CHECK(until([&] { return done; }));
  CHECK(waitIdle(ind, st));
  CHECK_EQ(st.state("BINANCE:BTCUSDT", 60).count, 0);
  CHECK_EQ(st.indState("BINANCE:BTCUSDT", 60).count, 0);
  CHECK(st.indicators("BINANCE:BTCUSDT", 60, 0, LLONG_MAX).isEmpty());
  CHECK(st.cancelled("BINANCE:BTCUSDT") && st.pairPaused("BINANCE:BTCUSDT"));
}

TEST("interleaving: a pair removed mid-history then added again starts clean from its first day") {
  Rig r;
  const qint64 n = 1500;
  r.fake->nowMs = (T0 + n * TF + 20) * 1000;
  r.col->start();
  CHECK(until([&] { return r.store->state("BINANCE:BTCUSDT", TF).count >= 300; }));
  r.store->removePair("BINANCE:BTCUSDT");
  until([] { return false; }, 400);
  r.store->wait();
  CHECK_EQ(r.store->state("BINANCE:BTCUSDT", TF).count, 0);
  CHECK(!r.store->pairs().contains("BINANCE:BTCUSDT"));
  r.store->setPairSpec("BINANCE:BTCUSDT", specToJson(PairSpec{{TF}, 0, {}}));
  CHECK(r.runUntilIdle());
  const SyncState s = r.store->state("BINANCE:BTCUSDT", TF);
  CHECK_EQ(s.count, n);
  CHECK_EQ(s.first, T0);
  CHECK(s.historyDone);
  CHECK(!r.store->isPaused("BINANCE:BTCUSDT"));
}

TEST("interleaving: a timeframe removed while its history downloads: nothing more written for it, the others go on") {
  Rig r;
  r.col->setTimeframes({{"1m", TF, "1m"}, {"1h", 3600, "1h"}});
  const qint64 n = 3000;
  r.fake->nowMs = (T0 + n * TF + 20) * 1000;
  r.store->setPairSpec("BINANCE:BTCUSDT", specToJson(PairSpec{{TF, 3600}, 0, {}}));
  r.store->wait();
  r.col->start();
  CHECK(until([&] { return r.store->state("BINANCE:BTCUSDT", TF).count >= 300; }));
  r.store->setPairSpec("BINANCE:BTCUSDT", specToJson(PairSpec{{3600}, 0, {}}));
  CHECK(r.runUntilIdle());
  CHECK_EQ(r.store->state("BINANCE:BTCUSDT", TF).count, 0);    // deleted, and not written again by a late page
  CHECK(r.store->range("BINANCE:BTCUSDT", TF, 0, LLONG_MAX).isEmpty());
  CHECK(r.store->state("BINANCE:BTCUSDT", 3600).historyDone);
}

TEST("interleaving: everything paused while a pair is paused; resuming everything leaves that pair paused") {
  Rig r;
  const qint64 n = 800;
  r.fake->nowMs = (T0 + n * TF + 20) * 1000;
  r.store->setPaused("BINANCE:BTCUSDT", true);
  r.store->setAllPaused(true);
  r.col->start();
  until([] { return false; }, 300);
  CHECK_EQ(r.fake->requests, 0);
  r.store->setAllPaused(false);
  until([] { return false; }, 300);
  CHECK_EQ(r.fake->requests, 0);                                  // its own pause stays
  CHECK(r.store->pairPaused("BINANCE:BTCUSDT") && !r.store->allPaused());
  r.store->setPaused("BINANCE:BTCUSDT", false);
  CHECK(r.runUntilIdle());
  CHECK_EQ(r.store->state("BINANCE:BTCUSDT", TF).count, n);
}

TEST("interleaving: a cancelled pair whose settings are saved stays cancelled; started again it collects from its first day") {
  Rig r;
  const qint64 n = 600;
  r.fake->nowMs = (T0 + n * TF + 20) * 1000;
  r.col->start();
  CHECK(r.runUntilIdle());
  bool done = false;
  r.store->cancelPair("BINANCE:BTCUSDT", nullptr, [&] { done = true; });
  CHECK(until([&] { return done; }));
  r.store->setPairSpec("BINANCE:BTCUSDT", specToJson(PairSpec{{TF}, T0 + 100 * TF, {}}));
  r.store->wait();
  until([] { return false; }, 300);
  CHECK(r.store->cancelled("BINANCE:BTCUSDT"));
  CHECK(specFromJson(r.store->pairSpec("BINANCE:BTCUSDT")).paused);
  CHECK_EQ(r.store->state("BINANCE:BTCUSDT", TF).count, 0);
  bool rowCancelled = false;
  for (const auto& p : r.col->progress()) if (p.market == "BINANCE:BTCUSDT") rowCancelled = p.cancelled;
  CHECK(rowCancelled);
  r.store->setPaused("BINANCE:BTCUSDT", false);
  CHECK(!r.store->cancelled("BINANCE:BTCUSDT"));
  CHECK(r.runUntilIdle());
  const SyncState s = r.store->state("BINANCE:BTCUSDT", TF);
  CHECK_EQ(s.first, T0 + 100 * TF);
  CHECK_EQ(s.count, n - 100);
  // reopened, the state is as it was
  r.store->cancelPair("BINANCE:BTCUSDT");
  r.store->wait();
  r.col.reset();
  r.store.reset();   // closed, as Deepwater at quitting
  Store again(r.dir.filePath("technical-data.sqlite"));
  CHECK(again.cancelled("BINANCE:BTCUSDT") && again.pairPaused("BINANCE:BTCUSDT"));
}

TEST("interleaving: forex settings changed mid-turn (a later first day): the turn starts again, nothing before the new day") {
  FxRig r(PairSpec{{3600, 86400}, utc(2024, 1, 1), {}});
  auto f = r.fake;
  auto sent = std::make_shared<int>(0);
  r.col->setFetch([f, sent](const QUrl& u, QObject* ctx, Collector::Reply rep) {
    ++*sent;
    QTimer::singleShot(100, ctx, [f, u, rep] { f->serve(u, rep); });
  });
  r.col->start();
  CHECK(until([&] { return *sent >= 1; }));
  r.store->setPairSpec(r.m, specToJson(PairSpec{{3600, 86400}, utc(2024, 2, 1), {}}));
  CHECK(r.runUntilIdle());
  const SyncState h = r.store->state(r.m, 3600);
  CHECK(h.first >= utc(2024, 2, 1));
  CHECK_EQ(h.last, utc(2024, 3, 5, 23));
  CHECK(r.store->range(r.m, 3600, 0, utc(2024, 2, 1) - 1).isEmpty());
  CHECK(r.store->range(r.m, 86400, 0, utc(2024, 1, 31, 23)).isEmpty());
  CHECK(h.historyDone);
}

// ------------------------------------------------------------------------------------------------ found by review
TEST("review: started again while its cancel is still being deleted, the history comes back whole from its first day") {
  Rig r;
  const qint64 n = 1500;
  r.fake->nowMs = (T0 + n * TF + 20) * 1000;
  r.col->start();
  CHECK(r.runUntilIdle());
  CHECK_EQ(r.store->state("BINANCE:BTCUSDT", TF).count, n);
  r.store->cancelPair("BINANCE:BTCUSDT");
  CHECK(r.store->busy("BINANCE:BTCUSDT"));
  r.store->setPaused("BINANCE:BTCUSDT", false);   // START AGAIN at once, the delete not written yet
  QEventLoop loop;
  QTimer::singleShot(5000, &loop, &QEventLoop::quit);
  QObject::connect(r.col.get(), &Collector::idle, &loop, [&] { if (!r.store->busy("BINANCE:BTCUSDT") && r.store->state("BINANCE:BTCUSDT", TF).historyDone) loop.quit(); });
  loop.exec();
  r.runUntilIdle(3000);
  const SyncState s = r.store->state("BINANCE:BTCUSDT", TF);
  CHECK_EQ(s.first, T0);
  CHECK_EQ(s.count, n);
}

TEST("review: a forex first day moved earlier mid-turn: the hours from the new day are all fetched") {
  FxRig r(PairSpec{{3600, 86400}, utc(2024, 2, 1), {}});
  auto f = r.fake;
  auto sent = std::make_shared<int>(0);
  r.col->setFetch([f, sent](const QUrl& u, QObject* ctx, Collector::Reply rep) {
    ++*sent;
    QTimer::singleShot(80, ctx, [f, u, rep] { f->serve(u, rep); });
  });
  r.col->start();
  CHECK(until([&] { return r.store->state(r.m, 3600).count > 0 && *sent >= 2; }));
  r.store->setPairSpec(r.m, specToJson(PairSpec{{3600, 86400}, utc(2024, 1, 1), {}}));
  CHECK(r.runUntilIdle());
  r.runUntilIdle(1500);
  const SyncState h = r.store->state(r.m, 3600);
  CHECK_EQ(h.first, utc(2024, 1, 1));
  CHECK_EQ(h.last, utc(2024, 3, 5, 23));
  CHECK_EQ(r.store->range(r.m, 86400, 0, LLONG_MAX).size(), 46);
}

TEST("review: an unreadable forex file ends the turn as failed (tried again later), never in a loop") {
  FxRig r(PairSpec{{300, 3600}, utc(2024, 3, 1), {}});
  r.col->setRetryDelays({5}, 60 * 60 * 1000);
  auto f = r.fake;
  r.col->setFetch([f](const QUrl& u, QObject*, Collector::Reply rep) {
    ++f->requests;
    if (u.path().endsWith("2024/02/05/BID_candles_min_1.bi5")) { QTimer::singleShot(0, [rep] { rep(200, "not lzma at all", {}); }); return; }
    f->serve(u, rep);
    --f->requests;   // serve counted it
  });
  r.col->start();
  CHECK(until([&] { return !r.col->busy(); }, 5000));
  CHECK(r.fake->requests <= 12);
  bool error = false;
  for (const auto& p : r.col->progress()) error |= !p.error.isEmpty();
  CHECK(error);
  if (!error) for (const auto& p : r.col->progress()) std::printf("    %s %s err=%s running=%d\n", qPrintable(p.market), qPrintable(p.tf), qPrintable(p.error), p.running);
  std::printf("    requests %d, status %s\n", r.fake->requests, qPrintable(r.col->status()));
}

TEST("review: a hole longer than a page is asked for to its end; a hole whose asking failed is not marked checked") {
  Rig r;
  const qint64 n = 1200;
  r.fake->nowMs = (T0 + n * TF + 20) * 1000;
  r.fake->flaky = {T0 + 300 * TF, T0 + 549 * TF};   // 250 bars missing on the first pass, pages of 100
  r.col->start();
  CHECK(r.runUntilIdle());
  CHECK_EQ(r.store->state("BINANCE:BTCUSDT", TF).count, n);
  CHECK(r.store->gaps("BINANCE:BTCUSDT", TF).isEmpty());
  // a store whose hole could not be asked for: both hosts down
  Rig q;
  q.fake->nowMs = (T0 + 600 * TF + 20) * 1000;
  q.fake->outage = {T0 + 200 * TF, T0 + 209 * TF};
  q.col->start();
  CHECK(until([&] { return q.store->state("BINANCE:BTCUSDT", TF).count >= 590; }));
  q.fake->mainDown = true;
  q.col->setFetch([](const QUrl&, QObject*, Collector::Reply rep) { QTimer::singleShot(0, [rep] { rep(0, {}, "network down"); }); });
  q.runUntilIdle(5000);
  const auto gaps = q.store->gaps("BINANCE:BTCUSDT", TF);
  CHECK_EQ(gaps.size(), 1);
  if (!gaps.isEmpty()) CHECK(!gaps[0].checked);   // asked again next run
}

TEST("review: indicators computed while a later first day trims the series leave no row before its first bar") {
  const QVector<Kline> bars = minuteFixture();
  QTemporaryDir dir;
  Store st(dir.filePath("m.sqlite"));
  st.add("BINANCE:BTCUSDT", 60, bars);
  st.wait();
  Indicators ind(&st);
  ind.setTimeframes({60});
  ind.setPiece(400);
  ind.start();
  CHECK(until([&] { return st.indState("BINANCE:BTCUSDT", 60).count > 0; }, 20000));
  PairSpec later = defaultSpec();
  later.start = bars[2500].t;
  st.setPairSpec("BINANCE:BTCUSDT", specToJson(later));
  st.wait();
  CHECK(until([&] { return !ind.busy() && st.indState("BINANCE:BTCUSDT", 60).version == versionOf(later) &&
                          st.indState("BINANCE:BTCUSDT", 60).count == st.state("BINANCE:BTCUSDT", 60).count; }, 30000));
  st.wait();
  const auto rows = st.indicators("BINANCE:BTCUSDT", 60, 0, bars[2500].t - 1);
  CHECK(rows.isEmpty());
  const IndState is = st.indState("BINANCE:BTCUSDT", 60);
  CHECK_EQ(is.count, st.state("BINANCE:BTCUSDT", 60).count);
  CHECK_EQ(is.version, versionOf(later));
}
