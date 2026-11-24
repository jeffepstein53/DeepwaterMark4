// What Data › Technical data records for one pair. See TechnicalDataSpec.h.
#include "technicaldata/TechnicalDataSpec.h"
#include "charts/core/Indicators.h"
#include "technicaldata/Collector.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QHash>
#include <QJsonDocument>
#include <QMutex>
#include <QRegularExpression>
#include <cmath>

namespace technicaldata {

namespace {
QJsonObject p(std::initializer_list<QPair<QString, QJsonValue>> kv) {
  QJsonObject o;
  for (const auto& x : kv) o.insert(x.first, x.second);
  return o;
}
// the 23 columns BTCUSDT had before pairs could be chosen, as indicator settings (indicatorColumns() keeps the columns)
QVector<IndSpec> defaultIndicators() {
  QVector<IndSpec> v;
  for (int n : {20, 50, 200}) v << IndSpec{"sma", p({{"length", n}})};
  for (int n : {20, 50, 200}) v << IndSpec{"ema", p({{"length", n}})};
  v << IndSpec{"bb", p({{"length", 20}, {"mult", 2}})} << IndSpec{"bbpb", p({{"length", 20}, {"mult", 2}})}
    << IndSpec{"bbw", p({{"length", 20}, {"mult", 2}})} << IndSpec{"rsi", p({{"length", 14}})}
    << IndSpec{"macd", p({{"fast", 12}, {"slow", 26}, {"signal", 9}})} << IndSpec{"atr", p({{"length", 14}})}
    << IndSpec{"dmi", p({{"diLength", 14}, {"adxSmoothing", 14}})} << IndSpec{"stoch", p({{"k", 14}, {"smoothK", 1}, {"d", 3}})};
  IndSpec obv{"obv", {}};
  obv.running = true;
  v << obv << IndSpec{"vwap", p({{"source", "hlc3"}, {"anchor", "day"}})};
  return v;
}

bool numeric(const QString& type) { return type == "int" || type == "float"; }
QString numText(double v) {
  QString t = QString::number(v, 'g', 10);
  return t.replace('.', 'p').replace('-', 'm');
}
QString clean(QString s) {
  static const QRegularExpression bad("[^a-z0-9_]");
  s = s.toLower().replace(bad, QString());
  return s;
}
bool sameValue(const QJsonValue& a, const QJsonValue& b) {
  if (a.isDouble() || b.isDouble()) return a.toDouble(NAN) == b.toDouble(NAN) || (a.isString() && a.toString().toDouble() == b.toDouble());
  return a == b;
}
bool sameParams(const QString& id, const QJsonObject& a, const QJsonObject& b) {
  const QJsonObject x = fullParams(id, a), y = fullParams(id, b);
  if (x.keys() != y.keys()) return false;
  for (const QString& k : x.keys()) if (!sameValue(x.value(k), y.value(k))) return false;
  return true;
}
}  // namespace

PairSpec defaultSpec() {
  PairSpec s;
  for (const auto& t : timeframes()) s.timeframes << t.seconds;
  s.indicators = defaultIndicators();
  return s;
}

PairSpec defaultForexSpec() {
  PairSpec s;
  s.timeframes = {300, 900, 3600, 14400, 86400, 604800};
  s.indicators = defaultIndicators();
  return s;
}

PairSpec withForexHours(PairSpec s) {
  bool derived = false;
  for (int tf : s.timeframes) derived |= tf > 3600;
  if (derived && !s.timeframes.contains(3600)) {
    QVector<int> out;
    for (const auto& t : timeframes()) if (s.timeframes.contains(t.seconds) || t.seconds == 3600) out << t.seconds;
    s.timeframes = out;
  }
  return s;
}

QJsonObject indToJson(const IndSpec& i) {
  QJsonObject o{{"id", i.id}, {"params", i.params}};
  if (i.running) o.insert("running", true);
  if (!i.tfs.isEmpty()) {
    QJsonArray a;
    for (int t : i.tfs) a << t;
    o.insert("tfs", a);
  }
  if (!i.outputs.isEmpty()) o.insert("outputs", QJsonArray::fromStringList(i.outputs));
  return o;
}
IndSpec indFromJson(const QJsonObject& o) {
  IndSpec i;
  i.id = o.value("id").toString();
  i.params = o.value("params").toObject();
  i.running = o.value("running").toBool();
  for (const QJsonValue& t : o.value("tfs").toArray()) i.tfs << t.toInt();
  for (const QJsonValue& t : o.value("outputs").toArray()) i.outputs << t.toString();
  return i;
}

PairSpec specFromJson(const QJsonObject& o) {
  PairSpec s;
  QVector<int> want;
  for (const QJsonValue& t : o.value("timeframes").toArray()) want << t.toInt();
  for (const auto& t : timeframes()) if (want.contains(t.seconds)) s.timeframes << t.seconds;
  s.start = qint64(o.value("start").toDouble(0));
  s.paused = o.value("paused").toBool();
  const QJsonObject fp = o.value("footprint").toObject();
  for (const char* v : {"spot", "perp"})   // in this order, known ones only
    if (fp.value("venues").toArray().contains(QJsonValue(QString::fromLatin1(v)))) s.footprint << QString::fromLatin1(v);
  s.footprintStart = qint64(fp.value("start").toDouble(0));
  const QJsonObject perp = o.value("perp").toObject();
  s.depth = perp.value("depth").toBool();
  s.openInterest = perp.value("oi").toBool();
  s.liveBook = o.value("liveBook").toBool();
  for (const QJsonValue& v : o.value("indicators").toArray()) {
    IndSpec i = indFromJson(v.toObject());
    if (!i.id.isEmpty()) s.indicators << i;
  }
  return s;
}

QJsonObject specToJson(const PairSpec& s) {
  QJsonArray tfs, ind;
  for (int t : s.timeframes) tfs << t;
  for (const IndSpec& i : s.indicators) ind << indToJson(i);
  QJsonObject o{{"timeframes", tfs}, {"start", double(s.start)}, {"indicators", ind}};
  if (s.paused) o.insert("paused", true);
  if (!s.footprint.isEmpty())
    o.insert("footprint", QJsonObject{{"venues", QJsonArray::fromStringList(s.footprint)}, {"start", double(s.footprintStart)}});
  if (s.depth || s.openInterest) {
    QJsonObject perp;
    if (s.depth) perp.insert("depth", true);
    if (s.openInterest) perp.insert("oi", true);
    o.insert("perp", perp);
  }
  if (s.liveBook) o.insert("liveBook", true);
  return o;
}

QJsonObject fullParams(const QString& id, const QJsonObject& params) {
  QJsonObject out = charts::ind::defaults(id);
  for (auto it = params.begin(); it != params.end(); ++it) out.insert(it.key(), it.value());
  return out;
}

QVector<IndColumn> columnsOf(const IndSpec& i) {
  QVector<IndColumn> out;
  // the columns BTCUSDT had: their names and settings as they were
  for (const IndColumn& c : indicatorColumns())
    if (c.id == i.id && sameParams(i.id, c.params, i.params) && (i.outputs.isEmpty() || i.outputs.contains(c.output))) {
      IndColumn x = c;
      x.running = i.running || c.id == "obv";
      x.tfs = i.tfs;
      out << x;
    }
  if (!out.isEmpty()) return out;
  const charts::ind::DefPtr d = charts::ind::byId(i.id);
  if (!d) return out;
  // the name: the indicator, its first number, its other settings when not the default (rsi7, ema100_hl2, bb20_mult2p5)
  const QJsonObject full = fullParams(i.id, i.params);
  QString base = clean(i.id);
  bool first = true;
  for (const auto& in : d->inputs) {
    if (in.type == "time") continue;
    const QJsonValue v = full.value(in.key);
    const double x = v.isString() ? v.toString().toDouble() : v.toDouble();
    if (numeric(in.type) && first) { base += numText(x); first = false; continue; }
    if (sameValue(v, in.def)) continue;
    base += "_" + (numeric(in.type) ? clean(in.key) + numText(x) : clean(v.toVariant().toString()));
  }
  for (const auto& o : d->outputs) {
    if (!i.outputs.isEmpty() && !i.outputs.contains(o.key)) continue;
    IndColumn c;
    c.name = d->outputs.size() > 1 && o.key != "value" ? base + "_" + clean(o.key) : base;
    c.id = i.id;
    c.params = i.params;
    c.output = o.key;
    c.running = i.running;
    c.tfs = i.tfs;
    out << c;
  }
  return out;
}

QVector<IndColumn> columnsOf(const PairSpec& s) {
  QVector<IndColumn> out;
  QStringList seen;
  for (const IndSpec& i : s.indicators)
    for (const IndColumn& c : columnsOf(i))
      if (!seen.contains(c.name)) { seen << c.name; out << c; }
  return out;
}

QStringList columnNames(const PairSpec& s) {
  QStringList n;
  for (const IndColumn& c : columnsOf(s)) n << c.name;
  return n;
}

int warmupOf(const PairSpec& s) {
  double longest = 0;
  for (const IndSpec& i : s.indicators) {
    const charts::ind::DefPtr d = charts::ind::byId(i.id);
    if (!d) continue;
    const QJsonObject full = fullParams(i.id, i.params);
    for (const auto& in : d->inputs)
      if (in.type == "int") longest = std::max(longest, full.value(in.key).toDouble(0));
  }
  return int(std::clamp(3 * longest, double(WARMUP), double(OVERLAP)));
}

QString versionOf(const PairSpec& s) {
  // the same text as indicatorVersion() for BTCUSDT's columns from its first day: its rows stay as they are
  QJsonArray a;
  for (const IndColumn& c : columnsOf(s)) {
    QJsonObject o{{"name", c.name}, {"id", c.id}, {"params", c.params}, {"output", c.output}, {"intraday", c.intradayOnly}};
    if (!c.tfs.isEmpty()) {
      QJsonArray t;
      for (int x : c.tfs) t << x;
      o.insert("tfs", t);
    }
    a << o;
  }
  a << QJsonObject{{"engine", "charts::ind 2026-11-18 stdev two-pass"}, {"overlap", OVERLAP}, {"warmup", warmupOf(s)}};
  if (s.start) a << QJsonObject{{"start", double(s.start)}};
  return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(a).toJson(QJsonDocument::Compact), QCryptographicHash::Sha1).toHex().left(12));
}

