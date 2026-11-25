#include "pages/market/watch/SheetCards.h"
#include "pages/market/watch/WatchStyle.h"
#include "seasonality/Seasonality.h"
#include "core/I18n.h"
#include <QCoreApplication>
#include "ui/Chart.h"
#include "ui/Format.h"
#include "ui/Theme.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QDateTime>
#include <QTimeZone>
#include <algorithm>

namespace watch {
using namespace watch::col;
namespace {
qreal adv(const QFont& f, const QString& s) { return QFontMetricsF(f).horizontalAdvance(s); }
QFont cardTitleFont() { QFont f = theme::condensed(12, QFont::DemiBold); f.setLetterSpacing(QFont::AbsoluteSpacing, 0.7); return f; }
QFont axisSans() { return theme::sans(11); }   // index performance and seasonality axes (proportional in the reference)
// chart tones designed for Deepwater, mapped to the current theme where painted
QColor kAxis() { return theme::map(QColor(0xa1, 0xa1, 0xa1)); }
QColor kAxisDim() { return theme::map(QColor(0x8a, 0x87, 0x7e)); }
QColor kGrid() { return theme::map(QColor(0x1a, 0x1a, 0x17)); }
QColor kZero() { return theme::map(QColor(0x3e, 0x3a, 0x20)); }
// month names from the interface locale ("Jan" / "Janv.")
QString monthShort(int m0) { QString s = i18n::locale().monthName(m0 + 1, QLocale::ShortFormat); if (!s.isEmpty()) s[0] = s[0].toUpper(); return s; }

void centeredNote(QPainter& p, const QRectF& r, const QString& s, const QString& sub = {}) {
  p.setFont(theme::condensed(14, QFont::Medium)); p.setPen(theme::map(QColor(0xa8, 0xa4, 0x9c)));
  if (sub.isEmpty()) { p.drawText(r, Qt::AlignCenter, s); return; }
  p.drawText(QRectF(r.left(), r.center().y() - 16, r.width(), 16), Qt::AlignCenter, s);
  p.setFont(theme::mono(9, QFont::Normal, 1.2)); p.setPen(theme::map(QColor(0x3e, 0x3b, 0x35)));
  p.drawText(QRectF(r.left() + 8, r.center().y() + 2, r.width() - 16, r.height() / 2 - 4), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, sub);
}
// tilted pie / donut ring with extruded walls. Slices: (fraction, top colour, side colour), start at 12 o'clock,
// first slice counter-clockwise (the reference draws SHORT left of 12, LONG on the right).
struct Slice { double frac; QColor top, side; };
QPainterPath ellArc(const QPointF& c, qreal rx, qreal ry, double a0, double a1, qreal dy = 0) {
  QPainterPath p; const QRectF r(c.x() - rx, c.y() - ry + dy, 2 * rx, 2 * ry);
  p.arcMoveTo(r, a0); p.arcTo(r, a0, a1 - a0); return p;
}
void pie3d(QPainter& p, const QPointF& c, qreal rx, qreal ry, qreal depth, const QVector<Slice>& slices, qreal hole = 0) {
  // angles in Qt degrees (counter-clockwise from 3 o'clock); 12 o'clock = 90
  QVector<QPair<double, double>> ang; double a = 90;
  for (const auto& s : slices) { ang << qMakePair(a, a + s.frac * 360); a += s.frac * 360; }
  // outer front walls (visible for angles in 180..360)
  for (int i = 0; i < slices.size(); ++i) {
    for (int k = -1; k <= 1; ++k) {
      const double lo = std::max(ang[i].first, 180.0 + 360 * k), hi = std::min(ang[i].second, 360.0 + 360 * k);
      if (hi <= lo) continue;
      QPainterPath w = ellArc(c, rx, ry, lo, hi);
      const QRectF rb(c.x() - rx, c.y() - ry + depth, 2 * rx, 2 * ry);
      w.arcTo(rb, hi, lo - hi); w.closeSubpath();
      QLinearGradient g(c.x() - rx, 0, c.x() + rx, 0);
      g.setColorAt(0, slices[i].side.darker(125)); g.setColorAt(0.5, slices[i].side); g.setColorAt(1, slices[i].side.darker(140));
      p.setPen(Qt::NoPen); p.setBrush(g); p.drawPath(w);
    }
  }
  // top faces
  for (int i = 0; i < slices.size(); ++i) {
    QPainterPath t;
    const QRectF r(c.x() - rx, c.y() - ry, 2 * rx, 2 * ry);
    if (hole <= 0) { t.moveTo(c); t.arcTo(r, ang[i].first, ang[i].second - ang[i].first); t.closeSubpath(); }
    else {
      const QRectF ri(c.x() - rx * hole, c.y() - ry * hole, 2 * rx * hole, 2 * ry * hole);
      t.arcMoveTo(r, ang[i].first); t.arcTo(r, ang[i].first, ang[i].second - ang[i].first);
      t.arcTo(ri, ang[i].second, ang[i].first - ang[i].second); t.closeSubpath();
    }
    QRadialGradient g(c - QPointF(rx * .25, ry * .4), rx * 1.3);
    g.setColorAt(0, slices[i].top.lighter(112)); g.setColorAt(1, slices[i].top.darker(108));
    p.setPen(QPen(theme::alpha(slices[i].top.lighter(130), 120), 0.8)); p.setBrush(g); p.drawPath(t);
  }
  // inner walls of the hole (back half, angles 0..180, seen through the hole)
  if (hole > 0) {
    const qreal hx = rx * hole, hy = ry * hole;
    QPainterPath clip; clip.addEllipse(c, hx, hy);
    p.save(); p.setClipPath(clip);
    p.setPen(Qt::NoPen); p.setBrush(theme::map(QColor(0x05, 0x04, 0x03))); p.drawEllipse(c, hx, hy);
    for (int i = 0; i < slices.size(); ++i) for (int k = -1; k <= 1; ++k) {
      const double lo = std::max(ang[i].first, 0.0 + 360 * k), hi = std::min(ang[i].second, 180.0 + 360 * k);
      if (hi <= lo) continue;
      QPainterPath w = ellArc(c, hx, hy, lo, hi);
      w.arcTo(QRectF(c.x() - hx, c.y() - hy + depth, 2 * hx, 2 * hy), hi, lo - hi); w.closeSubpath();
      p.setBrush(slices[i].side.darker(130)); p.drawPath(w);
    }
    p.restore();
  }
}
QPointF ellPoint(const QPointF& c, qreal rx, qreal ry, double deg) {
  const double r = deg * M_PI / 180; return {c.x() + rx * std::cos(r), c.y() - ry * std::sin(r)};
}
}

// ================================================================== SheetCard
SheetCard::SheetCard(const QString& title, QWidget* parent) : QWidget(parent), m_title(title) {
  m_spin.setInterval(50);
  connect(&m_spin, &QTimer::timeout, this, [this] { m_phase = (m_phase + 1) % 24; update(); });
}
void SheetCard::setState(State s, const QString& msg) {
  const bool changed = s != m_state;
  m_state = s; m_message = msg; m_layer = QImage(); syncSpinner(); update();
  if (changed) emit stateChanged();
}
void SheetCard::syncSpinner() { if (m_state == Computing && isVisible()) m_spin.start(); else m_spin.stop(); }
void SheetCard::showEvent(QShowEvent*) { syncSpinner(); }
void SheetCard::hideEvent(QHideEvent*) { m_spin.stop(); }
void SheetCard::paintSpinner(QPainter& p, const QPointF& c, qreal r) {
  p.save(); p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(theme::map(QColor(0x3a, 0x37, 0x31)), 1.6)); p.setBrush(Qt::NoBrush); p.drawEllipse(c, r, r);
  p.setPen(QPen(theme::map(QColor(0xc8, 0xc4, 0xbc)), 1.6, Qt::SolidLine, Qt::RoundCap));
  p.drawArc(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), -m_phase * 15 * 16, 100 * 16);
  p.restore();
}
void SheetCard::paintEvent(QPaintEvent*) {
  if (m_layered && m_state == Ready && card().alpha() == 255) {
    const qreal dpr = devicePixelRatioF();
    if (m_layer.isNull() || m_layer.size() != size() * dpr || m_layer.devicePixelRatio() != dpr) {
      m_layer = QImage(size() * dpr, QImage::Format_ARGB32_Premultiplied);
      m_layer.setDevicePixelRatio(dpr);
      m_layer.fill(Qt::transparent);
      QPainter q(&m_layer); q.setRenderHint(QPainter::Antialiasing); q.setRenderHint(QPainter::TextAntialiasing);
      paintFrame(q);
      paintBody(q, body());
    }
    QPainter p(this);
    p.drawImage(QPointF(0, 0), m_layer);   // opaque: a copy
    p.setRenderHint(QPainter::Antialiasing); p.setRenderHint(QPainter::TextAntialiasing);
    paintHover(p, body());
    return;
  }
  QPainter p(this); p.setRenderHint(QPainter::Antialiasing); p.setRenderHint(QPainter::TextAntialiasing);
  if (m_frameless && m_state == Computing) {
    p.fillRect(rect(), theme::map(QColor(0x04, 0x03, 0x00)));
    paintSpinner(p, QPointF(width() / 2.0, height() / 2.0 - 8), 6);
    p.setFont(theme::condensed(12, QFont::Medium)); p.setPen(theme::map(QColor(0xb4, 0xb0, 0xa8)));
    p.drawText(QRectF(0, height() / 2.0 + 5, width(), 16), Qt::AlignCenter, m_message.isEmpty() ? QCoreApplication::translate("watch", "Computing...") : m_message);
    return;
  }
  paintFrame(p);
  const QRectF b = body();
  if (m_state == Ready) { paintBody(p, b); paintHover(p, b); return; }
  if (paintPlaceholder(p, b, m_state)) return;
  switch (m_state) {
    case Loading: p.setFont(theme::condensed(12, QFont::Medium)); p.setPen(theme::map(QColor(0x45, 0x42, 0x3c))); p.drawText(b, Qt::AlignCenter, QCoreApplication::translate("watch", "Loading...")); break;
    case Computing:
      paintSpinner(p, b.center() - QPointF(0, 10), 6);
      p.setFont(theme::condensed(12, QFont::Medium)); p.setPen(theme::map(QColor(0xb4, 0xb0, 0xa8)));
      p.drawText(QRectF(b.left(), b.center().y() + 4, b.width(), 16), Qt::AlignCenter, m_message.isEmpty() ? QCoreApplication::translate("watch", "Computing...") : m_message);
      break;
    case Connecting: centeredNote(p, b, QCoreApplication::translate("watch", "Connecting…"), m_source.toUpper()); break;
    case NotConnected: centeredNote(p, b, m_message.isEmpty() ? QCoreApplication::translate("watch", "Not connected") : m_message, QCoreApplication::translate("watch", "DATA SOURCE NOT CONNECTED.")); break;
    case Message: centeredNote(p, b, m_message); break;
    case Ready: break;
  }
}

