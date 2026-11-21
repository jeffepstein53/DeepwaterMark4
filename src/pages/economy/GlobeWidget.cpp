#include "pages/economy/GlobeWidget.h"
#include "pages/economy/EcoKit.h"
#include "ui/Theme.h"
#include <QPainter>
#include <QCoreApplication>
#include "macro/Registry.h"
#include <QPainterPath>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtMath>
#include <cmath>

namespace economy {

namespace {
constexpr double kSpeedDegPerSec = 4.5;   // auto-rotation
constexpr int kFrameMs = 100;             // 10 fps: 0.45° per frame at the 4.5°/s spin, smooth enough; 15 fps cost ~30 % CPU

double wrap180(double a) { while (a > 180) a -= 360; while (a < -180) a += 360; return a; }
}

GlobeWidget::GlobeWidget(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  setAttribute(Qt::WA_OpaquePaintEvent, true);
  m_timer.setInterval(kFrameMs);
  connect(&m_timer, &QTimer::timeout, this, &GlobeWidget::tick);
  m_clock.start();
  m_idle.start();
  loadAssets();
}

void GlobeWidget::loadAssets() {
  QFile f(":/macro/countries.json");
  if (!f.open(QIODevice::ReadOnly)) return;
  const auto o = QJsonDocument::fromJson(f.readAll()).object();
  for (auto it = o.begin(); it != o.end(); ++it) {
    Country c; c.iso = it.key(); c.name = it.value().toObject().value("name").toString();
    for (const auto& rv : it.value().toObject().value("rings").toArray()) {
      const auto a = rv.toArray();
      QPolygonF ring;
      for (int i = 0; i + 1 < a.size(); i += 2) ring << QPointF(a[i].toDouble(), a[i + 1].toDouble());
      if (ring.size() < 3) continue;
      c.box = c.box.isNull() ? ring.boundingRect() : c.box.united(ring.boundingRect());
      c.rings << ring;
    }
    m_countries << c;
  }
}

QRectF GlobeWidget::disk() const {
  // wide windows (height-limited): a slightly larger globe uses the space
  const double frac = width() > height() * 1.8 ? std::min(0.40, m_radiusFrac * 1.18) : m_radiusFrac;
  const double R = std::floor(std::min(width(), height()) * frac);
  const double r = std::min(R, std::min(width(), height()) * 0.5 - 70.0);
  const QPointF c(width() * 0.504, height() * 0.5);
  return QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r);
}

void GlobeWidget::syncCamera() const {
  const QRectF d = disk();
  ui::DotGlobe::Camera cam; cam.centre = d.center(); cam.R = d.width() / 2; cam.lon = m_rot; cam.tilt = m_tilt;
  m_globe.setCamera(cam);
}

void GlobeWidget::ensureBase() {
  const qreal dpr = devicePixelRatioF();
  const QSize sz = (QSizeF(size()) * dpr).toSize();
  if (m_base.size() == sz && m_baseDisk == disk()) return;
  m_baseDisk = disk();
  m_baseBody = QImage();
  m_base = QImage(sz, QImage::Format_RGB32);
  m_base.setDevicePixelRatio(dpr);
  QPainter p(&m_base);
  p.setRenderHint(QPainter::Antialiasing);
  QRadialGradient bg(QPointF(width() * 0.25, height() * 0.85), width() * 0.9);
  bg.setColorAt(0, theme::map("#0f0a03")); bg.setColorAt(1, theme::map("#040301"));
  p.fillRect(rect(), bg);
  const QRectF d = m_baseDisk;
  const double R = d.width() / 2, HR = R + 90, k = R / HR;
  QRadialGradient g(d.center(), HR);
  if (theme::isDefault()) {   // measured Deepwater amber halo, unchanged
    g.setColorAt(0, QColor(120, 84, 6, 255));
    g.setColorAt(k * 0.985, QColor(120, 84, 6, 255));
    g.setColorAt(k, QColor(112, 78, 8, 230));
    g.setColorAt(k + (1 - k) * 0.18, QColor(70, 48, 4, 150));
    g.setColorAt(k + (1 - k) * 0.5, QColor(38, 26, 2, 60));
    g.setColorAt(1, QColor(20, 14, 0, 0));
  } else {                    // the theme's halo role; a lighter glow on light backgrounds
    const QColor h = theme::roles().halo;
    const double f = theme::isDark() ? 1.0 : 0.45;
    g.setColorAt(0, theme::alpha(h, int(255 * f)));
    g.setColorAt(k * 0.985, theme::alpha(h, int(255 * f)));
    g.setColorAt(k, theme::alpha(h, int(200 * f)));
    g.setColorAt(k + (1 - k) * 0.18, theme::alpha(h, int(120 * f)));
    g.setColorAt(k + (1 - k) * 0.5, theme::alpha(h, int(45 * f)));
    g.setColorAt(1, theme::alpha(h, 0));
  }
  p.setPen(Qt::NoPen); p.setBrush(g);
  p.drawEllipse(d.center(), HR, HR);
  p.setBrush(theme::map("#4a4530"));
  p.drawEllipse(QPointF(10, 14), 2.2, 2.2);
  eco::micro(p, 18, 18, QCoreApplication::translate("economy", "GLOBAL ASSET NETWORK"), theme::map("#565550"), 11, 3.2);
}

