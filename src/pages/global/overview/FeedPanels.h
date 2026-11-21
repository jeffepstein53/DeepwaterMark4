#pragma once
#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>
#include <QPixmap>
#include <functional>
#include "pages/global/overview/OverviewFeeds.h"

namespace overview {

// Overview panels fed by other packages' services through the structs of OverviewFeeds.h (phase 2 wires them).
// With no data they show their final frame and a quiet "connecting…" state, never invented numbers.

// SQUAWK: header (LIVE tag, description, EN/FR translate toggle, item count), scrolling wire line, filter tabs
// (ALL / DATA / MOVERS / RELEASES), feed list (hover rows, live ages, wheel scroll), footer "Terminal Data · time".
class SquawkPanel : public QWidget {
public:
  explicit SquawkPanel(QWidget* parent);
  void setFeed(const overview::SquawkFeed& feed);
  bool french() const { return m_fr; }
  std::function<void(bool french)> onTranslate;   // phase 2: ask the wire service for translations
protected:
  void paintEvent(QPaintEvent*) override;
  void resizeEvent(QResizeEvent*) override;
  void showEvent(QShowEvent*) override;
  void hideEvent(QHideEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void leaveEvent(QEvent*) override;
  void wheelEvent(QWheelEvent*) override;
  bool event(QEvent*) override;
private:
  int headerH() const;
  QStringList descLines() const;
  QRectF wireRect() const;
  QRectF tabsRect() const;
  QRectF listRect() const;
  QRectF buttonRect() const;
  QList<QRectF> tabRects() const;
  bool compactTabs() const;
  bool showAutoLabel() const;
  bool shortHeader() const;
  QList<int> filtered() const;
  // the list's wrapped lines per item (index = m_feed.items), cached per width, language and feed content: the list
  // is laid out on every paint, mouse move and wheel step
  const QVector<QStringList>& wrappedLines() const;
  void buildWire();
  overview::SquawkFeed m_feed;
  bool m_fr = false;
  int m_tab = 1;              // DATA
  int m_hoverRow = -1;
  bool m_hoverButton = false;
  int m_hoverTab = -1;
  qreal m_scroll = 0;
  QPixmap m_wire; qreal m_wireW = 0, m_wireOff = 0;
  QTimer m_ages;
  bool m_animOn = false;
  void startAnim(bool on);
  mutable QString m_descKey;
  mutable QStringList m_desc;
  mutable bool m_descCut = false;
  quint64 m_feedGen = 0;                     // bumped when the items' shown content changes
  mutable QString m_wrapKey;
  mutable QVector<QStringList> m_wrapped;
  QElapsedTimer m_clock; qint64 m_lastMs = 0;
};

// RISK SENTIMENT: 0–100 gauge with zone-coloured segments, 1W / 1M change chips, one-year history by zone.
class RiskPanel : public QWidget {
public:
  explicit RiskPanel(QWidget* parent);
  void setReading(const overview::RiskReading& r);
protected:
  void paintEvent(QPaintEvent*) override;
  bool event(QEvent*) override;
private:
  overview::RiskReading m_r;
};

// CENTRAL BANKS: 8 rows, policy rate, next meeting, cut / hold / hike odds (dominant one bold).
class CentralBanksPanel : public QWidget {
public:
  explicit CentralBanksPanel(QWidget* parent);
  void setBanks(const overview::CentralBanks& b);
protected:
  void paintEvent(QPaintEvent*) override;
  bool event(QEvent*) override;
private:
  overview::CentralBanks m_b;
};

// ASSET REGISTRY: countries by region (★ G20, AMERICA, EUROPE, AFRICA, ASIA, OCEANIA) with the institution that
// sets their rate; highlighted rows = central-bank / BIS series, dim rows = World Bank only.
class RegistryPanel : public QWidget {
public:
  explicit RegistryPanel(QWidget* parent);
  void setRegistry(const overview::Registry& r);
  QRect cursorRect() const;
protected:
  void paintEvent(QPaintEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void leaveEvent(QEvent*) override;
  void wheelEvent(QWheelEvent*) override;
  bool event(QEvent*) override;
private:
  QList<QRectF> tabRects() const;
  QRectF starRect(int row) const;   // the ☆ (watchlist) of a row, left of its badge
  int rowAt(const QPointF& pos) const;
  QList<overview::RegistryRow> rows() const;
  overview::Registry m_reg;
  int m_tab = 0;
  int m_hoverTab = -1, m_hoverRow = -1;
  QPointF m_mouse{-1, -1};
  qreal m_scroll = 0;
};
}  // namespace overview
