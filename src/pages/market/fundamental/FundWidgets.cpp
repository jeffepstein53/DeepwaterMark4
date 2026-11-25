#include "pages/market/fundamental/FundWidgets.h"
#include "pages/market/fundamental/FundStyle.h"
#include "macro/KeyFigures.h"
#include "ui/Theme.h"
#include "ui/Kit.h"
#include "ui/Chart.h"
#include <QPainter>
#include <QCoreApplication>
#include "core/I18n.h"
#include <QPainterPath>
#include <QMouseEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QTextLayout>
#include <QDateTime>
#include <QtMath>

using namespace fundamental;

namespace fundamental {

QFont mono(int px, int w, qreal ls) { return theme::mono(px, w, ls); }
QFont sans(int px, int w, qreal ls) {
  QFont f = theme::sans(px, w);
  if (ls != 0) f.setLetterSpacing(QFont::AbsoluteSpacing, ls);
  return f;
}
QFont prose(int px, int w) { return theme::condensed(px, w); }
QString dash() { return QString::fromUtf8("—"); }

void drawDiamond(QPainter& p, const QPointF& c, qreal r, const QColor& color) {
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  QPolygonF d{c + QPointF(0, -r), c + QPointF(r, 0), c + QPointF(0, r), c + QPointF(-r, 0)};
  p.setPen(Qt::NoPen); p.setBrush(color); p.drawPolygon(d);
  p.restore();
}

void drawMacroCard(QPainter& p, const QRectF& r, const QColor& bg, const QColor& border) {
  p.fillRect(r, bg);
  p.setPen(border); p.setBrush(Qt::NoBrush);
  p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
  ui::drawBrackets(p, r.adjusted(4, 4, -4, -4), col().bracket, 8, true, true);
}

void drawCardTitle(QPainter& p, const QPointF& at, const QString& text) {
  drawDiamond(p, at + QPointF(0, 0), 3.6, col().diamond);
  p.setFont(mono(10, QFont::Normal, 3)); p.setPen(col().label2);
  p.drawText(QRectF(at.x() + 9, at.y() - 8, 1200, 16), Qt::AlignLeft | Qt::AlignVCenter, text);
}

static QColor biasAccent(const QString& bias) {
  return bias == "BULLISH" ? col().accentCyan : bias == "BEARISH" ? col().accentRed : col().accentNeutral;
}

// ================================================================ CardStrip
CardStrip::CardStrip(QWidget* parent) : QWidget(parent) { setMouseTracking(true); }

void CardStrip::setScores(const QVector<Score>& ranked, bool ready) {
  m_ready = ready;
  m_scores = ranked;
  if (!ready || m_scores.isEmpty()) {
    m_scores.clear();
    for (const auto& m : currencies()) { Score s; s.ccy = m.ccy; s.bias = dash(); m_scores << s; }
  }
  update();
}
void CardStrip::setSelected(const QString& ccy, bool fresh) { m_sel = ccy; m_fresh = fresh; update(); }

QRectF CardStrip::cardRect(int i) const {
  if (width() < 760) {   // phone: 4 x 2
    const double gap = 8, w = (width() - 28 - 3 * gap) / 4.0;
    return QRectF(14 + (i % 4) * (w + gap), (i / 4) * (93 + 8), w, 93);
  }
  const double gap = 8, w = (width() - 28 - 7 * gap) / 8.0;
  return QRectF(14 + i * (w + gap), 0, w, 93);
}

void CardStrip::paintEvent(QPaintEvent*) {
  QPainter p(this);
  const auto& c = col();
  for (int i = 0; i < m_scores.size() && i < 8; ++i) {
    const Score& s = m_scores[i];
    const QRectF r = cardRect(i);
    const bool sel = s.ccy == m_sel;
    p.fillRect(r, c.card);
    // accent line + glow
    const QColor acc = m_ready && std::isfinite(s.score) ? biasAccent(s.bias) : theme::alpha(c.accentNeutral, 70);
    QLinearGradient g(0, r.top() + 2, 0, r.top() + 22);
    g.setColorAt(0, theme::alpha(acc, 40)); g.setColorAt(1, theme::alpha(acc, 0));
    p.fillRect(QRectF(r.left(), r.top() + 2, r.width(), 20), g);
    p.fillRect(QRectF(r.left(), r.top(), r.width(), 2), acc);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(sel ? (m_fresh ? c.selGold : c.selIdle) : (i == m_hover ? theme::mix(c.cardBorder, c.selIdle, 0.5) : c.cardBorder));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
    // row 1
    // narrow cards (< 150 px, windows under ~1300 px): no rank index, flag and code shift left, code 15 px;
    // phone cards (four per row): smaller flag, code and score so the score never runs into the code
    const bool compact = r.width() < 150, tiny = width() < 760;
    if (!compact) {
      p.setFont(mono(9)); p.setPen(c.label);
      p.drawText(QRectF(r.left() + 11, r.top() + 13, 20, 16), Qt::AlignLeft | Qt::AlignVCenter, QString("%1").arg(i + 1, 2, 10, QChar('0')));
    }
    if (tiny) p.drawPixmap(QPointF(r.left() + 6, r.top() + 15), ui::flag(s.ccy, QSize(16, 11)));
    else p.drawPixmap(QPointF(r.left() + (compact ? 9 : 30), r.top() + 14), ui::flag(s.ccy, QSize(20, 14)));
    p.setFont(sans(tiny ? 13 : compact ? 15 : 16, QFont::Bold)); p.setPen(sel && m_fresh ? theme::map("#f0d070") : c.white);
    p.drawText(QRectF(r.left() + (tiny ? 25 : compact ? 34 : 57), r.top() + 11, 80, 20), Qt::AlignLeft | Qt::AlignVCenter, s.ccy);
    const bool has = m_ready && std::isfinite(s.score);
    const QColor sc = !has ? c.sub : s.bias == "BULLISH" ? c.cyanScore.lighter(130) : s.bias == "BEARISH" ? c.redScore : c.text;
    p.setFont(mono(tiny ? 10 : 13, QFont::Bold)); p.setPen(sc);
    p.drawText(QRectF(r.right() - (tiny ? 76 : 81), r.top() + 11, 70, 20), Qt::AlignRight | Qt::AlignVCenter,
               has ? QString("%1%2").arg(s.score > 0 ? "+" : "").arg(int(s.score)) : dash());
    // row 2: centred gauge
    const double y = r.top() + 43, x0 = r.left() + 11, x1 = r.right() - 11, cx = (x0 + x1) / 2;
    p.fillRect(QRectF(x0, y - 1, x1 - x0, 2), c.track);
    p.fillRect(QRectF(cx - 0.5, y - 3, 1, 6), theme::alpha(c.text, 60));
    if (has) {
      p.setRenderHint(QPainter::Antialiasing);
      if (std::abs(s.score) < 5) { p.setPen(Qt::NoPen); p.setBrush(theme::map("#e8e6e0")); p.drawEllipse(QPointF(cx, y), 2, 2); }
      else {
        const double len = qMin(1.0, std::abs(s.score) / 60.0) * (cx - x0);
        const QColor bc = s.score > 0 ? c.cyan : c.red;
        const QRectF br = s.score > 0 ? QRectF(cx, y - 2, len, 4) : QRectF(cx - len, y - 2, len, 4);
        p.setPen(Qt::NoPen); p.setBrush(theme::alpha(bc, 60)); p.drawRoundedRect(br.adjusted(-1, -1.5, 1, 1.5), 2.5, 2.5);
        p.setBrush(bc); p.drawRoundedRect(br, 1.5, 1.5);
      }
      p.setRenderHint(QPainter::Antialiasing, false);
    }
    // row 3: bias pill
    const QRectF pr = tiny ? QRectF(r.left() + 6, r.top() + 52, r.width() - 12, 32) : QRectF(r.left() + 11, r.top() + 52, r.width() - 22, 32);
    QColor bg = c.pillNeutral, fg = c.pillNeutralText, bd = theme::mix(c.pillNeutral, c.text, 0.08);
    if (has && s.bias == "BULLISH") { bg = c.pillBull; fg = c.cyanText; bd = c.pillBullBorder; }
    else if (has && s.bias == "BEARISH") { bg = c.pillBear; fg = c.pillBearText; bd = theme::mix(c.pillBear, c.pillBearText, 0.2); }
    if (!has) { bg = theme::mix(c.card, c.pillNeutral, 0.6); fg = c.sub; }
    QLinearGradient pg(0, pr.top(), 0, pr.bottom());
    pg.setColorAt(0, bg.lighter(112)); pg.setColorAt(1, bg);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(bd, 1)); p.setBrush(pg); p.drawRoundedRect(pr.adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);
    p.setRenderHint(QPainter::Antialiasing, false);
    const QString word = has ? tw(s.bias) : (m_ready ? dash() : QCoreApplication::translate("seasonal", "LOADING"));
    QFont wf = mono(12, QFont::Bold, 2);
    if (tiny)   // phone: the bias word steps down until it fits the pill
      for (const auto& [px, sp] : {std::pair{12, 0.8}, std::pair{11, 0.4}, std::pair{10, 0.2}, std::pair{9, 0.0}}) {
        if (QFontMetricsF(wf).horizontalAdvance(word) <= pr.width() - 6) break;
        wf = mono(px, QFont::Bold, sp);
      }
    p.setFont(wf); p.setPen(fg);
    p.drawText(pr.adjusted(2, 0, 0, 0), Qt::AlignCenter, word);
  }
}
void CardStrip::mouseMoveEvent(QMouseEvent* e) {
  int h = -1;
  for (int i = 0; i < 8; ++i) if (cardRect(i).contains(e->position())) h = i;
  if (h != m_hover) { m_hover = h; setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor); update(); }
}
void CardStrip::mousePressEvent(QMouseEvent* e) {
  for (int i = 0; i < 8 && i < m_scores.size(); ++i)
    if (cardRect(i).contains(e->position())) emit picked(m_scores[i].ccy);
}
void CardStrip::leaveEvent(QEvent*) { m_hover = -1; update(); }

