#pragma once
#include <QDate>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <cmath>

// Fundamental scoring of the eight major currencies (Market › Fundamental Heatmap). No UI, no network: the page
// feeds it Inputs built from the Economy services (macro::MacroStore, macro::CentralBanks) through a Source.
//
// SCORING RULES (also printed in the page footnote and the header tooltip)
// Six oriented signals per currency, each in [-1, +1]; positive = the figure supports the currency.
//   tanh() squashes a gap into ±1; "trend" = +1 / -1 / 0 when the latest release is above / below / equal (within a
//   small tolerance) to the previous release.
//  1 RATES      word HIKE / HOLD / CUT from the last decision (policy rate vs the rate before it).
//               signal = 0.7 × move (+1 hike, -1 cut, 0 hold) + 0.3 × tanh(real rate / 2), real rate = policy − CPI.
//  2 INFLATION  word ABOVE / NEAR / BELOW TARGET: headline CPI y/y vs the bank's target band ± 0.5 pt (2% targets:
//               above 2.5, below 1.5; RBA 2–3%: above 3.0, below 2.0; SNB 0–2%: above 2.5, below 0.0).
//               signal = 0.7 × tanh((CPI − target midpoint) / 1) + 0.3 × trend. Above target supports the currency
//               (it keeps the central bank hawkish), below target weighs on it.
//  3 GROWTH     word EXPANDING (> 0.1% q/q) / FLAT / CONTRACTING (< -0.1%). signal = 0.7 × tanh(GDP q/q / 0.4) + 0.3 × trend.
//  4 LABOUR     word IMPROVING (unemployment down ≥ 0.1 pt) / STABLE / SOFTENING (up ≥ 0.1 pt).
//               signal = -0.7 × tanh(Δ / 0.15) − 0.3 × tanh(z), z = unemployment vs its 5-year mean in standard
//               deviations (0 without history). Lower unemployment supports the currency.
//  5 ACTIVITY   word EXPANSION / CONTRACTION: activity index (manufacturing PMI, neutral 50, or a business confidence
//               index, neutral 100) vs its neutral level. signal = 0.7 × tanh((value − neutral) / scale) + 0.3 × trend,
//               scale 3 points for a PMI, 1 point for a 100-based index.
//  6 SENTIMENT  word IMPROVING / WEAKENING / STEADY: consumer confidence vs its previous release.
//               signal = 0.6 × tanh(Δ / (2 × median monthly change)) + 0.4 × tanh(z vs 5-year history); without history
//               the change alone (typical change 1 point).
// LEVEL = 100 × mean of the available signals (−100…+100); "—" with fewer than 3 signals.
// SCORE = LEVEL − mean LEVEL of the scored currencies, rounded: a currency is strong or weak relative to the others
// (an exchange rate is a relative price). The cards and the heatmap show this same score.
// BIAS: score ≥ +8 BULLISH, ≤ −8 BEARISH, else NEUTRAL.
// SHADE of a cell: ≥ 0.5 strong support (bright teal), 0.15…0.5 weak support (dim teal), −0.15…0.15 neutral (grey),
// −0.5…−0.15 weak drag (dim red), ≤ −0.5 strong drag (red). Missing figure: "—" grey.
// PAIR COMPARATOR: seven rows (policy rate, headline CPI, core CPI, GDP q/q, unemployment, activity index, retail
// sales y/y); "better" = higher, except unemployment (lower). The verdict names the currency that leads on more rows
// (the score difference is shown beside it); a tie is "balanced". Rows missing for either side are not counted.
// Two more rows, shown and never counted (the policy rate and the CPI already are): the real rate, policy rate less
// headline CPI y/y and less core CPI y/y. Their difference is the carry after inflation.
namespace fundamental {

// ---------------------------------------------------------------- static metadata (not data)
struct CurrencyMeta {
  QString ccy, country, bankShort, bankTitle;   // "AUD", "AUSTRALIA", "RBA", "RBA"
  double target = 2.0, targetLo = 2.0, targetHi = 2.0;   // inflation target midpoint and band
  QString targetLabel;                           // "2%", "2–3%", "0–2%"
};
const QList<CurrencyMeta>& currencies();         // USD EUR GBP JPY CHF CAD AUD NZD
const CurrencyMeta* meta(const QString& ccy);

// ---------------------------------------------------------------- inputs (what the Economy services provide)
struct Point { QDate date; double value = NAN; };
using Series = QVector<Point>;                   // oldest first

// One published indicator: the latest release and the one before it.
struct Release {
  double value = NAN;        // latest published value (unit of the indicator)
  double previous = NAN;     // previous release of the same indicator (for the trend arrow)
  QDate period;              // reference period of the latest value (first day of the month / quarter)
  QDate released;            // publication date when known
  QString source;            // short publisher name shown in the release detail ("ABS", "BLS", "OECD"…)
  Series history;            // optional past values (≥ 5 years when available) for "level vs history"
  bool annual = false;       // annual figure used as a fallback for a monthly indicator
  QString note;              // why the figure is missing (tooltip next to "—"), empty when present
  bool ok() const { return std::isfinite(value); }
};

struct Inputs {
  QString ccy;               // "AUD"
  // monetary policy (macro::CentralBanks)
  double policyRate = NAN;   // current policy rate, %
  double priorRate = NAN;    // policy rate before the last decision, %
  QDate lastDecision;        // date of the last decision (hike, cut or hold)
  QDate nextMeeting;         // next scheduled decision
  Series policyHistory;      // monthly policy rate (BIS CBPOL end of month), as long as available
  QString policySource = "BIS monthly";
  QString rateSource;        // who publishes the current rate ("NY Fed (EFFR target range)", "BIS policy rate"…)
  QString rateText;          // "3.75–4.00%" when the bank sets a range (rate = upper bound)
  QString policyNote;        // why the policy figures are missing
  // prices (macro::MacroStore)
  Release cpi;               // headline CPI, % y/y
  Release coreCpi;           // core CPI, % y/y
  Series cpiHistory;         // headline CPI % y/y monthly, as long as available
  QString cpiSource = "BIS monthly";
  // activity (macro::MacroStore)
  Release gdp;               // real GDP, % q/q seasonally adjusted
  Release unemployment;      // unemployment rate, %
  Release activity;          // manufacturing PMI (or business confidence)
  QString activityName = "PMI";
  double activityNeutral = 50;   // 50 for a PMI, 100 for an OECD-style confidence index
  Release confidence;        // consumer confidence (any scale)
  Release retail;            // retail sales, % y/y
};

// Provider of the inputs. Phase 1 has no provider connected (pendingSource()); phase 2 adapts the Economy services.
class Source : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  virtual QList<Inputs> inputs() const = 0;
  virtual bool ready() const = 0;
  virtual QString status() const { return {}; }   // shown while not ready
  virtual void activate() {}                      // page shown: start / refresh loading
signals:
  void changed();
};
// The app's source: macroSource() (Economy services: macro::MacroStore + macro::CentralBanks).
// Test hook: DW_FUND_INPUT=<file.json> loads Inputs from a JSON file (format: loadJson()).
Source* defaultSource(QObject* parent);
// JSON test fixture → Inputs. {"currencies":[{"ccy":"AUD","policyRate":4.6,"priorRate":4.35,"lastDecision":"2026-09-29",
// "nextMeeting":"2026-11-03","policyHistory":[["2016-01",2.0],…],"cpi":{"value":4.0,"previous":3.8,"period":"2026-09",
// "released":"2026-10-29","source":"ABS"},"coreCpi":{…},"cpiHistory":[…],"gdp":{…},"unemployment":{…},
// "activity":{…},"activityName":"PMI","activityNeutral":50,"confidence":{…},"retail":{…}}, …]}
// Inputs from the Economy package (src/fundamental/MacroSource.cpp). Mapping:
//  policy rate / last decision / prior rate / next meeting  macro::CentralBanks::bank(ccy): rate, decisions[0]
//      ("Raised|Cut … by Nbp" / "Held …" → prior = rate ∓ N bp), nextMeeting(); history MacroStore PolicyRate (BIS
//      monthly) with the bank's current rate appended when newer.
//  headline CPI y/y + history   MacroStore Inflation (BIS monthly)       GDP q/q        MacroStore GdpQoq (OECD)
//  unemployment                  MacroStore Unemployment (OECD monthly; else UnemploymentAnnual, World Bank, "(yr)")   activity       MacroStore BusinessConf
//      (OECD, 100 = long-run average, shown as "BCI")                     confidence     MacroStore ConsumerConf
//  core CPI y/y   MacroStore CoreInflation      retail sales y/y   MacroStore RetailSalesYoy (missing → "—" + note). EUR = euro area aggregate ("ez").
Source* macroSource(QObject* parent);
QList<Inputs> loadJson(const QByteArray& json, QString* error = nullptr);

