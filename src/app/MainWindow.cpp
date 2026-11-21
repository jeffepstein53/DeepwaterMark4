#include "app/Zoom.h"
#include <QMenu>
#include "ui/Touch.h"
#include <QApplication>
#include "app/NavDrawer.h"
#include "app/MainWindow.h"
#include "app/Header.h"
#include "app/Pages.h"
#include "core/Settings.h"
#include "ui/Theme.h"
#include "app/OptionsDialog.h"
#include "app/HelpWindow.h"
#include "app/UpdateDialog.h"
#include "core/I18n.h"
#include "charts/data/Store.h"
#include "app/Preload.h"
#include "net/Http.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QMenuBar>
#include <QGuiApplication>
#include <QAction>
#include <QPointer>
#include <QHash>
#include <QCoreApplication>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QShortcut>
#include <QKeySequence>
#include <iterator>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle("Deepwater");
#ifdef Q_OS_IOS
  setMinimumSize(1, 1);        // the screen's size, whatever it is (pages lay out for phones: ui/Touch.h)
#else
  // 640x480 since 2026-11-21 (user: "on laptops the window should shrink further"; it was 1024x600): narrower than a
  // page's desktop layout, the page takes its phone layout (ui::compact(width, desktopMin)); layout audit at 640–1024 clean
  setMinimumSize(640, 480);
#endif
  auto* central = new QWidget(this);
  central->setAutoFillBackground(true);
  auto* v = new QVBoxLayout(central);
  v->setContentsMargins(0, 0, 0, 0); v->setSpacing(0);
  m_header = new Header(central);
  m_stack = new QStackedWidget(central);
  v->addWidget(m_header);
  v->addWidget(m_stack, 1);
  setCentralWidget(central);
  connect(m_header, &Header::navigate, this, &MainWindow::open);
  connect(m_header, &Header::optionsRequested, this, &MainWindow::showOptions);
  connect(m_header, &Header::helpRequested, this, [this] { showHelp(); });
  connect(m_header, &Header::updateRequested, this, &MainWindow::showUpdates);
  m_drawer = new app::NavDrawer(central);
  connect(m_header, &Header::menuRequested, this, [this] { m_drawer->open(m_header->currentMain(), m_header->currentSub()); });
  connect(m_drawer, &app::NavDrawer::navigate, this, &MainWindow::open);
  connect(&i18n::Notifier::instance(), &i18n::Notifier::changed, this, &MainWindow::rebuildPages);
  connect(&theme::Notifier::instance(), &theme::Notifier::changed, this, &MainWindow::rebuildPages);
  // Options: gear in the header, Cmd+, and the application menu's Settings… entry on macOS
  auto* prefs = new QAction(QCoreApplication::translate("options", "Settings…"), this);
  prefs->setMenuRole(QAction::PreferencesRole);
  prefs->setShortcut(QKeySequence::Preferences);
  connect(prefs, &QAction::triggered, this, &MainWindow::showOptions);
  addAction(prefs);
  // macOS shows it in the system menu bar (application menu › Settings…); elsewhere the gear is enough
  if (QGuiApplication::platformName() == "cocoa") menuBar()->addMenu("Deepwater")->addAction(prefs);
  // Interface zoom (app/Zoom.h): Cmd+= / Cmd+- / Cmd+0, applied at the next launch; macOS: View menu
#ifndef Q_OS_IOS
  {
    QMenu* view = QGuiApplication::platformName() == "cocoa" ? menuBar()->addMenu(QCoreApplication::translate("options", "View")) : nullptr;
    auto zoomAction = [&](const QString& text, const QList<QKeySequence>& keys, int dir) {
      auto* a = new QAction(text, this);
      a->setShortcuts(keys);
      a->setShortcutContext(Qt::ApplicationShortcut);
      connect(a, &QAction::triggered, this, [this, dir] { zoom::step(this, dir); });
      addAction(a);
      if (view) view->addAction(a);
    };
    zoomAction(QCoreApplication::translate("options", "Zoom in"), {QKeySequence(Qt::CTRL | Qt::Key_Equal), QKeySequence(Qt::CTRL | Qt::Key_Plus)}, 1);
    zoomAction(QCoreApplication::translate("options", "Zoom out"), {QKeySequence(Qt::CTRL | Qt::Key_Minus)}, -1);
    zoomAction(QCoreApplication::translate("options", "Actual size"), {QKeySequence(Qt::CTRL | Qt::Key_0)}, 0);
  }