void SheetCard::paintFrame(QPainter& p) {
  p.fillRect(rect(), card());
  p.setPen(QPen(cardBorder(), 1)); p.setBrush(Qt::NoBrush); p.drawRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5));
  p.fillRect(QRectF(1, 28, width() - 2, 1), cardRule());
  const QFont tf = cardTitleFont();
  p.setFont(tf); p.setPen(cardTitle()); p.drawText(QPointF(9, 18.5), m_title.toUpper());
  if (!m_suffix.isEmpty()) {
    p.setFont(theme::sans(9)); p.setPen(theme::map(QColor(0x3a, 0x37, 0x31)));
    p.drawText(QPointF(9 + adv(tf, m_title.toUpper()) + 7, 18.5), m_suffix);
  }
}

// ================================================================== Retail
RetailCard::RetailCard(QWidget* parent) : SheetCard(QCoreApplication::translate("watch", "Retail Position"), parent) { setState(Connecting); }
void RetailCard::setRetail(const RetailData& d) {
  m_d = d;
  setToolTip(QString("%1 · %2").arg(d.source, d.updated.isValid() ? i18n::locale().toString(d.updated.toLocalTime(), "dd MMM yyyy HH:mm") : QString()));
  setState(Ready);
}
bool RetailCard::paintPlaceholder(QPainter& p, const QRectF& b, State s) {
  if (s != NotConnected && s != Connecting) return false;
  // plug with a spark + two lines (reference loading frame: icon y+37, line y+58, caps line y+81)
  const qreal oy = std::max<qreal>(0, (height() - 93) / 2.0);   // centred when the card has its full height
  const QPointF c(b.center().x(), 37 + oy);
  p.save(); p.translate(c); p.rotate(-45);
  p.setPen(QPen(theme::map(QColor(0xa8, 0xa4, 0x9c)), 1.3, Qt::SolidLine, Qt::RoundCap)); p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(QRectF(-3.5, -2, 7, 7), 1.5, 1.5);
  p.drawLine(QPointF(-1.6, -2), QPointF(-1.6, -5)); p.drawLine(QPointF(1.6, -2), QPointF(1.6, -5)); p.drawLine(QPointF(0, 5), QPointF(0, 9));
  p.restore();
  p.setPen(QPen(theme::map(QColor(0xa8, 0xa4, 0x9c)), 1.2));
  p.drawPolyline(QPolygonF{c + QPointF(4, -8), c + QPointF(2, -4), c + QPointF(6, -4), c + QPointF(3, 0)});
  p.setFont(theme::condensed(13, QFont::Medium)); p.setPen(theme::map(QColor(0xc8, 0xc4, 0xbc)));
  p.drawText(QRectF(b.left(), 50 + oy, b.width(), 16), Qt::AlignCenter,
             s == Connecting ? QCoreApplication::translate("watch", "Retail positioning connecting…") : QCoreApplication::translate("watch", "Retail positioning unavailable"));
  p.setFont(theme::condensed(14, QFont::Medium)); p.setPen(theme::map(QColor(0x6e, 0x6a, 0x63)));
  p.drawText(QRectF(b.left() + 10, 72 + oy, b.width() - 20, 34), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
             s == Connecting ? (m_source.isEmpty() ? QCoreApplication::translate("watch", "CONNECTING TO THE DATA SOURCE.") : m_source.toUpper())
                             : (m_message.isEmpty() ? QCoreApplication::translate("watch", "DATA SOURCE NOT CONNECTED.") : m_message.toUpper()));
  return true;
}
void RetailCard::paintBody(QPainter& p, const QRectF& b) {
  const double L = m_d.longPct, S = m_d.shortPct;
  // pie sized by the card (reference 384x270: rx 118, centre 100 px under the title)
  const qreal depth = 17, ryMax = (b.height() - 30 - depth - 16) / 2.0;
  const qreal rx = std::min<qreal>(b.width() * 0.307, ryMax / 0.68), ry = rx * 0.68;   // grows with the card
  const QPointF c(b.center().x(), b.top() + (b.height() - 30 - depth) / 2.0 + 3);
  pie3d(p, c, rx, ry, depth, {{S / 100.0, theme::map(QColor(0xf4, 0x6a, 0x6a)), theme::map(QColor(0x8a, 0x30, 0x30))},
                              {L / 100.0, theme::map(QColor(0x1e, 0xd7, 0xee)), theme::map(QColor(0x0d, 0x62, 0x6d))}});
  auto label = [&](double midDeg, double rr, const QString& pct, const QString& word) {
    const QPointF at = ellPoint(c, rx * rr, ry * rr, midDeg);
    p.setFont(theme::condensed(24, QFont::Bold)); p.setPen(theme::map(QColor(0xff, 0xff, 0xff)));
    p.drawText(QRectF(at.x() - 50, at.y() - 22, 100, 26), Qt::AlignCenter, pct);
    p.setFont(theme::condensed(12, QFont::Bold)); p.drawText(QRectF(at.x() - 50, at.y() + 2, 100, 14), Qt::AlignCenter, word);
  };
  label(90 + S / 100.0 * 180, S < 25 ? 0.62 : 0.55, QString("%1%").arg(std::round(S)), QCoreApplication::translate("watch", "SHORT"));
  label(90 + S / 100.0 * 360 + L / 100.0 * 180, 0.5, QString("%1%").arg(std::round(L)), QCoreApplication::translate("watch", "LONG"));
  // legend
  const QFont lf = theme::sans(10);
  const QString a = QCoreApplication::translate("watch", "Long %1%").arg(std::round(L)), s2 = QCoreApplication::translate("watch", "Short %1%").arg(std::round(S));
  const qreal w = 10 + adv(lf, a) + 16 + 10 + adv(lf, s2);
  qreal x = b.center().x() - w / 2; const qreal y = b.bottom() - 16;
  p.setFont(lf); p.setPen(Qt::NoPen);
  p.setBrush(theme::map(QColor(0x4c, 0xcf, 0xde))); p.drawEllipse(QPointF(x + 3, y), 3, 3);
  p.setPen(theme::map(QColor(0x4c, 0xcf, 0xde))); p.drawText(QPointF(x + 10, y + 4), a); x += 10 + adv(lf, a) + 16;
  p.setPen(Qt::NoPen); p.setBrush(theme::map(QColor(0xf2, 0x5f, 0x6a))); p.drawEllipse(QPointF(x + 3, y), 3, 3);
  p.setPen(theme::map(QColor(0xf2, 0x5f, 0x6a))); p.drawText(QPointF(x + 10, y + 4), s2);
}

