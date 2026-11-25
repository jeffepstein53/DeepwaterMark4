#pragma once
#include "app/Watchdog.h"
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QTimer>

namespace news { struct Event; }

// Alerts on the data (Options > Alerts): the result of a high-impact release as it arrives, and what the Watchdog finds
// wrong (a source that stopped answering, two sources that differ, a late or missing figure). A Windows notification and
// a sound. Never on prices: those belong to the charts. Several at once are one notification; a problem already told is
// not told again for a week, even after a restart.
namespace app {
class Alerts : public QObject {
  Q_OBJECT
public:
  enum Kind { Results, Problems, Sound };
  static Alerts& instance();
  void start();                              // once, after the first page (main.cpp)
  static bool on(Kind k);                    // on by default
  static void setOn(Kind k, bool v);
  void tryOne();                             // Options: shows one now, with its sound
  // The sentence of a result: "USD · Core PCE Price Index m/m: 0.3%" / "Forecast 0.3% · previous 0.2%".
  static QPair<QString, QString> resultText(const news::Event& e);

signals:
  void openRequested(const QString& page);   // the notification was clicked

private:
  Alerts();
  void result(const news::Event& e);
  void problem(const Issue& i);
  void flushResults();
  void flushProblems();
  void send(const QString& title, const QString& text, const QString& page, bool warning);
  void loadTold();
  void saveTold() const;
  struct Note { QString title, text; };
  QHash<QString, QString> m_figures;         // event key → the figure told
  QList<Note> m_results;
  QList<Issue> m_problems;
  QHash<QString, QDateTime> m_told;          // problem key → when it was told (UTC), kept on disk
  QTimer m_resultTimer, m_problemTimer;
  bool m_started = false;
};
}
