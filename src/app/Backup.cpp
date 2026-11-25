#include "app/Backup.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "core/Settings.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QtConcurrent>

namespace app {
namespace {
QString tx(const char* s) { return QCoreApplication::translate("backup", s); }
// folders of the data that are not copied
const QStringList kLeftOut{QStringLiteral("cache"), QStringLiteral("technical-data"), QStringLiteral("macro-data"), QStringLiteral("mcp")};

QStringList filesOf(const QString& dir) {
  QStringList out;
  QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString rel = QDir(dir).relativeFilePath(it.next());
    if (!kLeftOut.contains(rel.section('/', 0, 0)) && !rel.endsWith(".lock")) out << rel;
  }
  out.sort();
  return out;
}

// The settings: the profile's file in a trial, the registry's key exported by Windows itself otherwise.
bool saveSettings(const QString& to) {
  if (settings::isTestProfile()) return QFile::copy(qEnvironmentVariable("DW_PROFILE") + "/settings.ini", to + "/reglages.ini");
#ifdef Q_OS_WIN
  QString k = QSettings().fileName();                     // \\HKEY_CURRENT_USER\\Software\\…
  while (k.startsWith('\\') || k.startsWith('/')) k.remove(0, 1);
  dw::Process p;
  p.start(QStringLiteral("reg.exe"), {QStringLiteral("export"), k, QDir::toNativeSeparators(to + "/reglages.reg"), QStringLiteral("/y")});
  return p.waitForFinished(20000) && p.exitCode() == 0 && QFileInfo(to + "/reglages.reg").size() > 0;
#else
  QSettings s;
  s.sync();
  return !QFileInfo::exists(s.fileName()) || QFile::copy(s.fileName(), to + "/" + QFileInfo(s.fileName()).fileName());
#endif
}
}

Backup& Backup::instance() { static Backup* b = new Backup; return *b; }
Backup::Backup() { connect(&m_timer, &QTimer::timeout, this, &Backup::look); }

QString Backup::folder() {
  return settings::isTestProfile() ? qEnvironmentVariable("DW_PROFILE") + "/sauvegardes" : QDir::homePath() + "/Deepwater-sauvegardes";
}

QDateTime Backup::last() {
  static const QRegularExpression rx(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$"));
  QDateTime t;
  for (const QFileInfo& d : QDir(folder()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
    if (rx.match(d.fileName()).hasMatch()) {
      const QDateTime at = QFileInfo(d.filePath() + "/LISEZ-MOI.txt").lastModified();
      if (at.isValid() && at > t) t = at;
    }
  return t;
}

void Backup::start() {
  if (m_started) return;
  m_started = true;
  m_timer.start(60 * 60 * 1000);
  QTimer::singleShot(120 * 1000, this, &Backup::look);
}

void Backup::look() {
  const QDateTime l = last();
  if (!l.isValid() || l.date() != QDate::currentDate()) runNow();
}

void Backup::runNow() {
  if (m_running) return;
  m_running = true;
  m_error.clear();
  for (const char* k : {"positioning.myfxbookEmail", "positioning.myfxbookSession"}) settings::secretValue(k);   // never in clear in a copy
  emit changed();
  QtConcurrent::run(&Backup::run, paths::data(), folder(), QDate::currentDate(), 14).then(this, [this](const QString& err) {
    m_running = false;
    m_error = err;
    emit changed();
  });
}

QString Backup::run(const QString& dataDir, const QString& folder, const QDate& day, int keep) {
  const QString name = day.toString(Qt::ISODate), part = folder + "/" + name + ".partiel", to = folder + "/" + name;
  QDir(part).removeRecursively();
  if (!QDir().mkpath(part + "/donnees")) return tx("The folder %1 cannot be written.").arg(QDir::toNativeSeparators(folder));
  if (!saveSettings(part)) { QDir(part).removeRecursively(); return tx("The settings could not be copied."); }
  int n = 0;
  qint64 bytes = 0;
  for (const QString& rel : filesOf(dataDir)) {
    const QString src = dataDir + "/" + rel, dst = part + "/donnees/" + rel;
    QDir().mkpath(QFileInfo(dst).absolutePath());
    if (!QFile::copy(src, dst)) {
      if (!QFileInfo::exists(src)) continue;               // removed meanwhile
      QDir(part).removeRecursively();
      return tx("%1 could not be copied.").arg(rel);
    }
    ++n;
    bytes += QFileInfo(dst).size();
  }
  QFile r(part + "/LISEZ-MOI.txt");
  if (!r.open(QIODevice::WriteOnly)) { QDir(part).removeRecursively(); return tx("The folder %1 cannot be written.").arg(QDir::toNativeSeparators(folder)); }
  // one sentence a line (the translation files hold no line breaks)
  const QStringList lines{
      tx("Deepwater's settings and records on %1: %2 files, %3 KB.").arg(name).arg(n).arg(bytes / 1024),
      tx("To put them back: 1. quit Deepwater; 2. double-click reglages.reg (the settings); 3. copy what is in the folder donnees into %1.").arg(QDir::toNativeSeparators(dataDir)),
      tx("The Myfxbook session is encrypted for this Windows account: elsewhere, sign in again.")};
  r.write((lines.join("\r\n") + "\r\n").toUtf8());
  r.close();
  QDir(to).removeRecursively();                            // today's earlier copy
  if (!QDir().rename(part, to)) return tx("The folder %1 cannot be written.").arg(QDir::toNativeSeparators(folder));
  // the copies too old: only folders this class named
  static const QRegularExpression rx(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$"));
  QStringList days;
  for (const QString& d : QDir(folder).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) if (rx.match(d).hasMatch()) days << d;
  while (days.size() > keep) QDir(folder + "/" + days.takeFirst()).removeRecursively();
  return {};
}
}