QString describe(const IndSpec& i) {
  const charts::ind::DefPtr d = charts::ind::byId(i.id);
  if (!d) return i.id;
  QStringList parts{QCoreApplication::translate("charts", qPrintable(d->name))};
  const QJsonObject full = fullParams(i.id, i.params);
  for (const auto& in : d->inputs) {
    if (in.type == "time") continue;
    const QJsonValue v = full.value(in.key);
    const QString val = numeric(in.type) ? QString::number(v.isString() ? v.toString().toDouble() : v.toDouble(), 'g', 10)
                                         : QCoreApplication::translate("charts", qPrintable(v.toVariant().toString()));
    parts << QCoreApplication::translate("charts", qPrintable(in.label)).toLower() + " " + val;
  }
  return parts.join(" · ");
}

// ------------------------------------------------------------------------------------------------ check
bool storable(const QString& id) {
  const charts::ind::DefPtr d = charts::ind::byId(id);
  if (!d || d->custom || d->outputs.isEmpty() || !d->calc) return false;
  for (const auto& in : d->inputs) if (in.type == "time") return false;
  return true;
}

namespace {
// a random walk of n bars of tf seconds from 2020-01-01 UTC (Monday 2019-12-30 for weeks), the same every time
charts::Bars synthetic(int tf, int n) {
  charts::Bars b;
  b.reserve(n);
  quint64 s = 0x9E3779B97F4A7C15ull ^ quint64(tf);
  auto u = [&s] { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return double(s >> 11) / double(1ull << 53); };
  const qint64 t0 = tf >= 604800 ? 1577664000 : 1577836800;
  double close = 30000;
  const double step = 0.004 * std::sqrt(std::max(1.0, tf / 60.0));
  for (int i = 0; i < n; ++i) {
    const double open = close;
    close = std::max(1.0, open * std::exp((u() - 0.5) * 2 * std::min(0.08, step)));
    const double hi = std::max(open, close) * (1 + u() * std::min(0.03, step)), lo = std::min(open, close) * (1 - u() * std::min(0.03, step));
    b.push_back(charts::Bar{t0 + qint64(i) * tf, open, hi, lo, close, 10 + 90 * u()});
  }
  return b;
}
bool same(double a, double b) {
  if (std::isnan(a) || std::isnan(b)) return std::isnan(a) && std::isnan(b);
  if (std::isinf(a) || std::isinf(b)) return a == b;
  return std::fabs(a - b) <= 1e-9 * std::max(1.0, std::fabs(b));
}
QStringList outputKeys(const QString& id) {
  QStringList k;
  if (const charts::ind::DefPtr d = charts::ind::byId(id)) for (const auto& o : d->outputs) k << o.key;
  return k;
}
QString key(const QString& id, const QJsonObject& params, const QVector<int>& tfs) {
  QString t;
  for (int x : tfs) t += QString::number(x) + ",";
  return id + QString::fromUtf8(QJsonDocument(fullParams(id, params)).toJson(QJsonDocument::Compact)) + t;
}
}  // namespace

