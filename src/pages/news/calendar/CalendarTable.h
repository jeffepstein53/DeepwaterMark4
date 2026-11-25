#pragma once
#include <QWidget>
#include <QTimer>
#include <QHash>
#include "news/Calendar.h"
#include "pages/news/calendar/HistoryCard.h"
class QPainter;

namespace news::pages {

// The week table of News › Calendar: sticky column header and day headers, rows, inline expansion with the
// indicator description and its release history card. Painted in one widget (own smooth scrolling).
class CalendarTable : public QWidget {
  Q_OBJECT
public:
  enum Col { Time, Currency, EventCol, Actual, Forecast, Previous, Surprise, Min, Max };
  explicit CalendarTable(QWidget* parent = nullptr);
  void setEvents(const QList<news::Event>& events);
  void setEmptyNote(const QString& note) { m_note = note; update(); }
  void setFocusCell(const QString& key, int col) { m_focusKey = key; m_focusCol = col; update(); }
  void toggleExpand(const QString& key);
  QString expandedKey() const { return m_expanded; }
  void scrollToKey(const QString& key);          // bring a row near the top
  void scrollBy(double dy) { setTarget(m_target + dy); }
  void setScroll(double y);                      // immediate
  // test hooks
  void hoverAt(const QPoint& p);
  void hoverChart(double fraction);
  void setChartMode(int mode, int span);
  // Phones / short windows: the page scrolls (one finger-scrollable column), so the table takes its whole height
  // (fullHeight()) and does not scroll itself; setVisibleBand tells it which part of it the page shows, so the
  // column header and the day header stay pinned at the top of that band.
  void setFullHeight(bool on);
  void openOnToday() { m_openToday = true; }    // the next setEvents() of a week that holds today starts there
  int fullHeight() const { return int(kHeaderH + contentHeight() + 8); }
  void setVisibleBand(double top, double height);

signals:
  void valueClicked(const news::Event& e, int col);
  void contentChanged();                         // rows or expansion changed: fullHeight() may differ
  void openAt(int y);                            // full-height mode: the page scrolls, to this place of the table

protected:
  void paintEvent(QPaintEvent*) override;
  void resizeEvent(QResizeEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void mouseDoubleClickEvent(QMouseEvent*) override;
  void wheelEvent(QWheelEvent*) override;
  void leaveEvent(QEvent*) override;

private:
  struct Item { enum Kind { Group, Row, Expand } kind; double y = 0, h = 0; int ev = -1; QDate day; bool first = false; };
  void relayout();
  double rightOf(int col) const;                 // right edge of numeric columns (-1000 when hidden)
  bool shown(int col) const;                     // narrow tables hide MIN/MAX, then SURPRISE (values in tooltips)
  double scale() const;                          // column width factor (1 at the reference, 0.8…1.6)
  double proseWidth() const { return std::min(width() - 28.0, 1200.0); }   // description lines ≤ 1200 px
  QRectF cellRect(const Item& it, int col) const;   // in content coordinates
  QRectF cardRect(const Item& exp) const;        // content coordinates
  QStringList descParas(const news::Event& e) const;
  double contentHeight() const { return m_items.isEmpty() ? 0 : m_items.last().y + m_items.last().h; }
  double maxScroll() const;
  void setTarget(double y);
  int itemAt(double contentY) const;
  void paintRow(QPainter& p, const Item& it, double y, bool pinned) const;
  void paintRowPhone(QPainter& p, const Item& it, double y) const;
  // phone width: two-line rows (time, flag, name / tag, actual, forecast, previous), no SURPRISE / MIN / MAX
  bool phone() const { return width() < 520; }
  double rowH() const { return phone() ? kPhoneRowH : kRowH; }
  double scrollPos() const { return m_full ? m_viewTop : m_scroll; }   // content y shown under the column header
  double headY() const { return m_full ? m_viewTop : 0; }              // where the column header is painted
  bool m_openToday = false;                      // the week was just opened: to be shown from today
  double contentY(double y) const { return y - kHeaderH - headY() + scrollPos(); }
  void paintGroup(QPainter& p, const QDate& day, double y, bool sticky) const;
  void paintExpand(QPainter& p, const Item& it, double y) const;
  void updateHover(const QPointF& pos);
  const Item* expandItem() const;

  static constexpr int kHeaderH = 34;            // column header (with its borders)
  static constexpr int kRowH = 37;
  static constexpr int kStickyH = 36;
  static constexpr int kPhoneRowH = 60;

  QList<news::Event> m_events;
  QList<Item> m_items;
  QString m_note;
  double m_scroll = 0, m_target = 0;
  QTimer m_anim;
  int m_hoverItem = -1, m_hoverCol = -1;
  QString m_expanded;
  HistoryCard m_card;
  bool m_cardPress = false;
  QString m_focusKey; int m_focusCol = -1;
  bool m_pin = false;                            // hover set by the test hook survives leave events
  bool m_full = false;                           // setFullHeight
  double m_viewTop = 0, m_viewH = 0;             // setVisibleBand
};
}  // namespace news::pages
