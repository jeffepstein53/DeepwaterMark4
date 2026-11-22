#include "pages/data/MacroCube.h"
#include "ui/Theme.h"
#include <QCoreApplication>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QSurfaceFormat>
#include <QWheelEvent>
#include <cmath>

namespace macrodatapage {
namespace {
// diverging scale: lowest of the series' history → blue, middle → grey, highest → orange
QColor rankColour(float r) {
  const bool dark = theme::isDark();
  const QColor lo = dark ? QColor("#3b82f6") : QColor("#1d4ed8"), mid = dark ? QColor("#64748b") : QColor("#94a3b8"),
               hi = dark ? QColor("#f97316") : QColor("#c2410c");
  const QColor a = r < 0.5f ? lo : mid, b = r < 0.5f ? mid : hi;
  const float t = r < 0.5f ? r * 2 : (r - 0.5f) * 2;
  return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t, a.blueF() + (b.blueF() - a.blueF()) * t);
}
}  // namespace

MacroCube::MacroCube(QWidget* parent) : QOpenGLWidget(parent) {
  setFormat(macrodata3d::surfaceFormat());
  setMouseTracking(true);
  setMinimumHeight(240);
  m_overlay = new macrodata3d::Overlay(this);
  m_overlay->paint = [this](QPainter& p) { drawLabels(p); };
}

MacroCube::~MacroCube() { makeCurrent(); m_boxes.release(); doneCurrent(); }

void MacroCube::setData(const QStringList& countries, const QStringList& indicators, QVector<Series> series) {
  // a store change reloads the same cube: the camera stays where the user left it; new axes (or a new time span)
  // frame the view again
  const bool sameAxes = countries == m_countries && indicators == m_indicators;
  const int oldM0 = m_m0, oldM1 = m_m1;
  m_countries = countries; m_indicators = indicators; m_series = std::move(series);
  m_index.clear();
  // the hovered series / cell are indexes into the old data: they could point past the new one (read in drawLabels)
  m_hoverS = -1; m_hoverC = -1;
  if (onInfo) onInfo({});
  m_m0 = INT_MAX; m_m1 = 0;
  for (int i = 0; i < m_series.size(); ++i) {
    const auto& s = m_series[i];
    m_index.insert({s.country, s.indicator}, i);
    for (const auto& c : s.cells) { m_m0 = qMin(m_m0, c.month); m_m1 = qMax(m_m1, c.month + s.span - 1); }
  }
  if (m_m0 == INT_MAX) { m_m0 = macrodata::monthIndex(QDate::currentDate()) - 120; m_m1 = m_m0 + 120; }
  if (m_axis == Date && (m_value < m_m0 || m_value > m_m1)) m_value = macrodata::monthIndex(QDate::currentDate().addMonths(-3));
  m_dirty = true;
  if (sameAxes && m_m0 == oldM0 && m_m1 == oldM1) update();
  else frame();
}

void MacroCube::setSlice(Axis axis, int value) {
  const bool turned = axis != m_axis;
  m_axis = axis; m_value = value;
  m_hoverS = -1;
  if (turned) frame(); else update();
}

QString MacroCube::sliceTitle() const {
  if (m_axis == Date) return QLocale().toString(macrodata::monthDate(m_value), "MMMM yyyy");
  if (m_axis == Country) return m_countries.value(m_value);
  return m_indicators.value(m_value);
}

void MacroCube::frame() {
  const float w = xOf(m_m1 + 1), h = yOf(int(m_countries.size())), d = zOf(int(m_indicators.size()));
  switch (m_axis) {
    case Date:      // the month's plane seen from the future side, its history receding behind it
      m_cam.target = QVector3D(xOf(m_value), h * 0.5f, d * 0.5f); m_cam.yaw = 72; m_cam.pitch = 10; m_cam.dist = qMax(h, d) * 1.35f; break;
    case Country:   // a horizontal plane: from above
      m_cam.target = QVector3D(w * 0.5f, yOf(m_value), d * 0.5f); m_cam.yaw = 0; m_cam.pitch = 68; m_cam.dist = qMax(w, d) * 1.05f; break;
    case Indicator: // a vertical plane facing the viewer
      m_cam.target = QVector3D(w * 0.5f, h * 0.5f, zOf(m_value)); m_cam.yaw = 8; m_cam.pitch = 8; m_cam.dist = qMax(w, h) * 1.05f; break;
  }
  update();
}

void MacroCube::initializeGL() { m_glReady = m_boxes.init(); m_dirty = true; }