#endif
  // Documentation: the header's ?, F1 and Cmd+? (macOS: the Help menu)
  auto* help = new QAction(QCoreApplication::translate("help", "Deepwater documentation"), this);
  help->setShortcuts({QKeySequence(Qt::Key_F1), QKeySequence::HelpContents});
  help->setShortcutContext(Qt::ApplicationShortcut);
  connect(help, &QAction::triggered, this, [this] { showHelp(); });
  addAction(help);
  if (QGuiApplication::platformName() == "cocoa") menuBar()->addMenu(QCoreApplication::translate("help", "Help"))->addAction(help);

  // Cmd+1..9 then Cmd+0: main tabs (Key_1 + 9 would be ':'); Cmd+[ / Cmd+]: previous / next sub-tab
  for (int i = 0; i < app::tabs().size() && i < 10; ++i) {
    auto* sc = new QShortcut(QKeySequence(Qt::CTRL | (i < 9 ? Qt::Key(Qt::Key_1 + i) : Qt::Key_0)), this);
    connect(sc, &QShortcut::activated, this, [this, i] { open(i, 0); });
  }
  auto step = [this](int d) {
    const int m = m_header->currentMain(), n = app::tabs()[m].subs.size();
    open(m, (m_header->currentSub() + d + n) % n);
  };
  connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_BracketRight), this), &QShortcut::activated, this, [step] { step(1); });
  connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_BracketLeft), this), &QShortcut::activated, this, [step] { step(-1); });

  // page ids renamed 2026-11-18 (Facts → Data, Chains → Macro data, Markets → Charts Data → Technical data, Sources › Data →
  // Sources graph)
  static const QHash<QString, QString> renamed{{"facts/chains", "data/macro-data"}, {"facts/markets", "data/technical-data"},
                                               {"data/charts-data", "data/technical-data"},
                                               {"sources/data", "sources/sources-graph"}};
  const QString saved = settings::value("ui.page").toString();
  if (!openPage(renamed.value(saved, saved))) open(0, 0);
  // after the first page has painted: the Charts page costs a few hundred ms to build
  QTimer::singleShot(3000, this, &MainWindow::keepPaperOrdersWatched);
  if (qEnvironmentVariableIsSet("DW_NO_PRELOAD")) m_preload = false;
  schedulePreloads(4000);
}

// Pages that load dozens of requests when first shown (measured 2026-11-19 on a fresh profile: Technical heatmap ~98
// Yahoo series, Overview ~67, Rates / Macro / Fundamental heatmap ~39 each with the World Bank and OECD, Session recap
// ~33, Markets watch ~21, Bank research ~21 SEC filings). Built hidden one by one after launch, their data loaded in the
// background, so a first visit finds it there. Data › Macro data is left out: it shows its local store at once, and
// starting its importer would start its periodic passes.
static const char* kPreloadPages[] = {"global/overview", "market/technical-heatmap", "economy/rates", "economy/macro",
                                      "market/fundamental-heatmap", "global/session-recap", "market/markets-watch",
                                      "news/bank-research"};

void MainWindow::setPreload(bool on) {
  m_preload = on;
  if (!on) ++m_preloadGen;   // a chain already scheduled stops at its next step
}

void MainWindow::schedulePreloads(int delayMs) {
  const int gen = ++m_preloadGen;
  m_preloadNext = 0;
  QTimer::singleShot(delayMs, this, [this, gen] { if (gen == m_preloadGen) preloadNext(); });
}

void MainWindow::preloadNext() {
  if (!m_preload) return;
  const int gen = m_preloadGen;
  while (m_preloadNext < int(std::size(kPreloadPages))) {
    const QString id = kPreloadPages[m_preloadNext++];
    if (m_pages.contains(id)) continue;   // already built (visited): it loaded when shown
    {
      net::BackgroundScope bg(id);
      QWidget* page = ensurePage(id);
      if (auto* p = dynamic_cast<app::Preloadable*>(page)) p->preload();
    }
    // one page per step: building a page takes tens to hundreds of ms on the GUI thread
    QTimer::singleShot(1500, this, [this, gen] { if (gen == m_preloadGen) preloadNext(); });
    return;
  }
}

QWidget* MainWindow::ensurePage(const QString& id) {
  QWidget* page = m_pages.value(id);
  if (!page) {
    page = app::createPage(id, m_stack);
    page->setObjectName(id);
    m_pages.insert(id, page);
    m_stack->addWidget(page);
  }
  return page;
}

