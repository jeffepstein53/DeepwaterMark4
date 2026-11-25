#pragma once
#include <QImage>
#include "pages/market/watch/WatchCalc.h"
#include <QWidget>
#include <QTimer>
#include <QDate>
#include <QDateTime>
#include <QStringList>
#include <QVector>
#include <cmath>

// Cards of the Markets Watch instrument sheet. Each card loads on its own and shows its own state
// (loading, computing with a spinner, connecting, not connected, a message, or its data).
// Cards fed by other services (retail, COT, data compare, meeting compare) take their data through small
// set…() calls; until then they show "Connecting…" — never placeholder numbers.
namespace watch {

class SheetCard : public QWidget {
  Q_OBJECT
public:
  enum State { Loading, Computing, Connecting, NotConnected, Message, Ready };
  explicit SheetCard(const QString& title, QWidget* parent = nullptr);
  void setState(State s, const QString& message = {});
  State state() const { return m_state; }
  void setSuffix(const QString& s) { m_suffix = s; m_layer = QImage(); update(); }   // grey text after the title ("2011-2026")
  void setSource(const QString& s) { m_source = s; m_layer = QImage(); update(); }   // shown under "Connecting…" / in the tooltip
  QString title() const { return m_title; }
  // Reference loading frame: the index performance card shows only its spinner, without frame or title.
  void setFramelessWhileComputing(bool on) { m_frameless = on; }
signals:
  void stateChanged();
protected:
  void paintEvent(QPaintEvent*) override;
  void showEvent(QShowEvent*) override;
  void hideEvent(QHideEvent*) override;
  QRectF body() const { return QRectF(1, 29, width() - 2, height() - 30); }
  virtual void paintBody(QPainter&, const QRectF&) {}
  // Placeholder of a state; return false to use the default one.
  virtual bool paintPlaceholder(QPainter&, const QRectF&, State) { return false; }
  void paintSpinner(QPainter& p, const QPointF& c, qreal r);
  // Cards with a hover readout over a costly chart (thousands of path points): frame + body are drawn once into a
  // layer while the card is Ready, a hover repaint blits it and draws paintHover() on top. Whatever changes the body
  // goes through setState / setSuffix / setSource / a resize, which drop the layer.
  void setLayered(bool on) { m_layered = on; }
  virtual void paintHover(QPainter&, const QRectF&) {}
  void resizeEvent(QResizeEvent*) override { m_layer = QImage(); }
  QString m_message, m_source;
private:
  void syncSpinner();
  void paintFrame(QPainter& p);
  bool m_layered = false; QImage m_layer;
  QString m_title, m_suffix;
  State m_state = Loading;
  bool m_frameless = false;
  QTimer m_spin; int m_phase = 0;
};

// ---------------------------------------------------------------- positioning (fed by positioning::Retail / ::Cot)
struct RetailData {
  double longPct = NAN, shortPct = NAN;   // percent of traders (or of volume) long / short, sum 100
  QString source;                         // "Myfxbook community outlook"
  QDateTime updated;
};
class RetailCard : public SheetCard {
  Q_OBJECT
public:
  explicit RetailCard(QWidget* parent = nullptr);
  void setRetail(const RetailData& d);    // switches to Ready
protected:
  void paintBody(QPainter&, const QRectF&) override;
  bool paintPlaceholder(QPainter&, const QRectF&, State) override;
private:
  RetailData m_d;
};

struct CotSide {
  QString label;                          // "EUR", "USD", "GOLD"
  double longPct = NAN, shortPct = NAN;   // non-commercial long / short share of their open positions, %
  QDate reportDate;
  QString tip;                            // contracts, change on the week…
};
class CotCard : public SheetCard {
  Q_OBJECT
public:
  explicit CotCard(QWidget* parent = nullptr);
  void setLabels(const QStringList& labels);       // one or two sides, shown with "–" until data comes
  void setSides(const QVector<CotSide>& sides);    // switches to Ready
protected:
  void paintBody(QPainter&, const QRectF&) override;
  bool paintPlaceholder(QPainter&, const QRectF&, State) override;
  bool event(QEvent*) override;
private:
  void paintSides(QPainter&, const QRectF&, bool data);
  QVector<CotSide> m_sides;
};

// ---------------------------------------------------------------- macro (fed by macro::MacroStore / ::CentralBanks)
struct CompareRow {
  QString label;                          // "Interest Rate"
  QStringList values;                     // formatted, one per column ("2.65%", "—")
  QStringList tips;                       // optional: period and source per value
  QStringList dates;                      // optional: the period each value measures, short ("Sep 26"), drawn beside it
  QList<bool> differs;                    // optional: another source gives another figure (macro/KeyFigures): amber, with ≠
};
class DataCompareCard : public SheetCard {
  Q_OBJECT
public:
  explicit DataCompareCard(QWidget* parent = nullptr);
  void setColumns(const QStringList& codes);       // "EUR", "USD"
  void setRows(const QVector<CompareRow>& rows);   // switches to Ready
  static QStringList defaultIndicators();          // the five rows of the reference
protected:
  void paintBody(QPainter&, const QRectF&) override;
  bool event(QEvent*) override;
private:
  QStringList m_cols; QVector<CompareRow> m_rows;
};

struct MeetingBucket { QString label; double prob = NAN; };   // "-25bps" 0.4 (percent)
struct MeetingData {
  QString bank;                           // "ECB", "FEDERAL RESERVE"
  QDate meeting;                          // next meeting date
  QVector<MeetingBucket> buckets;         // ordered cut → hike, e.g. -50+bps -25bps No change +25bps +50+bps
  QString source;
};
class MeetingCard : public SheetCard {
  Q_OBJECT
public:
  explicit MeetingCard(QWidget* parent = nullptr);
  void setBanks(const QStringList& names);         // one or two halves, each starts "Connecting…"
  void setBank(int side, const MeetingData& d);    // fills one half
  void setBankState(int side, State s, const QString& msg = {});
protected:
  bool event(QEvent*) override;
  void paintBody(QPainter&, const QRectF&) override;
  bool paintPlaceholder(QPainter&, const QRectF&, State) override;
private:
  void paintHalf(QPainter&, const QRectF&, int side);
  struct Half { QString name; State state = Connecting; QString msg; MeetingData d; };
  QVector<Half> m_halves;
};

// ---------------------------------------------------------------- computed by the sheet
class TechTilesCard : public SheetCard {
  Q_OBJECT
public:
  explicit TechTilesCard(QWidget* parent = nullptr);
  void setTiles(const QVector<Tile>& t);
  void setColumns(int n) { if (n != m_cols) { m_cols = n; update(); } }   // phones: tiles on rows of n (0 = one row)
protected:
  void paintBody(QPainter&, const QRectF&) override;
  bool event(QEvent*) override;
private:
  QRectF tileRect(int i) const;
  QVector<Tile> m_tiles;
  int m_cols = 0;
};

class IndexPerfCard : public SheetCard {
  Q_OBJECT
public:
  explicit IndexPerfCard(QWidget* parent = nullptr);
  void setLines(const PerfLine& a, const PerfLine& b);   // a blue, b gold (reference: USD blue, EUR gold)
protected:
  void paintBody(QPainter&, const QRectF&) override;
  void paintHover(QPainter&, const QRectF&) override;
  bool paintPlaceholder(QPainter&, const QRectF&, State) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void leaveEvent(QEvent*) override;
private:
  PerfLine m_a, m_b; QPointF m_mouse{-1, -1}; QRectF m_plot;
  qint64 m_t0 = 0, m_t1 = 0; double m_lo = 0, m_hi = 1;   // the body's scales, for paintHover
};

class SeasonMonthCard : public SheetCard {
  Q_OBJECT
public:
  explicit SeasonMonthCard(QWidget* parent = nullptr);
  void setData(const Seasonality& s);
protected:
  void paintBody(QPainter&, const QRectF&) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void leaveEvent(QEvent*) override;
private:
  Seasonality m_s; int m_hover = -1; QRectF m_plot;
};

class YearPathCard : public SheetCard {
  Q_OBJECT
public:
  explicit YearPathCard(QWidget* parent = nullptr);
  void setData(const Seasonality& s);
protected:
  void paintBody(QPainter&, const QRectF&) override;
  void paintHover(QPainter&, const QRectF&) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void leaveEvent(QEvent*) override;
private:
  Seasonality m_s; QPointF m_mouse{-1, -1}; QRectF m_plot;
  double m_lo = 0, m_hi = 1;   // the body's scale, for paintHover
};
}