// ================================================================== COT
CotCard::CotCard(QWidget* parent) : SheetCard(QCoreApplication::translate("watch", "COT Position"), parent) { setState(Connecting); }
void CotCard::setLabels(const QStringList& l) {
  m_sides.clear(); for (const auto& s : l) { CotSide c; c.label = s; m_sides << c; }
  update();
}
void CotCard::setSides(const QVector<CotSide>& s) { m_sides = s; setState(Ready); }
bool CotCard::paintPlaceholder(QPainter& p, const QRectF& b, State s) {
  if (s == Message) return false;
  paintSides(p, b, false);   // "–" placeholders and the labels, as the reference's loading frame
  return true;
}
void CotCard::paintBody(QPainter& p, const QRectF& b) { paintSides(p, b, true); }
void CotCard::paintSides(QPainter& p, const QRectF& b, bool data) {
  const int n = std::max<int>(1, m_sides.size());
  for (int i = 0; i < m_sides.size(); ++i) {
    const auto& s = m_sides[i];
    const qreal cx = n == 1 ? b.center().x() : b.left() + b.width() * (i == 0 ? 0.25 : 0.75);
    const QPointF c(cx, height() - 96);
    const bool has = data && std::isfinite(s.longPct);
    if (has) {
      // short counter-clockwise from 12 o'clock (left), long on the right, as in the reference
      pie3d(p, c, 55, 27, 11, {{s.shortPct / 100.0, theme::map(QColor(0xf0, 0x65, 0x63)), theme::map(QColor(0x86, 0x2e, 0x2e))},
                               {s.longPct / 100.0, theme::map(QColor(0x10, 0xd3, 0xe7)), theme::map(QColor(0x0b, 0x5c, 0x66))}}, 0.5);
      const QFont f = theme::mono(11, QFont::Bold);
      const QString l = QString("L %1%").arg(std::round(s.longPct)), sh = QString("S %1%").arg(std::round(s.shortPct));
      const qreal w = adv(f, l) + 8 + adv(f, sh);
      p.setFont(f); p.setPen(theme::map(QColor(0x2d, 0xd5, 0xee))); p.drawText(QPointF(cx - w / 2, height() - 40), l);
      p.setPen(theme::map(QColor(0xf0, 0x65, 0x63))); p.drawText(QPointF(cx - w / 2 + adv(f, l) + 8, height() - 40), sh);
    } else {
      p.setFont(theme::mono(11)); p.setPen(theme::map(QColor(0x45, 0x41, 0x3a)));
      p.drawText(QRectF(cx - 40, c.y() - 10, 80, 20), Qt::AlignCenter, "–");
    }
    p.setFont(theme::condensed(12, QFont::Bold)); p.setPen(theme::map(QColor(0xa8, 0xa4, 0x9c)));
    p.drawText(QRectF(cx - 60, height() - 24, 120, 16), Qt::AlignCenter, s.label);
  }
}
bool CotCard::event(QEvent* e) {
  if (e->type() == QEvent::ToolTip && state() == Ready) {
    auto* he = static_cast<QHelpEvent*>(e);
    const int i = m_sides.size() == 2 && he->pos().x() > width() / 2 ? 1 : 0;
    if (i < m_sides.size()) {
      const auto& s = m_sides[i];
      QToolTip::showText(he->globalPos(), QCoreApplication::translate("watch", "%1 · non-commercial long %2% / short %3%\nreport %4%5")
        .arg(s.label, fmt::num(s.longPct, 1), fmt::num(s.shortPct, 1), i18n::locale().toString(s.reportDate, "dd MMM yyyy"), s.tip.isEmpty() ? "" : "\n" + s.tip), this);
    }
    return true;
  }
  return SheetCard::event(e);
}

