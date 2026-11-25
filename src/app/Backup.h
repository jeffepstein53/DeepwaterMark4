#pragma once
#include <QDateTime>
#include <QObject>
#include <QTimer>

// A copy of the settings and of the user's own records (journal, calendar results, wire, research, sentiment) once a day,
// in a folder of the user's home: Deepwater-sauvegardes/2026-11-25/. The last fourteen days are kept. Not copied: what
// can be downloaded again or is too large (technical data, macro data, caches) and Claude's connection.
// The Myfxbook session in it is encrypted for this Windows account (core/Secret).
namespace app {
class Backup : public QObject {
  Q_OBJECT
public:
  static Backup& instance();
  void start();                              // once: a first look two minutes after the launch, then every hour
  void runNow();                             // Options; changed() when it is done
  bool running() const { return m_running; }
  QString error() const { return m_error; }  // of the last one; empty: it went well
  static QString folder();
  static QDateTime last();                   // local time of the newest copy; invalid: none yet
  // The work itself, off the interface's thread: copies, checks the copy, removes the copies too old. Empty: done.
  static QString run(const QString& dataDir, const QString& folder, const QDate& day, int keep = 14);

signals:
  void changed();

private:
  Backup();
  void look();
  QTimer m_timer;
  bool m_started = false, m_running = false;
  QString m_error;
};
}
