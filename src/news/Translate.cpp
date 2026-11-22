#include "news/Translate.h"
#include "core/Paths.h"
#include "core/Process.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QMetaObject>

#ifdef Q_OS_IOS
// translate/DWTranslateIOS.swift (a static library built into the app)
extern "C" void dw_translate_start(void (*line)(const char*));
extern "C" void dw_translate_request(const char* jsonLine);
#endif

namespace news {

static QString cacheFile() { return paths::cache("news") + "/translate-en-fr.json"; }
static constexpr int kInflight = 4;   // texts handed to the helper at once (it translates them one by one)

Translate& Translate::instance() { static Translate* t = new Translate; return *t; }

Translate::Translate() {
  QFile f(cacheFile());
  if (f.open(QIODevice::ReadOnly)) {
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isArray()) {   // [[en, fr], …] oldest first
      for (const auto& v : doc.array()) {
        const auto pair = v.toArray();
        if (pair.size() == 2) remember(pair[0].toString(), pair[1].toString());
      }
    } else {               // files written before 2026-11-16: an object, no order
      const auto o = doc.object();
      for (auto it = o.begin(); it != o.end(); ++it) remember(it.key(), it.value().toString());
    }
  }
  m_save.setSingleShot(true); m_save.setInterval(3000);
  connect(&m_save, &QTimer::timeout, this, [this] {
    QJsonArray a;
    for (const auto& en : m_order) a.append(QJsonArray{en, m_cache.value(en)});
    QSaveFile f(cacheFile());
    if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(a).toJson(QJsonDocument::Compact)); f.commit(); }
  });
}

// Keeps the most recent translations only (headlines come and go): the oldest are dropped past kMaxCached.
void Translate::remember(const QString& en, const QString& fr) {
  static constexpr int kMaxCached = 6000;
  if (en.isEmpty()) return;
  if (!m_cache.contains(en)) m_order << en;
  m_cache.insert(en, fr);
  if (m_order.size() > kMaxCached + kMaxCached / 10) {
    const qsizetype drop = m_order.size() - kMaxCached;
    for (qsizetype i = 0; i < drop; ++i) m_cache.remove(m_order[i]);
    m_order.remove(0, drop);
  }
}

QString Translate::stateText() const {
  switch (m_state) {
#ifdef Q_OS_IOS
    case State::Ready: return QCoreApplication::translate("news", "Translated on this device (Apple Translation)");
    case State::Starting: return QCoreApplication::translate("news", "Starting the on-device translator…");
    case State::MissingPack: return QCoreApplication::translate("news", "French language not downloaded: Settings › Apps › Translate › Downloaded Languages. Showing the original English.");
    case State::Unsupported: return QCoreApplication::translate("news", "English → French translation needs iOS 26 or later on this device. Showing the original English.");
    case State::NoHelper:
    case State::Failed: return QCoreApplication::translate("news", "The on-device translator stopped. Showing the original English.");
#else
    case State::Ready: return QCoreApplication::translate("news", "Translated on this Mac (Apple Translation)");
    case State::Starting: return QCoreApplication::translate("news", "Starting the on-device translator…");
    case State::MissingPack: return QCoreApplication::translate("news", "French language pack not installed: System Settings › General › Language & Region › "
                                    "Translation Languages. Showing the original English.");
    case State::Unsupported: return QCoreApplication::translate("news", "English → French translation is not available on this Mac. Showing the original English.");
    case State::NoHelper: return QCoreApplication::translate("news", "Translation helper (dw-translate) missing from the app. Showing the original English.");
    case State::Failed: return QCoreApplication::translate("news", "The on-device translator stopped. Showing the original English.");
#endif
  }
  return {};
}

QString Translate::cached(const QString& text) const { return m_cache.value(text.trimmed()); }