QPointF GlobeWidget::project(double lat, double lon, bool* vis) const {
  syncCamera();
  return m_globe.project(lat, lon, vis);
}

bool GlobeWidget::toLatLon(const QPointF& pos, double* lat, double* lon) const {
  syncCamera();
  return m_globe.unproject(pos, lat, lon);
}

QString GlobeWidget::countryAt(double lat, double lon) const {
  const QPointF pt(lon, lat);
  for (const auto& c : m_countries) {
    if (!c.box.contains(pt)) continue;
    for (const auto& r : c.rings) if (r.containsPoint(pt, Qt::OddEvenFill)) return c.iso;
  }
  return {};
}

void GlobeWidget::flyTo(double /*lat*/, double lon) {
  m_target = lon;
  m_idle.restart();
  if (isVisible() && !m_timer.isActive()) m_timer.start();
}
void GlobeWidget::setHighlight(const QStringList& isos) {
  if (isos == m_highlight) return;
  m_highlight = isos; update();
  // tick() stops the timer while a country is highlighted: clearing it (registry row left) must restart the spin
  if (isos.isEmpty()) { m_idle.restart(); if (isVisible() && !m_timer.isActive()) { m_clock.restart(); m_timer.start(); } }
}
void GlobeWidget::setTint(const QHash<QString, int>& t) {
  if (t == m_tint) return;
  m_tint = t; update();
}
void GlobeWidget::resetView() {
  m_target = NAN; m_highlight.clear(); m_idle = QElapsedTimer(); m_idle.start();
  m_rot = -40;
  if (isVisible() && !m_timer.isActive()) m_timer.start();
  update();
}

void GlobeWidget::tick() {
  const double dt = std::min(0.2, m_clock.restart() / 1000.0);
  if (m_dragging) return;
  if (std::isfinite(m_target)) {
    const double diff = wrap180(m_target - m_rot);
    const double step = std::clamp(diff * std::min(1.0, dt * 4.0), -150 * dt, 150 * dt);
    if (std::fabs(diff) < 0.05) { m_rot = m_target; m_target = NAN; m_idle.restart(); }
    else m_rot = wrap180(m_rot + step);
    update(spinRect());
    return;
  }
  // hold still while a country is shown or right after an interaction
  if (!m_highlight.isEmpty() || !m_hover.isEmpty() || m_idle.elapsed() < 2500) {
    if (!m_highlight.isEmpty() || !m_hover.isEmpty()) m_timer.stop();
    return;
  }
  m_rot = wrap180(m_rot + kSpeedDegPerSec * dt);
  update(spinRect());
}

// What a turn of the globe repaints: the disc and its 1 px rim (the sphere pixmap overhangs it by 2 px). The halo
// around it is in m_base and never changes; country outlines, tints and the dots stay inside the disc.
QRect GlobeWidget::spinRect() const { return disk().adjusted(-4, -4, 4, 4).toAlignedRect(); }

