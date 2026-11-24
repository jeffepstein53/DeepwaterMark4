#pragma once
#include <QMainWindow>
#include <QHash>

class Header;
class HelpWindow;
class QStackedWidget;
namespace app { class NavDrawer; }

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget* parent = nullptr);
  // "tab/sub" (e.g. "market/seasonality"); returns false if unknown
  bool openPage(const QString& id);
  void open(int main, int sub);
  QString currentPageId() const;
  void showOptions();
  // The documentation, on `chapter` (a main tab id or "start"; empty: the chapter of the tab shown), optionally at the
  // section titled `section`.
  void showHelp(const QString& chapter = {}, const QString& section = {});
  void showUpdates();   // the header's update icon: app/UpdateDialog
  // Re-create every page (after a language change); the current page is re-opened.
  void rebuildPages();
  // The slow pages built and loaded in the background after launch (app::Preloadable); off for the audits, which
  // measure one page at a time.
  void setPreload(bool on);
  // Layout audit (dev): the page shown, and dropping every other page so the window's minimum is this page's alone
  // (a QStackedWidget's minimum is the largest of all its pages).
  QWidget* currentPage() const;
  void dropPagesExcept(const QString& id);
protected:
  void resizeEvent(QResizeEvent* e) override;
private:
  QWidget* ensurePage(const QString& id);   // made on first use, not shown
  void schedulePreloads(int delayMs);
  void preloadNext();
  bool m_preload = true;
  int m_preloadNext = 0;
  int m_preloadGen = 0;     // a rebuild restarts the chain: an older chain's timer stops
  Header* m_header;
  QStackedWidget* m_stack;
  HelpWindow* m_help = nullptr;   // made on first use, then kept (its chapters stay parsed)
  app::NavDrawer* m_drawer;   // the phone menu (☰ of the compact header)
  QHash<QString, QWidget*> m_pages;
};