// ================================================================ HistoryChart
HistoryChart::HistoryChart(Kind k, QWidget* parent) : QWidget(parent), m_kind(k) {
  setMouseTracking(true);
  m_anim.setInterval(33);   // ≤ 30 fps
  connect(&m_anim, &QTimer::timeout, this, [this] {
    m_reveal = qMin(1.0, (QDateTime::currentMSecsSinceEpoch() - m_animStart) / 600.0);
    m_reveal = 1 - std::pow(1 - m_reveal, 2);   // ease-out
    if (QDateTime::currentMSecsSinceEpoch() - m_animStart >= 600) { m_reveal = 1; m_anim.stop(); }
    update();
  });
}
void HistoryChart::setData(const Series& s, const QString& t, const QString& sub, double target, const QString& tl,
                           const QString& status) {
  m_series = s; m_title = t; m_sub = sub; m_target = target; m_targetLabel = tl; m_status = status;
  m_visValid = false;
  update();
}
void HistoryChart::setYears(int y) { m_years = y; m_visValid = false; update(); }
void HistoryChart::replay() {
  if (!isVisible()) { m_reveal = 1; return; }
  if (qEnvironmentVariableIsSet("DW_FUND_REVEAL")) {   // test hook: freeze the draw-in at a fraction (0..1)
    m_reveal = qBound(0.0, qEnvironmentVariable("DW_FUND_REVEAL").toDouble(), 1.0); update(); return;
  }
  m_reveal = 0; m_animStart = QDateTime::currentMSecsSinceEpoch(); m_anim.start(); update();
}
void HistoryChart::hideEvent(QHideEvent*) { m_anim.stop(); m_reveal = 1; }
QRectF HistoryChart::plot() const { return QRectF(43, 61, width() - 43 - 14, height() - 61 - 50); }

// Painted on every animation frame and mouse move: the window and each point's time (a QDateTime per point) are
// kept until the series or the years change.
const Series& HistoryChart::visible() const {
  if (m_visValid) return m_vis;
  m_vis.clear(); m_visSecs.clear();
  if (m_series.isEmpty() || m_years <= 0) m_vis = m_series;
  else {
    const QDate from = m_series.last().date.addYears(-m_years);
    for (const auto& p : m_series) if (p.date >= from) m_vis << p;
  }
  m_visSecs.reserve(m_vis.size());
  for (const auto& p : m_vis) m_visSecs << QDateTime(p.date, QTime(0, 0)).toSecsSinceEpoch();
  m_visValid = true;
  return m_vis;
}