// ================================================================== Data compare
DataCompareCard::DataCompareCard(QWidget* parent) : SheetCard(QCoreApplication::translate("watch", "Data Compare"), parent) { setState(Connecting); }
QStringList DataCompareCard::defaultIndicators() {
  return {QCoreApplication::translate("watch", "Interest Rate"), QCoreApplication::translate("watch", "GDP (Quarter q/q)"), QCoreApplication::translate("watch", "Inflation Rate (Annual)"), QCoreApplication::translate("watch", "Unemployment Rate"), QCoreApplication::translate("watch", "PMI Composite")};
}
void DataCompareCard::setColumns(const QStringList& c) { m_cols = c; update(); }
void DataCompareCard::setRows(const QVector<CompareRow>& r) { m_rows = r; setState(Ready); }
void DataCompareCard::paintBody(QPainter& p, const QRectF& b) {
  const qreal right = b.right() - 7;
  const qreal colW = b.width() < 340 ? 64 : 85;
  const QFont hf = theme::condensed(10, QFont::Bold), lf = theme::condensed(13, QFont::Medium), vf = theme::mono(12, QFont::Medium);
  const QFont df = theme::condensed(10, QFont::Normal);
  p.setFont(hf); p.setPen(theme::map(QColor(0x76, 0x74, 0x65)));
  const qreal hy = b.top() + 13;
  p.drawText(QPointF(b.left() + 7, hy + 4), QCoreApplication::translate("watch", "INDICATOR"));
  for (int c = 0; c < m_cols.size(); ++c) {
    const qreal r = right - (m_cols.size() - 1 - c) * colW;
    p.drawText(QRectF(r - colW, hy - 8, colW, 16), Qt::AlignRight | Qt::AlignVCenter, m_cols[c]);
  }
  qreal y = b.top() + 25;
  for (const auto& row : m_rows) {
    p.fillRect(QRectF(b.left(), y, b.width(), 1), theme::map(QColor(0x25, 0x21, 0x17)));
    const qreal lw = b.width() - 14 - colW * m_cols.size();
    p.setFont(lf); p.setPen(theme::map(QColor(0x8a, 0x8a, 0x80)));
    p.drawText(QRectF(b.left() + 7, y, lw, 26), Qt::AlignVCenter, QFontMetricsF(lf).elidedText(row.label, Qt::ElideRight, lw));
    p.setFont(vf);
    for (int c = 0; c < m_cols.size(); ++c) {
      const bool differs = row.differs.value(c);
      const QString v = (differs ? QStringLiteral("≠ ") : QString()) + row.values.value(c, fmt::dash());
      p.setPen(v == fmt::dash() ? theme::map(QColor(0x55, 0x52, 0x4b)) : differs ? theme::pal().accent : theme::map(QColor(0xed, 0xe8, 0xdf)));
      const qreal r = right - (m_cols.size() - 1 - c) * colW;
      p.drawText(QRectF(r - colW, y, colW, 26), Qt::AlignRight | Qt::AlignVCenter, v);
      if (const QString d = row.dates.value(c); !d.isEmpty()) {   // the period, in small before the value
        const qreal room = colW - QFontMetricsF(vf).horizontalAdvance(v) - 5;
        p.setFont(df); p.setPen(theme::map(QColor(0x76, 0x74, 0x65)));
        if (QFontMetricsF(df).horizontalAdvance(d) <= room + 4)
          p.drawText(QRectF(r - colW - 4, y, room + 4, 26), Qt::AlignRight | Qt::AlignVCenter, d);
        p.setFont(vf);
      }
    }
    y += 25.7;
  }
}
bool DataCompareCard::event(QEvent* e) {
  if (e->type() == QEvent::ToolTip && state() == Ready) {
    auto* he = static_cast<QHelpEvent*>(e);
    const int r = int((he->pos().y() - (body().top() + 25)) / 25.7);
    const qreal right = body().right() - 7;
    const qreal colW = body().width() < 340 ? 64 : 85;
    int c = -1; for (int k = 0; k < m_cols.size(); ++k) { const qreal rr = right - (m_cols.size() - 1 - k) * colW; if (he->pos().x() > rr - colW && he->pos().x() <= rr) c = k; }
    if (r >= 0 && r < m_rows.size() && c >= 0 && !m_rows[r].tips.value(c).isEmpty()) QToolTip::showText(he->globalPos(), m_rows[r].tips[c], this);
    else if (r >= 0 && r < m_rows.size() && c < 0) QToolTip::showText(he->globalPos(), m_rows[r].label, this);
    else QToolTip::hideText();
    return true;
  }
  return SheetCard::event(e);
}

