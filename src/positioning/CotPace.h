#pragma once
#include <QDate>
#include <QDateTime>
#include <QTimeZone>
#include <algorithm>

// How old a COT answer may be before it is asked again. Pure arithmetic, tested by tests/freshness.
//
// The CFTC publishes on Friday at 15:30 New York time the positions of that week's Tuesday (the API's own timestamps:
// 15:30:08 to 15:30:34); after a bank holiday it is a following day, at the same hour (2026-11-10's came out on Monday
// the 16th). The report in hand being the one expected, 6 hours; that report missing, 10 minutes during the three hours
// after 15:30 on a weekday, an hour the rest of the time.
namespace positioning::cotpace {
constexpr int kUpToDateSec = 6 * 3600;
constexpr int kInWindowSec = 10 * 60;
constexpr int kMissingSec = 3600;

// The Tuesday of the last report that should be out at `now` (one minute after 15:30 on Friday, New York).
inline QDate expectedReport(const QDateTime& now) {
  const QDateTime ny = now.toTimeZone(QTimeZone("America/New_York"));
  QDate friday = ny.date().addDays(-((ny.date().dayOfWeek() - 5 + 7) % 7));   // this day or the last Friday before it
  if (friday == ny.date() && ny.time() < QTime(15, 31)) friday = friday.addDays(-7);
  return friday.addDays(-3);
}

// True when `last` (date of the newest report in hand) is the expected one: a holiday moves the as-of day to the Monday.
inline bool upToDate(const QDate& last, const QDateTime& now) {
  return last.isValid() && last >= expectedReport(now).addDays(-1);
}

inline int maxAgeSec(const QDate& last, const QDateTime& now) {
  if (!last.isValid() || upToDate(last, now)) return kUpToDateSec;
  const QDateTime ny = now.toTimeZone(QTimeZone("America/New_York"));
  if (ny.date().dayOfWeek() > 5) return kMissingSec;
  const QDateTime start(ny.date(), QTime(15, 31), ny.timeZone());
  const qint64 since = start.secsTo(ny);
  if (since < 0 || since > 3 * 3600) return kMissingSec;
  // a copy fetched before 15:31 is old at once, then one request per 10 minutes
  return int(std::clamp<qint64>(since, 1, kInWindowSec));
}
}
