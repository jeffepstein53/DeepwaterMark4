#pragma once
#include <QObject>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QPointer>
#include <functional>

#include "core/Process.h"

// On-device EN→FR translation of headlines and article text (News Feed; usable by Squawk, Session Recap).
//
//   auto& tr = news::Translate::instance();
//   QString fr = tr.cached(title);                      // empty if not translated yet
//   tr.request(titles, this, [this] { update(); });     // translates what is missing, then calls back once
//
// Uses Apple's Translation framework through a small Swift helper process (dw-translate, next to the app's
// executable) on macOS; on iOS, where an app cannot start one, the same protocol runs in process
// (translate/DWTranslateIOS.swift, built into the app). The GUI thread never waits. Results are cached on disk (paths::cache("news")). When the French
// language pack is not downloaded, state() is MissingPack and nothing is translated: pages show the English text
// and say so. The app UI itself stays in English; only news text is translated.
namespace news {

class Translate : public QObject {
  Q_OBJECT
public:
  enum class State { Starting, Ready, MissingPack, Unsupported, NoHelper, Failed };
  static Translate& instance();

  State state() const { return m_state; }
  QString stateText() const;                   // one line for tooltips / captions
  bool usable() const { return m_state == State::Ready || m_state == State::Starting; }

  QString cached(const QString& text) const;   // translation, or empty
  QString text(const QString& en) const { const QString t = cached(en); return t.isEmpty() ? en : t; }
  // Queue the texts that are not cached yet; `done` runs once on the GUI thread when all of them are answered
  // (or failed). Skipped if ctx is destroyed. Calls done immediately (async) when nothing is missing.
  void request(const QStringList& texts, QObject* ctx, std::function<void()> done = {});
  int pending() const { return int(m_inflight.size() + m_queue.size()); }

signals:
  void stateChanged();
  void progressed();                           // some translations arrived

private:
  Translate();
  void start();
  void pump();
  void onOutput();
  void consume(const QByteArray& chunk);       // protocol lines (the helper's output, or the in-process translator's)
  void finishWaiters();
  void scheduleSave();

  struct Waiter { QPointer<QObject> ctx; bool hasCtx; QStringList texts; std::function<void()> done; };
  State m_state = State::Starting;
  dw::Process* m_proc = nullptr;
  bool m_inProcess = false;                    // iOS: the translator built into the app was started
  QByteArray m_buf;
  void remember(const QString& en, const QString& fr);
  QHash<QString, QString> m_cache;             // en → fr
  QStringList m_order;                         // m_cache keys, oldest first (the cache is capped)
  QStringList m_queue;                         // texts waiting to be sent
  QHash<int, QString> m_inflight;              // id → text
  QSet<QString> m_failed;
  QList<Waiter> m_waiters;
  int m_nextId = 1;
  QTimer m_save;
};
}
