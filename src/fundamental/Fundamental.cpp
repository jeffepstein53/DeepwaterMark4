#include "fundamental/Fundamental.h"
#include <QFile>
#include <QCoreApplication>
#include "core/I18n.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <numeric>

namespace fundamental {

// ---------------------------------------------------------------- metadata
const QList<CurrencyMeta>& currencies() {
  static const QList<CurrencyMeta> m{
    {"USD", QT_TRANSLATE_NOOP("seasonal", "UNITED STATES"), "FED", QT_TRANSLATE_NOOP("seasonal", "FEDERAL RESERVE"), 2.0, 2.0, 2.0, "2%"},
    {"EUR", QT_TRANSLATE_NOOP("seasonal", "EURO AREA"), "ECB", QT_TRANSLATE_NOOP("seasonal", "EUROPEAN CENTRAL BANK"), 2.0, 2.0, 2.0, "2%"},
    {"GBP", QT_TRANSLATE_NOOP("seasonal", "UNITED KINGDOM"), "BOE", QT_TRANSLATE_NOOP("seasonal", "BANK OF ENGLAND"), 2.0, 2.0, 2.0, "2%"},
    {"JPY", QT_TRANSLATE_NOOP("seasonal", "JAPAN"), "BOJ", QT_TRANSLATE_NOOP("seasonal", "BANK OF JAPAN"), 2.0, 2.0, 2.0, "2%"},
    {"CHF", QT_TRANSLATE_NOOP("seasonal", "SWITZERLAND"), "SNB", QT_TRANSLATE_NOOP("seasonal", "SWISS NATIONAL BANK"), 1.0, 0.0, 2.0, QString::fromUtf8("0–2%")},
    {"CAD", QT_TRANSLATE_NOOP("seasonal", "CANADA"), "BOC", QT_TRANSLATE_NOOP("seasonal", "BANK OF CANADA"), 2.0, 1.0, 3.0, "2%"},
    {"AUD", QT_TRANSLATE_NOOP("seasonal", "AUSTRALIA"), "RBA", "RBA", 2.5, 2.0, 3.0, QString::fromUtf8("2–3%")},
    {"NZD", QT_TRANSLATE_NOOP("seasonal", "NEW ZEALAND"), "RBNZ", "RBNZ", 2.0, 1.0, 3.0, "2%"},
  };
  return m;
}
const CurrencyMeta* meta(const QString& ccy) {
  for (const auto& m : currencies()) if (m.ccy == ccy) return &m;
  return nullptr;
}

QString tw(const QString& key) { return key.isEmpty() ? key : QCoreApplication::translate("seasonal", key.toUtf8().constData()); }
QString tw(const char* key) { return QCoreApplication::translate("seasonal", key); }
QString bankShortName(const QString& id) {
  if (id == "FED") return QStringLiteral("Fed");
  if (id == "BOE") return QStringLiteral("BoE");
  if (id == "BOJ") return QStringLiteral("BoJ");
  if (id == "BOC") return QStringLiteral("BoC");
  if (id == "ECB") return QCoreApplication::translate("seasonal", "ECB");
  if (id == "SNB") return QCoreApplication::translate("seasonal", "SNB");
  return id;
}
QString activityLabel(const QString& name) {
  if (name == "BCI") return QCoreApplication::translate("seasonal", "BCI");
  return name;
}

QString columnName(int c) {
  static const char* n[] = {QT_TRANSLATE_NOOP("seasonal", "RATES"), QT_TRANSLATE_NOOP("seasonal", "INFLATION"), QT_TRANSLATE_NOOP("seasonal", "GROWTH"), QT_TRANSLATE_NOOP("seasonal", "LABOUR"), QT_TRANSLATE_NOOP("seasonal", "ACTIVITY"), QT_TRANSLATE_NOOP("seasonal", "SENTIMENT")};
  return (c >= 0 && c < kColumns) ? QString(n[c]) : QString();
}

Shade shadeOf(double v) {
  if (!std::isfinite(v)) return Shade::Missing;
  if (v >= 0.5) return Shade::StrongPos;
  if (v >= 0.15) return Shade::WeakPos;
  if (v > -0.15) return Shade::Neutral;
  if (v > -0.5) return Shade::WeakNeg;
  return Shade::StrongNeg;
}

QString trendArrow(double latest, double previous, double tol) {
  if (!std::isfinite(latest) || !std::isfinite(previous)) return {};
  if (latest > previous + tol) return QString::fromUtf8("↑");
  if (latest < previous - tol) return QString::fromUtf8("↓");
  return {};
}
static double trend(double latest, double previous, double tol = 0.05) {
  if (!std::isfinite(latest) || !std::isfinite(previous)) return 0;
  return latest > previous + tol ? 1 : latest < previous - tol ? -1 : 0;
}
int daysUntil(const QDate& d, const QDate& today) { return d.isValid() ? int(today.daysTo(d)) : INT_MIN; }

// z-score of the latest value against the last 5 years of history (0 without enough history)
static double zScore(const Release& r) {
  if (r.history.size() < 12 || !r.ok()) return 0;
  const QDate from = (r.period.isValid() ? r.period : r.history.last().date).addYears(-5);
  QVector<double> v;
  for (const auto& p : r.history) if (p.date >= from && std::isfinite(p.value)) v << p.value;
  if (v.size() < 12) return 0;
  const double m = std::accumulate(v.begin(), v.end(), 0.0) / v.size();
  double s = 0; for (double x : v) s += (x - m) * (x - m);
  s = std::sqrt(s / (v.size() - 1));
  return s > 1e-9 ? (r.value - m) / s : 0;
}
// typical absolute change between consecutive releases over the history (1 without history)
static double typicalChange(const Release& r) {
  if (r.history.size() < 12) return 1.0;
  QVector<double> d;
  for (int i = 1; i < r.history.size(); ++i)
    if (std::isfinite(r.history[i].value) && std::isfinite(r.history[i - 1].value))
      d << std::abs(r.history[i].value - r.history[i - 1].value);
  if (d.isEmpty()) return 1.0;
  std::sort(d.begin(), d.end());
  const double med = d[d.size() / 2];
  return med > 1e-9 ? 2.0 * med : 1.0;   // a change of twice the median move gives tanh(1) ≈ 0.76
}
QString num(double v, int d) {
  const QString s = QString::number(v, 'f', d);
  return s.startsWith('-') && s.toDouble() == 0 ? s.mid(1) : s;   // a fall too small to show is not "-0.0"
}
static QString withArrow(const QString& s, const QString& arrow) { return arrow.isEmpty() ? s : s + " " + arrow; }

Move lastMove(const Inputs& in) {
  if (!std::isfinite(in.policyRate) || !std::isfinite(in.priorRate)) return Move::Unknown;
  if (in.policyRate > in.priorRate + 0.001) return Move::Hike;
  if (in.policyRate < in.priorRate - 0.001) return Move::Cut;
  return Move::Hold;
}
QString moveWord(Move m) {
  switch (m) { case Move::Hike: return QT_TRANSLATE_NOOP("seasonal", "HIKE"); case Move::Cut: return QT_TRANSLATE_NOOP("seasonal", "CUT"); case Move::Hold: return QT_TRANSLATE_NOOP("seasonal", "HOLD"); default: return QString::fromUtf8("—"); }
}

// ---------------------------------------------------------------- signals
static Signal missing(const QString& why) { Signal s; s.word = QString::fromUtf8("—"); s.why = why; return s; }

static Signal ratesSignal(const Inputs& in) {
  if (!std::isfinite(in.policyRate)) return missing(in.policyNote.isEmpty() ? QCoreApplication::translate("seasonal", "No policy rate") : in.policyNote);
  Signal s;
  const Move m = lastMove(in);
  const double mv = m == Move::Hike ? 1 : m == Move::Cut ? -1 : 0;
  const double real = in.cpi.ok() ? in.policyRate - in.cpi.value : 0;
  s.value = 0.7 * mv + 0.3 * std::tanh(real / 2.0);
  s.word = m == Move::Unknown ? QString(QT_TRANSLATE_NOOP("seasonal", "RATE")) : moveWord(m);
  s.sub = num(in.policyRate, 2) + "%";
  if (m == Move::Hike || m == Move::Cut) {
    const int bp = qRound((in.policyRate - in.priorRate) * 100);
    s.sub += QString(" (%1%2bp)").arg(bp > 0 ? "+" : "").arg(bp);
  }
  s.why = QCoreApplication::translate("seasonal", "Last decision %1; real rate %2 pt (policy − CPI)").arg(tw(s.word).toLower(), in.cpi.ok() ? num(real, 2) : QCoreApplication::translate("seasonal", "n/a"));
  s.shade = shadeOf(s.value);
  return s;
}

static Signal inflationSignal(const Inputs& in, const CurrencyMeta& cm) {
  if (!in.cpi.ok()) return missing(in.cpi.note.isEmpty() ? QCoreApplication::translate("seasonal", "No CPI release") : in.cpi.note);
  Signal s;
  const double v = in.cpi.value;
  s.value = 0.7 * std::tanh((v - cm.target) / 1.0) + 0.3 * trend(v, in.cpi.previous);
  s.word = v > cm.targetHi + 0.5 ? QT_TRANSLATE_NOOP("seasonal", "ABOVE TARGET") : v < cm.targetLo - 0.5 ? QT_TRANSLATE_NOOP("seasonal", "BELOW TARGET") : QT_TRANSLATE_NOOP("seasonal", "NEAR TARGET");
  s.sub = withArrow(QCoreApplication::translate("seasonal", "%1% y/y").arg(num(v, 1)), trendArrow(v, in.cpi.previous));
  s.why = QCoreApplication::translate("seasonal", "Headline CPI %1% vs the %2 target").arg(num(v, 1), cm.targetLabel);
  s.shade = shadeOf(s.value);
  return s;
}

static Signal growthSignal(const Inputs& in) {
  if (!in.gdp.ok()) return missing(in.gdp.note.isEmpty() ? QCoreApplication::translate("seasonal", "No GDP release") : in.gdp.note);
  Signal s;
  const double v = in.gdp.value;
  s.value = 0.7 * std::tanh(v / 0.4) + 0.3 * trend(v, in.gdp.previous);
  s.word = v > 0.1 ? QT_TRANSLATE_NOOP("seasonal", "EXPANDING") : v < -0.1 ? QT_TRANSLATE_NOOP("seasonal", "CONTRACTING") : QT_TRANSLATE_NOOP("seasonal", "FLAT");
  s.sub = withArrow(QCoreApplication::translate("seasonal", "%1% q/q").arg(num(v, 1)), trendArrow(v, in.gdp.previous));
  s.why = QCoreApplication::translate("seasonal", "Real GDP %1% q/q").arg(num(v, 1));
  s.shade = shadeOf(s.value);
  return s;
}

static Signal labourSignal(const Inputs& in) {
  if (!in.unemployment.ok()) return missing(in.unemployment.note.isEmpty() ? QCoreApplication::translate("seasonal", "No unemployment release") : in.unemployment.note);
  Signal s;
  const double v = in.unemployment.value;
  const double d = std::isfinite(in.unemployment.previous) ? v - in.unemployment.previous : 0;
  s.value = -0.7 * std::tanh(d / 0.15) - 0.3 * std::tanh(zScore(in.unemployment));
  s.word = d <= -0.1 + 1e-9 ? QT_TRANSLATE_NOOP("seasonal", "IMPROVING") : d >= 0.1 - 1e-9 ? QT_TRANSLATE_NOOP("seasonal", "SOFTENING") : QT_TRANSLATE_NOOP("seasonal", "STABLE");
  s.sub = QCoreApplication::translate("seasonal", "%1% unemployment").arg(num(v, 1)) + (in.unemployment.annual ? QCoreApplication::translate("seasonal", " (yr)") : QString());
  s.why = QCoreApplication::translate("seasonal", "Unemployment %1% (%2 pt vs previous)").arg(num(v, 1), (d >= 0 ? "+" : "") + num(d, 1));
  s.shade = shadeOf(s.value);
  return s;
}

static Signal activitySignal(const Inputs& in) {
  if (!in.activity.ok()) return missing(in.activity.note.isEmpty() ? QCoreApplication::translate("seasonal", "No PMI / business confidence release") : in.activity.note);
  Signal s;
  const double v = in.activity.value, n = in.activityNeutral;
  const double scale = n >= 90 ? 1.0 : 3.0;
  s.value = 0.7 * std::tanh((v - n) / scale) + 0.3 * trend(v, in.activity.previous);
  s.word = v >= n ? QT_TRANSLATE_NOOP("seasonal", "EXPANSION") : QT_TRANSLATE_NOOP("seasonal", "CONTRACTION");
  s.sub = activityLabel(in.activityName) + " " + num(v, 1);
  s.why = QCoreApplication::translate("seasonal", "%1 %2 vs %3 neutral").arg(activityLabel(in.activityName), num(v, 1), num(n, 0));
  s.shade = shadeOf(s.value);
  return s;
}

static Signal sentimentSignal(const Inputs& in) {
  if (!in.confidence.ok()) return missing(in.confidence.note.isEmpty() ? QCoreApplication::translate("seasonal", "No consumer confidence release") : in.confidence.note);
  Signal s;
  const double v = in.confidence.value;
  const double d = std::isfinite(in.confidence.previous) ? v - in.confidence.previous : 0;
  const bool hist = in.confidence.history.size() >= 12;
  const double t = std::tanh(d / typicalChange(in.confidence));
  s.value = hist ? 0.6 * t + 0.4 * std::tanh(zScore(in.confidence)) : t;
  s.word = d > 1e-9 ? QT_TRANSLATE_NOOP("seasonal", "IMPROVING") : d < -1e-9 ? QT_TRANSLATE_NOOP("seasonal", "WEAKENING") : QT_TRANSLATE_NOOP("seasonal", "STEADY");
  s.sub = QCoreApplication::translate("seasonal", "confidence %1").arg(num(v, 1));
  s.why = QCoreApplication::translate("seasonal", "Consumer confidence %1 (%2 vs previous)").arg(num(v, 1), (d >= 0 ? "+" : "") + num(d, 1));
  s.shade = shadeOf(s.value);
  return s;
}

static QString biasOf(double score) {
  if (!std::isfinite(score)) return QString::fromUtf8("\u2014");
  return score >= 8 ? QT_TRANSLATE_NOOP("seasonal", "BULLISH") : score <= -8 ? QT_TRANSLATE_NOOP("seasonal", "BEARISH") : QT_TRANSLATE_NOOP("seasonal", "NEUTRAL");
}

Score scoreOne(const Inputs& in) {
  Score sc; sc.ccy = in.ccy;
  const CurrencyMeta* cm = meta(in.ccy);
  static const CurrencyMeta fallback;
  const CurrencyMeta& m = cm ? *cm : fallback;
  sc.sig[Rates] = ratesSignal(in);
  sc.sig[Inflation] = inflationSignal(in, m);
  sc.sig[Growth] = growthSignal(in);
  sc.sig[Labour] = labourSignal(in);
  sc.sig[Activity] = activitySignal(in);
  sc.sig[Sentiment] = sentimentSignal(in);
  double sum = 0;
  for (const auto& s : sc.sig) if (std::isfinite(s.value)) { sum += s.value; ++sc.available; }
  if (sc.available >= 3) {
    sc.level = 100.0 * sum / sc.available;
    sc.score = std::round(sc.level) + 0.0;   // + 0.0: no "-0"
  }
  sc.bias = biasOf(sc.score);
  return sc;
}

QVector<Score> scoreAll(const QList<Inputs>& inputs) {
  QVector<Score> out;
  for (const auto& in : inputs) out << scoreOne(in);
  double sum = 0; int n = 0;
  for (const auto& s : out) if (std::isfinite(s.level)) { sum += s.level; ++n; }
  for (auto& s : out) {
    if (!std::isfinite(s.level)) continue;
    s.score = std::round(s.level - sum / n) + 0.0;
    s.bias = biasOf(s.score);
  }
  std::stable_sort(out.begin(), out.end(), [](const Score& a, const Score& b) {
    const bool fa = std::isfinite(a.score), fb = std::isfinite(b.score);
    if (fa != fb) return fa;
    return fa && a.score > b.score;
  });
  for (int i = 0; i < out.size(); ++i) out[i].rank = i + 1;
  return out;
}

// ---------------------------------------------------------------- comparator
double realRate(const Inputs& in, bool core) {
  const Release& p = core ? in.coreCpi : in.cpi;
  return std::isfinite(in.policyRate) && p.ok() ? in.policyRate - p.value : NAN;
}

// "Oct 2026", "Q3 2026" when the figure is quarterly
static QString periodText(const Release& r) {
  if (!r.period.isValid()) return {};
  if (r.annual) return QString::number(r.period.year());
  const int n = int(r.history.size());
  if (n >= 2 && r.history[n - 2].date.daysTo(r.history[n - 1].date) > 80)
    return QCoreApplication::translate("seasonal", "Q%1 %2").arg((r.period.month() - 1) / 3 + 1).arg(r.period.year());
  return i18n::locale().toString(r.period, "MMM yyyy");
}

Comparison compare(const Inputs& a, const Inputs& b) {
  Comparison c;
  auto row = [&](const QString& label, double x, double y, bool higherBetter, const QString& unit, int dec) {
    CompareRow r{label, x, y, 0, unit, dec};
    if (std::isfinite(x) && std::isfinite(y)) {
      ++c.counted;
      if (std::abs(x - y) > 1e-9) {
        r.better = ((x > y) == higherBetter) ? 1 : -1;
        if (r.better > 0) ++c.leadsA; else ++c.leadsB;
      }
    }
    c.rows << r;
  };
  row(QCoreApplication::translate("seasonal", "Policy rate"), a.policyRate, b.policyRate, true, "%", 2);
  row(QCoreApplication::translate("seasonal", "Headline CPI y/y"), a.cpi.value, b.cpi.value, true, "%", 1);
  row(QCoreApplication::translate("seasonal", "Core CPI y/y"), a.coreCpi.value, b.coreCpi.value, true, "%", 1);
  row(QCoreApplication::translate("seasonal", "GDP q/q"), a.gdp.value, b.gdp.value, true, "%", 1);
  row(QCoreApplication::translate("seasonal", "Unemployment"), a.unemployment.value, b.unemployment.value, false, "%", 1);
  const bool sameActivity = a.activityName == b.activityName && a.activityNeutral == b.activityNeutral;
  row(a.activityName == "PMI" ? QCoreApplication::translate("seasonal", "PMI manufacturing") : a.activityName == "BCI" ? QCoreApplication::translate("seasonal", "Business conf. (OECD)") : a.activityName, a.activity.value,
      sameActivity ? b.activity.value : NAN, true, "", 1);
  row(QCoreApplication::translate("seasonal", "Retail sales y/y"), a.retail.value, b.retail.value, true, "%", 2);
  // why a side is missing (tooltips)
  const Release* ra[] = {nullptr, &a.cpi, &a.coreCpi, &a.gdp, &a.unemployment, &a.activity, &a.retail};
  const Release* rb[] = {nullptr, &b.cpi, &b.coreCpi, &b.gdp, &b.unemployment, &b.activity, &b.retail};
  for (int i = 0; i < c.rows.size() && i < kComparedRows; ++i) {
    QStringList n;
    if (i == 0) { if (!a.policyNote.isEmpty()) n << a.ccy + ": " + a.policyNote; if (!b.policyNote.isEmpty()) n << b.ccy + ": " + b.policyNote; }
    else {
      if (!ra[i]->ok() && !ra[i]->note.isEmpty()) n << a.ccy + ": " + ra[i]->note;
      if (!rb[i]->ok() && !rb[i]->note.isEmpty() && rb[i]->note != ra[i]->note) n << b.ccy + ": " + rb[i]->note;
      else if (!rb[i]->ok() && !rb[i]->note.isEmpty() && ra[i]->ok()) n << b.ccy + ": " + rb[i]->note;
    }
    if (i == 5 && !sameActivity) n << QCoreApplication::translate("seasonal", "Different activity indices, not compared");
    c.rows[i].note = n.join("\n");
  }
  // the real rates: shown, never counted
  for (const bool core : {false, true}) {
    CompareRow r{core ? QCoreApplication::translate("seasonal", "Real rate (core CPI)") : QCoreApplication::translate("seasonal", "Real rate (CPI)"),
                 realRate(a, core), realRate(b, core), 0, "%", 2};
    if (std::isfinite(r.a) && std::isfinite(r.b) && std::abs(r.a - r.b) > 1e-9) r.better = r.a > r.b ? 1 : -1;
    QStringList n;
    for (const Inputs* in : {&a, &b}) {
      const Release& p = core ? in->coreCpi : in->cpi;
      if (std::isfinite(realRate(*in, core)))
        n << QCoreApplication::translate("seasonal", "%1: policy rate %2% − CPI %3% (%4) = %5%")
                 .arg(in->ccy, num(in->policyRate, 2), num(p.value, 1), periodText(p), num(realRate(*in, core), 2));
      else n << in->ccy + ": " + (!std::isfinite(in->policyRate) && !in->policyNote.isEmpty() ? in->policyNote : !p.ok() && !p.note.isEmpty() ? p.note : QString::fromUtf8("—"));
    }
    r.note = n.join("\n");
    c.rows << r;
    if (core || !std::isfinite(r.a) || !std::isfinite(r.b)) continue;
    const double gap = r.a - r.b;
    c.carry = std::abs(gap) < 0.005 ? QCoreApplication::translate("seasonal", "Carry: %1 and %2 yield the same after inflation").arg(a.ccy, b.ccy)
                                    : QCoreApplication::translate("seasonal", "Carry: %1 yields %2 pt more than %3 after inflation")
                                          .arg(gap > 0 ? a.ccy : b.ccy, num(std::abs(gap), 2), gap > 0 ? b.ccy : a.ccy);
  }
  c.winner = c.leadsA > c.leadsB ? 1 : c.leadsA < c.leadsB ? -1 : 0;
  c.verdict = c.counted == 0 ? QCoreApplication::translate("seasonal", "Not enough releases to compare")
            : c.winner > 0 ? QCoreApplication::translate("seasonal", "Fundamentals favour %1").arg(a.ccy)
            : c.winner < 0 ? QCoreApplication::translate("seasonal", "Fundamentals favour %1").arg(b.ccy)
            : QCoreApplication::translate("seasonal", "Fundamentals balanced");
  return c;
}

// ---------------------------------------------------------------- automated read
static QString monthYear(const QDate& d) { return d.isValid() ? i18n::locale().toString(d, "MMM yyyy") : QString(); }

Reading automatedRead(const Inputs& in, const Score& s) {
  Reading r;
  const CurrencyMeta* cm = meta(in.ccy);
  const QString bankName = cm ? bankShortName(cm->bankShort) : in.ccy;
  for (const Release* x : {&in.cpi, &in.coreCpi, &in.gdp, &in.unemployment, &in.activity, &in.confidence, &in.retail})
    if (x->ok()) { const QDate d = x->released.isValid() ? x->released : x->period; if (d > r.asOf) r.asOf = d; }
  if (in.lastDecision > r.asOf) r.asOf = in.lastDecision;

  // headline: policy + inflation + the weakest / strongest other signal
  const Move mv = lastMove(in);
  QString policy;
  if (mv == Move::Hike) policy = QCoreApplication::translate("seasonal", "%1 hikes to %2%").arg(bankName, num(in.policyRate, 2));
  else if (mv == Move::Cut) policy = QCoreApplication::translate("seasonal", "%1 cuts to %2%").arg(bankName, num(in.policyRate, 2));
  else if (mv == Move::Hold) policy = QCoreApplication::translate("seasonal", "%1 holds at %2%").arg(bankName, num(in.policyRate, 2));
  else if (std::isfinite(in.policyRate)) policy = QCoreApplication::translate("seasonal", "%1 policy rate %2%").arg(bankName, num(in.policyRate, 2));
  QString infl;
  const Signal& si = s.sig[Inflation];
  if (std::isfinite(si.value)) {
    const QString arrow = trendArrow(in.cpi.value, in.cpi.previous);
    infl = (si.word == "ABOVE TARGET" ? QCoreApplication::translate("seasonal", "inflation above target") : si.word == "BELOW TARGET" ? QCoreApplication::translate("seasonal", "inflation below target") : QCoreApplication::translate("seasonal", "inflation near target"));
    if (arrow == QString::fromUtf8("↑")) infl += QCoreApplication::translate("seasonal", " and rising");
    else if (arrow == QString::fromUtf8("↓")) infl += QCoreApplication::translate("seasonal", " and easing");
  }
  int weakest = -1, strongest = -1;
  for (int c = Growth; c < kColumns; ++c) {
    if (!std::isfinite(s.sig[c].value)) continue;
    if (weakest < 0 || s.sig[c].value < s.sig[weakest].value) weakest = c;
    if (strongest < 0 || s.sig[c].value > s.sig[strongest].value) strongest = c;
  }
  QString act;
  auto phrase = [](int c, const Signal& g) -> QString {
    const QString& w = g.word;
    switch (c) {
      case Growth: return w == "EXPANDING" ? QCoreApplication::translate("seasonal", "growth expanding") : w == "FLAT" ? QCoreApplication::translate("seasonal", "growth flat") : QCoreApplication::translate("seasonal", "growth contracting");
      case Labour: return w == "IMPROVING" ? QCoreApplication::translate("seasonal", "labour market improving") : w == "SOFTENING" ? QCoreApplication::translate("seasonal", "labour market softening") : QCoreApplication::translate("seasonal", "labour market stable");
      case Activity: return w == "EXPANSION" ? QCoreApplication::translate("seasonal", "activity in expansion") : QCoreApplication::translate("seasonal", "activity in contraction");
      case Sentiment: return w == "IMPROVING" ? QCoreApplication::translate("seasonal", "confidence improving") : w == "WEAKENING" ? QCoreApplication::translate("seasonal", "confidence weakening") : QCoreApplication::translate("seasonal", "confidence steady");
    }
    return {};
  };
  if (weakest >= 0 && s.sig[weakest].value <= -0.15) act = phrase(weakest, s.sig[weakest]);
  else if (strongest >= 0 && s.sig[strongest].value >= 0.15) act = phrase(strongest, s.sig[strongest]);
  QStringList hp;
  if (!policy.isEmpty()) hp << policy;
  if (!infl.isEmpty()) hp << infl;
  if (!act.isEmpty()) hp << act;
  r.headline = hp.join(", ");
  if (!r.headline.isEmpty()) r.headline[0] = r.headline[0].toUpper();

  // paragraph: one sentence per available release, in a fixed order
  QStringList p;
  if (std::isfinite(in.policyRate)) {
    QString t = QCoreApplication::translate("seasonal", "The %1 policy rate stands at %2%").arg(bankName, num(in.policyRate, 2));
    const int bp = std::isfinite(in.priorRate) ? qAbs(qRound((in.policyRate - in.priorRate) * 100)) : 0;
    if (mv == Move::Hike) t += QCoreApplication::translate("seasonal", " after a hike of %1 bp").arg(bp);
    else if (mv == Move::Cut) t += QCoreApplication::translate("seasonal", " after a cut of %1 bp").arg(bp);
    else if (mv == Move::Hold) t += QCoreApplication::translate("seasonal", " after a hold");
    if (in.lastDecision.isValid()) t += QCoreApplication::translate("seasonal", " on %1").arg(in.lastDecision.toString("yyyy-MM-dd"));
    if (in.nextMeeting.isValid()) t += QCoreApplication::translate("seasonal", "; next decision %1").arg(in.nextMeeting.toString("yyyy-MM-dd"));
    p << t + ".";
  }
  if (in.cpi.ok()) {
    QString t = QCoreApplication::translate("seasonal", "Headline inflation is %1% y/y").arg(num(in.cpi.value, 1));
    if (std::isfinite(in.cpi.previous)) t += QCoreApplication::translate("seasonal", " (%1% before)").arg(num(in.cpi.previous, 1));
    if (in.coreCpi.ok()) t += QCoreApplication::translate("seasonal", ", core %1%").arg(num(in.coreCpi.value, 1));
    if (in.cpi.period.isValid()) t += QCoreApplication::translate("seasonal", " in %1").arg(monthYear(in.cpi.period));
    p << t + ".";
  }
  QStringList act2;
  if (in.gdp.ok()) act2 << QCoreApplication::translate("seasonal", "GDP %1% q/q").arg(num(in.gdp.value, 1));
  if (in.unemployment.ok()) act2 << QCoreApplication::translate("seasonal", "unemployment %1%").arg(num(in.unemployment.value, 1));
  if (in.activity.ok()) act2 << QString("%1 %2").arg(activityLabel(in.activityName), num(in.activity.value, 1));
  if (in.confidence.ok()) act2 << QCoreApplication::translate("seasonal", "consumer confidence %1").arg(num(in.confidence.value, 1));
  if (in.retail.ok()) act2 << QCoreApplication::translate("seasonal", "retail sales %1% y/y").arg(num(in.retail.value, 1));
  if (!act2.isEmpty()) { p << QCoreApplication::translate("seasonal", "Latest releases: %1.").arg(act2.join(", ")); }
  if (std::isfinite(s.score))
    p << QCoreApplication::translate("seasonal", "Score %1%2 (%3), mean of %4 oriented signals.").arg(s.score > 0 ? "+" : "").arg(s.score).arg(tw(s.bias).toLower()).arg(s.available);
  r.paragraph = p.join(" ");

  // bullets: the three signals with the largest |value|
  QList<int> order;
  for (int c = 0; c < kColumns; ++c) if (std::isfinite(s.sig[c].value)) order << c;
  std::sort(order.begin(), order.end(), [&](int a, int b) { return std::abs(s.sig[a].value) > std::abs(s.sig[b].value); });
  for (int k = 0; k < qMin(3, int(order.size())); ++k) {
    const int c = order[k];
    const Signal& g = s.sig[c];
    static const char* titles[] = {QT_TRANSLATE_NOOP("seasonal", "Rates"), QT_TRANSLATE_NOOP("seasonal", "Inflation"), QT_TRANSLATE_NOOP("seasonal", "Growth"), QT_TRANSLATE_NOOP("seasonal", "Labour"), QT_TRANSLATE_NOOP("seasonal", "Activity"), QT_TRANSLATE_NOOP("seasonal", "Sentiment")};
    const QString name = tw(titles[c]);
    const QString pat = g.value >= 0.15 ? QCoreApplication::translate("seasonal", "%1 supports %2: %3.") : g.value <= -0.15 ? QCoreApplication::translate("seasonal", "%1 weighs on %2: %3.")
                                         : QCoreApplication::translate("seasonal", "%1 is neutral for %2: %3.");
    QString why = g.why; if (why.endsWith('.')) why.chop(1);
    r.bullets << pat.arg(name, in.ccy, why);
  }
  return r;
}

// ---------------------------------------------------------------- sources
namespace {
class PendingSource : public Source {
public:
  using Source::Source;
  QList<Inputs> inputs() const override { return {}; }
  bool ready() const override { return false; }
  QString status() const override { return QCoreApplication::translate("seasonal", "Waiting for the Economy services (macro data not connected yet)"); }
};
class JsonSource : public Source {
public:
  JsonSource(const QList<Inputs>& in, QObject* parent) : Source(parent), m_in(in) {}
  QList<Inputs> inputs() const override { return m_in; }
  bool ready() const override { return !m_in.isEmpty(); }
private:
  QList<Inputs> m_in;
};
QDate parseDate(const QJsonValue& v) {
  const QString s = v.toString();
  if (s.size() == 7) return QDate::fromString(s + "-01", "yyyy-MM-dd");
  return QDate::fromString(s, "yyyy-MM-dd");
}
double dbl(const QJsonValue& v) { return v.isDouble() ? v.toDouble() : NAN; }
Series series(const QJsonValue& v) {
  Series s;
  for (const auto& e : v.toArray()) { const auto a = e.toArray(); if (a.size() >= 2) s << Point{parseDate(a[0]), dbl(a[1])}; }
  return s;
}
Release release(const QJsonValue& v) {
  const auto o = v.toObject();
  Release r;
  r.value = dbl(o["value"]); r.previous = dbl(o["previous"]);
  r.period = parseDate(o["period"]); r.released = parseDate(o["released"]);
  r.source = o["source"].toString(); r.history = series(o["history"]);
  return r;
}
}

QList<Inputs> loadJson(const QByteArray& json, QString* error) {
  QJsonParseError pe;
  const auto doc = QJsonDocument::fromJson(json, &pe);
  if (pe.error != QJsonParseError::NoError) { if (error) *error = pe.errorString(); return {}; }
  QList<Inputs> out;
  for (const auto& e : doc.object()["currencies"].toArray()) {
    const auto o = e.toObject();
    Inputs in;
    in.ccy = o["ccy"].toString();
    in.policyRate = dbl(o["policyRate"]); in.priorRate = dbl(o["priorRate"]);
    in.lastDecision = parseDate(o["lastDecision"]); in.nextMeeting = parseDate(o["nextMeeting"]);
    in.policyHistory = series(o["policyHistory"]);
    in.cpi = release(o["cpi"]); in.coreCpi = release(o["coreCpi"]); in.cpiHistory = series(o["cpiHistory"]);
    in.gdp = release(o["gdp"]); in.unemployment = release(o["unemployment"]);
    in.activity = release(o["activity"]);
    if (o.contains("activityName")) in.activityName = o["activityName"].toString();
    if (o.contains("activityNeutral")) in.activityNeutral = o["activityNeutral"].toDouble();
    in.confidence = release(o["confidence"]); in.retail = release(o["retail"]);
    if (!in.ccy.isEmpty()) out << in;
  }
  return out;
}

Source* defaultSource(QObject* parent) {
  const QString f = qEnvironmentVariable("DW_FUND_INPUT");
  if (!f.isEmpty()) {
    QFile file(f);
    if (file.open(QIODevice::ReadOnly)) {
      const auto in = loadJson(file.readAll());
      if (!in.isEmpty()) return new JsonSource(in, parent);
    }
  }
  return macroSource(parent);
}
}
