#include "net/Sources.h"

namespace sources {
bool isSilent(const QList<Health>& hosts, const QDateTime& now) {
  bool asked = false;
  QDateTime lastOk;
  for (const Health& h : hosts) {
    if (h.lastOk > lastOk) lastOk = h.lastOk;
    const bool failing = h.lastFail.isValid() && h.lastFail > h.lastOk && h.failStreak >= 2 && h.lastFail.secsTo(now) < 3 * 3600;
    const bool answering = h.lastOk.isValid() && h.lastOk >= h.lastFail && h.lastOk.secsTo(now) < 3 * 3600;
    if (answering) return false;   // one of its hosts answers
    asked |= failing;
  }
  return asked && lastOk.isValid() && lastOk.secsTo(now) >= 600 && lastOk.daysTo(now) <= 14;
}
}