void MacroCube::rebuild() {
  QVector<macrodata3d::Instance> v;
  int n = 0; for (const auto& s : m_series) n += int(s.cells.size());
  v.reserve(n);
  for (const auto& s : m_series) {
    for (const auto& c : s.cells) {
      const QColor col = rankColour(c.rank);
      v << macrodata3d::Instance{xOf(c.month) + s.span * kX * 0.5f, yOf(s.country), zOf(s.indicator), s.span * kX * 0.92f, kY * 0.82f, kZ * 0.8f,
                             float(col.redF()), float(col.greenF()), float(col.blueF()), 1.0f,
                             float(c.month), float(s.country), float(s.indicator), float(s.span)};
    }
  }
  m_boxes.setInstances(v);
  m_dirty = false;
}

void MacroCube::paintGL() {
  const auto& c = theme::pal();
  glClearColor(c.bg.redF(), c.bg.greenF(), c.bg.blueF(), 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  if (!m_glReady) return;
  if (m_dirty) rebuild();
  // the slice faces the camera; what lies between them is hidden, what lies behind stays as a faint ghost
  m_boxes.draw(m_cam.proj(size()) * m_cam.view(), int(m_axis), float(m_value), 0.03f, true);

  m_overlay->update();
}

void MacroCube::drawLabels(QPainter& p) {
  const auto& c = theme::pal();
  const QSize s = size();
  // labels of the slice's two axes, skipping those that would overlap
  auto labels = [&](const QList<QPair<QVector3D, QString>>& items, Qt::Alignment al, const QColor& col) {
    p.setFont(theme::sans(10)); p.setPen(col);
    QList<QRectF> used;
    for (const auto& [pt3, text] : items) {
      QPointF pt;
      if (!m_cam.project(pt3, s, &pt) || !rect().contains(pt.toPoint())) continue;
      const QRectF r = al & Qt::AlignRight ? QRectF(pt.x() - 184, pt.y() - 7, 180, 14) : QRectF(pt.x() + 4, pt.y() - 7, 180, 14);
      bool clash = false;
      for (const auto& u : used) if (u.adjusted(0, -2, 0, 2).intersects(r)) { clash = true; break; }
      if (clash) continue;
      used << r;
      p.drawText(r, al | Qt::AlignVCenter, text);
    }
  };
  QList<QPair<QVector3D, QString>> rows, cols;
  const float h = yOf(int(m_countries.size())), d = zOf(int(m_indicators.size()));
  if (m_axis == Date || m_axis == Indicator) {
    const float x = m_axis == Date ? xOf(m_value) : -1.0f;
    const float z = m_axis == Date ? -1.2f : zOf(m_value);
    for (int i = 0; i < m_countries.size(); ++i) rows << qMakePair(QVector3D(x, yOf(i) + kY * 0.4f, z), m_countries[i]);
    labels(rows, Qt::AlignRight, c.textDim);
  }
  if (m_axis == Date || m_axis == Country) {
    const float x = m_axis == Date ? xOf(m_value) : -1.0f;
    const float y = m_axis == Date ? h + 0.8f : yOf(m_value) + kY;
    for (int k = 0; k < m_indicators.size(); ++k) cols << qMakePair(QVector3D(x, y, zOf(k)), m_indicators[k]);
    labels(cols, m_axis == Date ? Qt::AlignLeft : Qt::AlignRight, c.textDim);
  }
  if (m_axis != Date) {   // years along the time axis
    QList<QPair<QVector3D, QString>> yrs;
    const int years = (m_m1 - m_m0) / 12, step = years > 60 ? 10 : years > 25 ? 5 : 2;
    const float y = m_axis == Country ? yOf(m_value) : -1.2f, z = m_axis == Country ? d + 1.0f : zOf(m_value);
    for (int yr = (m_m0 / 12 / step) * step; yr * 12 <= m_m1; yr += step) yrs << qMakePair(QVector3D(xOf(yr * 12), y, z), QString::number(yr));
    labels(yrs, Qt::AlignLeft, c.textMute);
  }
  // hovered cell
  if (m_hoverS >= 0 && m_hoverS < m_series.size() && m_hoverC >= 0 && m_hoverC < m_series[m_hoverS].cells.size()) {
    const auto& sr = m_series[m_hoverS];
    const auto& cell = sr.cells[m_hoverC];
    QPointF pt;
    if (m_cam.project(QVector3D(xOf(cell.month) + sr.span * kX * 0.5f, yOf(sr.country) + kY, zOf(sr.indicator)), s, &pt)) {
      const QString t = m_countries.value(sr.country) + " · " + m_indicators.value(sr.indicator);
      p.setFont(theme::sans(11));
      const QRectF br = QFontMetricsF(p.font()).boundingRect(t).adjusted(-6, -3, 6, 3).translated(pt.x() + 8, pt.y() - 12);
      p.setPen(QPen(rankColour(cell.rank), 1)); p.setBrush(theme::alpha(c.panel, 230)); p.drawRoundedRect(br, 3, 3);
      p.setPen(c.text); p.drawText(br, Qt::AlignCenter, t);
    }
  }
  if (m_series.isEmpty()) {
    p.setFont(theme::sans(12)); p.setPen(c.textMute);
    p.drawText(rect(), Qt::AlignCenter, QCoreApplication::translate("data", "No macro chain yet: the import fills them."));
  }
}

int MacroCube::pick(const QPointF& pos, int* cellOut) const {
  QVector3D o, d;
  m_cam.ray(pos, size(), &o, &d);
  float t = 0;
  if (m_axis == Date) { if (std::abs(d.x()) < 1e-6f) return -1; t = (xOf(m_value) + kX * 0.5f - o.x()) / d.x(); }
  else if (m_axis == Country) { if (std::abs(d.y()) < 1e-6f) return -1; t = (yOf(m_value) + kY * 0.4f - o.y()) / d.y(); }
  else { if (std::abs(d.z()) < 1e-6f) return -1; t = (zOf(m_value) - o.z()) / d.z(); }
  if (t <= 0) return -1;
  const QVector3D p = o + t * d;
  const int month = m_axis == Date ? m_value : m_m0 + int(std::floor(p.x() / kX));
  const int country = m_axis == Country ? m_value : int(std::floor(p.y() / kY + 0.05f));
  const int ind = m_axis == Indicator ? m_value : int(std::lround(p.z() / kZ));
  const int si = m_index.value({country, ind}, -1);
  if (si < 0) return -1;
  const auto& cells = m_series[si].cells;
  auto it = std::upper_bound(cells.begin(), cells.end(), month, [](int m, const macrodata::MonthCell& c) { return m < c.month; });
  if (it == cells.begin()) return -1;
  --it;
  if (month >= it->month + m_series[si].span) return -1;
  *cellOut = int(it - cells.begin());
  return si;
}

QString MacroCube::describe(int si, int cell) const {
  if (si < 0) return {};
  const auto& s = m_series[si];
  const auto& c = s.cells[cell];
  const QDate d = macrodata::monthDate(c.month);
  const QString period = s.span == 12 ? QString::number(d.year()) : s.span == 3 ? QString("Q%1 %2").arg((d.month() - 1) / 3 + 1).arg(d.year())
                                                                                : QLocale().toString(d, "MMM yyyy");
  const QString v = s.unit == "%" ? QString::number(c.value, 'f', 2) + "%" : QLocale(QLocale::English).toString(double(c.value), 'g', 6);
  return QCoreApplication::translate("data", "%1 · %2 — %3: %4 — rank %5 / 100 in its chain")
      .arg(m_countries.value(s.country), m_indicators.value(s.indicator), period, v).arg(qRound(c.rank * 100));
}

void MacroCube::mousePressEvent(QMouseEvent* e) { m_press = m_last = e->position(); m_moved = false; }

void MacroCube::mouseMoveEvent(QMouseEvent* e) {
  const QPointF d = e->position() - m_last;
  m_last = e->position();
  if (e->buttons() != Qt::NoButton) {
    if ((e->position() - m_press).manhattanLength() > 3) m_moved = true;
    if ((e->buttons() & Qt::RightButton) || (e->modifiers() & Qt::ShiftModifier)) {
      const float y = qDegreesToRadians(m_cam.yaw);
      const QVector3D right(std::cos(y), 0, -std::sin(y));
      const float k = m_cam.dist * 0.0018f;
      m_cam.target += (-float(d.x()) * right + float(d.y()) * QVector3D(0, 1, 0)) * k;
    } else {
      m_cam.yaw -= float(d.x()) * 0.35f;
      m_cam.pitch = qBound(-80.0f, m_cam.pitch + float(d.y()) * 0.3f, 88.0f);
    }
    update();
    return;
  }
  int cell = -1;
  const int si = pick(e->position(), &cell);
  if (si != m_hoverS || cell != m_hoverC) {
    m_hoverS = si; m_hoverC = cell;
    if (onInfo) onInfo(describe(si, cell));
    update();
  }
}

void MacroCube::mouseReleaseEvent(QMouseEvent*) {}

void MacroCube::mouseDoubleClickEvent(QMouseEvent* e) {
  int cell = -1;
  const int si = pick(e->position(), &cell);
  if (si >= 0 && onOpen2D) onOpen2D(m_series[si].key, m_series[si].cells[cell].month);
}

void MacroCube::wheelEvent(QWheelEvent* e) {
  const double steps = e->angleDelta().y() / 120.0;
  m_cam.dist = qBound(3.0f, float(m_cam.dist * std::pow(0.88, steps)), 4000.0f);
  update();
}
}  // namespace macrodatapage
