// Tests of the two pace rules (no network, no window): news/ActualsPace.h (when the calendar's actuals feed is asked)
// and positioning/CotPace.h (when a COT report is asked again).
#include "news/ActualsPace.h"
#include "positioning/CotPace.h"
#include <QCoreApplication>
#include <QTimeZone>
#include <cstdio>

static int g_ok = 0, g_bad = 0;
static void check(const char* what, bool ok) {
  if (ok) ++g_ok; else ++g_bad;
  std::printf("%s  %s\n", ok ? "ok " : "BAD", what);
}
static QDateTime utc(const char* s) { QDateTime d = QDateTime::fromString(s, "yyyy-MM-dd HH:mm:ss"); d.setTimeZone(QTimeZone::UTC); return d; }
static QDate day(const char* s) { return QDate::fromString(s, "yyyy-MM-dd"); }

static void actuals() {
  using namespace news::actualspace;
  const QList<QDateTime> none;
  const auto last = utc("2026-11-25 13:24:00");
  check("no release: 5 min after the last request", nextPoll(last, utc("2026-11-25 13:24:01"), none) == utc("2026-11-25 13:29:00"));
  check("no request yet: now", nextPoll({}, utc("2026-11-25 13:24:01"), none) == utc("2026-11-25 13:24:01"));
  check("late: now, never in the past", nextPoll(last, utc("2026-11-25 13:40:00"), none) == utc("2026-11-25 13:40:00"));
  const QList<QDateTime> nfp{utc("2026-11-25 13:30:00")};
  check("the poll at 13:29 is held for the 13:30 release", nextPoll(last, utc("2026-11-25 13:24:01"), nfp) == utc("2026-11-25 13:31:15"));
  check("a poll at 13:26:14 leaves room for it: not held", nextPoll(utc("2026-11-25 13:21:14"), utc("2026-11-25 13:21:20"), nfp) == utc("2026-11-25 13:26:14"));
  check("a poll at 13:26:16 does not: held", nextPoll(utc("2026-11-25 13:21:16"), utc("2026-11-25 13:21:20"), nfp) == utc("2026-11-25 13:31:15"));
  check("the last request too late for it: as soon as the limit allows", nextPoll(utc("2026-11-25 13:28:00"), utc("2026-11-25 13:28:01"), nfp) == utc("2026-11-25 13:33:00"));
  const QList<QDateTime> two{utc("2026-11-25 13:33:00"), utc("2026-11-25 13:30:00")};
  check("a release out and not asked yet: the poll is not held for the next one",
        nextPoll(utc("2026-11-25 13:31:15"), utc("2026-11-25 13:32:00"), {utc("2026-11-25 13:31:00"), utc("2026-11-25 13:37:00")}) == utc("2026-11-25 13:36:15"));
  check("the same when a page opens: the usual age", ttlSec(utc("2026-11-25 13:31:15"), utc("2026-11-25 13:36:00"), {utc("2026-11-25 13:31:00"), utc("2026-11-25 13:37:00")}) == 295);
  check("two releases: the first one", nextPoll(last, utc("2026-11-25 13:24:01"), two) == utc("2026-11-25 13:31:15"));
  // a whole day of releases, stepped: the limit of the source is never broken and no release waits more than it must
  QList<QDateTime> dayRel;
  for (int m : {0, 30, 45, 120, 121, 150, 480, 481, 487, 600, 615, 900, 905, 1200}) dayRel << utc("2026-11-25 00:00:00").addSecs(m * 60 + 7);
  QDateTime t = utc("2026-11-25 00:00:00"), prev;
  bool limit = true; qint64 worst = 0; int n = 0;
  QHash<QDateTime, QDateTime> seen;
  while (t < utc("2026-11-26 00:00:00")) {
    if (prev.isValid() && prev.secsTo(t) < kEverySec) limit = false;
    for (const auto& r : dayRel) if (!seen.contains(r) && t >= r.addSecs(kLagSec)) seen[r] = t;
    prev = t; ++n;
    QList<QDateTime> awaited; for (const auto& r : dayRel) if (!seen.contains(r) || seen.value(r).secsTo(prev) < 60) awaited << r;
    t = nextPoll(prev, prev.addSecs(1), awaited);
  }
  for (const auto& r : dayRel) worst = std::max(worst, r.secsTo(seen.value(r)));
  check("a day: never two requests within 5 min", limit);
  check("a day: every release found within 5 min 75 s", worst <= kEverySec + kLagSec);
  check("a day: no more requests than a plain 5-min timer", n <= 289);
  int lone = 0;   // releases with nothing else within 10 min before: found exactly at the lag
  for (int i = 0; i < dayRel.size(); ++i) {
    bool alone = dayRel[i] > utc("2026-11-25 00:20:00");
    for (int k = 0; k < dayRel.size(); ++k) if (k != i && std::abs(dayRel[k].secsTo(dayRel[i])) < 900) alone = false;
    if (alone && dayRel[i].secsTo(seen.value(dayRel[i])) == kLagSec) ++lone; else if (alone) lone = -1000;
  }
  check("a day: an isolated release is found 75 s after it", lone > 0);
  // the copy in hand
  check("usual age accepted: 295 s", ttlSec(last, utc("2026-11-25 13:26:00"), none) == 295);
  check("2 min before a release, a 4-min-old copy is kept", ttlSec(utc("2026-11-25 13:24:00"), utc("2026-11-25 13:28:00"), nfp) == 600);
  check("the same with an 11-min-old copy: asked", ttlSec(utc("2026-11-25 13:17:00"), utc("2026-11-25 13:28:00"), nfp) == 295);
  check("at the planned moment: asked", ttlSec(utc("2026-11-25 13:24:00"), utc("2026-11-25 13:31:15"), nfp) == 295);
  check("long before the release: the usual age", ttlSec(utc("2026-11-25 13:10:00"), utc("2026-11-25 13:14:00"), nfp) == 295);
}

