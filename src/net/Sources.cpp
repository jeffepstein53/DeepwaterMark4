#include "net/Sources.h"
#include <QCoreApplication>
#include <QRegularExpression>
#include <algorithm>
#include <QHash>
#include <QMutex>
#include <QSet>
#include <QTimer>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include "core/Paths.h"
#include "core/Settings.h"

namespace sources {
namespace {
// paceByHost: the largest pace of every declaration naming the host (a host shared by several sources, e.g.
// www.myfxbook.com for the calendar RSS and the community outlook, is paced for the most demanding of them)
struct Reg { QMutex m; QList<Source> list; QHash<QString, int> byHost; QHash<QString, int> paceByHost, parallelByHost; };
Reg& reg() { static Reg r; return r; }
}
void declare(const Source& s) {
  auto& r = reg(); QMutexLocker l(&r.m);
  for (const auto& h : s.hosts) {
    int& p = r.paceByHost[h.toLower()];
    p = std::max(p, s.paceMs);
    const auto par = r.parallelByHost.constFind(h.toLower());
    r.parallelByHost.insert(h.toLower(), par == r.parallelByHost.cend() ? std::max(1, s.parallel) : std::min(*par, std::max(1, s.parallel)));
  }
  // the same source declared by two services (OECD: macro and calendar history; FinancialJuice: wire and feed):
  // one entry whose hosts and pages are the union of both declarations
  for (int i = 0; i < r.list.size(); ++i) {
    auto& e = r.list[i];
    if (e.id != s.id) continue;
    for (const auto& h : s.hosts) if (!e.hosts.contains(h)) { e.hosts << h; r.byHost.insert(h.toLower(), i); }
    for (const auto& p : s.pages) if (!e.pages.contains(p)) e.pages << p;
    if (e.data != s.data && !s.data.isEmpty() && !e.data.contains(s.data)) e.data += "; " + s.data;
    return;
  }
  r.list.append(s);
  for (const auto& h : s.hosts) r.byHost.insert(h.toLower(), r.list.size() - 1);
}
const Source* forHost(const QString& host) {
  auto& r = reg(); QMutexLocker l(&r.m);
  auto it = r.byHost.constFind(host.toLower());
  return it == r.byHost.cend() ? nullptr : &r.list[*it];
}
int paceForHost(const QString& host) {
  auto& r = reg(); QMutexLocker l(&r.m);
  return r.paceByHost.value(host.toLower(), -1);
}
int parallelForHost(const QString& host) {
  auto& r = reg(); QMutexLocker l(&r.m);
  return r.parallelByHost.value(host.toLower(), 1);
}
QList<Source> all() { auto& r = reg(); QMutexLocker l(&r.m); return r.list; }
QString shownPublisher(const QString& p) { return p.isEmpty() ? p : QCoreApplication::translate("sourcecat", p.toUtf8().constData()); }
Source shown(const Source& s) {
  const auto tx = [](const QString& t) { return t.isEmpty() ? t : QCoreApplication::translate("sourcecat", t.toUtf8().constData()); };
  Source o = s;
  o.publisher = tx(s.publisher);
  o.terms = tx(s.terms);
  o.data = tx(s.data);
  if (o.data == s.data) {   // declared by two services: their texts are joined
    QStringList parts = s.data.split("; ");
    for (QString& p : parts) p = tx(p);
    o.data = parts.join("; ");
  }
  return o;
}
const Source* byId(const QString& id) {
  auto& r = reg(); QMutexLocker l(&r.m);
  for (const auto& e : r.list) if (e.id == id) return &e;
  return nullptr;
}

// ---------------------------------------------------------------------------------------------- health
namespace {
struct HealthStore {
  QHash<QString, Health> byHost;
  bool loaded = false;
  QTimer save, notify;
  HealthStore() {
    save.setSingleShot(true); save.setInterval(5000);
    QObject::connect(&save, &QTimer::timeout, [this] { write(); });
    notify.setSingleShot(true); notify.setInterval(400);
    QObject::connect(&notify, &QTimer::timeout, [] { emit Notifier::instance().changed(); });
  }
  QString file() const { return paths::data("sources") + "/health.json"; }
  void load() {
    if (loaded) return;
    loaded = true;
    QFile f(file());
    if (!f.open(QIODevice::ReadOnly)) return;
    const auto o = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = o.begin(); it != o.end(); ++it) {
      const auto h = it.value().toObject();
      Health x;
      x.lastOk = QDateTime::fromString(h["ok"].toString(), Qt::ISODate);
      x.lastFail = QDateTime::fromString(h["fail"].toString(), Qt::ISODate);
      x.lastError = redactUrls(h["err"].toString());   // files written before redactUrls existed
      if (x.lastError != h["err"].toString()) save.start();   // rewrite the file without the old URL
      x.okCount = h["okN"].toInt(); x.failCount = h["failN"].toInt(); x.failStreak = h["streak"].toInt();
      byHost.insert(it.key(), x);
    }
  }
  void write() {
    QJsonObject o;
    for (auto it = byHost.cbegin(); it != byHost.cend(); ++it) {
      const Health& x = *it;
      o[it.key()] = QJsonObject{{"ok", x.lastOk.toString(Qt::ISODate)}, {"fail", x.lastFail.toString(Qt::ISODate)},
                                {"err", x.lastError}, {"okN", x.okCount}, {"failN", x.failCount}, {"streak", x.failStreak}};
    }
    QSaveFile f(file());
    if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(o).toJson(QJsonDocument::Compact)); f.commit(); }
  }
};
HealthStore& store() { static HealthStore* s = new HealthStore; s->load(); return *s; }
QSet<QString> disabledIds() {
  const QStringList l = settings::value("sources.disabled").toStringList();
  return QSet<QString>(l.cbegin(), l.cend());
}
QSet<QString> enabledIds() {   // the ones off by default that were switched on
  const QStringList l = settings::value("sources.enabled").toStringList();
  return QSet<QString>(l.cbegin(), l.cend());
}
}

