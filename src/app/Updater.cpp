#include "app/Updater.h"
#include "core/Paths.h"
#include "net/Http.h"
#include "net/Sources.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QWebSocket>
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
#include <QFutureWatcher>
#include <QProcess>
#include <QtConcurrent>
#endif

namespace app {
namespace {
// Deepwater Mark 4 (2026-11-24): its own repository (public, so GitHub answers without a sign-in) and its own topic.
constexpr const char* kRepo = "jeffepstein53/DeepwaterMark4";
// The topic .github/workflows/release.yml posts the new tag to. Anyone may post to a public ntfy topic: a message only
// makes the app ask GitHub, which is what decides.
constexpr const char* kTopic = "deepwater-mark3-releases-7c0ad207";

const sources::Registrar regGithub({"github-releases", "GitHub (Deepwater Mark 4's releases)",
    {"api.github.com", "github.com", "objects.githubusercontent.com", "release-assets.githubusercontent.com"},
    sources::Access::Keyless, 1000, "Deepwater's new versions: the update check and its download",
    "Public repository; GitHub's REST API without a token (60 requests an hour per address)", {}});
const sources::Registrar regNtfy({"ntfy", "ntfy.sh", {"ntfy.sh"}, sources::Access::Keyless, 1000,
    "A message when a new Deepwater version is published (one WebSocket, no polling)",
    "Public notification service; the topic carries only the new version's tag", {}});

// The fingerprint GitHub gives for the file ("sha256:" and 64 hexadecimal digits); anything else is none.
QByteArray fingerprint(const QString& digest) {
  static const QRegularExpression rx(QStringLiteral("^sha256:([0-9a-f]{64})$"), QRegularExpression::CaseInsensitiveOption);
  return rx.match(digest).captured(1).toLower().toLatin1();
}
QByteArray fileFingerprint(const QString& file) {
  QFile f(file);
  QCryptographicHash h(QCryptographicHash::Sha256);
  return f.open(QIODevice::ReadOnly) && h.addData(&f) ? h.result().toHex() : QByteArray();
}

QString shellQuote(const QString& s) { QString q = s; q.replace("'", "'\\''"); return "'" + q + "'"; }
}

Updater& Updater::instance() { static Updater* u = new Updater; return *u; }

Updater::Updater() {
  m_current = QVersionNumber::fromString(QCoreApplication::applicationVersion());
  m_reconnect.setSingleShot(true);
  connect(&m_reconnect, &QTimer::timeout, this, [this] { if (m_ws) m_ws->open(m_ws->requestUrl()); });
  m_soon.setSingleShot(true);
  connect(&m_soon, &QTimer::timeout, this, &Updater::check);
}

QString Updater::platformSuffix() {
#if defined(Q_OS_IOS)
  return QStringLiteral("-ios.ipa");
#elif defined(Q_OS_MACOS)
  return QStringLiteral("-macos.dmg");
#elif defined(Q_OS_WIN)
  return QStringLiteral("-win64.exe");   // Mark 4 is published as an installer (windows/installer.nsi)
#else
  return {};
#endif
}

bool Updater::canInstallItself() {
#if defined(Q_OS_MACOS)
  return true;
#elif defined(Q_OS_WIN)
  // Mark 4 is published as an installer that runs silently, per user (no administrator's consent), into the folder
  // this program runs from. (install() never lets it near Program Files\Deepwater: the original's.)
  return true;
#else
  return false;
#endif
}

#ifdef Q_OS_WIN
bool Updater::isOriginalsFolder(const QString& dir) {
  const QString d = QDir::cleanPath(dir);
  for (const char* var : {"ProgramFiles", "ProgramW6432", "ProgramFiles(x86)"}) {
    const QString pf = qEnvironmentVariable(var);
    if (!pf.isEmpty() && d.compare(QDir::cleanPath(pf + "/Deepwater"), Qt::CaseInsensitive) == 0) return true;
  }
  return false;
}
#endif

QString Updater::apiBase() const {
  const QString t = qEnvironmentVariable("DW_UPDATE_API");
  return t.isEmpty() ? QStringLiteral("https://api.github.com") : t;
}

void Updater::setState(State s, const QString& error) {
  m_state = s;
  m_error = error;
  emit changed();
}

void Updater::start() {
  if (m_started) return;
  m_started = true;
  m_autoInstall = qEnvironmentVariable("DW_UPDATE_AUTO") == "1";
#ifdef Q_OS_MACOS
  // what an update left beside the app (the shell that started the new one removes it; a crash may not have)
  const QString bundle = QDir(QCoreApplication::applicationDirPath() + "/../..").canonicalPath();
  QDir(QFileInfo(bundle).absolutePath() + "/.Deepwater-old.app").removeRecursively();
#endif
  // after the first page and the launch's requests: the check is no hurry
  QTimer::singleShot(6000, this, [this] { check(); connectSocket(); });
}

void Updater::check() {
  if (m_state == State::Checking || m_state == State::Downloading || m_state == State::Installing) return;
  if (sources::isDisabled("github-releases")) {
    setState(State::Failed, QCoreApplication::translate("update", "The GitHub source is switched off in the Sources tab."));
    return;
  }
  const State before = m_state;
  setState(State::Checking);
  net::Request r;
  r.url = QUrl(apiBase() + "/repos/" + kRepo + "/releases?per_page=20");
  r.headers = {{"Accept", "application/vnd.github+json"}, {"X-GitHub-Api-Version", "2022-11-28"}};
  r.staleOnError = false;
  r.priority = true;
  net::get(r, this, [this, before](const net::Reply& rep) {
    m_lastCheck = QDateTime::currentDateTimeUtc();
    if (rep.ok()) { onReleases(rep.body); return; }
    const QString why = rep.status == 403 || rep.status == 429
        ? QCoreApplication::translate("update", "GitHub limits how often it is asked; try again later.")
        : rep.error.isEmpty() ? QString("HTTP %1").arg(rep.status) : rep.error;
    // a version already found stays offered
    if (before == State::Available && m_latest.version > m_current) { setState(State::Available, why); return; }
    setState(State::Failed, why);
  });
}

void Updater::onReleases(const QByteArray& json) {
  const QJsonArray list = QJsonDocument::fromJson(json).array();
  const QString suffix = platformSuffix();
  Release best;
  for (const QJsonValue& v : list) {
    const QJsonObject o = v.toObject();
    if (o.value("draft").toBool() || o.value("prerelease").toBool()) continue;
    const QString tag = o.value("tag_name").toString();
    // Mark 4's tags: mark4-0.4.0 (published by hand, without the workflow), mark3-0.3.1 (Mark 3's, older, so never
    // newer) or v0.4.0 (release.yml)
    static const QRegularExpression rx(QStringLiteral("^(?:mark[34]-|v)(\\d+(?:\\.\\d+){0,2})$"));
    const auto m = rx.match(tag);
    if (!m.hasMatch()) continue;   // any other tag is not a Mark 4 release
    const QVersionNumber ver = QVersionNumber::fromString(m.captured(1));
    if (ver <= best.version) continue;
    for (const QJsonValue& a : o.value("assets").toArray()) {
      const QJsonObject ao = a.toObject();
      const QString name = ao.value("name").toString();
      if (suffix.isEmpty() || !name.endsWith(suffix, Qt::CaseInsensitive)) continue;
      best.version = ver;
      best.tag = tag;
      best.name = o.value("name").toString();
      best.notes = o.value("body").toString();
      best.page = QUrl(o.value("html_url").toString());
      best.published = QDateTime::fromString(o.value("published_at").toString(), Qt::ISODate);
      best.asset = QUrl(ao.value("browser_download_url").toString());
      best.assetName = name;
      best.size = qint64(ao.value("size").toDouble());
      best.sha256 = fingerprint(ao.value("digest").toString());
      break;
    }
  }
  if (!best.version.isNull() && best.version > m_current) {
    m_latest = best;
    setState(State::Available);
    if (m_autoInstall) { m_autoInstall = false; install(); }
  } else {
    m_latest = Release{};
    setState(State::UpToDate);
  }
}

void Updater::connectSocket() {
  const QString env = qEnvironmentVariable("DW_UPDATE_NTFY");
  if (env == "off" || sources::isDisabled("ntfy") || m_ws) return;
  const QUrl url(env.isEmpty() ? QString("wss://ntfy.sh/%1/ws").arg(kTopic) : env);
  m_ws = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
  connect(m_ws, &QWebSocket::connected, this, [this] {
    m_backoffMs = 5000;
    // reconnected: a release may have been published while the connection was down
    if (m_wasConnected) m_soon.start(2000);
    m_wasConnected = true;
  });
  connect(m_ws, &QWebSocket::disconnected, this, [this] {
    if (sources::isDisabled("ntfy")) return;
    m_reconnect.start(m_backoffMs);
    m_backoffMs = std::min(m_backoffMs * 2, 5 * 60 * 1000);
  });
  connect(m_ws, &QWebSocket::textMessageReceived, this, [this](const QString& text) {
    // ntfy sends "open", "keepalive" and "message" events; only a message means a release
    if (QJsonDocument::fromJson(text.toUtf8()).object().value("event").toString() == "message") m_soon.start(3000);
  });
  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::UserAgentHeader, net::userAgent());
  m_ws->open(req);
}

