#pragma once
#include "fundamental/Fundamental.h"
#include <QWidget>
#include <QTimer>
#include <QFont>

class QPainter;

// Private widgets of Market › Fundamental Heatmap (painted; geometry measured on the 1600x966 reference).
namespace fundamental {

QFont mono(int px, int weight = QFont::Normal, qreal ls = 0);
QFont sans(int px, int weight = QFont::Normal, qreal ls = 0);
QFont prose(int px, int weight = QFont::Medium);
void drawDiamond(QPainter& p, const QPointF& c, qreal r, const QColor& color);
void drawMacroCard(QPainter& p, const QRectF& r, const QColor& bg, const QColor& border);   // box + corner brackets
void drawCardTitle(QPainter& p, const QPointF& at, const QString& text);                 // ◆ + spaced label
QString dash();

// ---------------------------------------------------------------- 8 score cards
class CardStrip : public QWidget {
  Q_OBJECT
public:
  explicit CardStrip(QWidget* parent);
  void setScores(const QVector<fundamental::Score>& ranked, bool ready);
  void setSelected(const QString& ccy, bool fresh);
  void setFresh(bool fresh) { m_fresh = fresh; update(); }
  // phones (strip under 760 px): two rows of four cards
  static int heightFor(int w) { return w < 760 ? 2 * 93 + 8 : 93; }
signals:
  void picked(const QString& ccy);
protected:
  void paintEvent(QPaintEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void leaveEvent(QEvent*) override;
private:
  QRectF cardRect(int i) const;
  QVector<fundamental::Score> m_scores;   // display order
  bool m_ready = false, m_fresh = true;
  QString m_sel;
  int m_hover = -1;
};

// ---------------------------------------------------------------- history chart (policy rate / inflation)
class HistoryChart : public QWidget {
  Q_OBJECT
public:
  enum Kind { Policy, Inflation };
  HistoryChart(Kind k, QWidget* parent);
  void setData(const fundamental::Series& s, const QString& title, const QString& subtitle, double target,
               const QString& targetLabel, const QString& status);
  void setYears(int years);              // 5, 10, 20, 0 = all
  void replay();                          // draw-in animation (≈0.6 s, 30 fps)
  void setHover(const QPoint& p) { m_hover = p; update(); }   // test hook
protected:
  void paintEvent(QPaintEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void leaveEvent(QEvent*) override;
  void hideEvent(QHideEvent*) override;
private:
  QRectF plot() const;
  const fundamental::Series& visible() const;
  // cache of visible() and its points' times in seconds, reset by setData / setYears
  mutable fundamental::Series m_vis; mutable QVector<qint64> m_visSecs; mutable bool m_visValid = false;
  Kind m_kind;
  fundamental::Series m_series;
  QString m_title, m_sub, m_targetLabel, m_status;
  double m_target = 2.0;
  int m_years = 10;
  double m_reveal = 1.0;
  QTimer m_anim;
  qint64 m_animStart = 0;
  QPoint m_hover{-1, -1};
};

// ---------------------------------------------------------------- monetary policy card
class PolicyCard : public QWidget {
  Q_OBJECT
public:
  explicit PolicyCard(QWidget* parent);
  void setData(const fundamental::Inputs* in, const QString& ccy, const QString& status, bool animate);
  HistoryChart* policyChart() const { return m_policy; }
  HistoryChart* inflationChart() const { return m_infl; }
  // phones (card under 760 px): rate, move, meeting and gauge one under the other, the two charts stacked
  static constexpr int kPhoneChartH = 260, kPhoneTop = 352;
  static int phoneHeight() { return kPhoneTop + 2 * kPhoneChartH + 10 + 9; }
protected:
  void paintEvent(QPaintEvent*) override;
  void resizeEvent(QResizeEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
private:
  QRectF rangeRect(int i) const;
  fundamental::Inputs m_in; bool m_has = false;
  QString m_ccy, m_status;
  HistoryChart *m_policy, *m_infl;
  int m_range = 1;   // 0 5Y, 1 10Y, 2 20Y, 3 MAX
};

// ---------------------------------------------------------------- automated read
class ReadCard : public QWidget {
  Q_OBJECT
public:
  explicit ReadCard(QWidget* parent);
  void setReading(const fundamental::Reading* r, const QString& loadingText);
  int heightFor(int width) const;
signals:
  void heightChanged();
protected:
  void paintEvent(QPaintEvent*) override;
private:
  fundamental::Reading m_r; bool m_has = false; QString m_loading;
};

// ---------------------------------------------------------------- pair comparator
class ComparatorCard : public QWidget {
  Q_OBJECT
public:
  explicit ComparatorCard(QWidget* parent);
  // 412 px with the seven compared rows; the real rates add two rows and a line
  static constexpr int kPitch = 31, kRowsTop = 86, kCarryH = fundamental::kRealRows * kPitch + 22, kHeight = 412 + kCarryH;
  void setData(const QString& base, const QList<fundamental::Inputs>& all, const QVector<fundamental::Score>& scores,
               bool ready);
  void setOther(const QString& ccy);
  QString other() const { return m_other; }
protected:
  bool event(QEvent*) override;
  void paintEvent(QPaintEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void leaveEvent(QEvent*) override;
private:
  QRectF tabRect(int i) const;
  QStringList others() const;
  QString m_base, m_other;
  QList<fundamental::Inputs> m_all; QVector<fundamental::Score> m_scores; bool m_ready = false;
  int m_hoverTab = -1;
};

// ---------------------------------------------------------------- heatmap
class HeatmapCard : public QWidget {
  Q_OBJECT
public:
  explicit HeatmapCard(QWidget* parent);
  void setScores(const QVector<fundamental::Score>& ranked, bool ready);
  void setSelected(const QString& ccy) { m_sel = ccy; update(); }
  static int heightForRows(int n) { return 70 + (n - 1) * 63.5 + 58 + 12; }
  // phones (card under 760 px): each currency on two lines (code, score and bias; then its six signal cells)
  static int phoneHeightForRows(int n) { return 70 + n * kPhonePitch + 6; }
  static constexpr int kPhonePitch = 84;
  void setHoverCell(int row, int col) { m_hr = row; m_hc = col; update(); }   // test hook
signals:
  void picked(const QString& ccy);
protected:
  void paintEvent(QPaintEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void leaveEvent(QEvent*) override;
  bool event(QEvent*) override;
private:
  QVector<QPair<double, double>> columns() const;   // x, width for the 8 columns (cached per width)
  QVector<QPair<double, double>> computeColumns() const;
  mutable QVector<QPair<double, double>> m_cols; mutable int m_colsW = -1;
  QRectF cell(int row, int col) const;
  QPair<int, int> hit(const QPointF& p) const;
  QVector<fundamental::Score> m_scores; bool m_ready = false;
  QString m_sel;
  int m_hr = -1, m_hc = -1;
};
}
