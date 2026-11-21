#pragma once
#include <QWidget>
#include <QImage>
#include <QPixmap>
#include <QTimer>
#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QVector>
#include <QPolygonF>
#include "ui/DotGlobe.h"

namespace economy {

// Rotating Earth: the dot-matrix globe of News › Bank Research (ui::DotGlobe) sized to this view, inside the
// amber halo; Natural Earth country rings (resources/macro/countries.json) for hover/click hit-tests, highlight
// outlines and tints. 10 fps while rotating, stopped when hidden or idle.
// Auto-rotates west→east; flyTo() turns the globe to a country; drag rotates; hover/click report countries.
class GlobeWidget : public QWidget {
  Q_OBJECT
public:
  explicit GlobeWidget(QWidget* parent = nullptr);
  void flyTo(double lat, double lon);
  void setHighlight(const QStringList& iso2s);      // amber outline + fill (several for the euro area)
  void setTint(const QHash<QString, int>& alphaByIso);   // subtle fills per country (cadence filters)
  void resetView();
  void setRadiusHint(double frac) { m_radiusFrac = frac; update(); }
  QString countryAtPoint(const QPointF& pos) const;   // ISO-2 of the country under that point, "" off the land
signals:
  void hovered(const QString& iso2);               // "" when off any country
  void clicked(const QString& iso2);
protected:
  void paintEvent(QPaintEvent*) override;
  void resizeEvent(QResizeEvent*) override;
  void showEvent(QShowEvent*) override;
  void hideEvent(QHideEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void leaveEvent(QEvent*) override;
private:
  struct Country { QString iso; QString name; QVector<QPolygonF> rings; QRectF box; };
  void loadAssets();
  void ensureBase();
  void syncCamera() const;
  void tick();
  bool toLatLon(const QPointF& pos, double* lat, double* lon) const;
  QString countryAt(double lat, double lon) const;
  QPointF project(double lat, double lon, bool* visible) const;
  QRectF disk() const;
  QRect spinRect() const;

  QVector<Country> m_countries;
  QImage m_base;                 // background + halo of the whole widget
  QRectF m_baseDisk;
  QPixmap m_body;                // static sphere for the current radius
  QImage m_baseBody;             // m_base over spinRect() with the sphere composed on it: one copy per frame, no blend
  double m_bodyR = 0;
  mutable ui::DotGlobe m_globe;
  double m_rot = -40;            // longitude at the centre, degrees
  double m_tilt = 22;            // latitude at the centre
  double m_target = NAN;         // fly-to longitude
  double m_radiusFrac = 0.34;
  bool m_dragging = false; QPointF m_dragFrom; double m_dragRot = 0;
  QElapsedTimer m_idle, m_clock;
  QTimer m_timer;
  QStringList m_highlight; QString m_hover; QPointF m_mouse;
  QHash<QString, int> m_tint;
};
}  // namespace economy
