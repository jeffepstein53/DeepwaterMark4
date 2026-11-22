#include "pages/data/Landscape.h"
#include "ui/Theme.h"
#include <QCoreApplication>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QSurfaceFormat>
#include <QWheelEvent>
#include <cmath>
#include <climits>

namespace macrodatapage {

Landscape::Landscape(QWidget* parent) : QOpenGLWidget(parent) {
  setFormat(macrodata3d::surfaceFormat());
  setMouseTracking(true);
  setMinimumHeight(240);
  m_overlay = new macrodata3d::Overlay(this);
  m_overlay->paint = [this](QPainter& p) { drawLabels(p); };
}

Landscape::~Landscape() { makeCurrent(); m_boxes.release(); m_slicePlane.release(); doneCurrent(); }

int Landscape::span(int row) const {
  const QString f = m_rows[row].chain.def.freq;
  return f == "A" ? 12 : f == "Q" ? 3 : 1;
}

void Landscape::setRows(QVector<Row> rows) {
  // a store change reloads the same chains: the camera (and the selection, same indices) stay where the user left
  // them; only a different set of rows resets them
  bool sameRows = rows.size() == m_rows.size();
  for (int i = 0; sameRows && i < rows.size(); ++i) sameRows = rows[i].chain.def.key == m_rows[i].chain.def.key;
  m_rows = std::move(rows);
  m_z.clear();
  // time span first (it fixes the width), then rows and height scaled to it
  m_m0 = INT_MAX; m_m1 = 0;
  for (int i = 0; i < m_rows.size(); ++i)
    for (const auto& c : m_rows[i].cells) { m_m0 = qMin(m_m0, c.month); m_m1 = qMax(m_m1, c.month + span(i) - 1); }
  if (m_m0 == INT_MAX) { m_m0 = macrodata::monthIndex(QDate::currentDate()) - 120; m_m1 = m_m0 + 132; }
  const float width = (m_m1 - m_m0 + 1) * kX;
  int groups = 1; for (int i = 1; i < m_rows.size(); ++i) if (m_rows[i].group != m_rows[i - 1].group) ++groups;
  kRow = qBound(0.12f, width * 0.95f / qMax(1, int(m_rows.size()) + groups * 4), width * 0.05f);   // few chains: wide rows
  kGap = kRow * 4;
  m_h = qMax(4.0f, width * 0.07f);
  float z = 0;
  QString g;
  for (int i = 0; i < m_rows.size(); ++i) {
    if (i > 0 && m_rows[i].group != g) z += kGap;
    if (i > 0 && !m_rows[i].forecast) z += kRow;
    else if (i > 0) z += kRow * 0.55f;   // a forecast sits close behind the chain it predicts
    g = m_rows[i].group;
    m_z << z;
  }
  if (!sameRows) { m_hover = m_sel = {-1, -1}; resetCamera(); }
  m_dirty = m_sliceDirty = true;
  update();
}

// Only the slice plane moves with the slider: the boxes stay uploaded (the slice highlight is a uniform).
void Landscape::setSlice(int month) { if (month == m_slice) return; m_slice = month; m_sliceDirty = true; update(); }

void Landscape::resetCamera() {
  const float w = (m_m1 - m_m0 + 1) * kX, d = m_z.isEmpty() ? 10 : m_z.last();
  m_cam.target = QVector3D(w * 0.5f, 0, d * 0.5f);
  m_cam.dist = qMax(30.0f, qMax(w, d) * 1.6f);   // whole landscape in view, labels included
  m_cam.yaw = -24; m_cam.pitch = 34;
  update();
}

void Landscape::initializeGL() { m_glReady = m_boxes.init() && m_slicePlane.init(); m_dirty = m_sliceDirty = true; }

void Landscape::rebuild() {
  QVector<macrodata3d::Instance> v;
  int n = 0; for (const auto& r : m_rows) n += int(r.cells.size());
  v.reserve(n * 1.1 + 8);
  const QColor warn = theme::pal().warn;
  for (int i = 0; i < m_rows.size(); ++i) {
    const Row& r = m_rows[i];
    const int sp = span(i);
    const float z = m_z[i];
    for (const auto& c : r.cells) {
      const float x = (c.month - m_m0 + sp * 0.5f) * kX;
      const float h = height(c);
      // brightness follows the rank: the family colour from dim (lowest) to full (highest)
      const float k = 0.30f + 0.70f * c.rank;
      const QColor col = r.colour;
      macrodata3d::Instance in{x, 0, z, sp * kX * 0.86f, h, kRow * (r.forecast ? 0.45f : 0.72f),
                           float(col.redF() * k), float(col.greenF() * k), float(col.blueF() * k), r.forecast ? 0.5f : 1.0f,
                           float(c.month), float(i), 0, float(sp)};
      v << in;
      if (c.revisions > 0)   // revised month: a thin orange slab on top
        v << macrodata3d::Instance{x, h, z, sp * kX * 0.86f, m_h * 0.02f, kRow * 0.72f, float(warn.redF()), float(warn.greenF()), float(warn.blueF()), 1.0f,
                               float(c.month), float(i), 0, float(sp)};
    }
  }
  // today: a translucent vertical plane across all chains (the slice plane is its own buffer, see rebuildSlice)
  plane(v, macrodata::monthIndex(QDate::currentDate()), theme::pal().accent, 0.16f);
  m_boxes.setInstances(v);
  m_dirty = false;
}

void Landscape::plane(QVector<macrodata3d::Instance>& v, int month, const QColor& col, float alpha) const {
  const float depth = (m_z.isEmpty() ? 10 : m_z.last()) + kRow * 2;
  const float x = (month - m_m0 + 0.5f) * kX;
  v << macrodata3d::Instance{x, 0, depth * 0.5f - 1, 0.06f, m_h * 1.15f, depth, float(col.redF()), float(col.greenF()), float(col.blueF()), alpha, -1, -1, -1, 0};
}

// The slice date's plane, drawn after the boxes (it was the last instance of their buffer: same blending order).
void Landscape::rebuildSlice() {
  QVector<macrodata3d::Instance> v;
  if (m_slice >= 0) plane(v, m_slice, theme::pal().text, 0.22f);
  m_slicePlane.setInstances(v);
  m_sliceDirty = false;
}

void Landscape::paintGL() {
  const auto& c = theme::pal();
  glClearColor(c.bg.redF(), c.bg.greenF(), c.bg.blueF(), 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  if (!m_glReady) return;
  if (m_dirty) rebuild();
  if (m_sliceDirty) rebuildSlice();
  const QMatrix4x4 vp = m_cam.proj(size()) * m_cam.view();
  m_boxes.draw(vp, m_slice >= 0 ? 0 : -1, float(m_slice), 1.0f);
  m_slicePlane.draw(vp);

  // labels on top (QPainter over the GL frame)
  m_overlay->update();
}

void Landscape::drawLabels(QPainter& p) {
  const auto& c = theme::pal();
  const QSize s = size();
  // years along the front edge
  const int years = (m_m1 - m_m0) / 12;
  const int step = years > 60 ? 10 : years > 25 ? 5 : years > 10 ? 2 : 1;
  p.setFont(theme::mono(9)); p.setPen(c.textMute);
  for (int y = (m_m0 / 12 / step) * step; y * 12 <= m_m1; y += step) {
    QPointF pt;
    const float front = (m_z.isEmpty() ? 0 : m_z.last()) + kRow * 4;   // the edge facing the camera
    if (m_cam.project(QVector3D((y * 12 - m_m0) * kX, 0, front), s, &pt) && rect().contains(pt.toPoint()))
      p.drawText(QRectF(pt.x() - 30, pt.y(), 60, 14), Qt::AlignCenter, QString::number(y));
  }
  // plateau names at the left edge
  QString g;
  QList<QRectF> used;   // a label that would overlap another is skipped (small plateaus)
  for (int i = 0; i < m_rows.size(); ++i) {
    if (m_rows[i].group == g) continue;
    g = m_rows[i].group;
    // the label sits at the middle of its plateau
    int last = i; while (last + 1 < m_rows.size() && m_rows[last + 1].group == g) ++last;
    QPointF pt;
    if (!m_cam.project(QVector3D(-(m_m1 - m_m0) * kX * 0.015f, 0, (m_z[i] + m_z[last]) * 0.5f), s, &pt) || !rect().contains(pt.toPoint())) continue;
    const QRectF r(pt.x() - 220, pt.y() - 8, 216, 16);
    bool clash = false;
    for (const auto& u : used) if (u.adjusted(0, -2, 0, 2).intersects(r)) clash = true;
    if (clash) continue;
    used << r;
    p.setFont(theme::mono(9, QFont::Bold, 1.4)); p.setPen(m_rows[i].colour);
    p.drawText(r, Qt::AlignRight | Qt::AlignVCenter, g);
  }
  // the hovered / selected chain's name next to its row
  for (const auto& hs : {m_hover, m_sel}) {
    if (hs.first < 0) continue;
    const Row& r = m_rows[hs.first];
    QPointF pt;
    const auto& cell = r.cells[hs.second];
    if (!m_cam.project(QVector3D((cell.month - m_m0 + 0.5f) * kX, height(cell) + m_h * 0.05f, m_z[hs.first]), s, &pt)) continue;
    const QString t = r.chain.def.subject + " · " + QCoreApplication::translate("data", r.chain.def.indicator.toUtf8().constData());
    p.setFont(theme::sans(11));
    const QRectF br = QFontMetricsF(p.font()).boundingRect(t).adjusted(-6, -3, 6, 3).translated(pt.x() + 8, pt.y() - 12);
    p.setPen(QPen(r.colour, 1)); p.setBrush(theme::alpha(c.panel, 230)); p.drawRoundedRect(br, 3, 3);
    p.setPen(c.text); p.drawText(br, Qt::AlignCenter, t);
  }
  if (m_rows.isEmpty()) {
    p.setFont(theme::sans(12)); p.setPen(c.textMute);
    p.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap,
               QCoreApplication::translate("data", "Tick chains on the left (or TICK ALL) to raise the landscape."));
  }
}

QPair<int, int> Landscape::pick(const QPointF& pos) const {
  QVector3D o, d;
  m_cam.ray(pos, size(), &o, &d);
  if (std::abs(d.z()) < 1e-6f) return {-1, -1};
  float best = 1e30f;
  QPair<int, int> hit{-1, -1};
  for (int i = 0; i < m_rows.size(); ++i) {
    const float t = (m_z[i] - o.z()) / d.z();
    if (t <= 0 || t >= best) continue;
    const float x = o.x() + t * d.x(), y = o.y() + t * d.y();
    const float month = m_m0 + x / kX;
    const auto& cells = m_rows[i].cells;
    const int sp = span(i);
    auto it = std::upper_bound(cells.begin(), cells.end(), month, [](float m, const macrodata::MonthCell& c) { return m < float(c.month); });
    if (it == cells.begin()) continue;
    --it;
    if (month >= it->month + sp) continue;
    if (y < -m_h * 0.02f || y > height(*it) + m_h * 0.02f) continue;
    best = t; hit = {i, int(it - cells.begin())};
  }
  return hit;
}

QString Landscape::describe(int row, int cell) const {
  if (row < 0) return {};
  const Row& r = m_rows[row];
  const auto& c = r.cells[cell];
  const QDate d = macrodata::monthDate(c.month);
  const QString period = r.chain.def.freq == "A" ? QString::number(d.year())
                       : r.chain.def.freq == "Q" ? QString("Q%1 %2").arg((d.month() - 1) / 3 + 1).arg(d.year()) : QLocale().toString(d, "MMM yyyy");
  QString v = r.chain.def.unit == "%" ? QString::number(c.value, 'f', 2) + "%" : QLocale(QLocale::English).toString(double(c.value), 'g', 6);
  QString t = QCoreApplication::translate("data", "%1 · %2 — %3: %4 — rank %5 / 100 in its chain")
                  .arg(r.chain.def.subject, QCoreApplication::translate("data", r.chain.def.indicator.toUtf8().constData()), period, v)
                  .arg(qRound(c.rank * 100));
  if (c.revisions) t += "  ·  " + QCoreApplication::translate("data", "%n revision(s)", nullptr, c.revisions);
  if (r.forecast) t += "  ·  " + QCoreApplication::translate("data", "forecast");
  return t;
}

void Landscape::mousePressEvent(QMouseEvent* e) { m_press = m_last = e->position(); m_moved = false; }

void Landscape::mouseMoveEvent(QMouseEvent* e) {
  const QPointF d = e->position() - m_last;
  m_last = e->position();
  if (e->buttons() != Qt::NoButton) {
    if ((e->position() - m_press).manhattanLength() > 3) m_moved = true;
    if ((e->buttons() & Qt::RightButton) || (e->modifiers() & Qt::ShiftModifier)) {
      // move the target on the ground, along the camera's right and forward directions
      const float y = qDegreesToRadians(m_cam.yaw);
      const QVector3D right(std::cos(y), 0, -std::sin(y)), fwd(-std::sin(y), 0, -std::cos(y));
      const float k = m_cam.dist * 0.0018f;
      m_cam.target += (-float(d.x()) * right + float(d.y()) * fwd) * k;
    } else {
      m_cam.yaw -= float(d.x()) * 0.35f;
      m_cam.pitch = qBound(5.0f, m_cam.pitch + float(d.y()) * 0.3f, 88.0f);
    }
    update();
    return;
  }
  const auto h = pick(e->position());
  if (h != m_hover) {
    m_hover = h;
    if (onInfo) onInfo(h.first >= 0 ? describe(h.first, h.second) : (m_sel.first >= 0 ? describe(m_sel.first, m_sel.second) : QString()));
    update();
  }
}

void Landscape::mouseReleaseEvent(QMouseEvent* e) {
  if (m_moved) return;
  m_sel = pick(e->position());
  if (onInfo) onInfo(describe(m_sel.first, m_sel.second));
  update();
}

void Landscape::mouseDoubleClickEvent(QMouseEvent* e) {
  const auto h = pick(e->position());
  if (h.first >= 0 && onOpen2D) onOpen2D(m_rows[h.first].chain.def.key, m_rows[h.first].cells[h.second].month);
}

void Landscape::wheelEvent(QWheelEvent* e) {
  const double steps = e->angleDelta().y() / 120.0;
  m_cam.dist = qBound(3.0f, float(m_cam.dist * std::pow(0.88, steps)), 6000.0f);
  update();
}
}  // namespace macrodatapage