// ================================================================== Meeting compare
MeetingCard::MeetingCard(QWidget* parent) : SheetCard(QCoreApplication::translate("watch", "Meeting Compare"), parent) { setState(Connecting); }
void MeetingCard::setBanks(const QStringList& names) {
  m_halves.clear(); for (const auto& n : names) { Half h; h.name = n; m_halves << h; }
  setState(Connecting);
}
void MeetingCard::setBank(int side, const MeetingData& d) {
  if (side < 0 || side >= m_halves.size()) return;
  m_halves[side].d = d; m_halves[side].state = Ready; m_halves[side].name = d.bank.isEmpty() ? m_halves[side].name : d.bank;
  setState(Ready);
}
bool MeetingCard::event(QEvent* e) {
  if (e->type() == QEvent::ToolTip && !m_halves.isEmpty()) {
    auto* he = static_cast<QHelpEvent*>(e);
    const int i = std::clamp(int(he->pos().x() / (width() / qreal(m_halves.size()))), 0, int(m_halves.size()) - 1);
    const auto& h = m_halves[i];
    QString tip = h.name;
    if (h.state == Ready) tip += QCoreApplication::translate("watch", "\nNext meeting %1\n%2").arg(i18n::locale().toString(h.d.meeting, "ddd dd MMM yyyy"), h.d.source);
    else if (!h.msg.isEmpty()) tip += "\n" + h.msg;
    QToolTip::showText(he->globalPos(), tip, this);
    return true;
  }
  return SheetCard::event(e);
}
void MeetingCard::setBankState(int side, State s, const QString& msg) {
  if (side < 0 || side >= m_halves.size()) return;
  m_halves[side].state = s; m_halves[side].msg = msg; update();
}
bool MeetingCard::paintPlaceholder(QPainter& p, const QRectF& b, State s) {
  if (s != Connecting && s != NotConnected) return false;
  paintBody(p, b);
  return true;
}
void MeetingCard::paintBody(QPainter& p, const QRectF& b) {
  const int n = std::max<int>(1, m_halves.size());
  for (int i = 0; i < m_halves.size(); ++i) {
    const QRectF h(b.left() + i * b.width() / n, b.top(), b.width() / n, b.height());
    if (i > 0) p.fillRect(QRectF(h.left(), h.top(), 1, h.height()), theme::map(QColor(0x23, 0x20, 0x16)));
    paintHalf(p, h, i);
  }
}
void MeetingCard::paintHalf(QPainter& p, const QRectF& h, int side) {
  const auto& hf = m_halves[side];
  p.setFont(theme::condensed(10, QFont::Bold)); p.setPen(theme::map(QColor(0x8a, 0x87, 0x7e)));
  const QString head = hf.name.toUpper();   // meeting date and source in the tooltip
  QFont hfnt = theme::condensed(10, QFont::Bold); hfnt.setLetterSpacing(QFont::AbsoluteSpacing, 0.8); p.setFont(hfnt);
  p.drawText(QRectF(h.left(), h.top() + 5, h.width(), 16), Qt::AlignCenter, head);
  const QRectF plot(h.left() + 50, h.top() + 46, h.width() - 68, h.height() - 46 - 33);
  if (hf.state != Ready && hf.state != Connecting) {
    if (hf.state == NotConnected) {
      const QRectF r = h.adjusted(14, 40, -14, -10);
      p.setFont(theme::sans(12)); p.setPen(theme::map(QColor(0xb8, 0xb4, 0xac)));
      p.drawText(QRectF(r.left(), r.center().y() - 30, r.width(), 18), Qt::AlignCenter, QCoreApplication::translate("watch", "No market pricing"));
      p.setFont(theme::sans(10)); p.setPen(theme::map(QColor(0x5e, 0x5a, 0x53)));
      p.drawText(QRectF(r.left(), r.center().y() - 6, r.width(), r.height() / 2), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, hf.msg);
    } else centeredNote(p, plot, hf.msg);
    return;
  }
  // axes 0..100 %
  p.setFont(theme::mono(10));
  for (int k = 0; k <= 5; ++k) {
    const qreal y = plot.bottom() - plot.height() * k / 5.0;
    p.fillRect(QRectF(plot.left(), y, plot.width(), 1), k == 0 ? theme::map(QColor(0x2e, 0x2a, 0x18)) : theme::map(QColor(0x1d, 0x1b, 0x12)));
    p.setPen(theme::map(QColor(0x8a, 0x87, 0x7e))); p.drawText(QRectF(h.left() + 4, y - 8, 40, 16), Qt::AlignRight | Qt::AlignVCenter, QString("%1%").arg(k * 20));
  }
  const auto& bk = hf.d.buckets;
  if (hf.state != Ready || bk.isEmpty()) return;   // loading: empty axes, as the reference
  const qreal slot = plot.width() / bk.size(), bw = slot * 0.7;
  int imax = 0; for (int i = 1; i < bk.size(); ++i) if (bk[i].prob > bk[imax].prob) imax = i;
  for (int i = 0; i < bk.size(); ++i) {
    const double v = std::isfinite(bk[i].prob) ? bk[i].prob : 0;
    const qreal x = plot.left() + slot * i + (slot - bw) / 2, hh = std::max<qreal>(1.5, plot.height() * v / 100.0);
    const bool hike = bk[i].label.startsWith('+'), cut = bk[i].label.startsWith('-');
    QColor c = i == imax ? theme::map(QColor(0xaa, 0xb1, 0xba)) : hike ? theme::map(QColor(0x00, 0xbf, 0xbe)) : cut ? theme::map(QColor(0xe0, 0x66, 0x6b)) : theme::map(QColor(0xaa, 0xb1, 0xba));
    p.setPen(Qt::NoPen); p.setBrush(c);
    QPainterPath bar; bar.addRoundedRect(QRectF(x, plot.bottom() - hh, bw, hh + 2), 2, 2);
    p.save(); p.setClipRect(QRectF(x - 1, plot.top() - 2, bw + 2, plot.height() + 2)); p.drawPath(bar); p.restore();
    const QString t = !std::isfinite(bk[i].prob) ? fmt::dash() : v < 1 ? "<1%" : fmt::num(v, 1) + "%";
    p.setFont(theme::mono(10)); p.setPen(theme::map(QColor(0x8a, 0x87, 0x7e)));
    p.drawText(QRectF(x - 10, plot.bottom() - hh - 18, bw + 20, 14), Qt::AlignCenter, t);
    QString lab = bk[i].label;
    if (slot < 62) lab = i == 2 ? QCoreApplication::translate("watch", "Hold") : QString(lab).remove("bps");   // narrow halves
    p.drawText(QRectF(plot.left() + slot * i - 4, plot.bottom() + 8, slot + 8, 14), Qt::AlignCenter, lab);
  }
}