void HistoryChart::paintEvent(QPaintEvent*) {
  QPainter p(this);
  const auto& c = col();
  p.fillRect(rect(), c.chartBg);
  p.setPen(c.chartBorder); p.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
  p.setFont(mono(9, QFont::Normal, 2.5)); p.setPen(c.chartTitle);
  p.drawText(QRectF(10, 8, width() - 100, 16), Qt::AlignLeft | Qt::AlignVCenter, m_title);
  p.setFont(mono(9)); p.setPen(theme::isDefault() ? c.chartSub : c.chartTitle);   // the measured near-black sub-line fades out in other themes
  p.drawText(QRectF(10, 26, width() - 20, 16), Qt::AlignLeft | Qt::AlignVCenter, m_sub);
  const Series& v = visible();
  const QVector<qint64>& vs = m_visSecs;   // seconds of v's dates (same indices)
  const QRectF pr = plot();
  if (v.size() < 2) {
    p.setFont(mono(10)); p.setPen(c.sub);
    p.drawText(pr, Qt::AlignCenter, m_status.isEmpty() ? QCoreApplication::translate("seasonal", "No history") : m_status);
    return;
  }
  // last value
  const double last = v.last().value;
  QColor lc = c.policyValue;
  if (m_kind == Inflation) lc = last > m_target + 0.5 ? c.inflRed : last < m_target - 0.5 ? c.policyValue : c.inflGold;
  p.setFont(mono(13)); p.setPen(lc);
  p.drawText(QRectF(width() - 110, 8, 100, 18), Qt::AlignRight | Qt::AlignVCenter,
             QString::number(last, 'f', m_kind == Policy ? 2 : 1) + "%");
  // ranges
  double lo = 1e9, hi = -1e9;
  for (const auto& x : v) if (std::isfinite(x.value)) { lo = qMin(lo, x.value); hi = qMax(hi, x.value); }
  if (m_kind == Inflation) { lo = qMin(lo, m_target); hi = qMax(hi, m_target); }
  lo = qMin(lo, 0.0);
  const double span = qMax(hi - lo, 0.5);
  hi += span * 0.08; if (lo < 0) lo -= span * 0.08;
  const QVector<double> ticks = chart::niceTicks(lo, hi, 5);
  if (!ticks.isEmpty()) { lo = qMin(lo, ticks.first()); hi = qMax(hi, ticks.last()); }
  const qint64 t0 = vs.first();
  const qint64 t1 = vs.last();
  auto XS = [&](qint64 secs) { return pr.left() + double(secs - t0) / qMax<qint64>(1, t1 - t0) * pr.width(); };
  auto X = [&](const QDate& d) { return XS(QDateTime(d, QTime(0, 0)).toSecsSinceEpoch()); };
  auto Y = [&](double val) { return pr.bottom() - (val - lo) / (hi - lo) * pr.height(); };
  p.setRenderHint(QPainter::Antialiasing);
  // grid + labels
  p.setFont(mono(11));
  const bool fine = (ticks.size() > 1 && ticks[1] - ticks[0] < 1 - 1e-9);
  for (double t : ticks) {
    const double y = Y(t);
    p.setPen(QPen(std::abs(t) < 1e-9 ? c.zero : c.grid, 1)); p.drawLine(QPointF(pr.left(), y), QPointF(pr.right(), y));
    p.setPen(c.axis);
    p.drawText(QRectF(0, y - 7, pr.left() - 8, 14), Qt::AlignRight | Qt::AlignVCenter,
               QString::number(t, 'f', fine ? 1 : 0) + "%");
  }
  {
    const int y0 = v.first().date.year(), y1 = v.last().date.year();
    const int step = (y1 - y0) > 12 ? 4 : 2;
    p.setFont(prose(13)); p.setPen(c.axis);
    for (int y = y0 + 1; y <= y1; ++y) {
      if (y % step) continue;
      const double x = X(QDate(y, 1, 1));
      if (x < pr.left() + 10 || x > pr.right() - 5) continue;
      p.drawText(QRectF(x - 30, pr.bottom() + 8, 60, 16), Qt::AlignCenter, QString::number(y));
    }
  }
  // series path
  QPainterPath line; QVector<QPointF> pts;
  for (int i = 0; i < v.size(); ++i) {
    if (!std::isfinite(v[i].value)) continue;
    const QPointF pt(XS(vs[i]), Y(v[i].value));
    if (m_kind == Policy && !pts.isEmpty()) { const QPointF h(pt.x(), pts.last().y()); pts << h; }
    pts << pt;
  }
  if (pts.isEmpty()) return;
  line.moveTo(pts.first());
  for (int i = 1; i < pts.size(); ++i) line.lineTo(pts[i]);
  p.save();
  p.setClipRect(QRectF(0, 0, pr.left() + m_reveal * pr.width() + 1, height()));
  if (m_kind == Policy) {
    QPainterPath area = line;
    const double zy = Y(qMax(0.0, lo));
    area.lineTo(pts.last().x(), zy); area.lineTo(pts.first().x(), zy); area.closeSubpath();
    QLinearGradient g(0, pr.top(), 0, zy);
    g.setColorAt(0, theme::alpha(theme::map("#1b5c63"), 150)); g.setColorAt(1, theme::alpha(c.policyFill, 40));
    p.fillPath(area, g);
    p.setPen(QPen(theme::alpha(c.policyLine, 50), 4.5)); p.setBrush(Qt::NoBrush); p.drawPath(line);
    p.setPen(QPen(c.policyLine, 1.6)); p.drawPath(line);
    if (m_reveal >= 1) {
      p.setPen(Qt::NoPen); p.setBrush(theme::alpha(c.policyLine, 70)); p.drawEllipse(pts.last(), 5, 5);
      p.setBrush(c.policyLine); p.drawEllipse(pts.last(), 2.6, 2.6);
    }
  } else {
    const double ty = Y(m_target);
    QPainterPath area = line;
    area.lineTo(pts.last().x(), ty); area.lineTo(pts.first().x(), ty); area.closeSubpath();
    p.save(); p.setClipRect(QRectF(pr.left() - 2, pr.top() - 20, pr.width() + 4, ty - pr.top() + 20), Qt::IntersectClip);
    p.fillPath(area, theme::alpha(theme::map("#5a2a1a"), 150)); p.restore();
    p.save(); p.setClipRect(QRectF(pr.left() - 2, ty, pr.width() + 4, pr.bottom() - ty + 20), Qt::IntersectClip);
    p.fillPath(area, theme::alpha(theme::map("#11403f"), 150)); p.restore();
    p.setPen(QPen(theme::alpha(c.inflLine, 45), 4.5)); p.setBrush(Qt::NoBrush); p.drawPath(line);
    p.setPen(QPen(c.inflLine, 1.6)); p.drawPath(line);
  }
  p.restore();
  if (m_kind == Inflation) {
    const double ty = Y(m_target);
    QPen tp(theme::alpha(c.target, 170), 1, Qt::CustomDashLine); tp.setDashPattern({3, 3});
    p.setPen(tp); p.drawLine(QPointF(pr.left(), ty), QPointF(pr.left() + m_reveal * pr.width(), ty));   // revealed with the line
    p.setFont(prose(13)); p.setPen(theme::map("#d8d6d0"));
    p.drawText(QRectF(pr.left() + 6, ty - 18, 200, 14), Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("seasonal", "%1 target").arg(m_targetLabel));
  }
  // hover
  if (m_reveal >= 1 && pr.adjusted(0, -10, 0, 10).contains(m_hover)) {
    int best = -1; double bd = 1e9;
    for (int i = 0; i < v.size(); ++i) {
      if (!std::isfinite(v[i].value)) continue;
      const double d = std::abs(XS(vs[i]) - m_hover.x());
      if (d < bd) { bd = d; best = i; }
    }
    if (best >= 0) {
      const QPointF pt(XS(vs[best]), Y(v[best].value));
      QPen cp(c.crosshair, 1, Qt::CustomDashLine); cp.setDashPattern({3, 3}); p.setPen(cp);
      p.drawLine(QPointF(pt.x(), pr.top()), QPointF(pt.x(), pr.bottom()));
      p.setPen(QPen(m_kind == Inflation ? c.inflLine : c.policyLine, 1.6)); p.setBrush(theme::map(255, 255, 255));
      p.drawEllipse(pt, 4, 4);
      const QString date = i18n::locale().toString(v[best].date, "MMM yyyy");
      const QString l1 = (m_kind == Inflation ? QCoreApplication::translate("seasonal", "Inflation") : QCoreApplication::translate("seasonal", "Policy rate")) + " ";
      const QString val = QString::number(v[best].value, 'f', m_kind == Policy ? 2 : 1) + "%";
      QString l2; QColor l2c = c.tipCyan;
      if (m_kind == Inflation) {
        const double gap = v[best].value - m_target;
        l2 = QCoreApplication::translate("seasonal", "%1%2 pt vs %3 target").arg(gap >= 0 ? "+" : "").arg(gap, 0, 'f', 1).arg(m_targetLabel);
        l2c = gap > 0 ? c.inflRed : c.tipCyan;
      } else if (best > 0 && std::isfinite(v[best - 1].value)) {
        const int bp = qRound((v[best].value - v[best - 1].value) * 100);
        l2 = bp == 0 ? QCoreApplication::translate("seasonal", "unchanged on the month") : QCoreApplication::translate("seasonal", "%1%2 bp on the month").arg(bp > 0 ? "+" : "").arg(bp);
        l2c = bp > 0 ? c.tipCyan : bp < 0 ? c.inflRed : c.sub;
      }
      const QFontMetricsF f1(mono(11)), f2(mono(12, QFont::Bold)), f3(mono(11));
      const double w = qMax(qMax(f1.horizontalAdvance(date), f2.horizontalAdvance(l1 + val)), f3.horizontalAdvance(l2)) + 22;
      const double h = l2.isEmpty() ? 44 : 62;
      double tx = pt.x() + 40; if (tx + w > width() - 4) tx = pt.x() - 40 - w;
      const double ty = qBound(pr.top(), (m_kind == Inflation ? Y(m_target) : pr.center().y()) - h / 2, pr.bottom() - h);
      const QRectF tr(tx, ty, w, h);
      p.setPen(QPen(c.tipBorder, 1)); p.setBrush(c.tipBg); p.drawRoundedRect(tr.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
      p.setFont(mono(11)); p.setPen(c.tipDate);
      p.drawText(QRectF(tx + 11, ty + 8, w, 14), Qt::AlignLeft | Qt::AlignVCenter, date);
      p.setFont(mono(12)); p.setPen(c.text);
      p.drawText(QRectF(tx + 11, ty + 25, w, 15), Qt::AlignLeft | Qt::AlignVCenter, l1);
      p.setFont(mono(12, QFont::Bold)); p.setPen(theme::map(255, 255, 255));
      p.drawText(QRectF(tx + 11 + QFontMetricsF(mono(12)).horizontalAdvance(l1), ty + 25, w, 15), Qt::AlignLeft | Qt::AlignVCenter, val);
      if (!l2.isEmpty()) {
        p.setFont(mono(11)); p.setPen(l2c);
        p.drawText(QRectF(tx + 11, ty + 43, w, 14), Qt::AlignLeft | Qt::AlignVCenter, l2);
      }
    }
  }
}
void HistoryChart::mouseMoveEvent(QMouseEvent* e) { m_hover = e->position().toPoint(); update(); }
void HistoryChart::leaveEvent(QEvent*) { m_hover = QPoint(-1, -1); update(); }

// ================================================================ PolicyCard
PolicyCard::PolicyCard(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  m_policy = new HistoryChart(HistoryChart::Policy, this);
  m_infl = new HistoryChart(HistoryChart::Inflation, this);
}
void PolicyCard::resizeEvent(QResizeEvent*) {
  if (width() < 760) {   // phone: the two charts one under the other
    m_policy->setGeometry(11, kPhoneTop, width() - 22, kPhoneChartH);
    m_infl->setGeometry(11, kPhoneTop + kPhoneChartH + 10, width() - 22, kPhoneChartH);
    return;
  }
  const int w = (width() - 22 - 12) / 2;
  m_policy->setGeometry(11, 186, w, height() - 186 - 9);
  m_infl->setGeometry(11 + w + 12, 186, width() - 22 - 12 - w, height() - 186 - 9);
}
QRectF PolicyCard::rangeRect(int i) const {
  static const int xs[] = {0, 32, 70, 106}, ws[] = {28, 34, 32, 36};
  if (width() < 760) {   // phone: finger-sized range buttons under the gauge
    static const int pxs[] = {0, 46, 96, 146}, pws[] = {42, 46, 46, 50};
    return QRectF(width() - 12 - 196 + pxs[i], kPhoneTop - 38, pws[i], 30);
  }
  return QRectF(width() - 15 - 142 + xs[i], 156, ws[i], 18);
}
void PolicyCard::setData(const Inputs* in, const QString& ccy, const QString& status, bool animate) {
  m_has = in != nullptr; if (in) m_in = *in; m_ccy = ccy; m_status = status;
  const CurrencyMeta* cm = meta(ccy);
  const QString bank = cm ? cm->bankShort : ccy;
  const QString title = cm ? (cm->bankShort == "RBA" || cm->bankShort == "RBNZ" || cm->bankShort == "ECB" || cm->bankShort == "SNB" || cm->bankShort == "BOC"
                                  ? bankShortName(cm->bankShort).toUpper() : tw(cm->bankTitle)) : ccy;
  const double target = cm ? cm->target : 2.0;
  const QString tl = cm ? cm->targetLabel : QString("2%");
  m_policy->setData(m_has ? m_in.policyHistory : Series(), QCoreApplication::translate("seasonal", "POLICY RATE · %1").arg(title),
                    m_has ? QCoreApplication::translate("seasonal", "%1 · last point = latest decision").arg(m_in.policySource) : QString(),
                    target, tl, m_has ? QCoreApplication::translate("seasonal", "No policy-rate history") : status);
  m_infl->setData(m_has ? m_in.cpiHistory : Series(), QCoreApplication::translate("seasonal", "INFLATION VS THE %1 TARGET").arg(tl),
                  m_has ? QCoreApplication::translate("seasonal", "CPI y/y · %1 · last point = latest release").arg(m_in.cpiSource) : QString(),
                  target, tl, m_has ? QCoreApplication::translate("seasonal", "No CPI history") : status);
  Q_UNUSED(bank);
  if (animate) { m_policy->replay(); m_infl->replay(); }
  update();
}

void PolicyCard::paintEvent(QPaintEvent*) {
  QPainter p(this);
  const auto& c = col();
  drawMacroCard(p, rect(), c.card, c.panelBorder);
  const CurrencyMeta* cm = meta(m_ccy);
  const QString bankTitle = cm ? tw(cm->bankTitle) : m_ccy;
  // phone (< 760 px): rate block, move pill, next meeting, inflation gauge and history row stacked (positions below)
  const bool phone = width() < 760;
  if (phone) {   // title: tighter letter spacing, then elided (long bank names)
    const QString t = QCoreApplication::translate("seasonal", "MONETARY POLICY · %1").arg(bankTitle);
    QFont tf = mono(10, QFont::Normal, 3);
    for (qreal sp : {1.5, 0.5}) if (QFontMetricsF(tf).horizontalAdvance(t) > width() - 40) tf = mono(10, QFont::Normal, sp);
    drawDiamond(p, QPointF(14, 13), 3.6, c.diamond);
    p.setFont(tf); p.setPen(c.label2);
    p.drawText(QRectF(23, 5, width() - 37, 16), Qt::AlignLeft | Qt::AlignVCenter, QFontMetricsF(tf).elidedText(t, Qt::ElideRight, width() - 37));
  } else drawCardTitle(p, QPointF(14, 13), QCoreApplication::translate("seasonal", "MONETARY POLICY · %1").arg(bankTitle));
  // policy rate
  p.setFont(mono(9, QFont::Normal, 2.5)); p.setPen(c.label);
  p.drawText(QRectF(11, 52, 400, 14), Qt::AlignLeft | Qt::AlignVCenter,
             m_has && !m_in.rateText.isEmpty() ? QCoreApplication::translate("seasonal", "POLICY RATE · TOP OF %1").arg(m_in.rateText) : QCoreApplication::translate("seasonal", "POLICY RATE"));
  const bool hasRate = m_has && std::isfinite(m_in.policyRate);
  p.setFont(mono(44, QFont::Medium)); p.setPen(hasRate ? c.white : c.sub);
  p.drawText(QRectF(9, 62, 190, 52), Qt::AlignLeft | Qt::AlignVCenter, hasRate ? QString::number(m_in.policyRate, 'f', 2) + "%" : dash());
  p.setFont(mono(10)); p.setPen(c.label);
  QString prior = dash();
  if (m_has && std::isfinite(m_in.priorRate))
    prior = QCoreApplication::translate("seasonal", "prior %1% · %2").arg(QString::number(m_in.priorRate, 'f', 2), m_in.lastDecision.toString("yyyy-MM-dd"));
  else if (!m_has) prior = QCoreApplication::translate("seasonal", "prior %1").arg(dash());
  p.drawText(QRectF(11, 117, 190, 14), Qt::AlignLeft | Qt::AlignVCenter, prior);
  // last move pill
  const Move mv = m_has ? lastMove(m_in) : Move::Unknown;
  const QRectF mp = phone ? QRectF(11, 140, width() - 22, 30) : QRectF(191, 73, 301, 28);
  p.setRenderHint(QPainter::Antialiasing);
  if (mv == Move::Hike || mv == Move::Cut) {
    QLinearGradient g(mp.topLeft(), mp.topRight());
    g.setColorAt(0, mv == Move::Hike ? c.hikeA : c.cutA); g.setColorAt(1, mv == Move::Hike ? c.hikeB : c.cutB);
    p.setPen(Qt::NoPen); p.setBrush(g);
  } else { p.setPen(Qt::NoPen); p.setBrush(mv == Move::Hold ? c.holdBg : theme::mix(c.card, c.holdBg, 0.5)); }
  p.drawRoundedRect(mp, 2, 2);
  p.setFont(mono(11, QFont::Medium, 2));
  p.setPen(mv == Move::Hike ? c.hikeText : mv == Move::Cut ? c.cutText : mv == Move::Hold ? c.holdText : c.sub);
  p.drawText(mp, Qt::AlignCenter, QCoreApplication::translate("seasonal", "LAST MOVE · %1").arg(tw(moveWord(mv))));
  // next meeting
  const QRectF nm = phone ? QRectF(11, 176, width() - 22, 26) : QRectF(191, 107, 301, 23);
  p.setRenderHint(QPainter::Antialiasing, false);
  p.setPen(c.meetBorder); p.setBrush(Qt::NoBrush); p.drawRect(nm.adjusted(0.5, 0.5, -0.5, -0.5));
  {
    QString a = QCoreApplication::translate("seasonal", "NEXT MEETING · "), b;
    if (m_has && m_in.nextMeeting.isValid()) {
      a += m_in.nextMeeting.toString("yyyy-MM-dd") + QString::fromUtf8(" · ");
      const int d = daysUntil(m_in.nextMeeting);
      b = d > 0 ? QCoreApplication::translate("seasonal", "IN %1D").arg(d) : d == 0 ? QCoreApplication::translate("seasonal", "TODAY") : QCoreApplication::translate("seasonal", "PASSED");
    } else a += dash();
    QFont fa = mono(11, QFont::Normal, 2), fb = mono(11, QFont::Bold, 2);
    double wa = QFontMetricsF(fa).horizontalAdvance(a), wb = QFontMetricsF(fb).horizontalAdvance(b);
    // long text (narrow windows, French): tighter spacing, then one or two px smaller, never touching the border
    for (const auto& [px, sp] : {std::pair{11, 1.2}, std::pair{11, 0.5}, std::pair{10, 0.3}, std::pair{9, 0.0}}) {
      if (wa + wb <= nm.width() - 12) break;
      fa = mono(px, QFont::Normal, sp); fb = mono(px, QFont::Bold, sp);
      wa = QFontMetricsF(fa).horizontalAdvance(a); wb = QFontMetricsF(fb).horizontalAdvance(b);
    }
    const double x = nm.left() + 8 + qMax(0.0, (nm.width() - 16 - wa - wb) / 2);
    p.setFont(fa); p.setPen(c.sub); p.drawText(QRectF(x, nm.top(), wa + 4, nm.height()), Qt::AlignLeft | Qt::AlignVCenter, a);
    p.setFont(fb); p.setPen(c.meetGold); p.drawText(QRectF(x + wa, nm.top(), wb + 4, nm.height()), Qt::AlignLeft | Qt::AlignVCenter, b);
  }
  // inflation gauge
  {
    const double target = cm ? cm->target : 2.0;
    const QString tl = cm ? cm->targetLabel : QString("2%");
    const double dy = phone ? 182 : 0;   // phone: the gauge block sits under the meeting box
    const double gx0 = phone ? 20 : 506, gx1 = width() - (phone ? 20 : 12), gy = 78 + dy;
    p.setFont(mono(9, QFont::Normal, 3)); p.setPen(c.label);
    p.drawText(QRectF(phone ? 11 : gx0, 40 + dy, 500, 14), Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("seasonal", "INFLATION VS THE %1 TARGET").arg(tl));
    double lo = 0, hi = 6;
    if (m_has) {
      for (double v : {m_in.cpi.value, m_in.coreCpi.value}) if (std::isfinite(v)) { lo = qMin(lo, std::floor(v)); hi = qMax(hi, std::ceil(v)); }
    }
    auto X = [&](double v) { return gx0 + (qBound(lo, v, hi) - lo) / (hi - lo) * (gx1 - gx0); };
    QLinearGradient g(gx0, 0, gx1, 0);
    g.setColorAt(0, c.gaugeTeal); g.setColorAt(qBound(0.0, (target - lo) / (hi - lo), 1.0), c.gaugeOlive);
    g.setColorAt(qBound(0.0, (target + 1.5 - lo) / (hi - lo), 1.0), c.gaugeBrown); g.setColorAt(1, c.gaugeRed);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen); p.setBrush(g); p.drawRoundedRect(QRectF(gx0, gy - 2, gx1 - gx0, 4), 2, 2);
    if (cm && cm->targetHi > cm->targetLo)   // target band
      p.fillRect(QRectF(X(cm->targetLo), gy - 4, X(cm->targetHi) - X(cm->targetLo), 8), theme::alpha(theme::map("#d8d0a0"), 40));
    p.fillRect(QRectF(X(target) - 1, gy - 15, 2, 30), theme::map("#e8e6e0"));
    p.setFont(mono(9)); p.setPen(c.gaugeLabel);
    p.drawText(QRectF(gx0, 97 + dy, 60, 14), Qt::AlignLeft | Qt::AlignVCenter, QString::number(lo, 'f', 0) + "%");
    p.drawText(QRectF(gx1 - 60, 97 + dy, 60, 14), Qt::AlignRight | Qt::AlignVCenter, QString::number(hi, 'f', 0) + "%");
    p.drawText(QRectF(X(target) - 60, 97 + dy, 120, 14), Qt::AlignCenter, QCoreApplication::translate("seasonal", "%1 target").arg(tl));
    const bool hasH = m_has && m_in.cpi.ok(), hasC = m_has && m_in.coreCpi.ok();
    if (hasC) {
      p.setPen(QPen(theme::map(0, 0, 0, 160), 1)); p.setBrush(theme::map(255, 255, 255)); p.drawEllipse(QPointF(X(m_in.coreCpi.value), gy), 3.5, 3.5);
    }
    if (hasH) {
      const QPointF hc(X(m_in.cpi.value), gy);
      const bool overlap = hasC && std::abs(X(m_in.coreCpi.value) - hc.x()) < 8;
      if (overlap) { p.setPen(Qt::NoPen); p.setBrush(theme::alpha(c.headline, 50)); p.drawEllipse(hc, 13, 13); }
      QPolygonF d{hc + QPointF(0, -8), hc + QPointF(8, 0), hc + QPointF(0, 8), hc + QPointF(-8, 0)};
      p.setPen(QPen(theme::map(20, 14, 0), 1.5)); p.setBrush(overlap ? Qt::NoBrush : QBrush(c.headline));
      if (overlap) p.setPen(QPen(c.headline, 2));
      p.drawPolygon(d);
      if (overlap) { p.setPen(Qt::NoPen); p.setBrush(theme::map(255, 255, 255)); p.drawEllipse(hc, 3, 3); }
    }
    // legend
    double lx = gx0;
    p.setFont(mono(11));
    drawDiamond(p, QPointF(lx + 4, 124 + dy), 4, c.headline);
     const QString hl = QCoreApplication::translate("seasonal", "headline %1").arg(hasH ? QString::number(m_in.cpi.value, 'f', 1) + "%" : dash());
    p.setPen(c.legendGold); p.drawText(QRectF(lx + 14, 117 + dy, 200, 14), Qt::AlignLeft | Qt::AlignVCenter, hl);
    lx += 14 + QFontMetricsF(mono(11)).horizontalAdvance(hl) + 18;
    p.setPen(Qt::NoPen); p.setBrush(theme::map(255, 255, 255)); p.drawEllipse(QPointF(lx + 3, 124 + dy), 3, 3);
    p.setPen(theme::map("#e8e6e0"));
    p.drawText(QRectF(lx + 12, 117 + dy, 200, 14), Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("seasonal", "core %1").arg(hasC ? QString::number(m_in.coreCpi.value, 'f', 1) + "%" : dash()));
    p.setRenderHint(QPainter::Antialiasing, false);
  }
  // history label + range buttons
  p.setFont(mono(9, QFont::Normal, 3)); p.setPen(c.label);
  p.drawText(QRectF(11, phone ? rangeRect(0).center().y() - 7 : 158, 200, 14), Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("seasonal", "HISTORY"));
  static const char* names[] = {QT_TRANSLATE_NOOP("seasonal", "5Y"), QT_TRANSLATE_NOOP("seasonal", "10Y"), QT_TRANSLATE_NOOP("seasonal", "20Y"), QT_TRANSLATE_NOOP("seasonal", "MAX")};
  for (int i = 0; i < 4; ++i) {
    const QRectF r = rangeRect(i);
    if (i == m_range) p.fillRect(r, c.segActive);
    p.setFont(mono(10, i == m_range ? QFont::Bold : QFont::Normal, 1)); p.setPen(i == m_range ? theme::onAccent() : c.sub);
    p.drawText(r, Qt::AlignCenter, tw(names[i]));
  }
}
void PolicyCard::mousePressEvent(QMouseEvent* e) {
  for (int i = 0; i < 4; ++i) {
    if (!rangeRect(i).contains(e->position())) continue;
    m_range = i;
    static const int ys[] = {5, 10, 20, 0};
    m_policy->setYears(ys[i]); m_infl->setYears(ys[i]);
    update();
  }
}
void PolicyCard::mouseMoveEvent(QMouseEvent* e) {
  bool on = false;
  for (int i = 0; i < 4; ++i) on |= rangeRect(i).contains(e->position());
  setCursor(on ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

// ================================================================ ReadCard
ReadCard::ReadCard(QWidget* parent) : QWidget(parent) {}
void ReadCard::setReading(const Reading* r, const QString& loading) {
  m_has = r != nullptr; if (r) m_r = *r; m_loading = loading;
  emit heightChanged();
  update();
}

// Lays out wrapped text; returns its height. Draws when p is non-null.
static double flow(QPainter* p, const QString& text, const QFont& f, const QRectF& box, double lineH, const QColor& color) {
  QTextLayout tl(text, f);
  tl.beginLayout();
  double y = 0;
  while (true) {
    QTextLine line = tl.createLine();
    if (!line.isValid()) break;
    line.setLineWidth(box.width());
    line.setPosition(QPointF(0, y + (lineH - line.height()) / 2));
    y += lineH;
  }
  tl.endLayout();
  if (p) { p->setPen(color); tl.draw(p, box.topLeft()); }
  return y;
}

int ReadCard::heightFor(int w) const {
  if (!m_has) return 64;
  double h = 38;
  h += flow(nullptr, m_r.headline, sans(14, QFont::Bold), QRectF(0, 0, w - 20, 0), 20, {}) + 4;
  h += flow(nullptr, m_r.paragraph, prose(14), QRectF(0, 0, w - 20, 0), 21, {}) + 8;
  for (const auto& b : m_r.bullets) h += flow(nullptr, b, prose(13), QRectF(0, 0, w - 38, 0), 17, {}) + 4;
  return qMax(295, int(h + 12));   // reference card height 295 (the comparator starts at y 463)
}

void ReadCard::paintEvent(QPaintEvent*) {
  QPainter p(this);
  const auto& c = col();
  p.fillRect(rect(), c.readBg);
  p.setPen(c.readBorder); p.setBrush(Qt::NoBrush); p.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
  // sparkle (four-point star) icon
  p.setRenderHint(QPainter::Antialiasing);
  {
    const QPointF s(16, 20);
    QPainterPath st;
    st.moveTo(s + QPointF(0, -6)); st.quadTo(s, s + QPointF(6, 0)); st.quadTo(s, s + QPointF(0, 6));
    st.quadTo(s, s + QPointF(-6, 0)); st.quadTo(s, s + QPointF(0, -6));
    p.setPen(QPen(c.diamond, 1.1)); p.setBrush(Qt::NoBrush); p.drawPath(st);
    p.setPen(Qt::NoPen); p.setBrush(c.diamond); p.drawEllipse(s + QPointF(5, -5), 1.2, 1.2);
  }
  p.setFont(mono(10, QFont::Normal, 3)); p.setPen(c.label);
  p.drawText(QRectF(29, 12, 300, 16), Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("seasonal", "AUTOMATED READ"));
  // badge: rule-based
  const QRectF badge(width() - 60, 11, 50, 18);
  p.setPen(Qt::NoPen); p.setBrush(c.badgeGoldBg); p.drawRoundedRect(badge, 2, 2);
  p.setFont(mono(9, QFont::Bold, 1)); p.setPen(c.badgeGold); p.drawText(badge, Qt::AlignCenter, QCoreApplication::translate("seasonal", "RULES"));
  if (!m_has) {
    p.setFont(mono(12)); p.setPen(c.sub);
    p.drawText(QRectF(10, 36, width() - 20, 20), Qt::AlignLeft | Qt::AlignVCenter, m_loading);
    return;
  }
  if (m_r.asOf.isValid()) {
    p.setFont(mono(10)); p.setPen(c.label);
    p.drawText(QRectF(width() - 170, 12, 100, 16), Qt::AlignRight | Qt::AlignVCenter, m_r.asOf.toString("yyyy-MM-dd"));
  }
  double y = 38;
  y += flow(&p, m_r.headline, sans(14, QFont::Bold), QRectF(10, y, width() - 20, 0), 20, theme::map(255, 255, 255)) + 4;
  y += flow(&p, m_r.paragraph, prose(14), QRectF(10, y, width() - 20, 0), 21, c.body) + 8;
  for (const auto& b : m_r.bullets) {
    const double h = flow(&p, b, prose(13), QRectF(28, y, width() - 38, 0), 17, c.body);
    p.fillRect(QRectF(18, y + 1, 1, h - 2), c.readRule);
    y += h + 4;
  }
}

// ================================================================ ComparatorCard
ComparatorCard::ComparatorCard(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  macro::KeyFigures::instance().start();
  connect(&macro::KeyFigures::instance(), &macro::KeyFigures::changed, this, [this] { update(); });
}
namespace {   // the comparator's rows that macro/KeyFigures follows
QString checkedFigure(int row) {
  static const char* const f[] = {"Policy rate", "CPI y/y", "Core CPI y/y", "GDP q/q", "Unemployment"};
  return row >= 0 && row < 5 ? QString::fromLatin1(f[row]) : QString();
}
}
QStringList ComparatorCard::others() const {
  QStringList o;
  static const QStringList order{"AUD", "EUR", "USD", "NZD", "GBP", "CHF", "JPY", "CAD"};
  for (const auto& c : order) if (c != m_base) o << c;
  return o;
}
void ComparatorCard::setData(const QString& base, const QList<Inputs>& all, const QVector<Score>& scores, bool ready) {
  if (base != m_base) m_other = base == "USD" ? "EUR" : "USD";
  m_base = base; m_all = all; m_scores = scores; m_ready = ready;
  update();
}
void ComparatorCard::setOther(const QString& c) { if (c != m_base) { m_other = c; update(); } }
QRectF ComparatorCard::tabRect(int i) const { return QRectF(66 + i * 36.3, 43, 32, 18); }

void ComparatorCard::paintEvent(QPaintEvent*) {
  QPainter p(this);
  const auto& c = col();
  drawMacroCard(p, rect(), c.card, c.panelBorder);
  drawCardTitle(p, QPointF(14, 14), QCoreApplication::translate("seasonal", "PAIR COMPARATOR"));
  p.setFont(mono(16, QFont::Bold)); p.setPen(c.baseCode);
  p.drawText(QRectF(10, 42, 50, 20), Qt::AlignLeft | Qt::AlignVCenter, m_base);
  // ⇆ icon
  p.setRenderHint(QPainter::Antialiasing);
  {
    QPen pen(c.sub, 1); p.setPen(pen);
    const QPointF o(47, 52);
    p.drawLine(o + QPointF(-5, -2.5), o + QPointF(5, -2.5)); p.drawLine(o + QPointF(5, -2.5), o + QPointF(2.5, -5));
    p.drawLine(o + QPointF(5, 2.5), o + QPointF(-5, 2.5)); p.drawLine(o + QPointF(-5, 2.5), o + QPointF(-2.5, 5));
  }
  const QStringList ot = others();
  for (int i = 0; i < ot.size(); ++i) {
    const QRectF r = tabRect(i);
    const bool act = ot[i] == m_other;
    if (act) { p.setPen(Qt::NoPen); p.setBrush(c.tabActiveBg); p.drawRoundedRect(r, 2, 2); }
    p.setFont(mono(11, act ? QFont::Bold : QFont::Normal)); p.setPen(act ? c.tabActive : (i == m_hoverTab ? c.text : c.sub));
    p.drawText(r, Qt::AlignCenter, ot[i]);
  }
  p.setRenderHint(QPainter::Antialiasing, false);
  // table
  const double xa = width() - 162, xb = width() - 83, xd = width() - 11;   // 267 / 346 at the reference 429 px
  p.setFont(mono(9, QFont::Normal, 2)); p.setPen(c.dim);
  p.drawText(QRectF(xa - 80, 70, 80, 14), Qt::AlignRight | Qt::AlignVCenter, m_base);
  p.drawText(QRectF(xb - 80, 70, 80, 14), Qt::AlignRight | Qt::AlignVCenter, m_other);
  p.drawText(QRectF(xd - 80, 70, 80, 14), Qt::AlignRight | Qt::AlignVCenter, QCoreApplication::translate("seasonal", "DIFF"));
  const Inputs *a = nullptr, *b = nullptr;
  for (const auto& in : m_all) { if (in.ccy == m_base) a = &in; if (in.ccy == m_other) b = &in; }
  Comparison cmp;
  if (m_ready && a && b) cmp = compare(*a, *b);
  else {
    Inputs ea, eb; ea.ccy = m_base; eb.ccy = m_other;
    cmp = compare(ea, eb);
  }
  for (int i = 0; i < cmp.rows.size(); ++i) {
    const auto& r = cmp.rows[i];
    const double y = kRowsTop + i * kPitch;
    p.setFont(prose(14)); p.setPen(theme::map("#c9c6c1"));
    p.drawText(QRectF(10, y, xa - 90 - 10, 24), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(p.font()).elidedText(r.label, Qt::ElideRight, int(xa - 100)));
    auto val = [&](double v) { return std::isfinite(v) ? num(v, r.decimals) + r.unit : dash(); };
    // another source gives another figure: amber, with ≠ (both values in the tooltip)
    const QString fig = checkedFigure(i);
    const bool da = std::isfinite(r.a) && !fig.isEmpty() && macro::checkDiffers(m_base, fig);
    const bool db = std::isfinite(r.b) && !fig.isEmpty() && macro::checkDiffers(m_other, fig);
    p.setFont(mono(13));
    p.setPen(da ? theme::pal().accent : r.better > 0 ? c.better : (std::isfinite(r.a) ? theme::map("#e8e5e0") : c.sub));
    p.drawText(QRectF(xa - 90, y, 90, 24), Qt::AlignRight | Qt::AlignVCenter, (da ? QStringLiteral("≠ ") : QString()) + val(r.a));
    p.setPen(db ? theme::pal().accent : r.better < 0 ? c.better : (std::isfinite(r.b) ? theme::map("#e8e5e0") : c.sub));
    p.drawText(QRectF(xb - 90, y, 90, 24), Qt::AlignRight | Qt::AlignVCenter, (db ? QStringLiteral("≠ ") : QString()) + val(r.b));
    p.setFont(mono(12)); p.setPen(c.sub);
    const QString d = std::isfinite(r.a) && std::isfinite(r.b) ? QString("%1%2").arg(r.a - r.b >= 0 ? "+" : "").arg(r.a - r.b, 0, 'f', 2) : dash();
    p.drawText(QRectF(xd - 70, y, 70, 24), Qt::AlignRight | Qt::AlignVCenter, d);
    p.fillRect(QRectF(10, y + 28, width() - 20, 1), c.rowRule);
  }
  if (!cmp.carry.isEmpty()) {
    p.setFont(mono(11)); p.setPen(theme::map("#c9c6c1"));
    p.drawText(QRectF(10, kRowsTop + cmp.rows.size() * kPitch, width() - 20, 20), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(p.font()).elidedText(cmp.carry, Qt::ElideRight, width() - 20));
  }
  // verdict
  const QRectF v(10, 314 + kCarryH, width() - 20, 90);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(c.verdictBorder, 1)); p.setBrush(c.verdictBg); p.drawRoundedRect(v.adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);
  p.setRenderHint(QPainter::Antialiasing, false);
  p.setFont(mono(9, QFont::Normal, 2)); p.setPen(c.verdictLabel);
  p.drawText(QRectF(v.left(), v.top() + 6, v.width(), 16), Qt::AlignCenter, QCoreApplication::translate("seasonal", "FUNDAMENTAL VERDICT · LATEST RELEASES COMPARED"));
  const bool ok = m_ready && a && b && cmp.counted > 0;
  p.setFont(mono(15, QFont::Bold)); p.setPen(ok ? (cmp.winner == 0 ? c.verdictNeutral : c.verdictCyan) : c.sub);
  p.drawText(QRectF(v.left(), v.top() + 24, v.width(), 22), Qt::AlignCenter,
             ok ? cmp.verdict : (m_ready ? cmp.verdict : QCoreApplication::translate("seasonal", "Waiting for the latest releases…")));
  p.setFont(mono(11)); p.setPen(c.sub);
  if (ok) {
    double sa = NAN, sb = NAN;
    for (const auto& s : m_scores) { if (s.ccy == m_base) sa = s.score; if (s.ccy == m_other) sb = s.score; }
    auto sc = [](double s) { return std::isfinite(s) ? QString("%1%2").arg(s > 0 ? "+" : "").arg(int(s)) : dash(); };
    QString line = QCoreApplication::translate("seasonal", "scores %1 %2 · %3 %4").arg(m_base, sc(sa), m_other, sc(sb));
    if (std::isfinite(sa) && std::isfinite(sb))
      line += QString::fromUtf8(" · Δ %1%2").arg(sa - sb >= 0 ? "+" : "").arg(int(sa - sb));
    p.drawText(QRectF(v.left(), v.top() + 46, v.width(), 18), Qt::AlignCenter, line);
    p.drawText(QRectF(v.left(), v.top() + 65, v.width(), 18), Qt::AlignCenter,
               QCoreApplication::translate("seasonal", "levels: %1 leads on %2, %3 on %4 of %5").arg(m_base).arg(cmp.leadsA).arg(m_other).arg(cmp.leadsB).arg(cmp.counted));
  }
}
bool ComparatorCard::event(QEvent* e) {
  if (e->type() == QEvent::ToolTip) {
    auto* he = static_cast<QHelpEvent*>(e);
    const int i = int((he->pos().y() - kRowsTop) / kPitch);
    QString tip;
    if (he->pos().y() >= kRowsTop && i >= 0 && i < kComparedRows + kRealRows) {
      const Inputs *a = nullptr, *b = nullptr;
      for (const auto& in : m_all) { if (in.ccy == m_base) a = &in; if (in.ccy == m_other) b = &in; }
      if (a && b) tip = compare(*a, *b).rows.value(i).note;
      if (const QString fig = checkedFigure(i); !fig.isEmpty())
        for (const QString& ccy : {m_base, m_other})
          if (const QString t = macro::checkTip(ccy, fig); !t.isEmpty()) tip += (tip.isEmpty() ? QString() : QStringLiteral("\n")) + ccy + ": " + QString(t).replace('\n', QStringLiteral("\n    "));
    }
    if (tip.isEmpty()) QToolTip::hideText(); else QToolTip::showText(he->globalPos(), tip, this);
    return true;
  }
  return QWidget::event(e);
}
void ComparatorCard::mousePressEvent(QMouseEvent* e) {
  const QStringList ot = others();
  for (int i = 0; i < ot.size(); ++i) if (tabRect(i).contains(e->position())) { m_other = ot[i]; update(); }
}
void ComparatorCard::mouseMoveEvent(QMouseEvent* e) {
  int h = -1;
  for (int i = 0; i < 7; ++i) if (tabRect(i).contains(e->position())) h = i;
  if (h != m_hoverTab) { m_hoverTab = h; setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor); update(); }
}
void ComparatorCard::leaveEvent(QEvent*) { m_hoverTab = -1; update(); }