void Updater::install() {
  if (m_state != State::Available || m_latest.version.isNull()) return;
  if (!canInstallItself()) { QDesktopServices::openUrl(m_latest.page); return; }
#ifdef Q_OS_WIN
  // a Deepwater installed in Program Files\Deepwater is the original's, not Mark 4's to replace: the page opens instead
  if (isOriginalsFolder(QCoreApplication::applicationDirPath())) { QDesktopServices::openUrl(m_latest.page); return; }
#endif
  download();
}

void Updater::download() {
  const QString dir = paths::cache("updates");
  const QString file = dir + "/" + m_latest.assetName;
  if (m_latest.sha256.isEmpty()) {   // nothing to compare the file with: nothing is installed
    setState(State::Available, QCoreApplication::translate("update", "GitHub gives no fingerprint for this file: it is not installed."));
    return;
  }
  if (QFileInfo(file).size() == m_latest.size && m_latest.size > 0 && fileFingerprint(file) == m_latest.sha256) { installFile(file); return; }   // already here
  if (sources::isHostDisabled(m_latest.asset.host())) {
    setState(State::Available, QCoreApplication::translate("update", "The GitHub source is switched off in the Sources tab."));
    return;
  }
  m_out = std::make_unique<QFile>(file + ".part");
  if (!m_out->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    setState(State::Available, m_out->errorString());
    m_out.reset();
    return;
  }
  m_progress = 0;
  setState(State::Downloading);
  QNetworkRequest nr(m_latest.asset);
  nr.setHeader(QNetworkRequest::UserAgentHeader, net::userAgent());
  nr.setRawHeader("Accept", "application/octet-stream");
  nr.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);   // github.com → its file host
  m_reply = net::manager()->get(nr);
  connect(m_reply, &QNetworkReply::readyRead, this, [this] {
    if (!m_reply || !m_out) return;
    const QByteArray chunk = m_reply->readAll();
    if (m_out->write(chunk) != chunk.size()) m_reply->abort();
  });
  connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
    const qint64 t = total > 0 ? total : m_latest.size;
    const double p = t > 0 ? double(got) / double(t) : 0;
    if (p - m_progress >= 0.01 || p >= 1) { m_progress = p; emit changed(); }
  });
  connect(m_reply, &QNetworkReply::finished, this, [this, file] {
    QNetworkReply* r = m_reply;
    if (!r) return;
    r->deleteLater();
    const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString err = r->error() != QNetworkReply::NoError ? sources::redactUrls(r->errorString())
                        : status != 200 ? QString("HTTP %1").arg(status) : QString();
    if (m_out) { m_out->write(r->readAll()); m_out->close(); }
    const qint64 got = m_out ? QFileInfo(m_out->fileName()).size() : 0;
    const QString part = m_out ? m_out->fileName() : QString();
    m_out.reset();
    if (!err.isEmpty() || (m_latest.size > 0 && got != m_latest.size)) {
      QFile::remove(part);
      setState(State::Available, err.isEmpty() ? QCoreApplication::translate("update", "The download was incomplete; try again.") : err);
      return;
    }
    if (fileFingerprint(part) != m_latest.sha256) {
      QFile::remove(part);
      setState(State::Available, QCoreApplication::translate("update", "The downloaded file is not the one GitHub describes (its fingerprint differs): it has been deleted."));
      return;
    }
    QFile::remove(file);
    if (!QFile::rename(part, file)) { setState(State::Available, QCoreApplication::translate("update", "The downloaded file could not be kept.")); return; }
    installFile(file);
  });
}

