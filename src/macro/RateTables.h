#pragma once
#include "macro/Ocr.h"
#include <QByteArray>
#include <QDate>
#include <QHash>
#include <QList>
#include <QString>
#include <cmath>

// Published tables of what the market prices for the next policy meetings, read where no futures feed is open to the
// app (RBA, RBNZ) and as a second reading of the other banks' odds. Pure functions (tests/calendar).
namespace macro::ratetables {

// FinancialJuice's "<bank> Interest Rate Probabilities" picture: one row per meeting (month), read by OCR. A row is kept
// when two of its three readings agree: the total, the previous total plus the move, the implied rate less the effective
// rate. The table stops at the first row that does not hold together.
struct Row { QDate month; double implied = NAN, move = NAN, total = NAN; };   // month: its first day; move, total in bp
enum class Fault { None, NotATable, OtherBank, Inconsistent };
struct Table {
  double effective = NAN;
  QList<Row> rows;
  Fault fault = Fault::NotATable;
  bool ok() const { return !rows.isEmpty(); }
};
Table readPicture(const QList<ocr::Line>& lines, const QString& bank, double policy);

// Trading Economics' "implied rates" page of a country: the outcomes of its next meeting, and one line per other country.
struct Outcome { double rate = NAN, p = NAN; };   // target rate %, probability 0-1
struct Implied { double current = NAN, rate = NAN, bp = NAN; QDate meeting; };   // current: the upper bound of a range
struct Page {
  QList<Outcome> next;                 // probabilities add up to 1 (2 points of rounding), else empty
  QHash<QString, Implied> banks;       // by bank id: FED ECB BOE BOJ BOC SNB (and RBA, RBNZ when listed)
};
Page readTradingEconomics(const QByteArray& html);
// Expected change at the next meeting, bp (outcomes against the rate in force); NaN when they are about another rate.
double expectedBp(const QList<Outcome>& next, double rate);

// investingLive's weekly article on rate expectations: per bank the chance of a move at the next meeting.
struct Weekly {
  double nextBp = NAN;                 // expected change at the next meeting (25 bp times the chance, signed)
  double chance = NAN;                 // 0-1, of `what`
  QString what;                        // "hike", "cut", "hold"
  double yearEndBp = NAN, nextYearBp = NAN;   // cumulative, signed
};
QHash<QString, Weekly> readWeekly(const QByteArray& html);

// The bank whose decision a calendar event is ("Cash Rate" of AUD: RBA), empty for any other event. Only the banks whose
// own site gives no daily rate to the app: the BIS comes a week or two later.
QString decisionOf(const QString& currency, const QString& title);
}