IndCheck checkIndicator(const QString& id, const QJsonObject& params, const QVector<int>& timeframesAsked) {
  static QMutex lock;
  static QHash<QString, IndCheck> cache;
  const QString k = key(id, params, timeframesAsked);
  {
    QMutexLocker l(&lock);
    const auto it = cache.constFind(k);
    if (it != cache.cend()) return *it;
  }
  IndCheck out;
  const auto finish = [&] { QMutexLocker l(&lock); cache.insert(k, out); return out; };
  if (!storable(id)) {
    out.reason = QCoreApplication::translate("data", "Not offered: only Deepwater's built-in indicators that draw lines and need no anchor time can be stored.");
    return finish();
  }
  const charts::ind::DefPtr def = charts::ind::byId(id);
  const auto label = [def](const QString& o) {
    for (const auto& x : def->outputs) if (x.key == o) return QCoreApplication::translate("charts", qPrintable(x.label));
    return o;
  };
  const QJsonObject full = fullParams(id, params);
  try {
    // 1. never looks ahead: on fewer bars, the earlier values stay the same (output by output)
    const charts::Bars hours = synthetic(3600, 3000);
    const charts::ind::Result whole = charts::ind::compute(id, hours, full);
    QStringList outs;
    for (const QString& o : outputKeys(id)) {
      bool any = false;
      for (double v : whole.value(o)) if (std::isfinite(v)) { any = true; break; }
      if (any) outs << o;
    }
    if (outs.isEmpty()) { out.reason = QCoreApplication::translate("data", "It gives no value on test bars with these settings."); return finish(); }
    for (int cut : {2999, 2997, 2990, 2960, 2700}) {
      const charts::ind::Result part = charts::ind::compute(id, hours.mid(0, cut), full);
      for (const QString& o : outs) {
        if (out.ahead.contains(o)) continue;
        const charts::Series a = whole.value(o), b = part.value(o);
        for (int i = 0; i < cut; ++i)
          if (!same(i < a.size() ? a[i] : NAN, i < b.size() ? b[i] : NAN)) { out.ahead << o; break; }
      }
    }
    for (const QString& o : out.ahead) outs.removeAll(o);
    QStringList aheadLabels;
    for (const QString& o : out.ahead) aheadLabels << label(o);
    if (outs.isEmpty()) {
      out.reason = QCoreApplication::translate("data", "It looks ahead: its values change once later bars come (it uses bars after the one it is drawn on), "
                      "so a stored history would hold what was not known at the time.");
      return finish();
    }
    // 2. computed from OVERLAP bars before a piece it gives the whole series' values (or them plus a constant)
    QStringList left;
    for (int tf : timeframesAsked) {
      const int n = OVERLAP + 2500, from = n - 2000, cut = from - OVERLAP;
      const charts::Bars bars = synthetic(tf, n);
      const charts::ind::Result w = charts::ind::compute(id, bars, full);
      const charts::ind::Result s = charts::ind::compute(id, bars.mid(cut), full);
      bool equal = true, shifted = true;
      for (const QString& o : outs) {
        const charts::Series a = w.value(o), b = s.value(o);
        double shift = NAN;
        for (int i = from; i < n; ++i) {
          const double x = i < a.size() ? a[i] : NAN, y = i - cut < b.size() ? b[i - cut] : NAN;
          if (!same(x, y)) equal = false;
          if (std::isnan(x) != std::isnan(y)) { shifted = false; continue; }
          if (std::isnan(x)) continue;
          if (std::isnan(shift)) shift = x - y;
          else if (std::fabs((x - y) - shift) > 1e-9 * std::max(1.0, std::fabs(x))) shifted = false;
        }
      }
      if (equal) out.tfs << tf;
      else if (shifted) { out.tfs << tf; out.running = true; }
      else for (const auto& t : timeframes()) if (t.seconds == tf) left << t.name;
    }
    if (out.tfs.isEmpty()) {
      out.reason = QCoreApplication::translate("data", "It depends on more than the %1 bars Deepwater computes again before new ones (a very long window, or "
                      "the whole history), so stored values would differ from those computed over the whole history.").arg(OVERLAP);
      return finish();
    }
    out.ok = true;
    out.outputs = outs;
    QStringList notes;
    if (!aheadLabels.isEmpty())
      notes << QCoreApplication::translate("data", "%1 left out: it looks ahead (its values change once later bars come).").arg(aheadLabels.join(", "));
    if (!left.isEmpty())
      notes << QCoreApplication::translate("data", "Not stored on %1: there its window is longer than the %2 bars Deepwater computes again.").arg(left.join(", ")).arg(OVERLAP);
    out.reason = notes.join(" ");
  } catch (const std::exception& e) {
    out.reason = QCoreApplication::translate("data", "It fails with these settings: %1").arg(QString::fromUtf8(e.what()));
  }
  return finish();
}