QString redactUrls(const QString& text) {
  // scheme://[user[:pass]@]host[:port] then anything up to a blank: path, query and fragment go, user info too.
  static const QRegularExpression rx(QStringLiteral(R"(\b([A-Za-z][A-Za-z0-9+.-]*)://(?:[^@/\s?#]*@)?([^/\s?#@]+)[^\s]*)"));
  QString out = text;
  out.replace(rx, QStringLiteral("\\1://\\2"));
  return out;
}

void recordResult(const QString& host, bool ok, const QString& errorIn) {
  const QString error = redactUrls(errorIn);
  auto& st = store();
  Health& h = st.byHost[host.toLower()];
  const QDateTime now = QDateTime::currentDateTimeUtc();
  if (ok) { h.lastOk = now; ++h.okCount; h.failStreak = 0; }
  else { h.lastFail = now; ++h.failCount; ++h.failStreak; h.lastError = error; }
  if (!st.save.isActive()) st.save.start();
  if (!st.notify.isActive()) st.notify.start();
}

Health health(const QString& id) {
  Health out;
  const Source* s = byId(id);
  if (!s) return out;
  auto& st = store();
  for (const auto& host : s->hosts) {
    const auto it = st.byHost.constFind(host.toLower());
    if (it == st.byHost.cend()) continue;
    const Health& h = *it;
    if (h.lastOk > out.lastOk) out.lastOk = h.lastOk;
    if (h.lastFail > out.lastFail) { out.lastFail = h.lastFail; out.lastError = h.lastError; }
    out.okCount += h.okCount; out.failCount += h.failCount;
    out.failStreak = qMax(out.failStreak, h.failStreak);
  }
  return out;
}

QList<Silent> silent(const QDateTime& now) {
  QList<Silent> out;
  auto& st = store();
  for (const Source& s : all()) {
    if (s.access == Access::Forbidden || isDisabled(s.id)) continue;
    QList<Health> hosts;
    for (const auto& host : s.hosts)
      if (const auto it = st.byHost.constFind(host.toLower()); it != st.byHost.cend() && (it->okCount > 0 || it->failCount > 0)) hosts << *it;
    if (!isSilent(hosts, now)) continue;
    const Health h = health(s.id);
    out << Silent{s.id, s.publisher, h.lastError, h.lastOk, h.failStreak};
  }
  return out;
}

Status status(const QString& id) {
  if (isDisabled(id)) return Status::Disabled;
  const Health h = health(id);
  if (h.failStreak >= 2 || (!h.lastOk.isValid() && h.failCount > 0)) return Status::Failing;
  if (h.lastOk.isValid()) return Status::Active;
  return Status::Waiting;
}

bool isDisabled(const QString& id) {
  if (disabledIds().contains(id)) return true;
  const Source* s = byId(id);
  return s && s->optIn && !enabledIds().contains(id);
}
bool isHostDisabled(const QString& host) {
  const QSet<QString> off = disabledIds(), on = enabledIds();
  const QString h = host.toLower();
  for (const auto& s : all())
    if (off.contains(s.id) || (s.optIn && !on.contains(s.id))) for (const auto& x : s.hosts) if (x.toLower() == h) return true;
  return false;
}
void setDisabled(const QString& id, bool disabled) {
  if (const Source* s = byId(id); s && s->optIn) {
    QSet<QString> on = enabledIds();
    if (disabled) on.remove(id); else on.insert(id);
    QStringList l(on.cbegin(), on.cend()); l.sort();
    if (l.isEmpty()) settings::remove("sources.enabled"); else settings::setValue("sources.enabled", l);
  }
  QSet<QString> off = disabledIds();
  if (disabled) off.insert(id); else off.remove(id);
  QStringList l(off.cbegin(), off.cend()); l.sort();
  if (l.isEmpty()) settings::remove("sources.disabled"); else settings::setValue("sources.disabled", l);
  emit Notifier::instance().changed();
}
QStringList sharingHosts(const QString& id) {
  QStringList out;
  const Source* s = byId(id);
  if (!s) return out;
  for (const auto& o : all()) {
    if (o.id == id) continue;
    for (const auto& h : o.hosts) if (s->hosts.contains(h, Qt::CaseInsensitive)) { out << o.id; break; }
  }
  return out;
}
Notifier& Notifier::instance() { static Notifier* n = new Notifier; return *n; }
}