void Updater::installFile(const QString& file) {
  setState(State::Installing);
#if defined(Q_OS_MACOS)
  installMac(file);
#elif defined(Q_OS_WIN)
  installWindows(file);
#else
  Q_UNUSED(file);
#endif
}

#ifdef Q_OS_MACOS
namespace {
// A build in a CMake folder: the next `cmake --build` writes there, so an update installs to /Applications instead.
bool insideBuildTree(const QString& bundle) {
  QDir d(QFileInfo(bundle).absolutePath());
  for (int i = 0; i < 6; ++i) {
    if (d.exists("CMakeCache.txt")) return true;
    if (!d.cdUp()) break;
  }
  return false;
}
struct MacResult { QString error, target, old; };
QString run(const QString& program, const QStringList& args, int timeoutMs) {
  QProcess p;
  p.setProcessChannelMode(QProcess::MergedChannels);
  p.start(program, args);
  if (!p.waitForFinished(timeoutMs)) { p.kill(); return program + ": " + QCoreApplication::translate("update", "no answer"); }
  if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
    return QFileInfo(program).fileName() + ": " + QString::fromLocal8Bit(p.readAll()).trimmed().left(300);
  return {};
}
// On a worker thread: mount, check, copy beside the target, swap. Nothing of the running app is touched until the new
// one is complete next to it; the old one is kept (renamed) until Deepwater has quit.
MacResult putInPlace(const QString& dmg, const QString& expected) {
  MacResult res;
  const QString mnt = QDir::tempPath() + "/deepwater-update-" + QString::number(QCoreApplication::applicationPid());
  QDir().mkpath(mnt);
  if (QString e = run("/usr/bin/hdiutil", {"attach", "-nobrowse", "-readonly", "-noautoopen", "-mountpoint", mnt, dmg}, 180000); !e.isEmpty()) {
    res.error = e; return res;
  }
  auto detach = [&] { run("/usr/bin/hdiutil", {"detach", mnt, "-force"}, 60000); QDir().rmdir(mnt); };
  const QString src = mnt + "/Deepwater.app";
  QFile plist(src + "/Contents/Info.plist");
  QString version;
  if (plist.open(QIODevice::ReadOnly)) {
    static const QRegularExpression rx(QStringLiteral("<key>CFBundleShortVersionString</key>\\s*<string>([^<]+)</string>"));
    version = rx.match(QString::fromUtf8(plist.readAll())).captured(1).trimmed();
  }
  if (version != expected) {
    detach();
    res.error = QCoreApplication::translate("update", "The downloaded app is version %1, not %2.").arg(version.isEmpty() ? "?" : version, expected);
    return res;
  }
  const QString bundle = QDir(QCoreApplication::applicationDirPath() + "/../..").canonicalPath();
  QString target = bundle;
  if (!bundle.endsWith(".app") || insideBuildTree(bundle) || !QFileInfo(QFileInfo(bundle).absolutePath()).isWritable()) {
    target = QFileInfo(QStringLiteral("/Applications")).isWritable() ? QStringLiteral("/Applications/Deepwater.app")
                                                                     : QDir::homePath() + "/Applications/Deepwater.app";
    QDir().mkpath(QFileInfo(target).absolutePath());
  }
  const QString parent = QFileInfo(target).absolutePath();
  const QString staged = parent + "/.Deepwater-update.app";
  res.old = parent + "/.Deepwater-old.app";
  QDir(staged).removeRecursively();
  if (QString e = run("/usr/bin/ditto", {src, staged}, 300000); !e.isEmpty()) { detach(); QDir(staged).removeRecursively(); res.error = e; return res; }
  detach();
  QDir(res.old).removeRecursively();
  if (QFileInfo::exists(target) && !QDir().rename(target, res.old)) {
    QDir(staged).removeRecursively();
    res.error = QCoreApplication::translate("update", "%1 could not be replaced (no permission to write there).").arg(target);
    return res;
  }
  if (!QDir().rename(staged, target)) {
    QDir().rename(res.old, target);   // put the old one back
    res.error = QCoreApplication::translate("update", "%1 could not be replaced (no permission to write there).").arg(target);
    return res;
  }
  res.target = target;
  return res;
}
}

