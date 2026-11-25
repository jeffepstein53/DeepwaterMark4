#pragma once
#include "macro/MacroStore.h"
#include "macro/Official.h"

// National series laid over the international compilations (BIS, OECD): the statistics office's own figure, read the day
// it is published, where the compilation comes weeks later or stopped. And the release calendar's figure for the few days
// when neither has the new period yet. Used by MacroStore; exposed for the tests.
namespace macro::national {

struct Def {
  QString iso2;
  Ind ind;
  official::Agency agency;
  QString key;
  bool yoy = false;      // index level: the figure is its 12-month change
  QString dataset;       // "CPI-U, y/y"
  bool instead = false;  // another definition than the compilation's (trimmed mean, less fresh food): nothing is spliced
};
const QList<Def>& defs();
int ttlSec(official::Agency a);   // how old a cached copy may be (BLS allows 25 keyless requests a day)

// Official answer → series (12-month change computed when the definition says so).
Series build(const Def& d, const official::Answer& a);

// The national series with the compilation's older history before it. The compilation stays when it ends later, or (other
// frequency) when the national series is empty.
Series overlay(const Series& base, const Series& nat, bool instead);

// `ccy` and `title` as in the calendar ("USD", "Prelim GDP q/q"); `value` in percent. Invalid when the event is not one of
// the indicators above. US GDP is published annualised: the quarter's own growth is computed.
CalendarFigure flashOf(const QString& ccy, const QString& title, double value, const QDateTime& releasedUtc, const QString& source);
// The same for another figure of a release ("New Zealand Retail Sales YoY").
CalendarFigure flashOfDetail(const QString& ccy, const QString& name, double value, const QDateTime& releasedUtc, const QString& source);
// The month or quarter a release measures.
QDate periodOf(const QString& ccy, const QString& title, char freq, const QDateTime& releasedUtc);
// The series with the calendar's figure when it is for a later period (or, for GDP, a newer estimate of the same one).
Series withFlash(const Series& s, const CalendarFigure& f, const QDateTime& nowUtc);
}
