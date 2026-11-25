#pragma once
#include <QObject>
#include <QDate>
#include <QDateTime>
#include <QList>
#include <QVector>
#include <QHash>
#include <QString>
#include <QSet>
#include <QTimer>
#include <cmath>

// The 8 central banks of the FX majors: policy rate, meeting calendar, market-implied odds, implied path,
// committees and votes. Used by Economy › Rates, Global › Overview (Central Banks panel), Market › Fundamental
// Heatmap and the Markets Watch instrument sheet.
//
//   auto& cb = macro::CentralBanks::instance();
//   cb.ensureLoaded();                                   // idempotent; refreshes stale parts
//   connect(&cb, &macro::CentralBanks::updated, this, [this] { refresh(); });
//   for (const macro::Bank& b : cb.banks()) {            // Fed ECB BoE BoJ SNB RBA RBNZ BoC (reference order)
//     b.rate; b.nextMeeting().decision; b.daysToNext();
//     if (b.priced) { macro::Odds o = b.nextOdds(); o.cut(); o.hold; o.hike(); }   // else show "—" + b.pricingNote
//   }
//   const macro::Bank* fed = cb.bank("USD");             // by id "FED", code "Fed", currency or iso2
//
// Odds are computed by Deepwater from futures by the standard method (piecewise-constant expected overnight rate
// between meetings fitted to each contract's average; 25 bp steps; meetings treated independently): Fed — CME
// 30-day fed funds futures (Yahoo ZQ…​.CBT); ECB — CME 3-month €STR futures (Yahoo ESR…​.CME, thin); BoC — Montréal
// Exchange 3-month CORRA futures (CRA, settlement prices); BoE — Bank of England instantaneous OIS (SONIA) forward
// curve; BoJ — JPX/OSE 3-month TONA futures settlement prices; SNB — Eurex 3-month SARON futures settlement prices;
// RBA / RBNZ — FinancialJuice's "<bank> Interest Rate Probabilities" picture read by OCR, kept only if it validates.
// When a bank has no valid feed, priced == false and pricingNote says why. Polymarket is probed only.
namespace macro {

// Outcome probabilities (0–1) relative to the current rate. Invalid (NaN) when the bank is not priced.
struct Odds {
  double cut50 = NAN, cut25 = NAN, hold = NAN, hike25 = NAN, hike50 = NAN;
  bool valid() const { return std::isfinite(hold); }
  double cut() const { return cut50 + cut25; }
  double hike() const { return hike25 + hike50; }
  double at(int bucket) const;   // 0 = -50+bps … 4 = +50+bps
};

struct MeetingDate {
  QDate decision;        // announcement day (the day charts and chips use)
  QDate first;           // first day of a two-day meeting (the day tables show), = decision for one-day meetings
  bool verified = true;  // false: entered by hand from a secondary source (site refuses programs)
};

struct PathPoint {
  MeetingDate meeting;
  double rate = NAN;     // implied policy rate after the meeting (policy rate + cumulative implied change)
  double bp = NAN;       // implied change at this meeting, bp
  Odds odds;             // cumulative distribution after this meeting vs today's rate
};

struct OddsSample {
  QDateTime t;           // UTC
  QVector<Odds> meetings;   // aligned with Bank::path (cumulative vs the rate at that time)
};

struct Decision {
  QDate date;            // decision day
  QString text;          // "Raised the target range by 25bp to 3.75–4.00%"
  QString textEn;        // the same in English whatever the interface language (Facts stores this one)
  QString vote;          // "10–2" / "Unanimous" / "" (not published)
  bool unanimous = false;
  QString dissent;       // "Lisa D. Cook (+25bp hike), Beth M. Hammack (+25bp hike)"
  QString url;           // official statement
  bool inferred = false; // read from the policy-rate series (a change between two dates), not from a statement
};

struct Bank {
  QString id;            // FED ECB BOE BOJ SNB RBA RBNZ BOC
  QString code;          // Fed ECB BoE BoJ SNB RBA RBNZ BoC
  QString name;          // "Federal Reserve"
  QString iso2, currency;
  QString committee;     // "FOMC"
  QString committeeNote; // "Each statement lists every member's vote and names any dissent."
  QString headTitle, head;   // "CHAIR", "Kevin Warsh" (verify yearly, resources/macro/meetings.json)
  QString scheduleUrl, officialUrl;

  double rate = NAN;     // policy rate (Fed: upper bound of the target range)
  QString rateText;      // "3.75–4.00%" for a range, else empty
  QDate rateAsOf;
  QString rateNote;      // "as of Nov 12" or "Sep '26 avg"
  QString rateSource;    // "NY Fed (EFFR target range)", "ECB deposit facility", "BIS policy rate"…
  QString rateSourceEn;  // the same in English whatever the interface language (Facts keys its chain with it)
  QDateTime rateFetched;
  QDate rateSince;       // date of the last change when the note reads "since …" (ECB)