void Updater::installMac(const QString& dmg) {
  auto* watcher = new QFutureWatcher<MacResult>(this);
  connect(watcher, &QFutureWatcher<MacResult>::finished, this, [this, watcher] {
    const MacResult r = watcher->result();
    watcher->deleteLater();
    if (!r.error.isEmpty()) { setState(State::Available, r.error); return; }
    // once this process has quit: remove the old app, start the new one (directly when a test profile runs, so it
    // keeps the profile's environment; otherwise through Launch Services)
    const bool test = qEnvironmentVariableIsSet("DW_PROFILE");
    const QString start = test ? shellQuote(r.target + "/Contents/MacOS/Deepwater") + " >/dev/null 2>&1 &"
                               : "/usr/bin/open -n " + shellQuote(r.target);
    const QString script = QString("while kill -0 %1 2>/dev/null; do sleep 0.2; done; rm -rf %2; %3")
                               .arg(QCoreApplication::applicationPid()).arg(shellQuote(r.old), start);
    if (!QProcess::startDetached("/bin/sh", {"-c", script})) {
      setState(State::Available, QCoreApplication::translate("update", "Installed in %1; start it from there.").arg(r.target));
      return;
    }
    qApp->quit();
  });
  watcher->setFuture(QtConcurrent::run(putInPlace, dmg, m_latest.version.toString()));
}
#endif