bool isOverlay(const QString& id) {
  const charts::ind::DefPtr d = charts::ind::byId(id);
  return d && d->overlay;
}

Scale scaleOf(const IndColumn& c) {
  static QMutex lock;
  static QHash<QString, Scale> cache;
  const QString k = c.id + QString::fromUtf8(QJsonDocument(fullParams(c.id, c.params)).toJson(QJsonDocument::Compact)) + "." + c.output;
  {
    QMutexLocker l(&lock);
    const auto it = cache.constFind(k);
    if (it != cache.cend()) return *it;
  }
  Scale out = Scale::Other;
  try {
    const charts::Bars base = synthetic(3600, 1500);
    charts::Bars prices = base, volumes = base;
    for (auto& b : prices) { b.open *= 10; b.high *= 10; b.low *= 10; b.close *= 10; }
    for (auto& b : volumes) b.volume *= 10;
    const QJsonObject full = fullParams(c.id, c.params);
    const charts::Series a = charts::ind::compute(c.id, base, full).value(c.output);
    const charts::Series p = charts::ind::compute(c.id, prices, full).value(c.output);
    const charts::Series v = charts::ind::compute(c.id, volumes, full).value(c.output);
    bool free = true, price = true, any = false;
    for (int i = 0; i < a.size(); ++i) {
      const double x = a[i], y = i < p.size() ? p[i] : NAN, z = i < v.size() ? v[i] : NAN;
      if (!std::isfinite(x)) continue;
      any = true;
      if (!same(x, z)) { free = false; price = false; break; }   // follows the volume
      if (!same(x, y)) free = false;
      if (!same(10 * x, y)) price = false;
    }
    if (any) out = free ? Scale::Free : price ? Scale::Price : Scale::Other;
  } catch (const std::exception&) {}
  QMutexLocker l(&lock);
  cache.insert(k, out);
  return out;
}

qint64 estimateBars(int tfSeconds, qint64 from, qint64 to) {
  return tfSeconds > 0 && to > from ? (to - from) / tfSeconds : 0;
}
qint64 estimateBytes(qint64 bars, int columns) {
  return bars * (95 + (columns > 0 ? 26 + 10 * qint64(columns) : 0));
}

}  // namespace technicaldata