static void cot() {
  using namespace positioning::cotpace;
  // winter: 15:30 New York = 20:30 UTC; summer: 19:30 UTC
  check("winter, Friday 20:30 UTC: the previous week's still expected", expectedReport(utc("2026-11-20 20:30:30")) == day("2026-11-10"));
  check("winter, Friday 20:31 UTC: this week's expected", expectedReport(utc("2026-11-20 20:31:00")) == day("2026-11-17"));
  check("summer, Friday 19:31 UTC: this week's expected", expectedReport(utc("2026-10-16 19:31:00")) == day("2026-10-13"));
  check("summer, Friday 19:29 UTC: not yet", expectedReport(utc("2026-10-16 19:29:00")) == day("2026-10-06"));
  check("the next Wednesday: last Friday's", expectedReport(utc("2026-11-25 00:10:00")) == day("2026-11-17"));
  check("Saturday and Sunday: Friday's", expectedReport(utc("2026-11-22 12:00:00")) == day("2026-11-17"));
  check("in hand: 6 h", maxAgeSec(day("2026-11-17"), utc("2026-11-25 00:10:00")) == 6 * 3600);
  check("in hand, as of a Monday (Tuesday a holiday): 6 h", maxAgeSec(day("2026-11-16"), utc("2026-11-25 00:10:00")) == 6 * 3600);
  check("nothing in hand: 6 h (then checked once read)", maxAgeSec({}, utc("2026-11-20 20:40:00")) == 6 * 3600);
  check("Friday 20:31 UTC, report missing: what was fetched before is old", maxAgeSec(day("2026-11-10"), utc("2026-11-20 20:31:00")) == 1);
  check("Friday 20:35, missing: what was fetched before 20:31 is old", maxAgeSec(day("2026-11-10"), utc("2026-11-20 20:35:00")) == 240);
  check("Friday 21:00, missing: every 10 min", maxAgeSec(day("2026-11-10"), utc("2026-11-20 21:00:00")) == 600);
  check("Friday 23:32 UTC, missing: every hour", maxAgeSec(day("2026-11-10"), utc("2026-11-20 23:32:00")) == 3600);
  check("Saturday, missing: every hour", maxAgeSec(day("2026-11-10"), utc("2026-11-21 20:45:00")) == 3600);
  // Veterans Day (Wednesday 2026-11-11): the report of the 10th came out on Monday the 16th at 20:30:19 UTC
  check("holiday week, Monday 20:31 UTC: asked again at once", maxAgeSec(day("2026-11-03"), utc("2026-11-16 20:31:20")) <= 20);
  check("holiday week, Monday morning: every hour", maxAgeSec(day("2026-11-03"), utc("2026-11-16 14:00:00")) == 3600);
  check("holiday week, once the 10th's report is in: 6 h", maxAgeSec(day("2026-11-10"), utc("2026-11-16 20:40:00")) == 6 * 3600);
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  actuals();
  cot();
  std::printf("%d checks passed, %d failed\n", g_ok, g_bad);
  return g_bad ? 1 : 0;
}
