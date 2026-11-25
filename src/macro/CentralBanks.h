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
#include <functional>

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
// RBA / RBNZ — FinancialJuice's "<bank> Interest Rate Probabilities" picture read by OCR, kept only if it validates
// (RateTables.h); past 36 market hours, the next meeting alone from investingLive's weekly article or (RBA, a source off
// by default) Trading Economics.
// When a bank has no valid feed, priced == false and pricingNote says why. Polymarket is probed only.
//
// FinancialJuice's pictures of the six other banks are read too: they say how a move that one 3-month contract prices
// for two meetings is shared between them, and stand beside the computed odds (otherOdds).
namespace wire { struct Item; }
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
  QDate decision;        // announcement day, the one every page shows
  QDate first;           // first day of a two-day meeting, = decision for one-day meetings
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
  QString shownCode() const;   // the same in the interface language ("BCE")
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
  int daysToNext() const;        // days to the decision of the next meeting; -1 if unknown
  QList<MeetingDate> upcoming(int n = 10) const;

  bool priced = false;
  QString pricingSource; // "computed by Deepwater from CME 30-day fed funds futures (ZQ, via Yahoo)"
  QString pricingSourceEn;   // the same in English (Facts)
  QString pricingNote;   // why there are no odds, or caveats (thin market)
  QDateTime pricedAt;    // when the odds were computed (published pictures and articles: when they came out)
  QDate pricedDay;       // the market day they come from, for sources that publish once a day (BoE, BoJ, SNB); else invalid
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

// What another source prices for the next meeting, beside the odds shown.
struct OtherOdds {
  QString source;          // "FinancialJuice", "investingLive", "Trading Economics"
  double bp = NAN;         // expected change at the next meeting
  QDateTime at;            // when it was published or read (UTC)
  QDate meeting;           // its meeting date, when it gives one
  bool fresh = false;      // published less than 36 market hours ago
  bool used = false;       // the odds shown already lean on it (they come from it, or its split of a shared contract)
  double level = NAN;      // the rate it prices after the meeting (%), when it gives one
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
  QList<OtherOdds> otherOdds(const QString& id) const;
  // The newest table read from the bank's FinancialJuice picture (UTC); invalid when there is none.
  QDateTime pictureAt(const QString& id) const;
  // Seconds between two moments without Saturdays and Sundays (UTC): nothing is priced then.
  static qint64 marketSeconds(QDateTime a, const QDateTime& b);

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
  void nextPicture();            // the six other banks' pictures, one every four minutes
  void readPictures(const QString& bankId, const QList<wire::Item>& newestFirst);
  void readPicture(const QString& bankId, const QString& item, const QString& url, const QDateTime& published, bool again, std::function<void()> next);
  void loadWeeklyArticle(const QString& url, const QDateTime& published);
  void loadTradingEconomics();
  void readCalendarDecisions();   // decisions of the RBA, RBNZ, BoJ and SNB from the release calendar, until the BIS has them
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
  struct PictureTry { int tries = 0; qint64 last = 0; bool busy = false, done = false; };
  QHash<QString, PictureTry> m_pictures;   // FinancialJuice item id → its reading
  QHash<QString, int> m_weeklyTried;       // weekly article → readings tried
  bool m_fjWatching = false;
  QStringList m_pictureQueue;              // banks whose picture is to be read, one at a time
  QTimer m_pictureTimer;
  QHash<QString, QDateTime> m_split;       // bank id → the picture its shared contracts are split with
  qint64 m_teAsked = 0;                    // Trading Economics: last reading asked (ms)
  QDate m_teRefused;                       // the day it refused
  QVector<QPair<QDate, double>> m_sonia;   // BoE: SONIA fixings (IADB IUDSOIA, last 30 days), ascending
  bool m_calendarWatched = false;
  int m_snbWaits = 0;            // SNB pricing: 5 s waits for the policy rate and SARON so far
  int m_boeWaits = 0;            // BoE pricing: 5 s waits for Bank Rate and SONIA so far
  bool m_boeNoSonia = false;     // BoE pricing ran without SONIA: run it again when SONIA arrives
  int m_fedWaits = 0;            // Fed pricing: 4 s waits for the EFFR so far
};

// Solver used for the odds (exposed for tests). Each contract is the average overnight rate over [start, end).
struct RateContract { QDate start, end; double avg = NAN; };
// Returns the expected overnight rate after each meeting (effective dates, ascending); NaN where no contract covers it.
// `hint`: another table's expected move at each meeting (percent): a contract that prices several meetings at once is
// shared between them as the table shares them, what is left over equally. Without it: equally.
QVector<double> solveMeetingRates(double current, const QVector<QDate>& effective, const QVector<RateContract>& contracts,
                                  const QVector<double>& hint = {});
// Outcome odds after each meeting from the expected path (25 bp steps, independent meetings).
QVector<Odds> oddsFromPath(double current, const QVector<double>& rates);
// A decision's English text (as Facts stores it, "Held Bank Rate at 3.75% (vote 6–3)") in the interface language;
// unchanged when it is not one of the sentences above.
QString decisionTextShown(const QString& english);
}
