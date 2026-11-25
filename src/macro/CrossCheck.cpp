#include "macro/CrossCheck.h"
#include "core/I18n.h"
#include <QStringList>

namespace macro::check {

QString figureText(const Item& it, double v) {
  if (!std::isfinite(v)) return QStringLiteral("—");
  if (it.figure == "Meeting") return i18n::locale().toString(QDate::fromJulianDay(qint64(v)), "d MMM yyyy");
  QString s = QString::number(v, 'f', it.decimals);
  if (s.startsWith('-') && s.toDouble() == 0) s.remove(0, 1);
  return it.unit == "%" ? s + "%" : it.unit.isEmpty() ? s : s + " " + it.unit;
}

void judge(Item& it) {
  it.against.clear(); it.on = it.period; it.earlier = false;
  if (!std::isfinite(it.value)) { it.verdict = Verdict::Missing; return; }
  // the readings of the period shown; none: those of the newest earlier period there is
  QDate on;
  for (const Reading& r : it.others)
    if (r.judged && std::isfinite(r.value) && std::isfinite(r.ours) && r.period == it.period) on = it.period;
  if (!on.isValid())
    for (const Reading& r : it.others)
      if (r.judged && std::isfinite(r.value) && std::isfinite(r.ours) && r.period < it.period && (!on.isValid() || r.period > on)) on = r.period;
  QStringList diff;
  int same = 0;
  if (on.isValid())
    for (const Reading& r : it.others) {
      if (!r.judged || r.period != on || !std::isfinite(r.value) || !std::isfinite(r.ours)) continue;
      if (std::fabs(std::isfinite(r.gap) ? r.gap : r.value - r.ours) > (std::isfinite(r.tol) ? r.tol : it.tolerance) + 1e-9) diff << r.source + " " + figureText(it, r.value);
      else ++same;
    }
  it.on = on.isValid() ? on : it.period;
  it.earlier = on.isValid() && on != it.period;
  it.against = diff.join(", ");
  if (it.due.isValid() && it.due > it.period) it.verdict = Verdict::Late;
  else if (!diff.isEmpty() && !(it.each && same > 0)) it.verdict = Verdict::Differ;
  else it.verdict = same > 0 ? Verdict::Agree : Verdict::Alone;
}

Summary summary(const QList<Item>& items) {
  Summary s;
  for (const Item& it : items) {
    ++s.figures;
    switch (it.verdict) {
      case Verdict::Agree: ++s.agree; break;
      case Verdict::Differ: ++s.differ; break;
      case Verdict::Alone: ++s.alone; break;
      case Verdict::Late: ++s.late; break;
      case Verdict::Missing: ++s.missing; break;
    }
  }
  return s;
}
}
