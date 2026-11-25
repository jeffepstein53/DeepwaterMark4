#pragma once
#include <QWidget>
#include <QTimer>

// App header: wordmark, the main tabs, clock, the update icon, the documentation ? and the options gear on the first row; sub-tabs of the active tab
// on the second. Compact (ui::compact, a phone): one row of kCompactHeight with ☰ (the menu: app/NavDrawer), the
// current tab › sub-tab, the time, the ? and the gear.
class Header : public QWidget {
  Q_OBJECT
public:
  explicit Header(QWidget* parent = nullptr);
  void setCurrent(int main, int sub);
  int currentMain() const { return m_main; }
  int currentSub() const { return m_sub; }
  static constexpr int kHeight = 76;
  static constexpr int kCompactHeight = 48;
  bool isCompact() const;
  void syncHeight();   // one row when compact: on its own resize and the window's (a phone turned keeps the width)
  struct Hit { QRectF r; int main; int sub; };   // a tab's rectangle (sub < 0: main tab)
signals:
  void navigate(int main, int sub);
  void optionsRequested();
  void helpRequested();      // the ? left of the gear: the documentation (app/HelpWindow)
  void updateRequested();    // the update icon left of the ?: app/UpdateDialog
  void checksRequested();    // the ⚠ left of it, shown while the data has an issue (app/Watchdog): Sources › Data Sources
  void menuRequested();
protected:
  void paintEvent(QPaintEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void leaveEvent(QEvent*) override;
  void resizeEvent(QResizeEvent*) override;
private:
  void paintCompact(QPainter& p);
  QRectF menuRect() const;
  bool m_hoverMenu = false;
  QList<Hit> hits() const;
  int m_main = 0, m_sub = 0;
  int m_hoverMain = -1, m_hoverSub = -1;
  bool m_hoverGear = false, m_hoverHelp = false, m_hoverUpdate = false, m_hoverCheck = false;
  QRectF gearRect() const;
  QRectF helpRect() const;
  QRectF updateRect() const;
  QRectF checkRect() const;
  QList<int> m_lastSub;     // remembered sub-tab per main tab
  QTimer m_clock;
};