// ---------------------------------------------------------------- scoring
enum Column { Rates, Inflation, Growth, Labour, Activity, Sentiment, kColumns };
QString columnName(int c);   // "RATES" … (English key: display with tw())
// Display text of an English key stored in the model (signal words, bias, move, column, country, bank title):
// the model keeps English keys (the logic compares them), the UI shows tw(key) in the interface language.
QString tw(const QString& key);
QString tw(const char* key);
QString bankShortName(const QString& id);    // FED → "Fed", ECB → "ECB" / "BCE"…
QString activityLabel(const QString& name);  // "BCI" → "BCI" / "ICA"

enum class Shade { Missing, StrongPos, WeakPos, Neutral, WeakNeg, StrongNeg };
Shade shadeOf(double signal);

struct Signal {
  QString word;              // "HIKE", "ABOVE TARGET"… ("—" when missing)
  QString sub;               // "4.60% (+25bp)", "4.0% y/y ↑"
  double value = NAN;        // -1..+1
  Shade shade = Shade::Missing;
  QString why;               // one sentence: the rule applied, for tooltips
};

enum class Move { Unknown, Hike, Hold, Cut };
Move lastMove(const Inputs& in);
QString moveWord(Move m);

struct Score {
  QString ccy;
  Signal sig[kColumns];
  int available = 0;
  double level = NAN;        // 100 × mean of the signals (absolute), NAN with fewer than 3 signals
  double score = NAN;        // level − mean level of the scored currencies, rounded (scoreAll); scoreOne: = level
  QString bias;              // BULLISH / NEUTRAL / BEARISH / "—"
  int rank = 0;              // 1 = strongest
};
Score scoreOne(const Inputs& in);   // absolute: score = rounded level, bias from it
// Scores every input relative to the others (score = level − mean level) and ranks them (best first; currencies
// without a score go last).
QVector<Score> scoreAll(const QList<Inputs>& inputs);