void Translate::start() {
#ifdef Q_OS_IOS
  if (m_inProcess) return;
  m_inProcess = true;
  // lines come from the translator's own threads: copied, then handled on the GUI thread
  dw_translate_start([](const char* line) {
    const QByteArray copy = QByteArray(line) + '\n';
    QMetaObject::invokeMethod(&Translate::instance(), [copy] { Translate::instance().consume(copy); }, Qt::QueuedConnection);
  });
  return;
#endif
  if (m_proc) return;
  const QString exe = QCoreApplication::applicationDirPath() + "/dw-translate";
  if (!QFileInfo::exists(exe)) { m_state = State::NoHelper; emit stateChanged(); finishWaiters(); return; }
  m_proc = new dw::Process(this);
  m_proc->setProgram(exe);
  m_proc->setArguments({"en", "fr"});
  m_proc->setProcessChannelMode(dw::Process::SeparateChannels);
  connect(m_proc, &dw::Process::readyReadStandardOutput, this, &Translate::onOutput);
  connect(m_proc, &dw::Process::errorOccurred, this, [this](dw::Process::ProcessError) {
    if (m_state == State::Starting || m_state == State::Ready) { m_state = State::Failed; emit stateChanged(); }
    finishWaiters();
  });
  connect(m_proc, &dw::Process::finished, this, [this] {
    if (m_state == State::Ready || m_state == State::Starting) { m_state = State::Failed; emit stateChanged(); }
    m_inflight.clear(); m_queue.clear();
    finishWaiters();
  });
  m_proc->start();
}

void Translate::request(const QStringList& texts, QObject* ctx, std::function<void()> done) {
  QStringList want;
  for (QString t : texts) {
    t = t.trimmed();
    if (t.isEmpty() || m_cache.contains(t) || m_failed.contains(t)) continue;
    want << t;
  }
  if (want.isEmpty() || !(usable())) {
    if (done) QTimer::singleShot(0, ctx ? ctx : this, [done] { done(); });
    return;
  }
  QSet<QString> busy(m_queue.cbegin(), m_queue.cend());
  for (const auto& v : m_inflight) busy.insert(v);
  for (const auto& t : want) if (!busy.contains(t)) { m_queue << t; busy.insert(t); }
  if (done) m_waiters << Waiter{ctx, ctx != nullptr, want, std::move(done)};
  start();
  pump();
}

void Translate::pump() {
  if ((!m_proc && !m_inProcess) || m_state != State::Ready) return;
  while (m_inflight.size() < kInflight && !m_queue.isEmpty()) {
    const QString t = m_queue.takeFirst();
    const int id = m_nextId++;
    m_inflight.insert(id, t);
    const QByteArray line = QJsonDocument(QJsonObject{{"id", id}, {"text", t}}).toJson(QJsonDocument::Compact);
#ifdef Q_OS_IOS
    dw_translate_request(line.constData());
#else
    m_proc->write(line + "\n");
#endif
  }
}

void Translate::onOutput() { consume(m_proc->readAllStandardOutput()); }

void Translate::consume(const QByteArray& chunk) {
  m_buf += chunk;
  bool any = false;
  for (int nl; (nl = m_buf.indexOf('\n')) >= 0;) {
    const QByteArray line = m_buf.left(nl); m_buf.remove(0, nl + 1);
    const auto o = QJsonDocument::fromJson(line).object();
    if (o.contains("status")) {
      const QString s = o["status"].toString();
      m_state = s == "installed" ? State::Ready : s == "supported" ? State::MissingPack : State::Unsupported;
      emit stateChanged();
      if (m_state != State::Ready) { m_queue.clear(); finishWaiters(); }
      continue;
    }
    const int id = o["id"].toInt();
    const QString src = m_inflight.take(id);
    if (src.isEmpty()) continue;
    if (o.contains("text")) { remember(src, o["text"].toString()); any = true; }
    else m_failed.insert(src);
  }
  if (any) { scheduleSave(); emit progressed(); }
  finishWaiters();
  pump();
}

void Translate::finishWaiters() {
  const bool dead = !usable();
  for (int i = 0; i < m_waiters.size();) {
    Waiter& w = m_waiters[i];
    bool complete = dead;
    if (!complete) {
      complete = true;
      for (const auto& t : w.texts) if (!m_cache.contains(t) && !m_failed.contains(t)) { complete = false; break; }
    }
    if (!complete) { ++i; continue; }
    Waiter done = m_waiters.takeAt(i);
    if (done.hasCtx && !done.ctx) continue;
    if (done.done) done.done();
  }
}

void Translate::scheduleSave() { if (!m_save.isActive()) m_save.start(); }
}