// ================================================================== Technical tiles
TechTilesCard::TechTilesCard(QWidget* parent) : SheetCard(QCoreApplication::translate("watch", "Technical Heatmap"), parent) { setState(Loading); }
void TechTilesCard::setTiles(const QVector<Tile>& t) { m_tiles = t; setState(t.isEmpty() ? Message : Ready, QCoreApplication::translate("watch", "Not enough daily history")); }
QRectF TechTilesCard::tileRect(int i) const {
  const QRectF b = body(); const int n = m_cols > 0 ? m_cols : std::max<int>(1, m_tiles.size());
  const qreal gap = 4, w = (b.width() - 14 - gap * (n - 1)) / n;
  return QRectF(b.left() + 7 + (i % n) * (w + gap), b.top() + 7 + (i / n) * 53, w, 49);
}
void TechTilesCard::paintBody(QPainter& p, const QRectF&) {
  for (int i = 0; i < m_tiles.size(); ++i) {
    const auto& t = m_tiles[i]; const QRectF r = tileRect(i);
    const QColor bg = t.bias < 0 ? tileRed() : t.bias > 0 ? tileCyan() : tileGrey();
    p.setPen(QPen(t.bias < 0 ? theme::map(QColor(0x55, 0x22, 0x20)) : t.bias > 0 ? theme::map(QColor(0x16, 0x50, 0x58)) : theme::map(QColor(0x2c, 0x2a, 0x27)), 1));
    p.setBrush(bg); p.drawRoundedRect(r.adjusted(.5, .5, -.5, -.5), 2, 2);
    QFont lf = theme::condensed(9, QFont::DemiBold); lf.setLetterSpacing(QFont::AbsoluteSpacing, 0.4);
    p.setFont(lf); p.setPen(tileLabel()); p.drawText(QRectF(r.left(), r.top() + 6, r.width(), 13), Qt::AlignCenter, QCoreApplication::translate("watch", t.key.toUtf8().constData()));
    p.setFont(theme::condensed(13, QFont::DemiBold));
    p.setPen(t.bias < 0 ? tileRedText() : t.bias > 0 ? tileCyanText() : tileText());
    p.drawText(QRectF(r.left(), r.top() + 23, r.width(), 18), Qt::AlignCenter, t.value);
  }
}
bool TechTilesCard::event(QEvent* e) {
  if (e->type() == QEvent::ToolTip && state() == Ready) {
    auto* he = static_cast<QHelpEvent*>(e);
    for (int i = 0; i < m_tiles.size(); ++i) if (tileRect(i).contains(he->pos())) {
      QToolTip::showText(he->globalPos(), m_tiles[i].tip + QCoreApplication::translate("watch", "\nComputed by Deepwater from Yahoo daily candles."), this); return true;
    }
    QToolTip::hideText(); return true;
  }
  return SheetCard::event(e);
}