// ================================================================ HeatmapCard
HeatmapCard::HeatmapCard(QWidget* parent) : QWidget(parent) { setMouseTracking(true); }
void HeatmapCard::setScores(const QVector<Score>& ranked, bool ready) {
  m_ready = ready; m_scores = ranked;
  if (!ready || m_scores.isEmpty()) {
    m_scores.clear();
    for (const auto& m : currencies()) { Score s; s.ccy = m.ccy; m_scores << s; }
  }
  update();
}
// cell() asks for the columns once per cell, in every paint and up to 8 times per row on each mouse move (hit()):
// they are kept per width (the font metrics of the bias words were measured on each call).
QVector<QPair<double, double>> HeatmapCard::columns() const {
  if (m_colsW == width() && !m_cols.isEmpty()) return m_cols;
  m_colsW = width();
  m_cols = computeColumns();
  return m_cols;
}
QVector<QPair<double, double>> HeatmapCard::computeColumns() const {
  static const double w[] = {144, 123, 150, 115, 157, 138, 149, 88};
  const double gap = 6, total = width() - 26 - 7 * gap;
  double sum = 0; for (double x : w) sum += x;
  // BIAS keeps room for its word ("HAUSSIER" at 11 px, 8 px each side) on narrow cards; the other columns share the rest
  double biasMin = 0;
  for (const char* b : {"BULLISH", "BEARISH", "NEUTRAL"}) biasMin = std::max(biasMin, QFontMetricsF(mono(11, QFont::Bold, 0.5)).horizontalAdvance(tw(b)) + 16);
  const double biasW = std::max(w[7] / sum * total, std::min(biasMin, total * 0.2));
  const double k = (total - biasW) / (sum - w[7]);
  QVector<QPair<double, double>> out; double x = 13;
  for (int i = 0; i < 8; ++i) { const double cw = i == 7 ? biasW : w[i] * k; out << qMakePair(x, cw); x += cw + gap; }
  return out;
}
QRectF HeatmapCard::cell(int row, int c) const {
  if (width() < 760) {   // phone: line 1 currency + bias, line 2 the six signals
    const double y = 70 + row * kPhonePitch, biasW = 112, gap = 5;
    if (c == 0) return QRectF(13, y, width() - 26 - biasW - gap, 30);
    if (c == 7) return QRectF(width() - 13 - biasW, y, biasW, 30);
    const double cw = (width() - 26 - 5 * gap) / 6.0;
    return QRectF(13 + (c - 1) * (cw + gap), y + 34, cw, 42);
  }
  const auto cols = columns();
  return QRectF(cols[c].first, 70 + row * 63.5, cols[c].second, 58);
}
QPair<int, int> HeatmapCard::hit(const QPointF& p) const {
  for (int r = 0; r < m_scores.size(); ++r)
    for (int c = 0; c < 8; ++c) if (cell(r, c).contains(p)) return {r, c};
  return {-1, -1};
}