void GlobeWidget::paintEvent(QPaintEvent* e) {
  ensureBase();
  QPainter p(this);
  const QRectF d = disk();
  const double R = d.width() / 2;
  const qreal dpr = devicePixelRatioF();
  syncCamera();
  if (m_bodyR != R || m_body.devicePixelRatio() != dpr) { m_body = m_globe.body(dpr); m_bodyR = R; m_baseBody = QImage(); }
  // the background of spinRect() with the sphere composed on it, once (each frame used to blend the sphere): a turn
  // repaints spinRect() only, so a frame is one copy from it
  const QRect sr = spinRect();
  if (m_baseBody.isNull()) {
    m_baseBody = m_base.copy(QRect((QPointF(sr.topLeft()) * dpr).toPoint(), (QSizeF(sr.size()) * dpr).toSize()));
    m_baseBody.setDevicePixelRatio(dpr);
    QPainter b(&m_baseBody);
    b.translate(-sr.topLeft());
    b.drawPixmap(QPoint(qRound(d.center().x() - R - 2), qRound(d.center().y() - R - 2)), m_body);
  }
  const QRect ex = e->rect();
  p.setCompositionMode(QPainter::CompositionMode_Source);   // opaque: plain copies of the device pixels
  if (!sr.contains(ex)) p.drawImage(QPointF(ex.topLeft()), m_base, QRectF(QPointF(ex.topLeft()) * dpr, QSizeF(ex.size()) * dpr));
  const QRect in = ex & sr;
  if (!in.isEmpty())
    p.drawImage(QPointF(in.topLeft()), m_baseBody, QRectF(QPointF(in.topLeft() - sr.topLeft()) * dpr, QSizeF(in.size()) * dpr));
  p.setCompositionMode(QPainter::CompositionMode_SourceOver);
  m_globe.paintGraticule(p);
  m_globe.paintLand(p);
  p.setRenderHint(QPainter::Antialiasing);
  // tints and highlight
  auto drawCountry = [&](const Country& c, const QColor& fill, const QPen& pen) {
    for (const auto& ring : c.rings) {
      QPainterPath path; bool open = false; int vis = 0;
      for (const auto& pt : ring) {
        bool v = false; const QPointF s = m_globe.project(pt.y(), pt.x(), &v);   // camera synced above
        if (v) { ++vis; if (!open) { path.moveTo(s); open = true; } else path.lineTo(s); }
        else open = false;
      }
      if (vis < 2) continue;
      if (vis == ring.size() && fill.alpha() > 0) { p.setPen(Qt::NoPen); p.setBrush(fill); p.drawPath(path); }
      p.setBrush(Qt::NoBrush); p.setPen(pen); p.drawPath(path);
    }
  };
  for (const auto& c : m_countries) {
    if (const int a = m_tint.value(c.iso, 0); a > 0 && !m_highlight.contains(c.iso) && c.iso != m_hover)
      drawCountry(c, theme::map(245, 197, 24, a), QPen(Qt::NoPen));
    if (m_highlight.contains(c.iso)) drawCountry(c, theme::map(245, 197, 24, 0), QPen(theme::map(255, 220, 90, 210), 1.2));
    else if (c.iso == m_hover) drawCountry(c, theme::map(245, 197, 24, 40), QPen(theme::map(245, 197, 24, 200), 1.1));
  }
  if (!m_hover.isEmpty()) {
    QString name;
    for (const auto& c : m_countries) if (c.iso == m_hover) name = c.name;   // Natural Earth (English) name
    if (const auto* e = macro::economy(m_hover)) name = macro::displayName(*e);   // registry economies: interface language
    if (!name.isEmpty()) {
      const QFont f = eco::cond(15, QFont::Bold);
      p.setFont(f);
      const QRectF r(m_mouse.x() + 16, m_mouse.y() + 14, QFontMetricsF(f).horizontalAdvance(name.toUpper()) + 16, 22);
      p.setPen(QPen(theme::alpha(eco::amber(), 120), 1)); p.setBrush(theme::map(8, 5, 2, 235));
      p.drawRoundedRect(r, 3, 3);
      p.setPen(eco::txt()); p.drawText(r, Qt::AlignCenter, name.toUpper());
    }
  }
}

void GlobeWidget::resizeEvent(QResizeEvent*) { m_bodyR = 0; }
void GlobeWidget::showEvent(QShowEvent*) { m_clock.restart(); m_timer.start(); }
void GlobeWidget::hideEvent(QHideEvent*) { m_timer.stop(); }

void GlobeWidget::mousePressEvent(QMouseEvent* e) {
  double la, lo;
  if (e->button() == Qt::LeftButton && toLatLon(e->position(), &la, &lo)) {
    m_dragging = true; m_dragFrom = e->position(); m_dragRot = m_rot; m_target = NAN;
  }
}
void GlobeWidget::mouseMoveEvent(QMouseEvent* e) {
  m_mouse = e->position();
  if (m_dragging) {
    const double R = disk().width() / 2;
    m_rot = wrap180(m_dragRot - (e->position().x() - m_dragFrom.x()) / R * 60.0);
    m_idle.restart(); update();
    return;
  }
  double la, lo; QString iso;
  if (toLatLon(e->position(), &la, &lo)) iso = countryAt(la, lo);
  const bool changed = iso != m_hover;
  if (changed) {
    m_hover = iso; emit hovered(iso);
    if (iso.isEmpty()) { m_idle.restart(); if (isVisible() && !m_timer.isActive()) { m_clock.restart(); m_timer.start(); } }
  }
  setCursor(iso.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
  // the frame shows the mouse only through the hovered country and its name label (which follows the pointer):
  // over the sea or off the globe with nothing hovered before, there is nothing to redraw
  if (changed || !iso.isEmpty()) update();
}
void GlobeWidget::mouseReleaseEvent(QMouseEvent* e) {
  const bool wasDrag = m_dragging && (e->position() - m_dragFrom).manhattanLength() > 4;
  m_dragging = false; m_idle.restart();
  if (!m_timer.isActive() && isVisible()) { m_clock.restart(); m_timer.start(); }
  if (wasDrag || e->button() != Qt::LeftButton) return;   // the right button opens the page's menu (watchlist)
  double la, lo;
  if (toLatLon(e->position(), &la, &lo)) { const QString iso = countryAt(la, lo); if (!iso.isEmpty()) emit clicked(iso); }
}
QString GlobeWidget::countryAtPoint(const QPointF& pos) const {
  double la, lo;
  return toLatLon(pos, &la, &lo) ? countryAt(la, lo) : QString();
}
void GlobeWidget::leaveEvent(QEvent*) {
  if (!m_hover.isEmpty()) { m_hover.clear(); emit hovered({}); update(); }
  m_idle.restart();
  if (isVisible() && !m_timer.isActive()) { m_clock.restart(); m_timer.start(); }
}
}  // namespace economy