// ================================================================== Index performance
IndexPerfCard::IndexPerfCard(QWidget* parent) : SheetCard(QCoreApplication::translate("watch", "Index Performance"), parent) { setMouseTracking(true); setLayered(true); setState(Computing, QCoreApplication::translate("watch", "Computing relative performance...")); }
void IndexPerfCard::setLines(const PerfLine& a, const PerfLine& b) { m_a = a; m_b = b; setState(Ready); }
bool IndexPerfCard::paintPlaceholder(QPainter&, const QRectF&, State) { return false; }
void IndexPerfCard::paintBody(QPainter& p, const QRectF& b) {
  double lo = INFINITY, hi = -INFINITY; qint64 t0 = LLONG_MAX, t1 = 0;
  for (const auto* l : {&m_a, &m_b}) for (int i = 0; i < l->pct.size(); ++i) {
    lo = std::min(lo, l->pct[i]); hi = std::max(hi, l->pct[i]); t0 = std::min(t0, l->t[i]); t1 = std::max(t1, l->t[i]);
  }
  m_plot = QRectF();
  if (!std::isfinite(lo) || t1 <= t0) { centeredNote(p, b, QCoreApplication::translate("watch", "No history")); return; }
  const double span = std::max(hi - lo, 0.5); lo -= span * 0.04; hi += span * 0.04;   // flat lines: an axis still
  const auto ticks = chart::niceTicks(lo, hi, 6);
  if (ticks.isEmpty()) { centeredNote(p, b, QCoreApplication::translate("watch", "No history")); return; }
  lo = std::min(lo, ticks.first()); hi = std::max(hi, ticks.last());
  m_lo = lo; m_hi = hi; m_t0 = t0; m_t1 = t1;
  const int dp = (ticks.size() > 1 && ticks[1] - ticks[0] >= 10) ? 0 : 1;
  qreal lw = 0; for (double t : ticks) lw = std::max(lw, adv(axisSans(), fmt::num(t, dp) + "%"));
  m_plot = QRectF(b.left() + lw + 16, b.top() + 34, b.width() - lw - 32, b.height() - 34 - 36);
  auto Y = [&](double v) { return m_plot.bottom() - (v - lo) / (hi - lo) * m_plot.height(); };
  auto X = [&](qint64 t) { return m_plot.left() + double(t - t0) / (t1 - t0) * m_plot.width(); };
  p.setFont(axisSans());
  for (double t : ticks) {
    const qreal y = Y(t);
    p.fillRect(QRectF(m_plot.left(), y, m_plot.width(), 1), std::abs(t) < 1e-9 ? theme::map(QColor(0x26, 0x24, 0x1a)) : kGrid());
    p.setPen(kAxis()); p.drawText(QRectF(b.left(), y - 8, lw + 10, 16), Qt::AlignRight | Qt::AlignVCenter, fmt::num(t, dp) + "%");
  }
  const int y0 = QDateTime::fromSecsSinceEpoch(t0, QTimeZone::UTC).date().year(), y1 = QDateTime::fromSecsSinceEpoch(t1, QTimeZone::UTC).date().year();
  const int step = (y1 - y0) > 6 ? 2 : 1;
  for (int y = y0 + 1; y <= y1; ++y) {
    if ((y - y0 - 1) % step) continue;
    const qint64 t = QDateTime(QDate(y, 1, 1), QTime(0, 0), QTimeZone::UTC).toSecsSinceEpoch();
    p.setPen(kAxis()); p.drawText(QRectF(X(t) - 30, m_plot.bottom() + 8, 60, 14), Qt::AlignCenter, QString::number(y));
  }
  auto draw = [&](const PerfLine& l, const QColor& c) {
    QPainterPath path;
    for (int i = 0; i < l.pct.size(); ++i) { const QPointF pt(X(l.t[i]), Y(l.pct[i])); if (!i) path.moveTo(pt); else path.lineTo(pt); }
    p.setPen(QPen(c, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.setBrush(Qt::NoBrush); p.drawPath(path);
  };
  draw(m_a, seriesBlue()); draw(m_b, seriesGold());
  // legend centred on top: —○— A  —○— B
  const QFont lf = theme::sans(11);
  const qreal w = 2 * (28 + 6) + adv(lf, m_a.name) + 14 + adv(lf, m_b.name);
  qreal x = b.center().x() - w / 2; const qreal ly = b.top() + 17;
  auto item = [&](const QString& n, const QColor& c) {
    p.setPen(QPen(c, 2)); p.drawLine(QPointF(x, ly), QPointF(x + 28, ly));
    p.setBrush(theme::map(QColor(0xf2, 0xf2, 0xf2))); p.setPen(QPen(c, 2.4)); p.drawEllipse(QPointF(x + 14, ly), 4.5, 4.5);
    p.setFont(lf); p.setPen(theme::map(QColor(0xc8, 0xc4, 0xbc))); p.drawText(QPointF(x + 34, ly + 4), n);
    x += 34 + adv(lf, n) + 14;
  };
  item(m_a.name, seriesBlue()); item(m_b.name, seriesGold());
}
void IndexPerfCard::paintHover(QPainter& p, const QRectF&) {
  // hover readout
  const double lo = m_lo, hi = m_hi; const qint64 t0 = m_t0, t1 = m_t1;
  auto Y = [&](double v) { return m_plot.bottom() - (v - lo) / (hi - lo) * m_plot.height(); };
  auto X = [&](qint64 t) { return m_plot.left() + double(t - t0) / (t1 - t0) * m_plot.width(); };
  if (m_plot.contains(m_mouse)) {
    const qint64 t = t0 + qint64((m_mouse.x() - m_plot.left()) / m_plot.width() * (t1 - t0));
    QStringList lines{i18n::locale().toString(QDateTime::fromSecsSinceEpoch(t, QTimeZone::UTC).date(), "dd MMM yyyy")};
    QList<QColor> cols{theme::map(QColor(0x8a, 0x87, 0x7e))};
    for (const auto* l : {&m_a, &m_b}) {
      auto it = std::lower_bound(l->t.cbegin(), l->t.cend(), t);
      if (it == l->t.cend()) --it;
      const int i = int(it - l->t.cbegin());
      lines << QString("%1  %2").arg(l->name, fmt::pct(l->pct[i], 1)); cols << (l == &m_a ? seriesBlue() : seriesGold());
      p.setPen(QPen(l == &m_a ? seriesBlue() : seriesGold(), 1.5)); p.setBrush(theme::map(QColor(0xf2, 0xf2, 0xf2)));
      p.drawEllipse(QPointF(X(l->t[i]), Y(l->pct[i])), 3, 3);
    }
    p.setPen(QPen(theme::map(QColor(0x8b, 0x88, 0x81)), 1, Qt::DashLine)); p.drawLine(QPointF(m_mouse.x(), m_plot.top()), QPointF(m_mouse.x(), m_plot.bottom()));
    chart::tooltip(p, m_mouse, lines, rect(), cols);
  }
}
void IndexPerfCard::mouseMoveEvent(QMouseEvent* e) { m_mouse = e->position(); if (state() == Ready) update(); }
void IndexPerfCard::leaveEvent(QEvent*) { m_mouse = {-1, -1}; update(); }

// ================================================================== Seasonality by month
SeasonMonthCard::SeasonMonthCard(QWidget* parent) : SheetCard(QCoreApplication::translate("watch", "Seasonality · By Month"), parent) { setMouseTracking(true); setState(Loading); }
void SeasonMonthCard::setData(const Seasonality& s) {
  m_s = s;
  setSuffix(s.ok ? QString("%1-%2").arg(s.firstYear).arg(s.lastYear) : QString());
  setState(s.ok ? Ready : Message, QCoreApplication::translate("watch", "Not enough daily history"));
}
void SeasonMonthCard::paintBody(QPainter& p, const QRectF& b) {
  m_plot = QRectF(b.left() + 54, b.top() + 22, b.width() - 66, b.height() - 22 - 40);
  double m = 0; for (double v : m_s.month) if (std::isfinite(v)) m = std::max(m, std::abs(v));
  if (m <= 0) m = 1;
  // symmetric axis whose outer tick covers the largest bar
  const auto probe = chart::niceTicks(-m * 1.05, m * 1.05, 6);
  const double step = probe.size() > 1 ? probe[1] - probe[0] : m;
  const double lim = std::ceil(m * 1.02 / step) * step;
  QVector<double> ticks; for (double t = -lim; t <= lim + step * 1e-6; t += step) ticks << (std::abs(t) < step * 1e-6 ? 0.0 : t);
  auto Y = [&](double v) { return m_plot.center().y() - v / lim * m_plot.height() / 2; };
  p.setFont(axisSans());
  const int dp = step < 0.1 ? 2 : step < 10 ? 1 : 0;
  for (double t : ticks) {
    const qreal y = Y(t);
    p.fillRect(QRectF(m_plot.left(), y, m_plot.width(), 1), std::abs(t) < 1e-9 ? kZero() : kGrid());
    p.setPen(kAxis()); p.drawText(QRectF(b.left(), y - 8, 46, 16), Qt::AlignRight | Qt::AlignVCenter, fmt::num(t, dp) + "%");
  }
  const int cur = QDate::currentDate().month() - 1;
  const qreal slot = m_plot.width() / 12, bw = slot * 0.44;
  for (int i = 0; i < 12; ++i) {
    const qreal cx = m_plot.left() + slot * (i + 0.5);
    p.setFont(theme::sans(11)); p.setPen(kAxisDim());
    p.drawText(QRectF(cx - slot / 2, m_plot.bottom() + 12, slot, 14), Qt::AlignCenter, monthShort(i));
    const double v = m_s.month[i];
    if (!std::isfinite(v)) continue;
    const qreal y0 = Y(0), y1 = Y(v);
    QRectF r(cx - bw / 2, std::min(y0, y1), bw, std::max<qreal>(1.5, std::abs(y1 - y0)));
    if (i == cur) { p.setPen(QPen(theme::map(QColor(0xff, 0x9a, 0x3c)), 1)); p.setBrush(barNow()); p.drawRect(r.adjusted(.5, .5, -.5, -.5)); }
    else { p.setPen(Qt::NoPen); p.setBrush(v >= 0 ? barPos() : barNeg()); p.drawRect(r); }
    if (i == m_hover) {
      const QStringList lines{monthShort(i), fmt::pct(v, 2), QCoreApplication::translate("watch", "%1 years · %2-%3").arg(m_s.monthN[i]).arg(m_s.firstYear).arg(m_s.lastYear)};
      chart::tooltip(p, QPointF(cx, y1), lines, rect(), {kAxisDim(), v >= 0 ? up() : down(), kAxisDim()});
    }
  }
}
void SeasonMonthCard::mouseMoveEvent(QMouseEvent* e) {
  int h = -1;
  if (m_plot.adjusted(0, -10, 0, 30).contains(e->position())) h = std::clamp(int((e->position().x() - m_plot.left()) / (m_plot.width() / 12)), 0, 11);
  if (h != m_hover) { m_hover = h; update(); }
}
void SeasonMonthCard::leaveEvent(QEvent*) { m_hover = -1; update(); }

// ================================================================== Year path
YearPathCard::YearPathCard(QWidget* parent) : SheetCard(QCoreApplication::translate("watch", "Seasonality · Year Path"), parent) { setMouseTracking(true); setLayered(true); setState(Loading); }
void YearPathCard::setData(const Seasonality& s) {
  m_s = s;
  setSuffix(s.ok ? QString("%1-%2").arg(s.pathFirstYear).arg(s.pathLastYear) : QString());
  setState(s.ok ? Ready : Message, QCoreApplication::translate("watch", "Not enough daily history"));
}
void YearPathCard::paintBody(QPainter& p, const QRectF& b) {
  m_plot = QRectF(b.left() + 44, b.top() + 32, b.width() - 56, b.height() - 32 - 40);
  double lo = 0, hi = 0;
  for (const auto* v : {&m_s.avgPath, &m_s.curPath}) for (double x : *v) if (std::isfinite(x)) { lo = std::min(lo, x); hi = std::max(hi, x); }
  const double span = std::max(hi - lo, 0.5);
  const auto ticks = chart::niceTicks(lo - span * 0.05, hi + span * 0.05, 9);
  lo = std::min(lo, ticks.first()); hi = std::max(hi, ticks.last());
  m_lo = lo; m_hi = hi;
  auto Y = [&](double v) { return m_plot.bottom() - (v - lo) / (hi - lo) * m_plot.height(); };
  auto X = [&](int d) { return m_plot.left() + d / 365.0 * m_plot.width(); };
  p.setFont(axisSans());
  const double step = ticks.size() > 1 ? ticks[1] - ticks[0] : 1;
  for (double t : ticks) {
    const qreal y = Y(t);
    p.fillRect(QRectF(m_plot.left(), y, m_plot.width(), 1), std::abs(t) < 1e-9 ? kZero() : kGrid());
    p.setPen(kAxis()); p.drawText(QRectF(b.left(), y - 8, 38, 16), Qt::AlignRight | Qt::AlignVCenter, fmt::num(t, step < 1 ? 1 : 0) + "%");
  }
  for (int m = 0; m < 12; ++m) {
    const int d = seasonality::monthFirstSlot(m + 1);
    p.setFont(theme::sans(11)); p.setPen(kAxisDim());
    p.drawText(QRectF(X(d) - 20, m_plot.bottom() + 12, 40, 14), Qt::AlignCenter, monthShort(m));
  }
  auto path = [&](const QVector<double>& v) {
    QPainterPath pa; bool st = false;
    for (int d = 0; d < v.size(); ++d) { if (!std::isfinite(v[d])) continue; const QPointF pt(X(d), Y(v[d])); if (!st) { pa.moveTo(pt); st = true; } else pa.lineTo(pt); }
    return pa;
  };
  const QPainterPath avg = path(m_s.avgPath);
  if (!avg.isEmpty()) {
    QPainterPath fill = avg; fill.lineTo(fill.currentPosition().x(), Y(0)); fill.lineTo(avg.elementAt(0).x, Y(0)); fill.closeSubpath();
    p.fillPath(fill, theme::map(QColor(0xef, 0xd5, 0x60, 26)));
  }
  p.setBrush(Qt::NoBrush);
  p.setPen(QPen(theme::map(QColor(0xef, 0xd5, 0x60)), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.drawPath(avg);
  p.setPen(QPen(seriesTeal(), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.drawPath(path(m_s.curPath));
  // legend top-right
  const QFont lf = theme::sans(11);
  const QString la = QCoreApplication::translate("watch", "Average %1-%2").arg(m_s.pathFirstYear).arg(m_s.pathLastYear), lc = QString::number(m_s.currentYear);
  qreal x = b.right() - 14 - adv(lf, lc) - 22 - 26 - adv(lf, la) - 22;
  const qreal ly = b.top() + 15;
  p.setFont(lf);
  p.fillRect(QRectF(x, ly - 2, 14, 4), theme::map(QColor(0xef, 0xd5, 0x60))); p.setPen(theme::map(QColor(0xc8, 0xc4, 0xbc))); p.drawText(QPointF(x + 20, ly + 4), la);
  x += 20 + adv(lf, la) + 26;
  p.fillRect(QRectF(x, ly - 2, 14, 4), seriesTeal()); p.drawText(QPointF(x + 20, ly + 4), lc);
}
void YearPathCard::paintHover(QPainter& p, const QRectF&) {
  auto X = [&](int d) { return m_plot.left() + d / 365.0 * m_plot.width(); };
  if (m_plot.contains(m_mouse)) {
    const int d = std::clamp(int(std::round((m_mouse.x() - m_plot.left()) / m_plot.width() * 365)), 0, 365);
    QStringList lines{i18n::locale().toString(seasonality::dateOfSlot(d, 2024), "d MMM")}; QList<QColor> cols{kAxisDim()};
    if (std::isfinite(m_s.avgPath.value(d, NAN))) { lines << QCoreApplication::translate("watch", "Average %1").arg(fmt::pct(m_s.avgPath[d], 2)); cols << theme::map(QColor(0xef, 0xd5, 0x60)); }
    if (std::isfinite(m_s.curPath.value(d, NAN))) { lines << QString("%1  %2").arg(m_s.currentYear).arg(fmt::pct(m_s.curPath[d], 2)); cols << seriesTeal(); }
    p.setPen(QPen(theme::map(QColor(0x8b, 0x88, 0x81)), 1, Qt::DashLine)); p.drawLine(QPointF(X(d), m_plot.top()), QPointF(X(d), m_plot.bottom()));
    chart::tooltip(p, m_mouse, lines, rect(), cols);
  }
}
void YearPathCard::mouseMoveEvent(QMouseEvent* e) { m_mouse = e->position(); if (state() == Ready) update(); }
void YearPathCard::leaveEvent(QEvent*) { m_mouse = {-1, -1}; update(); }
}
