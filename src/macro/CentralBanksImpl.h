#pragma once
#include "macro/CentralBanks.h"
#include "market/Candles.h"
#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <climits>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <functional>

// Private helpers shared by CentralBanks.cpp and CentralBanksMore.cpp (not for pages).
namespace macro::detail {
// Prices per contract (bars: t = UTC seconds, c = settlement / close).
struct Gather {
  int pending = 0;
  QHash<QString, market::Series> daily, hourly;
  std::function<void()> done;
};
// Fills b.path, odds, impliedBp, impliedBpWeekAgo and history from futures prices (price = 100 - rate).
// `hint`: the moves a published table gives for the same meetings, at a moment (solveMeetingRates); may be empty.
using Hint = std::function<QVector<double>(qint64 utcSec)>;
void computeFromQuotes(Bank& b, const QVector<QDate>& eff, const QList<MeetingDate>& meets,
                       const QList<QPair<QString, RateContract>>& contracts, const Gather& g, int effLagDays, const Hint& hint = {});
// The bank's FinancialJuice tables as a hint for these meetings; empty (and *at invalid) when the newest is older than the
// last decision or than five market days.
Hint pictureHint(const Bank& b, const QList<MeetingDate>& meets, QDateTime* at);
QDate immDate(int y, int m);   // third Wednesday
// Model limit of solveMeetingRates (3-month contracts, ECB / BoC / BoJ / SNB): when two priced meetings fall inside one
// contract, its move is shared between both: as in the FinancialJuice table of `splitAt`, equally without one. Returns
// `note` (the bank's other note) with that caveat (translated) appended when it applies to b.path, else `note` unchanged.
// *applies: it does.
QString withSharedContractCaveat(const QString& note, const Bank& b, const QVector<QDate>& eff,
                                 const QList<QPair<QString, RateContract>>& contracts, const QDateTime& splitAt = {}, bool* applies = nullptr);
// Appends one snapshot of prices to paths::data("macro")/<file> (if the last is older than 50 min, keeps 120 days)
// and merges every recorded snapshot into g.daily (the terminal's own history for snapshot-only sources).
void recordSnapshots(const QString& file, qint64 fetchedUtc, Gather& g);
// Odds history files (odds-*.json, fj-tables.json): months of snapshots that can't be downloaded again.
// readJson: a file that doesn't parse is renamed <file>.unreadable-<UTC time> (kept, never overwritten) and read as
// empty; *ok = false when the file can't be read or set aside (the caller then doesn't write);
// writeJson: through QSaveFile (a crash mid-write keeps the previous file).
QJsonDocument readJson(const QString& file, bool* ok);
bool writeJson(const QString& file, const QJsonDocument& doc);

// FOMC statement, the text after "Voting against the action were": one entry per dissenter with the move each group
// preferred ("Stephen I. Miran, who preferred to lower … by 1/2 percentage point …, and Austan D. Goolsbee and Jeffrey R.
// Schmid, who preferred no change …") → {"Stephen I. Miran", -50}, {"Austan D. Goolsbee", 0}, {"Jeffrey R. Schmid", 0}.
// bp: +n hike, -n cut, 0 no change, INT_MIN when the statement gives no preference.
struct Dissenter { QString name; int bp = INT_MIN; };
inline QList<Dissenter> fomcDissenters(const QString& votingAgainst) {
  auto names = [](QString who) {
    who = who.trimmed();
    while (who.endsWith('.') || who.endsWith(',')) who.chop(1);
    if (who.startsWith("and ")) who = who.mid(4);
    QStringList out;
    static const QRegularExpression sep(",\\s*(?:and\\s+)?|\\s+and\\s+");
    for (const QString& n : who.split(sep, Qt::SkipEmptyParts)) if (!n.trimmed().isEmpty()) out << n.trimmed();
    return out;
  };
  auto preference = [](const QString& clause) {
    static const QRegularExpression size("by ([\\d]+(?:[-\\s]\\d+)?/\\d+|[\\d.]+) percentage point");
    int bp = 25;
    if (const auto m = size.match(clause); m.hasMatch()) {
      const QString f = m.captured(1);
      double v = f.toDouble();   // "0.5"
      if (f.contains('/')) {     // "1/2", "1-1/4"
        const int sp = int(f.lastIndexOf(QRegularExpression("[-\\s]")));
        const QString fr = f.mid(sp + 1);
        v = (sp > 0 ? f.left(sp).toDouble() : 0) + fr.section('/', 0, 0).toDouble() / qMax(1.0, fr.section('/', 1, 1).toDouble());
      }
      if (v > 0) bp = qRound(v * 100);
    }
    if (clause.contains(QRegularExpression("\\bno change\\b|\\bto (?:maintain|leave)\\b"))) return 0;
    if (clause.contains(QRegularExpression("\\bto (?:raise|increase)\\b"))) return bp;
    if (clause.contains(QRegularExpression("\\bto (?:lower|reduce|cut)\\b"))) return -bp;
    return int(INT_MIN);
  };
  // "<names>, who preferred <clause>" groups, separated by ", and " / "; " / ". "
  static const QRegularExpression grp("^(.+?),?\\s+who\\s+(?:would have )?preferred\\b(.*?)(?:,\\s+and\\s+(?=[A-Z])|;\\s*(?:and\\s+)?|\\.(?=\\s|$)|$)");
  QList<Dissenter> out;
  QString rest = votingAgainst.trimmed();
  while (!rest.isEmpty()) {
    const auto m = grp.match(rest);
    if (!m.hasMatch() || m.capturedLength(0) == 0) break;
    const int bp = preference(m.captured(2));
    for (const QString& n : names(m.captured(1))) out << Dissenter{n, bp};
    rest = rest.mid(m.capturedEnd(0)).trimmed();
  }
  for (const QString& n : names(rest)) out << Dissenter{n, int(INT_MIN)};   // no "who preferred": names only
  return out;
}

// BoE: one day of the Bank of England's instantaneous OIS (SONIA) forward curve (months ahead → forward, %).
struct OisCurve { QDate date; QVector<double> months, fwd; };
// Forward at date t on the curve (flat before the first point, NaN beyond the last).
inline double oisForward(const OisCurve& c, const QDate& t) {
  if (c.months.isEmpty() || c.months.size() != c.fwd.size()) return NAN;
  const double m = c.date.daysTo(t) / 30.4375;
  if (m <= c.months.first()) return c.fwd.first();
  for (int k = 1; k < c.months.size(); ++k)
    if (m <= c.months[k]) { const double w = (m - c.months[k - 1]) / (c.months[k] - c.months[k - 1]); return c.fwd[k - 1] + w * (c.fwd[k] - c.fwd[k - 1]); }
  return NAN;
}
// Expected Bank-Rate level after each decision (ascending): the forward in the middle of the period up to the next
// decision (at most 45 days after it). *r0, the level the first move is measured from, is `sonia` (SONIA of the curve's
// day) when finite; otherwise the forward the day before the first decision — biased, the curve is flat below about
// one month, so it already holds a move priced for a meeting less than a month away (the odds then read Hold).
inline QVector<double> boeMeetingRates(const OisCurve& c, const QVector<QDate>& decisions, double sonia, double* r0) {
  QVector<double> r;
  if (decisions.isEmpty()) { *r0 = NAN; return r; }
  *r0 = std::isfinite(sonia) ? sonia : oisForward(c, decisions.first().addDays(-1));
  for (int i = 0; i < decisions.size(); ++i) {
    const QDate e = decisions[i];
    const int gap = i + 1 < decisions.size() ? int(e.daysTo(decisions[i + 1])) : 60;
    r << oisForward(c, e.addDays(std::min(gap / 2, 45)));
  }
  return r;
}
}