#ifdef Q_OS_WIN
void Updater::installWindows(const QString& installer) {
  // The installer replaces files Deepwater is running from, so it starts once this process has quit. It runs
  // silently and per user (no administrator's consent) into the folder this program runs from, then Deepwater starts
  // again from there. /D= comes last and is read to the end of the line: no quotes around the path. A test profile
  // (DW_PROFILE) passes /NOREG: the installer then leaves the registry and the Start menu alone. The fingerprint is
  // compared once more just before it runs.
  auto psQuote = [](QString s) { s.replace("'", "''"); return "'" + s + "'"; };
  const QString dir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
  const QString app = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
  QStringList args{"'/S'"};
  if (qEnvironmentVariableIsSet("DW_PROFILE")) args << "'/NOREG'";
  args << psQuote("/D=" + dir);
  const QString script = QString("Wait-Process -Id %1 -ErrorAction SilentlyContinue; "
                                 "if ((Get-FileHash -LiteralPath %2 -Algorithm SHA256).Hash -eq '%5') { "
                                 "$p = Start-Process -FilePath %2 -ArgumentList %3 -Wait -PassThru; "
                                 "if ($p -and $p.ExitCode -eq 0) { Remove-Item -LiteralPath %2 -ErrorAction SilentlyContinue } }; "
                                 "Start-Process -FilePath %4")
                             .arg(QCoreApplication::applicationPid())
                             .arg(psQuote(QDir::toNativeSeparators(installer)), args.join(","), psQuote(app), QString::fromLatin1(m_latest.sha256));
  if (!QProcess::startDetached("powershell.exe", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden", "-Command", script})) {
    setState(State::Available, QCoreApplication::translate("update", "The installer could not be started; run %1 yourself.").arg(installer));
    return;
  }
  qApp->quit();
}
#endif
}  // namespace app
