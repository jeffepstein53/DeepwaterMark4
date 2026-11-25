#pragma once
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QTimer>

// What is wrong with the data right now, in one list: a key figure that another source gives differently (or that is late),
// a source that has stopped answering, a result the calendar still misses. Shown by the header's ⚠, at the top of
// Sources › Data Sources and by Claude's get_data_checks.
namespace app {

struct Issue {
  enum Kind { Network, Source, Figure, Odds, Result };
  Kind kind = Figure;
  QString key;            // the same issue from one look to the next
  QString title;          // "AUD · Inflation (12 months)", "Yahoo Finance"
  QString detail;         // "Two sources differ: 3.8% here, BIS 4.0% (Oct 2026)"
  QDateTime since;        // UTC, when Deepwater saw it first
  QString page;           // where to look, "economy/rates"
};

class Watchdog : public QObject {
  Q_OBJECT
public:
  static Watchdog& instance();
  void start();                             // once, shortly after launch
  bool started() const { return m_started; }
  QList<Issue> issues() const { return m_issues; }
  void look();                              // now

signals:
  void changed();
  void appeared(const app::Issue& i);       // not there at the previous look

private:
  Watchdog();
  void later() { if (!m_soon.isActive()) m_soon.start(); }
  QList<Issue> m_issues;
  QHash<QString, QDateTime> m_since;
  QTimer m_timer, m_soon;
  QDateTime m_from;
  bool m_started = false;
};
}
