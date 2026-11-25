#pragma once
#include <QDateTime>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVersionNumber>
#include <memory>

class QWebSocket;
class QNetworkReply;

// Deepwater's updates (header icon, app/UpdateDialog), from the GitHub releases of the public repository
// jeffepstein53/DeepwaterMark4 (Deepwater Mark 4, 2026-11-24; user's choices 2026-11-21: one release per version;
// checked at launch and on a WebSocket, never by polling).
//
//   Releases     tag mark4-X.Y.Z (or mark3-X.Y.Z, or vX.Y.Z), assets …-macos.dmg, Deepwater-Mark4-X.Y.Z-win64.exe, -ios.ipa; the release's text is
//                what's new. Drafts and pre-releases are ignored; the newest version above this one that has this
//                platform's file is offered.
//   Detection    one check at launch, then a WebSocket held open on the ntfy.sh topic the release workflow posts to
//                when it publishes (.github/workflows/release.yml): a message only triggers a check against GitHub,
//                so it needn't be trusted. A dropped connection is opened again (5 s … 5 min apart) and checks once
//                reconnected (a release published meanwhile).
//   Install      macOS: the .dmg downloaded, mounted, its app's version checked, then put in place of this one (a
//                build in a CMake folder installs to /Applications instead: never over a build), and Deepwater
//                started again. Windows (Mark 4): the installer runs silently, per user, into the running program's own folder. iOS: an app
//                cannot install itself, the release page opens (sideloading or TestFlight).
//   Sources      "github-releases" and "ntfy" in the Sources tab; switched off there, nothing is asked.
//   Test hooks   DW_UPDATE_API=<base url> (a fake GitHub), DW_UPDATE_NTFY=<ws url>|off, DW_UPDATE_AUTO=1 (installs
//                what the first check finds, for an end-to-end test).
namespace app {
class Updater : public QObject {
  Q_OBJECT
public:
  enum class State { Idle, Checking, UpToDate, Available, Downloading, Installing, Failed };
  struct Release {
    QVersionNumber version;
    QString tag, name, notes;     // notes: the release's text (Markdown)
    QUrl page, asset;             // the release on GitHub; this platform's file
    QString assetName;
    qint64 size = 0;
    QByteArray sha256;             // the file's fingerprint as GitHub gives it (lower-case hex); empty: it gives none
    QDateTime published;
  };
  static Updater& instance();
  void start();                   // main(): the launch check, then the WebSocket
  void check();                   // ask GitHub now (the dialog's button; ignored while one is running)
  void install();                 // download + install + start again (macOS, Windows); opens the page (iOS)
  static bool canInstallItself(); // macOS and Windows (an installer that runs silently); iOS cannot
  static QString platformSuffix();// the asset name's end for this platform ("-macos.dmg", …)

  State state() const { return m_state; }
  Release latest() const { return m_latest; }       // valid when Available (and while installing it)
  QVersionNumber current() const { return m_current; }
  QString error() const { return m_error; }
  double progress() const { return m_progress; }     // 0..1 while downloading
  QDateTime lastCheck() const { return m_lastCheck; }

signals:
  void changed();

private:
  Updater();
  void setState(State s, const QString& error = {});
  void onReleases(const QByteArray& json);
  void connectSocket();
  void download();
  void installFile(const QString& file);
#ifdef Q_OS_MACOS
  void installMac(const QString& dmg);
#endif
#ifdef Q_OS_WIN
  void installWindows(const QString& installer);
  static bool isOriginalsFolder(const QString& dir);   // Windows: Program Files\Deepwater, the original's
#endif
  QString apiBase() const;

  State m_state = State::Idle;
  QVersionNumber m_current;
  Release m_latest;
  QString m_error;
  double m_progress = 0;
  QDateTime m_lastCheck;
  bool m_started = false, m_autoInstall = false;
  QWebSocket* m_ws = nullptr;
  QTimer m_reconnect, m_soon;
  int m_backoffMs = 5000;
  bool m_wasConnected = false;
  QPointer<QNetworkReply> m_reply;
  std::unique_ptr<QFile> m_out;
};
}