// Narrow cells (small windows): tighten the letter spacing, then step the size down (never under 9 px).
static QFont fitFont(const QString& text, int px, qreal ls, double width, int weight = QFont::Bold) {
  for (int size = px; size >= 9; --size)
    for (qreal sp : {ls, ls * 0.5, 0.0}) {
      const QFont f = mono(size, weight, sp);
      if (QFontMetricsF(f).horizontalAdvance(text) <= width) return f;
      if (ls == 0) break;
    }
  return mono(9, weight, 0);
}

void HeatmapCard::paintEvent(QPaintEvent*) {
  QPainter p(this);
  const auto& k = col();
  drawMacroCard(p, rect(), k.card, k.panelBorder);
  drawCardTitle(p, QPointF(14, 14), QCoreApplication::translate("seasonal", "FUNDAMENTAL HEATMAP · %1 CURRENCIES").arg(m_scores.size()));
  const QStringList heads{QCoreApplication::translate("seasonal", "CURRENCY"), tw(columnName(0)), tw(columnName(1)), tw(columnName(2)), tw(columnName(3)), tw(columnName(4)), tw(columnName(5)), QCoreApplication::translate("seasonal", "BIAS")};
  const bool phone = width() < 760;
  if (phone)   // phone: the six signal names over their cells (the currency and bias speak for themselves)
    for (int c = 1; c <= 6; ++c) {
      const QRectF r = cell(0, c);
      const QFont f = fitFont(heads[c], 9, 1, r.width(), QFont::Normal);
      p.setFont(f); p.setPen(k.dim);
      p.drawText(QRectF(r.left(), 45, r.width(), 16), Qt::AlignHCenter | Qt::AlignVCenter, QFontMetricsF(f).elidedText(heads[c], Qt::ElideRight, r.width()));
    }
  const auto cols = phone ? QVector<QPair<double, double>>() : columns();
  for (int c = 0; c < 8 && !phone; ++c) {
    const QFont f = fitFont(heads[c], 10, c == 7 ? 2 : 3, cols[c].second - (c == 0 ? 6 : 2), c == 7 ? QFont::Bold : QFont::Normal);
    p.setFont(f);
    p.setPen(c == 7 ? k.biasHeader : k.dim);
    p.drawText(QRectF(cols[c].first + (c == 0 ? 6 : 0), 45, cols[c].second - (c == 0 ? 6 : 0), 16), (c == 0 ? Qt::AlignLeft : Qt::AlignHCenter) | Qt::AlignVCenter,
               QFontMetricsF(f).elidedText(heads[c], Qt::ElideRight, cols[c].second));
  }
  p.setRenderHint(QPainter::Antialiasing);
  auto drawCell = [&](int r, int c, bool hover) {
    const Score& s = m_scores[r];
    const bool sel = s.ccy == m_sel;
    QRectF rc = cell(r, c);
    if (hover) rc = rc.adjusted(-2, -2, 2, 2);
    QColor fill, border, word, sub;
    QString w, sl;
    if (c == 0) {
      fill = sel ? k.cellSelected : k.cellCurrency; border = sel ? k.cellSelBorder : theme::mix(k.cellCurrency, k.text, 0.06);
    } else if (c == 7) {
      const bool has = m_ready && std::isfinite(s.score);
      if (has && s.bias == "BULLISH") { fill = theme::map("#00565a"); border = theme::map("#1f7476"); word = theme::map("#5fd8e4"); }
      else if (has && s.bias == "BEARISH") { fill = theme::map("#5a2420"); border = theme::map("#78403a"); word = theme::map("#f07060"); }
      else { const auto l = look(has ? Shade::Neutral : Shade::Missing); fill = l.fill; border = l.border; word = has ? l.word : k.sub; }
      w = has ? tw(s.bias) : dash();
    } else {
      const Signal& g = s.sig[c - 1];
      const auto l = look(m_ready ? g.shade : Shade::Missing);
      fill = l.fill; border = l.border; word = l.word; sub = l.sub;
      w = m_ready && std::isfinite(g.value) ? tw(g.word) : dash();
      sl = m_ready && std::isfinite(g.value) ? g.sub : QString();
    }
    if (hover) { fill = fill.lighter(118); border = border.lighter(125); }
    if (sel && c > 0) border = k.rowSelBorder;
    if (hover) {   // drop shadow
      p.setPen(Qt::NoPen); p.setBrush(QColor(0, 0, 0, theme::isDark() ? 110 : 40)); p.drawRoundedRect(rc.translated(0, 3).adjusted(1, 1, 1, 2), 3, 3);
    }
    p.setPen(QPen(border, 1)); p.setBrush(fill); p.drawRoundedRect(rc.adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);
    if (c == 0) {
      const QString sc = m_ready && std::isfinite(s.score) ? QString("%1%2").arg(s.score > 0 ? "+" : "").arg(int(s.score)) : dash();
      const QFont cf = sans(15, QFont::Bold), vf = mono(11);
      const double need = 28 + QFontMetricsF(cf).horizontalAdvance(s.ccy) + 8 + QFontMetricsF(vf).horizontalAdvance(sc) + 8;
      const double fx = phone ? 10 : std::clamp(rc.width() - need, 6.0, 18.0);
      const bool two = !phone && need + fx > rc.width();   // a narrow column: the score goes under the name
      p.drawPixmap(QPointF(rc.left() + fx, rc.center().y() - 7), ui::flag(s.ccy, QSize(20, 14)));
      p.setFont(cf); p.setPen(sel ? theme::map("#f0d070") : k.white);
      p.drawText(QRectF(rc.left() + fx + 28, rc.top() + (two ? 8 : 0), 60, two ? 22 : rc.height()), Qt::AlignLeft | Qt::AlignVCenter, s.ccy);
      p.setFont(vf); p.setPen(k.scoreDim);
      if (two) p.drawText(QRectF(rc.left() + fx + 28, rc.top() + 30, rc.width() - fx - 30, 18), Qt::AlignLeft | Qt::AlignVCenter, sc);
      else p.drawText(QRectF(rc.right() - 60, rc.top(), phone ? 50 : 52, rc.height()), Qt::AlignRight | Qt::AlignVCenter, sc);
    } else if (c == 7) {
      // words keep a margin inside the cell (6 px each side); still too wide at 9 px → elided, the tooltip has it all
      const QFont f = fitFont(w, 13, 2, rc.width() - 12);
      p.setFont(f); p.setPen(word);
      p.drawText(rc.adjusted(2, 0, 0, 0), Qt::AlignCenter, QFontMetricsF(f).elidedText(w, Qt::ElideRight, rc.width() - 8));
    } else {
      const QFont f = fitFont(w, 12, 1.5, rc.width() - (phone ? 6 : 12));
      const QString we = QFontMetricsF(f).elidedText(w, Qt::ElideRight, rc.width() - (phone ? 4 : 8));
      p.setFont(f); p.setPen(word);
      if (sl.isEmpty()) p.drawText(rc, Qt::AlignCenter, we);
      else {
        const double wy = phone ? 5 : 13, sy = phone ? 21 : 29;   // phone cells are 42 px high (58 on desktop)
        p.drawText(QRectF(rc.left(), rc.top() + wy, rc.width(), 16), Qt::AlignCenter, we);
        const QFont sf = fitFont(sl, 10, 0, rc.width() - (phone ? 6 : 12), QFont::Normal);
        p.setFont(sf); p.setPen(sub);
        p.drawText(QRectF(rc.left(), rc.top() + sy, rc.width(), 15), Qt::AlignCenter, QFontMetricsF(sf).elidedText(sl, Qt::ElideRight, rc.width() - (phone ? 4 : 8)));
      }
    }
  };
  for (int r = 0; r < m_scores.size(); ++r)
    for (int c = 0; c < 8; ++c) if (!(r == m_hr && c == m_hc)) drawCell(r, c, false);
  if (m_hr >= 0 && m_hc >= 0 && m_hr < m_scores.size()) drawCell(m_hr, m_hc, true);
}
void HeatmapCard::mouseMoveEvent(QMouseEvent* e) {
  const auto h = hit(e->position());
  if (h.first != m_hr || h.second != m_hc) { m_hr = h.first; m_hc = h.second; setCursor(m_hr >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor); update(); }
}
void HeatmapCard::mousePressEvent(QMouseEvent* e) {
  const auto h = hit(e->position());
  if (h.first >= 0) emit picked(m_scores[h.first].ccy);
}
void HeatmapCard::leaveEvent(QEvent*) { m_hr = m_hc = -1; update(); }
bool HeatmapCard::event(QEvent* e) {
  if (e->type() == QEvent::ToolTip) {
    auto* he = static_cast<QHelpEvent*>(e);
    const auto h = hit(he->pos());
    QString tip;
    if (h.first >= 0 && m_ready) {
      const Score& s = m_scores[h.first];
      if (h.second >= 1 && h.second <= 6) {
        const Signal& g = s.sig[h.second - 1];
        tip = std::isfinite(g.value) ? QCoreApplication::translate("seasonal", "%1 %2: %3\nSignal %4 (−1…+1), computed by Deepwater")
                                           .arg(s.ccy, tw(columnName(h.second - 1)), g.why).arg(g.value, 0, 'f', 2)
                                     : QString("%1 %2: %3").arg(s.ccy, tw(columnName(h.second - 1)), g.why);
      } else if (h.second == 0 || h.second == 7) {
        tip = std::isfinite(s.score)
                  ? QCoreApplication::translate("seasonal", "%1 score %2: level %3 (100 × mean of %4 oriented signals) minus the 8-currency average\n"
                            "BULLISH ≥ +8 · BEARISH ≤ −8 · computed by Deepwater")
                        .arg(s.ccy).arg(int(s.score)).arg(s.level, 0, 'f', 0).arg(s.available)
                  : QCoreApplication::translate("seasonal", "%1: fewer than 3 signals, no score").arg(s.ccy);
      }
    }
    if (tip.isEmpty()) QToolTip::hideText(); else QToolTip::showText(he->globalPos(), tip, this);
    return true;
  }
  return QWidget::event(e);
}
}