  QList<MeetingDate> meetings;   // known schedule, ascending
  MeetingDate nextMeeting() const;
  int daysToNext() const;        // days to the first day of the next meeting, 0 while it is under way; -1 if unknown
  QList<MeetingDate> upcoming(int n = 10) const;

  bool priced = false;
  QString pricingSource; // "computed by Deepwater from CME 30-day fed funds futures (ZQ, via Yahoo)"
  QString pricingSourceEn;   // the same in English (Facts)
  QString pricingNote;   // why there are no odds, or caveats (thin market)
  QDateTime pricedAt;
  double overnight = NAN;   // EFFR / €STR / CORRA / SONIA / TONA / SARON used as today's level
  QList<PathPoint> path;    // next meetings that the futures cover (≤ 10)
  double impliedBp = NAN;   // next meeting
  double impliedBpWeekAgo = NAN;
  QVector<OddsSample> history;   // hourly (Fed), daily or recorded snapshots (others)
  QString historySource;

  QList<Decision> decisions;   // most recent first

  Odds nextOdds() const { return path.isEmpty() ? Odds{} : path.first().odds; }
  QString action() const;      // "Cut" / "Hold" / "Hike" (most likely outcome), "" if not priced
  QString bias(int* pct = nullptr) const;   // "HAWKISH" (P(hike) ≥ 10%), "DOVISH" (P(cut) ≥ 10%), else "NEUTRAL"
};

class CentralBanks : public QObject {
  Q_OBJECT
public:
  static CentralBanks& instance();
  void ensureLoaded();
  // A page showing the odds is on screen (RatesPage, Overview): the Fed's and the ECB's, the only ones that move during
  // the day with a source that allows it (CME futures via Yahoo), are asked every 5 min instead of 15 min / 1 h.
  void setActive(QObject* owner, bool active);
  // Fed and ECB odds older than `maxAgeSec` are asked again now (Claude's tool); `updated` follows.
  void refreshOdds(int maxAgeSec);
  const QList<Bank>& banks() const { return m_banks; }
  const Bank* bank(const QString& key) const;   // id, code, currency or iso2 (case-insensitive)

  // Polymarket (reference's second odds source): reachability only.
  bool polymarketReachable() const { return m_polyOk; }
  QString polymarketStatus() const { return m_polyStatus; }

signals:
  void updated();

private:
  CentralBanks();
  Bank* mut(const QString& id);
  void loadSchedule();
  void loadRates();
  void loadFedSchedule();
  void loadFedPricing();
  void loadEcbPricing();
  void loadBocPricing();
  void loadBoePricing();         // CentralBanksMore.cpp: BoE OIS forward curve
  void loadBojPricing();         // JPX 3M TONA futures + BoJ TONA
  void loadSnbPricing();         // Eurex 3M SARON futures
  void loadFjPricing();          // RBA / RBNZ: FinancialJuice pictures, OCR
  void applyFjTable(const QString& bankId);
  void loadDecisions();
  void probePolymarket();
  void derive(Bank& b);           // decisions from the rate history where no statement is parsed
  void changed();
  void retranslate();             // language changed: rebuild every text (loaders re-run from the HTTP cache)

  QList<Bank> m_banks;
  QHash<QString, QVector<QPair<QDate, double>>> m_rateHist;   // id → daily policy rate (BIS / official)
  QHash<QString, qint64> m_started;
  QSet<QObject*> m_active;
  QTimer m_activeTimer;
  bool m_polyOk = false;
  QString m_polyStatus;
  bool m_emitQueued = false;
  QSet<QString> m_fjSeen;        // FinancialJuice item ids already read
  QVector<QPair<QDate, double>> m_sonia;   // BoE: SONIA fixings (IADB IUDSOIA, last 30 days), ascending
  int m_boeWaits = 0;            // BoE pricing: 5 s waits for Bank Rate and SONIA so far
  bool m_boeNoSonia = false;     // BoE pricing ran without SONIA: run it again when SONIA arrives
  int m_fedWaits = 0;            // Fed pricing: 4 s waits for the EFFR so far
};

// Solver used for the odds (exposed for tests). Each contract is the average overnight rate over [start, end).
struct RateContract { QDate start, end; double avg = NAN; };
// Returns the expected overnight rate after each meeting (effective dates, ascending); NaN where no contract covers it.
QVector<double> solveMeetingRates(double current, const QVector<QDate>& effective, const QVector<RateContract>& contracts);
// Outcome odds after each meeting from the expected path (25 bp steps, independent meetings).
QVector<Odds> oddsFromPath(double current, const QVector<double>& rates);
// A decision's English text (as Facts stores it, "Held Bank Rate at 3.75% (vote 6–3)") in the interface language;
// unchanged when it is not one of the sentences above.
QString decisionTextShown(const QString& english);
}
