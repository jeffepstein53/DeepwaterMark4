#pragma once
#include <QWidget>
#include <QList>

// Sources › Data Sources, above the sources: what is wrong with the data now (app/Watchdog) and, unfolded, every key
// figure with the other sources that give it (macro/KeyFigures).
namespace sourcespage {
class CheckPanel : public QWidget {
  Q_OBJECT
public:
  explicit CheckPanel(QWidget* parent = nullptr);
  void setPhone(bool on);
  void refresh();

protected:
  void paintEvent(QPaintEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void leaveEvent(QEvent*) override;
  void resizeEvent(QResizeEvent*) override;
  bool event(QEvent*) override;

private:
  struct Line { QString mark, left, mid, right, tip; int tone = 0; bool head = false; };   // tone: 0 plain, 1 good, 2 warning, 3 bad
  void build();
  int lineH() const { return m_phone ? 40 : 24; }
  QRectF foldRect() const;
  QList<Line> m_issues, m_figures;
  QString m_summary;
  bool m_open = false, m_phone = false, m_hover = false;
};
}
