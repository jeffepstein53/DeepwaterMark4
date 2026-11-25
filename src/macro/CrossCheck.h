#pragma once
#include <QDate>
#include <QList>
#include <QString>
#include <cmath>

// One figure against the other publishers that give it: the verdict, pure arithmetic (tests/calendar). What is compared
// with what is macro/KeyFigures.
namespace macro::check {

// Another source's figure. `ours`: what the shown source says for that same period (the shown figure, or an earlier point
// of its series when the other source has not published the latest period yet).
struct Reading {
  QString source;
  double value = NAN;
  double ours = NAN;
  QDate period;
  bool judged = true;    // false: shown beside, never an alarm (older than the market it prices, another definition…)
  QString note;          // why it is not judged, or when it was published
  double tol = NAN;      // its own tolerance; NaN: the item's
  double gap = NAN;      // how far apart the two are when it is not value − ours (the same thing counted from another base)
};

enum class Verdict {
  Agree,     // at least one other source gives the same figure, none gives another
  Differ,    // at least one other source gives another figure
  Alone,     // no other source to compare with
  Late,      // a newer figure has been published and is not shown
  Missing    // no figure at all
};

struct Item {
  QString ccy;           // "AUD"
  QString figure;        // English key: "Policy rate", "CPI y/y", "Core CPI y/y", "Unemployment", "GDP q/q", "Odds", "Meeting"
  double value = NAN;
  QString source;
  QDate period;          // first day of the month or quarter; the day for rates; the meeting for odds
  char freq = 'M';
  QString unit;          // "%", "bp", "" (a date: value = Julian day)
  int decimals = 1;
  double tolerance = 0.11;
  QList<Reading> others;
  QDate due;             // a later period already published, when there is one
  bool each = false;     // every publisher computes its own (odds): one that agrees settles it, the others are named apart

  Verdict verdict = Verdict::Missing;
  QDate on;              // the period the verdict was reached on
  bool earlier = false;  // on an earlier period than the one shown
  QString against;       // the sources that differ, with their figure ("BIS 4.0%"); on Agree: one that stands apart (`each`)
};

void judge(Item& it);

struct Summary { int figures = 0, agree = 0, differ = 0, alone = 0, late = 0, missing = 0; };
Summary summary(const QList<Item>& items);

QString figureText(const Item& it, double v);   // "4.9%", "-0.3 bp", "17 Dec 2026"
}