// ---------------------------------------------------------------- pair comparator
QString num(double v, int decimals);                    // never "-0.0"
double realRate(const Inputs& in, bool core = false);   // policy rate less headline (core) CPI y/y, points; NaN when one is missing
constexpr int kComparedRows = 7, kRealRows = 2;         // of a comparison: the counted rows, then the real rates
struct CompareRow { QString label; double a = NAN, b = NAN; int better = 0; QString unit; int decimals = 2; QString note; };
struct Comparison {
  QList<CompareRow> rows;    // better: +1 base better, -1 other better, 0 none / missing / equal
  QString carry;             // "Carry: AUD yields 2.85 pt more than CHF after inflation"; empty when a real rate is missing
  int leadsA = 0, leadsB = 0, counted = 0;
  QString verdict;           // "Fundamentals favour AUD" / "Fundamentals balanced"
  int winner = 0;            // +1 base, -1 other, 0 balanced
};
Comparison compare(const Inputs& a, const Inputs& b);

// ---------------------------------------------------------------- automated read (rule-based, no model)
struct Reading {
  QString headline;
  QString paragraph;
  QStringList bullets;       // three strongest signals explained
  QDate asOf;                // latest release date used
};
Reading automatedRead(const Inputs& in, const Score& s);

int daysUntil(const QDate& d, const QDate& today = QDate::currentDate());
QString trendArrow(double latest, double previous, double tol = 0.05);   // "↑" / "↓" / ""
}
