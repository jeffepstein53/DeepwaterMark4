#pragma once
#include <QDateTime>
#include <QList>
#include <algorithm>

// When to ask the calendar's actuals feed (Myfxbook) again. Pure arithmetic, tested by tests/freshness.
//
// The source asks for one request per 5 minutes at most; a fixed 5-minute timer started at launch finds a release up to
// 5 minutes after it is out. Within the same limit, the request is placed just after the release instead: a poll that
// would leave less than 5 minutes before that moment is held until it. No request is added, one is moved; and none is
// moved while a release already out has not been asked for: it never waits longer than with the plain timer.
namespace news::actualspace {
constexpr int kEverySec = 300;        // never two requests closer than this
constexpr int kLagSec = 75;           // the feed carries a figure about a minute after its release (54 s on 2026-11-22)
constexpr int kTtlSec = kEverySec - 5;   // a copy younger than this is served without a request

// A release out (its lag included) that no request has come after yet.
inline bool pending(const QDateTime& last, const QDateTime& at, const QList<QDateTime>& releases) {
  for (const QDateTime& r : releases)
    if (const QDateTime t = r.addSecs(kLagSec); t > last && t < at) return true;
  return false;
}

// Next request. `last`: the last one (UTC, invalid: none yet). `releases`: the awaited release times (UTC, any order),
// the ones of the last ten minutes included.
inline QDateTime nextPoll(const QDateTime& last, const QDateTime& now, const QList<QDateTime>& releases) {
  const QDateTime base = last.isValid() ? std::max(now, last.addSecs(kEverySec)) : now;
  QDateTime best;
  if (last.isValid() && pending(last, base, releases)) return base;
  for (const QDateTime& r : releases) {
    const QDateTime target = r.addSecs(kLagSec);
    if (target < base || target >= base.addSecs(kEverySec)) continue;   // the base poll is after it, or leaves room for it
    if (!best.isValid() || target < best) best = target;
  }
  return best.isValid() ? best : base;
}

// Age under which the copy in hand is good enough for a request made now (a page opened, Claude asking): the usual one,
// or longer when a request now would forbid the one planned just after a release.
inline int ttlSec(const QDateTime& last, const QDateTime& now, const QList<QDateTime>& releases) {
  if (!last.isValid() || pending(last, now, releases)) return kTtlSec;
  for (const QDateTime& r : releases) {
    const qint64 wait = now.secsTo(r.addSecs(kLagSec));
    if (wait > 2 && wait < kEverySec && last.secsTo(now) < 2 * kEverySec) return 2 * kEverySec;
  }
  return kTtlSec;
}
}
