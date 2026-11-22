#include "ai/Slots.h"
#include "ai/ModelHub.h"
#include "ai/ClaudeCode.h"
#include <QFileInfo>
#include "core/Paths.h"
#include "core/Settings.h"
#include <QStringList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QFile>
#ifdef __APPLE__
#include <sys/sysctl.h>
#elif defined(Q_OS_WIN)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace ai {

bool Slot::isIntegration() const { return repo == claudecode::kRepo; }
QString Slot::displayName() const {
  if (claudecode::isKey(repo, file)) return QStringLiteral("Claude Code");
  return QFileInfo(file).completeBaseName();
}

Slots& Slots::instance() { static Slots* s = new Slots; return *s; }

Slots::Slots() {
  m_slots.resize(kCount);
  m_skills.resize(kCount);
  m_asOf.resize(kCount);
  m_blind.resize(kCount);
  const QStringList bl = settings::value("ai.blind").toStringList();
  for (int i = 0; i < kCount && i < bl.size(); ++i) m_blind[i] = bl[i].toUInt();
  const QStringList ao = settings::value("ai.asof").toStringList();
  for (int i = 0; i < kCount && i < ao.size(); ++i) m_asOf[i] = qMax<qint64>(0, ao[i].toLongLong());
  const QStringList sk = settings::value("ai.skills").toStringList();
  for (int i = 0; i < kCount && i < sk.size(); ++i) m_skills[i] = sk[i].split(',', Qt::SkipEmptyParts);
  const QStringList saved = settings::value("ai.slots").toStringList();
  for (int i = 0; i < kCount; ++i) {
    m_slots[i].index = i;
    const QString k = i < saved.size() ? saved[i] : QString();
    if (k == QString(claudecode::kRepo) + "/" + claudecode::kFile) {   // "integration/claude-code": one "/" only
      m_slots[i].repo = claudecode::kRepo; m_slots[i].file = claudecode::kFile;
      continue;
    }
    const int cut = int(k.indexOf('/', k.indexOf('/') + 1));     // "owner/repo/file…"
    if (cut <= 0) continue;
    const QString repo = k.left(cut), file = k.mid(cut + 1);
    if (ModelHub::instance().isInstalled(repo, file)) { m_slots[i].repo = repo; m_slots[i].file = file; }
  }
  // a model deleted in AI › LLM leaves its cube (EJECT stops the process that held it)
  connect(&ModelHub::instance(), &ModelHub::installedChanged, this, [this] {
    for (int i = 0; i < kCount; ++i) {
      const Slot& s = m_slots[i];
      if (!s.isEmpty() && !claudecode::isKey(s.repo, s.file) && !ModelHub::instance().isInstalled(s.repo, s.file)) eject(i);
    }
  });
}

QString Slots::folder(int i) const { return paths::data(QString("ai/env-%1").arg(i + 1)); }

void Slots::save() {
  QStringList l;
  for (const auto& s : m_slots) l << s.key();
  settings::setValue("ai.slots", l);
}

void Slots::load(int i, const QString& repo, const QString& file) {
  if (i < 0 || i >= kCount) return;
  Slot& s = m_slots[i];
  if (s.repo == repo && s.file == file) return;
  s.repo = repo; s.file = file; s.loadedAt = QDateTime::currentDateTimeUtc();
  // the environment's own record of what it holds (its folder is private to it)
  paths::dropLink(folder(i) + "/environment.json");   // QSaveFile writes through a link
  QSaveFile f(folder(i) + "/environment.json");
  if (f.open(QIODevice::WriteOnly)) {
    f.write(QJsonDocument(QJsonObject{{"environment", i + 1}, {"repo", repo}, {"file", file},
                                      {"loadedAt", s.loadedAt.toString(Qt::ISODate)}}).toJson());
    f.commit();
  }
  save();
  emit changed(i);
}

void Slots::eject(int i) {
  if (i < 0 || i >= kCount || m_slots[i].isEmpty()) return;
  m_slots[i].repo.clear(); m_slots[i].file.clear(); m_slots[i].loadedAt = {};
  QFile::remove(folder(i) + "/environment.json");
  save();
  emit changed(i);
}

QStringList Slots::skills(int i) const { return i >= 0 && i < kCount ? m_skills[i] : QStringList(); }

void Slots::setSkill(int i, const QString& id, bool on) {
  if (i < 0 || i >= kCount || m_skills[i].contains(id) == on) return;
  if (on) m_skills[i] << id; else m_skills[i].removeAll(id);
  QStringList l;
  for (const auto& k : m_skills) l << k.join(',');
  settings::setValue("ai.skills", l);
  emit skillsChanged(i);
}

void Slots::setAsOf(int i, qint64 secs) {
  if (i < 0 || i >= kCount) return;
  secs = qMax<qint64>(0, secs);
  if (m_asOf[i] == secs) return;
  m_asOf[i] = secs;
  QStringList l;
  for (qint64 v : m_asOf) l << (v > 0 ? QString::number(v) : QString());
  settings::setValue("ai.asof", l);
  emit asOfChanged(i);
}

void Slots::setBlind(int i, quint32 seed) {
  if (i < 0 || i >= kCount || m_blind[i] == seed) return;
  m_blind[i] = seed;
  QStringList l;
  for (quint32 v : m_blind) l << (v ? QString::number(v) : QString());
  settings::setValue("ai.blind", l);
  emit asOfChanged(i);   // what the model sees changes as with the clock: a fresh context (Engine::syncClock)
}

qint64 Slots::memoryTotal() const {
#ifdef __APPLE__
  qint64 mem = 0; size_t len = sizeof(mem);
  if (sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) == 0) return mem;
#elif defined(Q_OS_WIN)
  // the Agents page's RAM gauge stayed hidden on Windows (0 = unknown)
  MEMORYSTATUSEX ms{};
  ms.dwLength = sizeof(ms);
  if (GlobalMemoryStatusEx(&ms)) return qint64(ms.ullTotalPhys);
#endif
  return 0;
}
qint64 Slots::memoryLoaded() const {
  qint64 sum = 0;
  const auto inst = ModelHub::instance().installed();
  for (const auto& s : m_slots)
    for (const auto& m : inst) if (!s.isEmpty() && m.repo == s.repo && m.file == s.file) sum += m.size;
  return sum;
}
}  // namespace ai
