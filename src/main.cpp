#include "ui/Touch.h"
#include "core/Startup.h"
#include "app/SelfTest.h"
#include "app/MainWindow.h"
#include "app/LayoutAudit.h"
#include "app/PerfAudit.h"
#include "ui/Theme.h"
#include "account/BridgeSync.h"
#include "core/I18n.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>
#include <QLineEdit>
#include "app/HelpWindow.h"
#include "app/Zoom.h"
#include "ai/McpBridge.h"
#include "ai/McpServer.h"
#include "app/Updater.h"
#include <QSurfaceFormat>
#include <QScreen>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include "ml/Runs.h"
#include "ai/Evaluation.h"

// Dev flags (all optional, unknown flags are ignored):
//   --page tab/sub          open this page (e.g. market/seasonality)
//   --goto tab/sub --goto-at ms   open that page later (tests: what a visit finds after the background preloads)
//   --size WxH              window size in logical pixels (1600x1042 gives a 1600x966 page, the reference size); below
//                           640x480 the minimum is lifted: --size 430x932 shows the phone layout (+ DW_TOUCH_SIM=1)
//   --screenshot file.png   grab the window after --delay ms (default 6000) and quit
//   --quit-after ms         quit after this many ms (profiling runs)
//   --lang en|fr            interface language (saved, as from the Options dialog)
//   --options               open the Options dialog
//   --complete-ml-references  no window: add no_skill / persistence / data fingerprint to older ML runs (verified), quit
//   --theme id              theme (deepwater, ember, abyss, jade, oxide, nova, bloomberg, glacier, dune, sakura; saved)
//   --layout-audit dir      every page alone at each size: captures + report.txt (see app/LayoutAudit.h); with
//                           --audit-sizes WxH,WxH (default 1024x600,1280x720,1440x900,1728x1052), --audit-pages a/b,c/d,
//                           --audit-load ms (wait for data on each page, default 5000). Use with DW_PROFILE.
//   --perf-audit dir        every page in turn: build / first-paint time, GUI-thread stalls, idle CPU, memory, and what the
//                           hidden pages cost (see app/PerfAudit.h); --perf-load ms, --perf-idle ms, --audit-pages.
// Env: DW_PROFILE=<dir> isolates settings writes and user data (tests), DW_CACHE_DIR=<dir> shares an HTTP cache.
int main(int argc, char** argv) {
  QApplication::setAttribute(Qt::AA_DontShowIconsInMenus);
  // The Facts 3D views use OpenGL 3.3 core: on macOS a core context only shares with core contexts, so the default
  // format (Qt's own compositing context included) must be core too, set before the application exists.
#ifndef Q_OS_IOS   // iOS has OpenGL ES only: its own default format
  {
    QSurfaceFormat f = QSurfaceFormat::defaultFormat();
    f.setVersion(3, 3); f.setProfile(QSurfaceFormat::CoreProfile); f.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(f);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  }
#else
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
#endif
  // the names first: the saved zoom is read from the settings before the application exists (QT_SCALE_FACTOR)
  QCoreApplication::setOrganizationName("deepwater-terminal");
  QCoreApplication::setOrganizationDomain("deepwater-terminal.com");
  QCoreApplication::setApplicationName("Deepwater");
#ifndef Q_OS_IOS
  if (ai::mcpbridge::wanted(argc, argv)) return ai::mcpbridge::run(argc, argv);   // started by Claude: no window
#endif
  zoom::installSaved();
  QApplication app(argc, argv);
  dw::runStartupFunctions();   // iOS: the services Qt could not start inside the constructor (core/Startup.h)
  ui::touch::install();        // finger scrolling (iOS, or DW_TOUCH_SIM=1 on a Mac)
  QApplication::setApplicationVersion(QStringLiteral(DW_VERSION));   // CMakeLists.txt project() VERSION

  QCommandLineParser cli;
  cli.addOptions({{"lang", "en | fr (saved, like the Options dialog)", "lang"}, {"theme", "theme id (saved)", "id"}, {"options", "open the Options dialog"}, {"help-page", "open the documentation: chapter[#section]", "chapter"}, {"help-search", "search the documentation", "text"}, {"updates", "open the update window"},
                  {"page", "page id", "id"}, {"size", "WxH", "size"}, {"screenshot", "png path", "file"},
                  {"delay", "ms", "ms", "6000"}, {"goto", "page", "page"}, {"goto-at", "ms", "ms", "5000"}, {"quit-after", "ms", "ms"}, {"layout-audit", "dir", "dir"},
                  {"audit-sizes", "WxH,...", "sizes", "1024x600,1280x720,1440x900,1728x1052"}, {"audit-pages", "ids", "ids"},
                  {"audit-load", "ms", "ms", "5000"}, {"perf-audit", "dir", "dir"}, {"perf-load", "ms", "ms", "6000"},
                  {"perf-idle", "ms", "ms", "8000"}, {"complete-ml-references", "add the references to older ML runs, then quit"},
                  {"selftest", "check this platform's PDF and OCR engines, write the result (JSON) to file, then quit", "file"}});
  cli.parse(app.arguments());   // parse(), not process(): unknown flags are ignored
  // --selftest: no window; the platform engines (PDF reader, OCR) checked end to end, for CI (app/SelfTest.h)
  if (cli.isSet("selftest")) return app::selftest::run(cli.value("selftest"));

  // --complete-ml-references: no window; what the app does 20 s after launch for older ML runs (ml::Runs::completeReferences,
  // the trainer in its sandbox), for every run that needs it, then quits. The ML page is not opened, nothing else starts.
  if (cli.isSet("complete-ml-references")) {
    auto& runs = ml::Runs::instance();
    QObject::connect(&runs, &ml::Runs::referencesDone, &app, [](int added, int refused) {
      qInfo("[ml] references: %d added, %d refused", added, refused);
      QCoreApplication::exit(0);
    });
    QTimer::singleShot(0, &runs, &ml::Runs::completeReferences);
    return app.exec();
  }
  theme::install();
  i18n::installSaved();
  if (cli.isSet("theme")) theme::setCurrent(cli.value("theme"));
  if (cli.isSet("lang")) i18n::setLanguage(cli.value("lang") == "fr" ? i18n::Lang::French : i18n::Lang::English);
  MainWindow w;
  account::BridgeSync::instance();   // live MT4/MT5/cTrader sync runs from launch, not from the first visit
  ai::Evaluation::instance().resumeForward();   // the agents' forward tests ask at their times while the app runs
  if (cli.isSet("page") && !w.openPage(cli.value("page"))) qWarning("unknown page %s", qPrintable(cli.value("page")));
  if (cli.isSet("size")) {
    const auto wh = cli.value("size").split('x');
    if (wh.size() == 2) {
      // a phone's size on a Mac (e.g. --size 430x932): below the window's usual minimum, to see the phone layout
      if (wh[0].toInt() < w.minimumWidth() || wh[1].toInt() < w.minimumHeight()) w.setMinimumSize(1, 1);   // (0, 0) would mean "none": the layout's own minimum
      w.resize(wh[0].toInt(), wh[1].toInt());
    }
  } else {
    const QRect avail = w.screen()->availableGeometry();
    w.resize(qMin(1600, avail.width()), qMin(1042, avail.height()));
  }
  w.show();
  if (cli.isSet("options")) QTimer::singleShot(300, &w, [&w] { w.showOptions(); });
  // updates (app/Updater): the launch check and the release WebSocket; not during the audits, which measure pages
  if (!cli.isSet("layout-audit") && !cli.isSet("perf-audit")) app::Updater::instance().start();
  // Claude outside Deepwater (AI › LLM › Integration): served while the app runs, when switched on
  if (!cli.isSet("layout-audit") && !cli.isSet("perf-audit")) {
    ai::McpServer::instance().startExternal();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [] {   // only this instance's file (another one may be open)
      QFile f(ai::McpServer::connectionFile());
      if (f.open(QIODevice::ReadOnly) && QJsonDocument::fromJson(f.readAll()).object().value("pid").toInteger() == QCoreApplication::applicationPid())
        f.remove();
    });
  }
  if (cli.isSet("updates")) QTimer::singleShot(300, &w, [&w] { w.showUpdates(); });
  if (cli.isSet("help-page"))
    QTimer::singleShot(300, &w, [&w, v = cli.value("help-page"), q = cli.value("help-search")] {
      w.showHelp(v.section('#', 0, 0), v.section('#', 1));
      if (!q.isEmpty())
        if (auto* h = w.findChild<HelpWindow*>()) if (auto* f = h->findChild<QLineEdit*>()) f->setText(q);
      if (qEnvironmentVariableIsSet("DW_HELP_PANE"))
        if (auto* h = w.findChild<HelpWindow*>()) QTimer::singleShot(500, h, [h] { h->openPane(qEnvironmentVariableIntValue("DW_HELP_PANE")); });
    });
  if (cli.isSet("goto"))
    QTimer::singleShot(cli.value("goto-at").toInt(), &w, [&w, id = cli.value("goto")] {
      if (!w.openPage(id)) qWarning("unknown page %s", qPrintable(id));
      qInfo("goto %s", qPrintable(id));
    });

  if (cli.isSet("screenshot")) {
    const QString file = cli.value("screenshot");
    QTimer::singleShot(cli.value("delay").toInt(), &w, [&w, file] {
      QDir().mkpath(QFileInfo(file).absolutePath());
      QPixmap shot = w.grab();
      // dialogs are separate windows: paint the visible ones over the capture
      for (QWidget* top : QApplication::topLevelWidgets()) {
        if (top == &w || !top->isVisible() || !top->inherits("QDialog")) continue;
        QPainter p(&shot);
        p.drawPixmap(top->geometry().topLeft() - w.geometry().topLeft(), top->grab());
      }
      shot.save(file);
      qApp->quit();
    });
  }
  // the audits measure each page alone: no page built and loading behind them
  if (cli.isSet("layout-audit") || cli.isSet("perf-audit")) w.setPreload(false);
  if (cli.isSet("layout-audit")) {
    QList<QSize> sizes;
    for (const QString& s : cli.value("audit-sizes").split(',', Qt::SkipEmptyParts)) {
      const auto wh = s.split('x');
      if (wh.size() == 2) sizes << QSize(wh[0].toInt(), wh[1].toInt());
    }
    layoutaudit::run(&w, cli.value("layout-audit"), sizes, cli.value("audit-pages").split(',', Qt::SkipEmptyParts),
                     cli.value("audit-load").toInt());
  }
  if (cli.isSet("perf-audit"))
    perfaudit::run(&w, cli.value("perf-audit"), cli.value("audit-pages").split(',', Qt::SkipEmptyParts),
                   cli.value("perf-load").toInt(), cli.value("perf-idle").toInt());
  if (cli.isSet("quit-after")) QTimer::singleShot(cli.value("quit-after").toInt(), &app, &QApplication::quit);
  return app.exec();
}