void MainWindow::keepPaperOrdersWatched() {
  static const QString kCharts = "charts/charts";
  if (m_pages.contains(kCharts)) return;
  const QJsonObject paper = charts::store::read("state", QJsonObject()).toObject().value("paper").toObject();
  if (paper.value("orders").toArray().isEmpty()) return;
  ensurePage(kCharts);
}

void MainWindow::open(int main, int sub) {
  const auto& t = app::tabs();
  if (main < 0 || main >= t.size() || sub < 0 || sub >= t[main].subs.size()) return;
  const QString id = app::pageId(t[main].id, t[main].subs[sub].id);
  net::promote(id);   // what was being preloaded for this page goes ahead of the other pages' preloads now
  QWidget* page = ensurePage(id);
  m_stack->setCurrentWidget(page);
  m_header->setCurrent(main, sub);
  // touch: a page's first text field could take the focus as it shows, and on iOS that opens the keyboard over the
  // page; nothing is focused until the user taps a field
  if (ui::touch::enabled())
    QTimer::singleShot(0, this, [] {
      QWidget* f = QApplication::focusWidget();
      if (f && f->testAttribute(Qt::WA_InputMethodEnabled)) f->clearFocus();
    });
  settings::setValue("ui.page", id);
}

bool MainWindow::openPage(const QString& id) {
  const auto& t = app::tabs();
  for (int i = 0; i < t.size(); ++i)
    for (int j = 0; j < t[i].subs.size(); ++j)
      if (app::pageId(t[i].id, t[i].subs[j].id) == id) { open(i, j); return true; }
  return false;
}

void MainWindow::showOptions() {
  static QPointer<OptionsDialog> dlg;
  if (dlg) { dlg->raise(); dlg->activateWindow(); return; }
  dlg = new OptionsDialog(this);
  dlg->setAttribute(Qt::WA_DeleteOnClose);   // it sizes and centres itself on the window's content (OptionsDialog::fit)
  dlg->show();
}

void MainWindow::showHelp(const QString& chapter, const QString& section) {
  if (!m_help) m_help = new HelpWindow(this);
  const int m = m_header->currentMain();
  const QString id = !chapter.isEmpty() ? chapter : m >= 0 && m < app::tabs().size() ? app::tabs()[m].id : QStringLiteral("start");
  m_help->showChapter(id, section);
  m_help->show();
  m_help->raise();
  m_help->activateWindow();
}

void MainWindow::showUpdates() {
  static QPointer<UpdateDialog> dlg;
  if (!dlg) dlg = new UpdateDialog(this);   // kept: a download goes on while it is closed
  dlg->fit();
  dlg->show();
  dlg->raise();
  dlg->activateWindow();
}

void MainWindow::rebuildPages() {
  const QString id = currentPageId();
  // Non-current pages first, the current one last: removing the current page makes the stack show the next one, whose
  // showEvent ran in full just before its deletion (Squawk loaded its player, News Feed polled every source, ...).
  QWidget* cur = m_stack->currentWidget();
  for (QWidget* w : std::as_const(m_pages)) if (w != cur) { m_stack->removeWidget(w); w->deleteLater(); }
  if (cur && m_pages.values().contains(cur)) { m_stack->removeWidget(cur); cur->deleteLater(); }
  m_pages.clear();
  m_header->update();
  if (!openPage(id)) open(0, 0);
  QTimer::singleShot(0, this, &MainWindow::keepPaperOrdersWatched);   // the Charts page went with the others
  schedulePreloads(2000);
}

void MainWindow::resizeEvent(QResizeEvent* e) {
  QMainWindow::resizeEvent(e);
  m_header->syncHeight();
  if (m_drawer->isVisible()) m_drawer->setGeometry(centralWidget()->rect());
}

QWidget* MainWindow::currentPage() const { return m_stack->currentWidget(); }

void MainWindow::dropPagesExcept(const QString& id) {
  for (auto it = m_pages.begin(); it != m_pages.end();) {
    if (it.key() == id) { ++it; continue; }
    m_stack->removeWidget(it.value());
    it.value()->deleteLater();
    it = m_pages.erase(it);
  }
}

QString MainWindow::currentPageId() const { return m_stack->currentWidget() ? m_stack->currentWidget()->objectName() : QString(); }
