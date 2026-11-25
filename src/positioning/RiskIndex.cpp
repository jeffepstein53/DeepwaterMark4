#include "positioning/RiskIndex.h"
#include "market/Candles.h"
#include "ui/Format.h"
#include <QCoreApplication>
#include <QTimeZone>
#include <algorithm>

namespace positioning {
namespace {
struct Def { const char* id; const char* name; const char* desc; };
const Def kDefs[7] = {
  {"vix", QT_TRANSLATE_NOOP("positioning", "Volatility (VIX)"), QT_TRANSLATE_NOOP("positioning", "VIX level – lower volatility = more risk appetite")},
  {"trend", QT_TRANSLATE_NOOP("positioning", "Equity trend"), QT_TRANSLATE_NOOP("positioning", "S&P 500 vs its 125-day average")},
  {"credit", QT_TRANSLATE_NOOP("positioning", "Credit appetite"), QT_TRANSLATE_NOOP("positioning", "High-yield vs investment-grade bonds (HYG/LQD), 20-day change")},
  {"audjpy", "AUD/JPY", QT_TRANSLATE_NOOP("positioning", "Risk-sensitive AUD against the safe-haven JPY, 20-day change")},
  {"cugold", QT_TRANSLATE_NOOP("positioning", "Copper / Gold"), QT_TRANSLATE_NOOP("positioning", "Copper (growth) against gold (safety), 20-day change of the ratio")},
  {"stocks", QT_TRANSLATE_NOOP("positioning", "Stocks vs safe haven"), QT_TRANSLATE_NOOP("positioning", "S&P 500 20-day return minus gold's")},
  {"crypto", QT_TRANSLATE_NOOP("positioning", "Crypto momentum"), QT_TRANSLATE_NOOP("positioning", "Bitcoin 20-day change")},
};
constexpr int kLookback = 252;

// Value of series s at the last date <= d (and not older than maxAgeDays), NAN otherwise.
double at(const QVector<QDate>& d, const QVector<double>& v, const QDate& day, int maxAgeDays = 7) {
  auto it = std::upper_bound(d.begin(), d.end(), day);
  if (it == d.begin()) return NAN;
  const int i = int(it - d.begin()) - 1;
  if (d[i].daysTo(day) > maxAgeDays) return NAN;
  return v[i];
}

// Percentile of r[i] within r[i-252 .. i-1] (ties count half), for every i with a full window.
QVector<double> percentiles(const QVector<double>& r) {
  QVector<double> out(r.size(), NAN);
  for (int i = kLookback; i < r.size(); ++i) {
    if (!std::isfinite(r[i])) continue;
    int below = 0, equal = 0, n = 0;
    for (int k = i - kLookback; k < i; ++k) {
      if (!std::isfinite(r[k])) continue;
      ++n;
      if (r[k] < r[i]) ++below; else if (r[k] == r[i]) ++equal;
    }
    if (n >= kLookback / 2) out[i] = (below + 0.5 * equal) / n * 100.0;
  }
  return out;
}
QVector<double> change(const QVector<double>& c, int n) {
  QVector<double> out(c.size(), NAN);
  for (int i = n; i < c.size(); ++i) if (c[i - n] > 0) out[i] = (c[i] / c[i - n] - 1.0) * 100.0;
  return out;
}
}

RiskIndex& RiskIndex::instance() { static RiskIndex* r = new RiskIndex; return *r; }

QStringList RiskIndex::inputs() { return {"^VIX", "^GSPC", "HYG", "LQD", "AUDJPY=X", "HG=F", "GC=F", "BTC-USD"}; }

int RiskIndex::zone(double v) {
  if (!std::isfinite(v)) return 2;
  if (v < 20) return 0;
  if (v < 40) return 1;
  if (v < 60) return 2;
  if (v < 80) return 3;
  return 4;
}
QString RiskIndex::zoneLabel(double v) {
  static const char* l[5] = {QT_TRANSLATE_NOOP("positioning", "EXTREME OFF"), QT_TRANSLATE_NOOP("positioning", "RISK-OFF"),
                             QT_TRANSLATE_NOOP("positioning", "NEUTRAL"), QT_TRANSLATE_NOOP("positioning", "RISK-ON"),
                             QT_TRANSLATE_NOOP("positioning", "EXTREME ON")};
  return QCoreApplication::translate("positioning", l[zone(v)]);   // translated at call time, never cached
}

void RiskIndex::load(int ttlSec) {
  if (m_left > 0) return;
  const QStringList syms = inputs();
  m_left = syms.size();
  for (const auto& s : syms) {
    // priority: eight small series for the gauge at the top of the Overview and Risk Sentiment pages, which waited
    // ~13 s behind Currency Strength's 28 pairs on a first launch (Yahoo is asked one request per 350 ms)
    market::candles(s, "1d", "5y", this, [this, s](const market::Series& ser) {
      Series out;
      if (ser.ok()) {
        for (const auto& b : ser.bars) {
          const QDate d = market::sessionDay(ser, b.t);   // exchange-local session day
          if (!out.d.isEmpty() && out.d.last() == d) { out.c.last() = b.c; continue; }
          if (!out.d.isEmpty() && d < out.d.last()) continue;
          out.d << d; out.c << b.c;
        }
      }
      m_in.insert(s, out);
      if (--m_left == 0) { m_fetched = QDateTime::currentDateTime(); compute(); emit updated(); }
    }, ttlSec, true);
  }
}

void RiskIndex::compute() {
  RiskData d;
  d.updated = m_fetched;
  const Series spx = m_in.value("^GSPC"), vix = m_in.value("^VIX"), hyg = m_in.value("HYG"), lqd = m_in.value("LQD"),
               aj = m_in.value("AUDJPY=X"), hg = m_in.value("HG=F"), gc = m_in.value("GC=F"), btc = m_in.value("BTC-USD");

  // reading series of each component on its own date grid
  struct R { QVector<QDate> d; QVector<double> r; QVector<double> raw; };
  R reads[7];
  {  // vix: inverted level
    for (int i = 0; i < vix.c.size(); ++i) { reads[0].d << vix.d[i]; reads[0].r << -vix.c[i]; reads[0].raw << vix.c[i]; }
  }
  {  // trend: S&P vs 125-day average
    double sum = 0;
    for (int i = 0; i < spx.c.size(); ++i) {
      sum += spx.c[i];
      if (i >= 125) sum -= spx.c[i - 125];
      const double v = i >= 124 ? (spx.c[i] / (sum / 125.0) - 1.0) * 100.0 : NAN;
      reads[1].d << spx.d[i]; reads[1].r << v; reads[1].raw << v;
    }
  }
  auto ratio20 = [](const Series& a, const Series& b, R& out) {
    QVector<double> rt;
    for (int i = 0; i < a.c.size(); ++i) {
      const double bv = at(b.d, b.c, a.d[i], 4);
      if (!std::isfinite(bv) || bv <= 0) continue;
      out.d << a.d[i]; rt << a.c[i] / bv;
    }
    out.r = change(rt, 20); out.raw = out.r;
  };
  ratio20(hyg, lqd, reads[2]);
  { reads[3].d = aj.d; reads[3].r = change(aj.c, 20); reads[3].raw = reads[3].r; }
  ratio20(hg, gc, reads[4]);
  {  // stocks vs gold: S&P 20d return minus gold 20d return, on S&P dates
    QVector<double> g;
    for (const auto& day : spx.d) g << at(gc.d, gc.c, day, 4);
    const auto rs = change(spx.c, 20), rg = change(g, 20);
    reads[5].d = spx.d;
    for (int i = 0; i < rs.size(); ++i) reads[5].r << (std::isfinite(rs[i]) && std::isfinite(rg[i]) ? rs[i] - rg[i] : NAN);
    reads[5].raw = reads[5].r;
  }
  { reads[6].d = btc.d; reads[6].r = change(btc.c, 20); reads[6].raw = reads[6].r; }

  QVector<double> pct[7];
  for (int k = 0; k < 7; ++k) {
    pct[k] = percentiles(reads[k].r);
    RiskComponent c; c.id = kDefs[k].id; c.name = kDefs[k].name; c.description = kDefs[k].desc;
    // latest finite reading
    for (int i = reads[k].r.size() - 1; i >= 0; --i) {
      if (!std::isfinite(pct[k][i])) continue;
      if (reads[k].d[i].daysTo(QDate::currentDate()) > 10) break;   // too old: treat as unavailable
      c.raw = reads[k].raw[i]; c.percentile = pct[k][i];
      break;
    }
    if (std::isfinite(c.raw)) {
      if (k == 0) c.rawText = fmt::num(c.raw, 1);
      else if (k == 5) c.rawText = fmt::signedNum(c.raw, 2) + " pts";
      else c.rawText = fmt::pct(c.raw, 2);
    } else c.rawText = fmt::dash();
    d.components << c;
  }

  // composite on the S&P 500's trading days, last year (+ 21 days for the 1M comparison)
  const int n = spx.d.size();
  const int from = std::max(0, n - kLookback - 22);
  QVector<QDate> days; QVector<double> comp;
  for (int i = from; i < n; ++i) {
    double s = 0; int m = 0;
    for (int k = 0; k < 7; ++k) {
      const double v = at(reads[k].d, pct[k], spx.d[i], 7);
      if (std::isfinite(v)) { s += v; ++m; }
    }
    days << spx.d[i]; comp << (m > 0 ? s / m : NAN);
  }
  // today's composite uses the components' latest readings (FX and crypto may be newer than the S&P close)
  {
    double s = 0; int m = 0;
    for (const auto& c : d.components) if (c.ok()) { s += c.percentile; ++m; }
    d.available = m;
    if (m > 0) {
      d.value = s / m;
      if (!comp.isEmpty()) comp.last() = d.value;
    }
  }
  const int keep = std::min<int>(kLookback, days.size());
  d.dates = days.mid(days.size() - keep); d.composite = comp.mid(comp.size() - keep);
  if (comp.size() > 5) d.weekAgo = comp[comp.size() - 1 - 5];
  if (comp.size() > 21) d.monthAgo = comp[comp.size() - 1 - 21];

  const QDate yearAgo = QDate::currentDate().addYears(-1);
  for (int i = 0; i < spx.d.size(); ++i) if (spx.d[i] >= yearAgo) { d.spxDates << spx.d[i]; d.spx << spx.c[i]; }
  for (int i = 0; i < vix.d.size(); ++i) if (vix.d[i] >= yearAgo) { d.vixDates << vix.d[i]; d.vix << vix.c[i]; }
  if (!d.ok()) d.error = QT_TRANSLATE_NOOP("positioning", "Yahoo Finance daily closes unavailable");   // translated where shown
  m_data = d;
}
}
