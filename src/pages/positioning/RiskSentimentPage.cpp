#include "pages/positioning/RiskSentimentPage.h"
#include "pages/positioning/PosKit.h"
#include "positioning/RiskIndex.h"
#include "app/Pages.h"
#include "ui/Theme.h"
#include "ui/Chart.h"
#include "ui/Format.h"
#include "ui/Touch.h"
#include "core/I18n.h"
#include <QCoreApplication>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QScrollBar>
#include <QToolTip>
#include <QHelpEvent>
#include <QLocale>
#include <QTimer>
#include <QtMath>
#include <cmath>


using positioning::RiskIndex;
using positioning::RiskData;

namespace positioning {
namespace {
const QColor kOff(0xf8, 0x71, 0x71), kNeu(0xfa, 0xcc, 0x15), kOn(0x22, 0xd3, 0xee);
// history zone bands, measured on t000.0 (0–20 … 80–100)
const QColor kBand[5] = {QColor(0x37, 0x1b, 0x15), QColor(0x1f, 0x11, 0x08), QColor(0x1b, 0x15, 0x03), QColor(0x0b, 0x19, 0x15), QColor(0x0f, 0x2d, 0x2c)};
QVector<double> niceTicks3(double lo, double hi, int n) {
  QVector<double> out;
  if (!(hi > lo)) return out;
  const double raw = (hi - lo) / n, mag = std::pow(10.0, std::floor(std::log10(raw))), f = raw / mag;
  const double step = (f <= 1 ? 1 : f <= 2 ? 2 : f <= 3 ? 3 : f <= 5 ? 5 : 10) * mag;
  // counted on an integer and capped, as chart::niceTicks: a step below the values' resolution never advanced v
  if (!(step > 0) || !std::isfinite(step) || lo + step == lo || hi + step == hi) return out;
  const double k0 = std::ceil(lo / step);
  for (int k = 0; k < 1000 && (k0 + k) * step <= hi + step * 1e-9; ++k) out << (k0 + k) * step;
  return out;
}
QColor zoneColor(double v) { return !std::isfinite(v) ? theme::map(0x88, 0x85, 0x7e) : v < 40 ? theme::map(kOff) : v < 60 ? theme::map(kNeu) : theme::map(kOn); }

void paintPanel(QPainter& p, const QRectF& r, bool brackets = true) {
  p.fillRect(r, pk::col::kPanel());
  p.setPen(QPen(theme::map(0x36, 0x31, 0x22), 1)); p.setBrush(Qt::NoBrush);
  p.drawRect(r.adjusted(.5, .5, -.5, -.5));
  if (!brackets) return;
  const qreal L = 9, i = 4;
  p.setPen(QPen(theme::map(0x6f, 0x6a, 0x45), 1.4));
  p.drawLine(QPointF(r.left() + i, r.top() + i), QPointF(r.left() + i + L, r.top() + i));
  p.drawLine(QPointF(r.left() + i, r.top() + i), QPointF(r.left() + i, r.top() + i + L));
  p.setPen(QPen(theme::map(0x4a, 0x46, 0x2c), 1.4));
  p.drawLine(QPointF(r.right() - i, r.bottom() - i), QPointF(r.right() - i - L, r.bottom() - i));
  p.drawLine(QPointF(r.right() - i, r.bottom() - i), QPointF(r.right() - i, r.bottom() - i - L));
}
qreal panelTitle(QPainter& p, qreal x, qreal cy, const QString& t, const QColor& c = theme::map(0x5e, 0x5c, 0x57)) {
  const QFont f = theme::mono(10, QFont::Normal, 3.0);
  const QFontMetricsF fm(f);
  p.setFont(f); p.setPen(c); p.drawText(QPointF(x, cy + fm.capHeight() / 2), t);
  return fm.horizontalAdvance(t);
}
void glowText(QPainter& p, const QRectF& r, int flags, const QString& t, const QColor& c, qreal rad) {
  p.save();
  // halos look dirty on light backgrounds: drawn only in dark themes
  for (int k = 0; k < (theme::isDark() ? 12 : 0); ++k) {
    const qreal a = k * M_PI / 6, rr = rad * (k % 2 ? 1.0 : 0.55);
    p.setPen(theme::alpha(c, 16));
    p.drawText(r.translated(rr * std::cos(a), rr * std::sin(a)), flags, t);
  }
  p.setPen(c); p.drawText(r, flags, t);
  p.restore();
}
// A cached layer is redrawn when missing or drawn for another pixel ratio (window moved between a Retina and a
// standard screen: it was kept and shown blurry or too sharp-small).
bool cacheStale(const QPixmap& pm, const QWidget* w) { return pm.isNull() || pm.devicePixelRatio() != w->devicePixelRatioF(); }
QPixmap makeCache(QWidget* w) {
  const qreal dpr = w->devicePixelRatioF();
  QPixmap pm(w->size() * dpr); pm.setDevicePixelRatio(dpr); pm.fill(Qt::transparent);
  return pm;
}
QString monthCaps(const QDate& d) { return i18n::locale().monthName(d.month(), QLocale::ShortFormat).toUpper().remove('.'); }

// month-start positions for x axes
QList<QPair<int, QString>> monthTicks(const QVector<QDate>& d) {
  QList<QPair<int, QString>> out;
  for (int i = 1; i < d.size(); ++i) if (d[i].month() != d[i - 1].month()) out << qMakePair(i, monthCaps(d[i]));
  return out;
}
// Phone-width panels (< 520 px): twelve month labels no longer fit side by side; a label that would touch the
// previous one is skipped (wider panels keep every month, as before).
bool skipMonth(qreal x, qreal& lastRight, const QString& t, const QFontMetricsF& fm, qreal panelW) {
  if (panelW >= 520) return false;
  const qreal w = fm.horizontalAdvance(t);
  if (x - w / 2 < lastRight + 6) return true;
  lastRight = x + w / 2;
  return false;
}

// ------------------------------------------------------------------------------------------------ gauge
class GaugePanel : public QWidget {
public:
  explicit GaugePanel(QWidget* p) : QWidget(p) {}
  void setData(const RiskData& d) { m_d = d; m_cache = {}; update(); }
  QRect cursorRect() const { return QRect(14 + 240, 10, 14, 18); }
protected:
  void resizeEvent(QResizeEvent*) override { m_cache = {}; }
  void paintEvent(QPaintEvent*) override {
    if (cacheStale(m_cache, this)) { m_cache = makeCache(this); QPainter cp(&m_cache); render(cp); }
    QPainter p(this);
    p.drawPixmap(0, 0, m_cache);
    pk::drawBlockCursor(p, QRectF(m_cursorX, 12, 7, 12));
  }
private:
  void render(QPainter& p) {
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = rect();
    paintPanel(p, r);
    // scale 1 at the reference panel (867x492); grows with taller panels on big screens, shrinks on small ones
    const qreal s = std::min({1.7, (width() - 40) / 640.0, (height() - 20) / 472.0});
    const QPointF c(width() / 2.0, 329 * s + (height() - 492 * s) * 0.5);
    if (theme::isDark()) {  // warm glow behind the gauge (dark themes only)
      QRadialGradient g(c, 300 * s);
      g.setColorAt(0, theme::map(0x2a, 0x18, 0x08, 120)); g.setColorAt(1, theme::map(0, 0, 0, 0));
      p.fillRect(r.adjusted(1, 1, -1, -1), g);
    }
    m_cursorX = 14 + panelTitle(p, 14, 18, QCoreApplication::translate("positioning", "COMPOSITE RISK APPETITE")) + 2;
    const double v = m_d.value;
    const bool ok = std::isfinite(v);
    auto ang = [](double x) { return qDegreesToRadians(210.0 - 2.4 * x); };
    auto pt = [&](double x, qreal rad) { const double a = ang(x); return QPointF(c.x() + rad * s * std::cos(a), c.y() - rad * s * std::sin(a)); };
    // outer scale arc + ticks
    for (int i = 0; i < 120; ++i) {
      const double x0 = i * 100.0 / 120, x1 = (i + 1) * 100.0 / 120;
      const QColor ac = theme::mix(theme::map(0x6a, 0x60, 0x40), theme::map(0x2a, 0x26, 0x18), x0 / 100.0);
      p.setPen(QPen(ac, 1)); p.drawLine(pt(x0, 268), pt(x1, 268));
    }
    for (int k = 0; k <= 50; ++k) {
      const double x = k * 2;
      const bool major = k % 5 == 0;
      if (major) { p.setPen(QPen(theme::map(0xd3, 0xbf, 0x5f), 2, Qt::SolidLine, Qt::FlatCap)); p.drawLine(pt(x, 268), pt(x, 255)); }
      else { p.setPen(QPen(theme::map(0x4e, 0x48, 0x30), 1)); p.drawLine(pt(x, 268), pt(x, 263)); }
    }
    p.setFont(theme::mono(int(std::round(15 * std::max(0.8, s))))); p.setPen(theme::map(0x9e, 0x9b, 0x95));
    for (int k = 0; k <= 10; ++k) {
      const QPointF q = pt(k * 10, 290);
      p.drawText(QRectF(q.x() - 24, q.y() - 11, 48, 22), Qt::AlignCenter, QString::number(k * 10));
    }
    // segment ring
    const QColor zc = zoneColor(v);
    const QColor litA = v < 40 ? theme::map(0xbb, 0x5d, 0x56) : v < 60 ? theme::map(0xa8, 0x8c, 0x22) : theme::map(0x1e, 0x8a, 0x96);
    const QColor litB = v < 40 ? theme::map(0xfb, 0x8b, 0x86) : v < 60 ? theme::map(0xf8, 0xdc, 0x5a) : theme::map(0x6c, 0xe6, 0xf2);
    for (int i = 0; i <= 60; ++i) {
      const double x = i * 100.0 / 60;
      const bool lit = ok && x <= v + 1e-9;
      QColor col;
      if (lit) col = theme::mix(litA, litB, v > 0 ? x / v : 1);
      else col = x < 40 ? theme::map(0x37, 0x28, 0x15) : x < 60 ? theme::map(0x35, 0x30, 0x11) : theme::mix(theme::map(0x1b, 0x30, 0x26), theme::map(0x18, 0x2c, 0x23), (x - 60) / 40);
      // ring measured on t000.0: bars from r≈199 to r≈240, ≈10 px wide, lit bars with a soft glow
      if (lit && theme::isDark()) for (auto [w, a] : {std::pair{19.0, 14}, std::pair{15.0, 26}}) {
        p.setPen(QPen(theme::alpha(col, a), w * s, Qt::SolidLine, Qt::RoundCap)); p.drawLine(pt(x, 203), pt(x, 236));
      }
      p.setPen(QPen(col, 9.5 * s, Qt::SolidLine, Qt::RoundCap)); p.drawLine(pt(x, 204), pt(x, 235));
    }
    // zone labels along the inner side
    struct Z { double at; const char* t; QColor c; };
    const Z zones[] = {{9, QT_TRANSLATE_NOOP("positioning", "EXTREME OFF"), theme::map(0x6a, 0x3a, 0x30)}, {30, QT_TRANSLATE_NOOP("positioning", "RISK-OFF"), theme::map(0xd0, 0x6c, 0x66)},
                       {50, QT_TRANSLATE_NOOP("positioning", "NEUTRAL"), theme::map(0x72, 0x6d, 0x43)}, {69, QT_TRANSLATE_NOOP("positioning", "RISK-ON"), theme::map(0x2d, 0x6f, 0x6a)},
                       {90, QT_TRANSLATE_NOOP("positioning", "EXTREME ON"), theme::map(0x1f, 0x4c, 0x48)}};
    const QFont zf = theme::mono(int(std::round(10 * std::max(0.85, s))), QFont::Medium, 2.2);
    const QFontMetricsF zm(zf);
    for (const auto& z : zones) {
      const QPointF q = pt(z.at, 186);
      p.save(); p.translate(q); p.rotate(90 - (210.0 - 2.4 * z.at));
      p.setFont(zf); p.setPen(z.c);
      const QString zt = QCoreApplication::translate("positioning", z.t);
      const qreal w = zm.horizontalAdvance(zt);
      p.drawText(QPointF(-w / 2, zm.capHeight() / 2), zt);
      p.restore();
    }
    // 1W / 1M markers
    auto marker = [&](double x, const QString& lab) {
      if (!std::isfinite(x)) return;
      QPen dp(theme::map(0xd8, 0xd2, 0xb8), 2, Qt::CustomDashLine, Qt::FlatCap); dp.setDashPattern({1, 1.5});
      p.setPen(dp); p.drawLine(pt(x, 162), pt(x, 180));
      const QPointF q = pt(x, 146);
      p.setFont(theme::mono(10, QFont::Medium)); p.setPen(theme::map(0x9c, 0x99, 0x90));
      p.drawText(QRectF(q.x() - 16, q.y() - 8, 32, 16), Qt::AlignCenter, lab);
    };
    marker(m_d.monthAgo, QCoreApplication::translate("positioning", "1M")); marker(m_d.weekAgo, QCoreApplication::translate("positioning", "1W"));
    // needle + hub
    if (ok) {
      const QPointF tip = pt(v, 192);
      const QColor core = theme::mix(zc, theme::map(QColor(Qt::white)), 0.75);
      for (int k = theme::isDark() ? 3 : 0; k >= 1; --k) { p.setPen(QPen(theme::alpha(zc, 40), 3 + 4 * k, Qt::SolidLine, Qt::RoundCap)); p.drawLine(c, tip); }
      p.setPen(QPen(theme::mix(zc, theme::map(QColor(Qt::white)), 0.35), 3, Qt::SolidLine, Qt::RoundCap)); p.drawLine(c, tip);
      p.setPen(QPen(core, 1.2, Qt::SolidLine, Qt::RoundCap)); p.drawLine(c, tip);
    }
    QPen dot(theme::alpha(zc, 120), 1, Qt::CustomDashLine); dot.setDashPattern({1, 3});
    p.setPen(dot); p.setBrush(Qt::NoBrush); p.drawEllipse(c, 38 * s, 38 * s);
    p.setPen(Qt::NoPen); p.setBrush(theme::alpha(zc, 40)); p.drawEllipse(c, 24 * s, 24 * s);
    p.setBrush(theme::map(0x14, 0x08, 0x06)); p.drawEllipse(c, 17 * s, 17 * s);
    p.setPen(QPen(theme::mix(zc, theme::map(QColor(Qt::white)), 0.25), 3 * s)); p.setBrush(Qt::NoBrush); p.drawEllipse(c, 16 * s, 16 * s);
    p.setPen(Qt::NoPen); p.setBrush(theme::map(0xff, 0xf0, 0xe0)); p.drawEllipse(c, 3.5 * s, 3.5 * s);
    // value + zone label
    QFont vf = theme::mono(int(66 * s), QFont::ExtraBold);
    p.setFont(vf);
    glowText(p, QRectF(c.x() - 150, c.y() + 57 * s - 40 * s, 300, 80 * s), Qt::AlignCenter, ok ? fmt::whole(v) : fmt::dash(), theme::mix(zc, theme::map(QColor(Qt::white)), 0.08), 7);
    p.setFont(theme::mono(int(20 * s), QFont::Medium, 6 * s));
    glowText(p, QRectF(c.x() - 200, c.y() + 107 * s - 14, 400, 28), Qt::AlignCenter,
             ok ? RiskIndex::zoneLabel(v) : (m_d.error.isEmpty() ? QCoreApplication::translate("positioning", "LOADING") : QCoreApplication::translate("positioning", "NO DATA")), zc, 3);
    // change pill
    if (ok) {
      const QFont f = theme::mono(11, QFont::Normal, 1.0);
      const QFontMetricsF fm(f);
      struct Part { QString t; QColor c; };
      auto block = [&](const QString& lab, double prev) {
        QList<Part> out;
        out << Part{lab + "  ", theme::map(0x8e, 0x8c, 0x81)};
        if (!std::isfinite(prev)) { out << Part{fmt::dash(), theme::map(0x5c, 0x59, 0x4b)}; return out; }
        const int d = fmt::roundInt(v) - fmt::roundInt(prev);
        const QColor cc = d < 0 ? theme::map(0xd3, 0x6a, 0x66) : d > 0 ? theme::map(0x4f, 0xc8, 0xd8) : theme::map(0x8e, 0x8c, 0x81);
        out << Part{d < 0 ? "▼ " : d > 0 ? "▲ " : "= ", cc} << Part{QString::number(std::abs(d)) + " ", cc}
            << Part{QCoreApplication::translate("positioning", "(WAS %1)").arg(fmt::roundInt(prev)), theme::map(0x5c, 0x59, 0x4b)};
        return out;
      };
      // two separate pills, as on t000.0: [1W ▼ 1 (WAS 33)]  [1M ▼ 31 (WAS 63)]
      const QList<QList<Part>> blocks{block(QCoreApplication::translate("positioning", "1W"), m_d.weekAgo), block(QCoreApplication::translate("positioning", "1M"), m_d.monthAgo)};
      QList<qreal> ws; qreal total = 0;
      for (const auto& b : blocks) { qreal w = 0; for (const auto& x : b) w += fm.horizontalAdvance(x.t); ws << w + 22; total += w + 22; }
      total += 8;
      qreal x0 = c.x() - total / 2;
      p.setFont(f);
      for (int k = 0; k < blocks.size(); ++k) {
        // phone-width panel (< 520 px): the two pills span more than the "0"–"100" gap, so they go under those labels
        const QRectF pill(x0, c.y() + 129 * s + (width() < 520 ? 24 : 0), ws[k], 19);
        p.setPen(QPen(theme::map(0x2b, 0x2a, 0x1d), 1)); p.setBrush(theme::map(0x14, 0x10, 0x00));
        p.drawRoundedRect(pill.adjusted(.5, .5, -.5, -.5), 2, 2);
        qreal x = pill.left() + 11;
        for (const auto& part : blocks[k]) {
          p.setPen(part.c); p.drawText(QPointF(x, pill.center().y() + fm.capHeight() / 2), part.t);
          x += fm.horizontalAdvance(part.t);
        }
        x0 += ws[k] + 8;
      }
    }
  }
  RiskData m_d; QPixmap m_cache; qreal m_cursorX = 250;
};

// ------------------------------------------------------------------------------------------------ history
class HistoryPanel : public QWidget {
public:
  explicit HistoryPanel(QWidget* p) : QWidget(p) { setMouseTracking(true); }
  void setData(const RiskData& d) { m_d = d; m_cache = {}; update(); }
  void setHover(const QPointF& pt) { m_hover = pt; m_has = true; update(); }
protected:
  // narrow panels (< 660 px): the legend goes on a second line under the title
  bool legendBelow() const { return width() < 660; }
  QRectF plot() const { const int top = legendBelow() ? 54 : 39; return QRectF(9, top, width() - 9 - 53, height() - top - 28); }
  void resizeEvent(QResizeEvent*) override { m_cache = {}; }
  void mouseMoveEvent(QMouseEvent* e) override { setHover(e->position()); }
  void leaveEvent(QEvent*) override { m_has = false; update(); }
  void paintEvent(QPaintEvent*) override {
    if (cacheStale(m_cache, this)) { m_cache = makeCache(this); QPainter cp(&m_cache); render(cp); }
    QPainter p(this); p.drawPixmap(0, 0, m_cache);
    const QRectF pl = plot();
    const int n = m_d.composite.size();
    if (!m_has || n < 2 || !pl.contains(m_hover)) return;
    p.setRenderHint(QPainter::Antialiasing);
    const int i = qBound(0, fmt::roundInt((m_hover.x() - pl.left()) / pl.width() * (n - 1)), n - 1);
    const double v = m_d.composite[i];
    const qreal x = pl.left() + pl.width() * i / (n - 1);
    QPen dp(theme::map(0xf9, 0xf4, 0xb5, 200), 1, Qt::CustomDashLine); dp.setDashPattern({3, 3});
    p.setPen(dp); p.drawLine(QPointF(x, pl.top()), QPointF(x, pl.bottom()));
    if (!std::isfinite(v)) return;
    const qreal y = pl.bottom() - v / 100.0 * pl.height();
    p.setPen(QPen(theme::map(0x2a, 0x22, 0x08), 1.5)); p.setBrush(theme::map(0xf9, 0xf4, 0xc5)); p.drawEllipse(QPointF(x, y), 3.5, 3.5);
    // box left of and just below the cursor, as on t007.5 (it keeps its place near the cursor while the page scrolls)
    QRectF box(m_hover.x() - 25 - 92, m_hover.y() + 17, 92, 60);
    if (box.left() < pl.left()) box.moveLeft(m_hover.x() + 16);
    if (box.bottom() > height() - 4) box.moveBottom(height() - 4);
    p.setPen(QPen(theme::map(0x62, 0x55, 0x3f), 1)); p.setBrush(theme::map(0x0c, 0x08, 0x00, 245));
    p.drawRoundedRect(box, 3, 3);
    p.setFont(theme::mono(12)); p.setPen(theme::map(0xf2, 0xef, 0xe6));
    p.drawText(QRectF(box.left(), box.top() + 6, box.width(), 18), Qt::AlignCenter, m_d.dates[i].toString("yyyy-MM-dd"));
    p.setPen(Qt::NoPen); p.setBrush(zoneColor(v)); p.drawEllipse(QPointF(box.left() + 18, box.top() + 39), 5, 5);
    p.setFont(theme::mono(12, QFont::Bold)); p.setPen(theme::map(QColor(Qt::white)));
    p.drawText(QRectF(box.left(), box.top() + 30, box.width() - 12, 18), Qt::AlignRight | Qt::AlignVCenter, fmt::whole(v));
  }
private:
  void render(QPainter& p) {
    p.setRenderHint(QPainter::Antialiasing);
    paintPanel(p, rect());
    panelTitle(p, 11, 14, QCoreApplication::translate("positioning", "COMPOSITE HISTORY · 1 YEAR"));
    {  // legend
      const QFont f = theme::mono(10, QFont::Bold, 1.6);
      const QFontMetricsF fm(f);
      const QList<QPair<QString, QColor>> items{{QCoreApplication::translate("positioning", "RISK-OFF < 40"), theme::map(0xe9, 0x8e, 0x8b)}, {QCoreApplication::translate("positioning", "NEUTRAL 40-60"), theme::map(0xf0, 0xd9, 0x62)}, {QCoreApplication::translate("positioning", "RISK-ON > 60"), theme::map(0x5c, 0xcd, 0xd9)}};
      qreal x = width() - 12;
      const qreal ly = legendBelow() ? 33 : 14;
      for (int k = items.size() - 1; k >= 0; --k) {
        const qreal w = fm.horizontalAdvance(items[k].first);
        x -= w; p.setFont(f); p.setPen(items[k].second); p.drawText(QPointF(x, ly + fm.capHeight() / 2), items[k].first);
        x -= 13; p.fillRect(QRectF(x, ly - 3, 6, 6), items[k].second); x -= 16;
      }
    }
    const QRectF pl = plot();
    for (int z = 0; z < 5; ++z) {
      const qreal y0 = pl.bottom() - (z + 1) * pl.height() / 5, y1 = pl.bottom() - z * pl.height() / 5;
      p.fillRect(QRectF(pl.left(), y0, pl.width(), y1 - y0), theme::map(kBand[z]));
    }
    p.setRenderHint(QPainter::Antialiasing, false);
    for (int k = 1; k < 5; ++k) {
      const qreal y = pl.bottom() - k * pl.height() / 5;
      p.setPen(QPen(theme::map(255, 255, 255, 8), 1)); p.drawLine(QPointF(pl.left(), y), QPointF(pl.right(), y));
    }
    { QPen mid(theme::map(0x39, 0x33, 0x25), 1, Qt::DotLine); p.setPen(mid); const qreal y = pl.center().y(); p.drawLine(QPointF(pl.left(), y), QPointF(pl.right(), y)); }
    p.setFont(theme::mono(11)); p.setPen(theme::map(0x7c, 0x79, 0x72));
    for (int k = 0; k <= 5; ++k) {
      const qreal y = pl.bottom() - k * pl.height() / 5;
      p.drawText(QRectF(pl.right() + 8, y - 8, 40, 16), Qt::AlignLeft | Qt::AlignVCenter, QString::number(k * 20));
    }
    const int n = m_d.composite.size();
    if (n < 2) {
      p.setPen(theme::map(0x7c, 0x79, 0x72)); p.drawText(pl, Qt::AlignCenter, m_d.error.isEmpty() ? QCoreApplication::translate("positioning", "Loading Yahoo Finance daily closes…") : QCoreApplication::translate("positioning", m_d.error.toUtf8().constData()));
      return;
    }
    p.setFont(theme::sans(11, QFont::Medium)); p.setPen(theme::map(0x86, 0x83, 0x7a));
    qreal lastRight = -1e9;
    for (const auto& [i, t] : monthTicks(m_d.dates)) {
      const qreal x = pl.left() + pl.width() * i / (n - 1);
      if (x < pl.left() + 14 || x > pl.right() - 14) continue;
      if (skipMonth(x, lastRight, t, QFontMetricsF(p.font()), width())) continue;
      p.drawText(QRectF(x - 30, pl.bottom() + 4, 60, 20), Qt::AlignCenter, t);
    }
    // zone-coloured line with glow, split where it crosses 40 / 60
    p.setRenderHint(QPainter::Antialiasing);
    auto P = [&](double i, double v) { return QPointF(pl.left() + pl.width() * i / (n - 1), pl.bottom() - v / 100.0 * pl.height()); };
    struct Seg { QPointF a, b; QColor c; };
    QVector<Seg> segs;
    for (int i = 1; i < n; ++i) {
      double v0 = m_d.composite[i - 1], v1 = m_d.composite[i];
      if (!std::isfinite(v0) || !std::isfinite(v1)) continue;
      QVector<double> cuts{0.0};
      for (double th : {40.0, 60.0}) if ((v0 - th) * (v1 - th) < 0) cuts << (th - v0) / (v1 - v0);
      std::sort(cuts.begin(), cuts.end()); cuts << 1.0;
      for (int k = 1; k < cuts.size(); ++k) {
        const double t0 = cuts[k - 1], t1 = cuts[k], tm = (t0 + t1) / 2;
        segs << Seg{P(i - 1 + t0, v0 + (v1 - v0) * t0), P(i - 1 + t1, v0 + (v1 - v0) * t1), zoneColor(v0 + (v1 - v0) * tm)};
      }
    }
    for (int pass = theme::isDark() ? 0 : 1; pass < 2; ++pass)
      for (const auto& s : segs) {
        p.setPen(pass == 0 ? QPen(theme::alpha(s.c, 45), 6, Qt::SolidLine, Qt::RoundCap) : QPen(s.c, 1.8, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(s.a, s.b);
      }
  }
  RiskData m_d; QPixmap m_cache; QPointF m_hover; bool m_has = false;
};

// ------------------------------------------------------------------------------------------------ components
class ComponentsPanel : public QWidget {
public:
  explicit ComponentsPanel(QWidget* p) : QWidget(p) { setMouseTracking(true); }
  void setData(const RiskData& d) { m_d = d; m_cache = {}; update(); }
  void setHoverRow(int r) { if (r != m_hoverRow) { m_hoverRow = r; m_cache = {}; update(); } }
  QRect dotRect() const { return QRect(10, 10, 16, 16); }
  static constexpr qreal kHead = 36, kRow = 74.3;
  // rows keep the reference pitch (74.3) and share the extra height of a taller panel (up to 104 px)
  qreal rowH() const { return phone() ? kPhoneRow : std::min(104.0, kRow + std::max(0, height() - 797) / 7.0); }
  // phone width (< 520 px): the description wraps on two lines, so rows are taller and the panel's height is fixed
  static constexpr qreal kPhoneRow = 86;
  bool phone() const { return width() < 520; }
  static int phoneHeight() { return int(kHead + 7 * kPhoneRow + 2); }
protected:
  void mouseMoveEvent(QMouseEvent* e) override {
    const qreal y = e->position().y() - kHead;
    setHoverRow(y >= 0 && y < 7 * rowH() ? int(y / rowH()) : -1);
  }
  void leaveEvent(QEvent*) override { setHoverRow(-1); }
  bool event(QEvent* e) override {
    if (e->type() == QEvent::ToolTip) {
      auto* he = static_cast<QHelpEvent*>(e);
      if (m_titleCut && he->pos().y() < kHead) QToolTip::showText(he->globalPos(), m_title, this);
      else QToolTip::hideText();
      return true;
    }
    return QWidget::event(e);
  }
  void resizeEvent(QResizeEvent*) override { m_cache = {}; }
  // static content cached; only the pulsing dot is painted live (the ticker repaints its rect at 15 fps)
  void paintEvent(QPaintEvent*) override {
    if (cacheStale(m_cache, this)) { m_cache = makeCache(this); QPainter cp(&m_cache); render(cp); }
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    p.drawPixmap(0, 0, m_cache);
    pk::drawPulseDot(p, QPointF(18, 18), 2.8, true);
  }
  void render(QPainter& p) {
    p.setRenderHint(QPainter::Antialiasing);
    paintPanel(p, rect(), false);
    // narrow panel (1024 px windows): the title is elided with "…", the full text in a tooltip over the header
    m_title = QCoreApplication::translate("positioning", "COMPONENTS — PERCENTILE VS OWN PAST YEAR");
    const QString shown = QFontMetricsF(theme::mono(10, QFont::Normal, 3.0)).elidedText(m_title, Qt::ElideRight, width() - 31 - 12);
    m_titleCut = shown != m_title;
    panelTitle(p, 31, 18, shown, theme::map(0x60, 0x5b, 0x51));
    p.fillRect(QRectF(1, kHead - 1, width() - 2, 1), theme::map(0x2a, 0x24, 0x14));
    const qreal L = 11, R = width() - 11;
    for (int i = 0; i < 7; ++i) {
      const qreal y = kHead + i * rowH(); const qreal kRow = rowH(); const qreal oy = phone() ? 2 : (kRow - 74.3) / 2;
      if (i == m_hoverRow) {
        QLinearGradient g(0, 0, width() * 0.6, 0);
        g.setColorAt(0, theme::map(0x2b, 0x21, 0x04)); g.setColorAt(1, theme::map(0x2b, 0x21, 0x04, 0));
        p.fillRect(QRectF(1, y, width() - 2, kRow - 1), g);
      }
      p.fillRect(QRectF(1, y + kRow - 1, width() - 2, 1), theme::map(0x1a, 0x15, 0x0a));
      if (i >= m_d.components.size()) continue;
      const auto& c = m_d.components[i];
      p.setFont(theme::mono(14, QFont::Bold)); p.setPen(theme::map(0xff, 0xfe, 0xfb));
      p.drawText(QPointF(L, y + oy + 25), QCoreApplication::translate("positioning", c.name.toUtf8().constData()));
      const QString pct = c.ok() ? fmt::whole(c.percentile) : fmt::dash();
      const QFont pf = theme::mono(18, QFont::Bold);
      p.setFont(pf); p.setPen(c.ok() ? zoneColor(c.percentile) : theme::map(0x5c, 0x59, 0x4b));
      const qreal pw = QFontMetricsF(pf).horizontalAdvance(pct);
      p.drawText(QPointF(R - pw, y + oy + 26), pct);
      p.setFont(theme::mono(12)); p.setPen(theme::map(0x88, 0x85, 0x7e));
      const qreal rw = QFontMetricsF(theme::mono(12)).horizontalAdvance(c.rawText);
      p.drawText(QPointF(R - 37 - rw, y + oy + 25), c.rawText);
      const QRectF track(L, y + oy + 39, R - L, 4);
      p.setPen(Qt::NoPen); p.setBrush(theme::map(0x28, 0x23, 0x1a)); p.drawRoundedRect(track, 2, 2);
      p.fillRect(QRectF(track.center().x(), track.top() - 1, 1, 6), theme::map(0x4f, 0x4b, 0x43));
      if (c.ok()) {
        const qreal w = std::max<qreal>(4, track.width() * c.percentile / 100.0);
        QLinearGradient g(track.left(), 0, track.left() + w, 0);
        g.setColorAt(0, theme::map(0xef, 0x7a, 0x75)); g.setColorAt(0.25, theme::map(0xf1, 0xa1, 0x44)); g.setColorAt(0.5, theme::map(0xe6, 0xc7, 0x18));
        g.setColorAt(0.75, theme::map(0x80, 0xc7, 0x97)); g.setColorAt(1, theme::map(0x4e, 0xc4, 0xc9));
        if (c.percentile < 3) { p.setBrush(theme::map(0xe6, 0xc7, 0x18)); p.drawEllipse(QPointF(track.left() + 3, track.center().y()), 2.6, 2.6); }
        else { p.setBrush(g); p.drawRoundedRect(QRectF(track.left(), track.top(), w, 4), 2, 2); }
      }
      p.setFont(theme::condensed(12, QFont::Medium)); p.setPen(theme::map(0x52, 0x4e, 0x46));
      if (phone()) p.drawText(QRectF(L, y + oy + 47, R - L, 34), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, QCoreApplication::translate("positioning", c.description.toUtf8().constData()));
      else p.drawText(QPointF(L, y + oy + 59), QCoreApplication::translate("positioning", c.description.toUtf8().constData()));
    }
    if (m_d.components.isEmpty()) {
      p.setFont(theme::mono(11)); p.setPen(theme::map(0x7c, 0x79, 0x72));
      p.drawText(rect().adjusted(0, int(kHead), 0, 0), Qt::AlignCenter, m_d.error.isEmpty() ? QCoreApplication::translate("positioning", "Loading…") : QCoreApplication::translate("positioning", m_d.error.toUtf8().constData()));
    }
  }
private:
  RiskData m_d; int m_hoverRow = -1; QPixmap m_cache; QString m_title; bool m_titleCut = false;
};

// ------------------------------------------------------------------------------------------------ S&P / VIX
class LinePanel : public QWidget {
public:
  LinePanel(QWidget* p, bool vix) : QWidget(p), m_vix(vix) {}
  void setData(const RiskData& d) { m_d = d; m_cache = {}; update(); }
protected:
  void resizeEvent(QResizeEvent*) override { m_cache = {}; }
  void paintEvent(QPaintEvent*) override {
    if (cacheStale(m_cache, this)) { m_cache = makeCache(this); QPainter cp(&m_cache); render(cp); }
    QPainter p(this); p.drawPixmap(0, 0, m_cache);
  }
private:
  void render(QPainter& p) {
    p.setRenderHint(QPainter::Antialiasing);
    paintPanel(p, rect());
    panelTitle(p, 11, 22, m_vix ? QCoreApplication::translate("positioning", "VIX · VOLATILITY INDEX · 1 YEAR") : QCoreApplication::translate("positioning", "S&P 500 · 1 YEAR"));
    const auto& v = m_vix ? m_d.vix : m_d.spx;
    const auto& dts = m_vix ? m_d.vixDates : m_d.spxDates;
    const QColor c = m_vix ? theme::map(0xf8, 0x71, 0x71) : theme::map(0x22, 0xd3, 0xee);
    if (v.size() < 2) {
      p.setFont(theme::mono(11)); p.setPen(theme::map(0x7c, 0x79, 0x72)); p.drawText(rect(), Qt::AlignCenter, m_d.error.isEmpty() ? QCoreApplication::translate("positioning", "Loading…") : QCoreApplication::translate("positioning", m_d.error.toUtf8().constData()));
      return;
    }
    {  // last value and 1Y change
      const double chg = (v.last() / v.first() - 1) * 100;
      const QFont sf = theme::mono(11); const QFontMetricsF sm(sf);
      const QString cs = QCoreApplication::translate("positioning", "%1 1Y").arg(fmt::pct(chg, 1));
      qreal x = width() - 12 - sm.horizontalAdvance(cs);
      const qreal dy = phone() ? 24 : 0;   // phone: the title takes the whole first line, the value goes under it
      p.setFont(sf); p.setPen(chg >= 0 ? theme::map(0x4f, 0xd0, 0xe0) : theme::map(0xe8, 0x6a, 0x66)); p.drawText(QPointF(x, 26 + dy), cs);
      const QFont bf = theme::mono(18, QFont::Bold); const QString vs = fmt::num(v.last(), 2);
      x -= QFontMetricsF(bf).horizontalAdvance(vs) + 10;
      p.setFont(bf); p.setPen(theme::map(0xfa, 0xf8, 0xf2)); p.drawText(QPointF(x, 27 + dy), vs);
    }
    const qreal top = phone() ? 66 : 47;
    const QRectF pl(10, top, width() - 10 - 64, height() - top - 30);
    auto rg = chart::range(v, 0.06);
    const auto ticks = niceTicks3(rg.first, rg.second, 7);   // steps 1/2/3/5 ×10ⁿ (VIX uses 3, as on t010.5)
    if (ticks.size() >= 2) { rg.first = std::min(rg.first, ticks.first()); rg.second = std::max(rg.second, ticks.last()); }
    auto Y = [&](double x) { return pl.bottom() - (x - rg.first) / (rg.second - rg.first) * pl.height(); };
    p.setFont(theme::mono(11));
    for (double t : ticks) {
      const qreal y = Y(t);
      QPen gp(theme::map(0x22, 0x1e, 0x10), 1, Qt::DotLine); p.setPen(gp); p.drawLine(QPointF(pl.left(), y), QPointF(pl.right(), y));
      p.setPen(theme::map(0x7f, 0x7c, 0x75));
      p.drawText(QRectF(pl.right() + 8, y - 8, 56, 16), Qt::AlignLeft | Qt::AlignVCenter, fmt::num(t, 0));
    }
    if (m_vix) {
      for (auto [lvl, lab, lc] : {std::tuple{30.0, QCoreApplication::translate("positioning", "30 · stress"), theme::map(0x90, 0x8d, 0x87)}, std::tuple{20.0, QCoreApplication::translate("positioning", "20 · elevated"), theme::map(0xcd, 0x97, 0x91)}}) {
        if (lvl < rg.first || lvl > rg.second) continue;
        const qreal y = Y(lvl);
        QPen dp(theme::alpha(lc, 150), 1, Qt::CustomDashLine); dp.setDashPattern({4, 3}); p.setPen(dp);
        p.drawLine(QPointF(pl.left(), y), QPointF(pl.right(), y));
        p.setFont(theme::condensed(13, QFont::Medium)); p.setPen(lc); p.drawText(QPointF(pl.left() + 6, y - 5), lab);
      }
    }
    const int n = v.size();
    p.setFont(theme::sans(11, QFont::Medium)); p.setPen(theme::map(0x86, 0x83, 0x7a));
    qreal lastRight = -1e9;
    for (const auto& [i, t] : monthTicks(dts)) {
      const qreal x = pl.left() + pl.width() * i / (n - 1);
      if (x < pl.left() + 14 || x > pl.right() - 10) continue;
      if (skipMonth(x, lastRight, t, QFontMetricsF(p.font()), width())) continue;
      p.drawText(QRectF(x - 30, pl.bottom() + 6, 60, 20), Qt::AlignCenter, t);
    }
    QPainterPath path;
    for (int i = 0; i < n; ++i) {
      const QPointF q(pl.left() + pl.width() * i / (n - 1), Y(v[i]));
      if (i == 0) path.moveTo(q); else path.lineTo(q);
    }
    QPainterPath area = path; area.lineTo(pl.right(), pl.bottom()); area.lineTo(pl.left(), pl.bottom()); area.closeSubpath();
    QLinearGradient g(0, pl.top(), 0, pl.bottom());
    g.setColorAt(0, theme::alpha(c, 50)); g.setColorAt(1, theme::alpha(c, 0));
    p.fillPath(area, g);
    p.setBrush(Qt::NoBrush);
    if (theme::isDark()) { p.setPen(QPen(theme::alpha(c, 45), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.drawPath(path); }
    p.setPen(QPen(theme::mix(c, theme::map(QColor(Qt::white)), 0.15), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.drawPath(path);
  }
  bool phone() const { return width() < 520; }
  bool m_vix; RiskData m_d; QPixmap m_cache;
};
}

// ------------------------------------------------------------------------------------------------ content
class RiskContent : public QWidget {
public:
  explicit RiskContent(QWidget* p) : QWidget(p) {
    gauge = new GaugePanel(this); history = new HistoryPanel(this); comps = new ComponentsPanel(this);
    spx = new LinePanel(this, false); vix = new LinePanel(this, true);
    setMinimumHeight(kHeight);
  }
  static constexpr int kHeight = 1142;   // max scroll 212 at 1600x1042, as measured on t010.5
  void setData(const RiskData& d) {
    m_d = d; gauge->setData(d); history->setData(d); comps->setData(d); spx->setData(d); vix->setData(d); update();
  }
  GaugePanel* gauge; HistoryPanel* history; ComponentsPanel* comps; LinePanel *spx, *vix;
protected:
  // Reference layout at 1600 wide: gauge 492, history 293, charts 280 (content 1142). Taller viewports share the extra
  // height (gauge 45 %, history 25 %, charts 30 %) so big screens have no empty band; smaller ones scroll.
  void resizeEvent(QResizeEvent*) override {
    const int W = width(), m = 14, gap = 11;
    if (ui::compact(W)) { layoutPhone(); return; }
    m_phone = false;
    setMinimumHeight(kHeight);
    const int extra = std::max(0, height() - kHeight);
    const int gh = 492 + extra * 45 / 100, hh = 293 + extra * 25 / 100, ch = 280 + extra * 30 / 100;
    const int lw = int(std::round((W - 2 * m - gap) * 0.5554));
    gauge->setGeometry(m, 15, lw, gh);
    history->setGeometry(m, 15 + gh + 12, lw, hh);
    comps->setGeometry(m + lw + gap, 15, W - 2 * m - gap - lw, gh + 12 + hh);
    const int cy = 15 + gh + 12 + hh + 12;
    const int bw = (W - 2 * m - gap) / 2;
    spx->setGeometry(m, cy, bw, ch);
    vix->setGeometry(m + bw + gap, cy, W - 2 * m - gap - bw, ch);
    m_footY = cy + ch + 6;
  }
  // Phone held upright: one column, gauge first, then the components, the history and the two 1-year charts; the
  // content is as tall as they need (the page scrolls).
  void layoutPhone() {
    m_phone = true;
    const int W = width(), m = 10, gap = 10, w = W - 2 * m;
    // the gauge's scale follows the width (s = (w - 40) / 640): the panel is just tall enough for that scale
    const int gh = std::max(260, int(std::round(472 * (w - 40) / 640.0)) + 76);   // + room for the pills under "0"/"100"
    int y = 12;
    gauge->setGeometry(m, y, w, gh); y += gh + gap;
    comps->setGeometry(m, y, w, ComponentsPanel::phoneHeight()); y += ComponentsPanel::phoneHeight() + gap;
    history->setGeometry(m, y, w, 270); y += 270 + gap;
    spx->setGeometry(m, y, w, 240); y += 240 + gap;
    vix->setGeometry(m, y, w, 240); y += 240 + 6;
    m_footY = y;
    const QRect fr = QFontMetrics(theme::mono(9, QFont::Normal, 1.0)).boundingRect(QRect(0, 0, W - 28, 10000), Qt::TextWordWrap, footText());
    setMinimumHeight(y + fr.height() + 24);
  }
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.fillRect(rect(), pk::col::kPage());
    const QString t = footText();
    p.setFont(theme::mono(9, QFont::Normal, 1.0)); p.setPen(theme::map(0x51, 0x4f, 0x49));
    // narrow windows: wrap rather than cut the UPDATED time (phone: as many lines as it needs)
    p.drawText(QRectF(14, m_footY, width() - 28, m_phone ? height() - m_footY : 30), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, t);
  }
private:
  QString footText() const {
    const QString upd = m_d.updated.isValid() ? m_d.updated.toString("HH:mm") : fmt::dash();
    return QCoreApplication::translate("positioning",
        "EACH COMPONENT: TODAY'S READ AGAINST ITS OWN PREVIOUS 252 TRADING DAYS (PERCENTILE), ORIENTED SO "
        "HIGHER = MORE RISK APPETITE · COMPOSITE = AVERAGE OF AVAILABLE COMPONENTS, COMPUTED BY DEEPWATER · "
        "YAHOO FINANCE DAILY CLOSES · UPDATED %1").arg(upd);
  }
  RiskData m_d; int m_footY = 1110; bool m_phone = false;
};

RiskSentimentPage::RiskSentimentPage(QWidget* parent) : QWidget(parent) {
  setAutoFillBackground(true);
  QPalette pl = palette(); pl.setColor(QPalette::Window, pk::col::kPage()); setPalette(pl);
  auto* v = new QVBoxLayout(this); v->setContentsMargins(0, 0, 0, 0); v->setSpacing(0);
  m_title = new pk::TitleBar(pk::TitleBar::Gauge, QCoreApplication::translate("positioning", "RISK SENTIMENT — CROSS-ASSET RISK APPETITE"), this, 36);
  m_title->setPulse(true);
  applyBadge();
  m_scroll = new pk::Scroll(this);
  m_content = new RiskContent(m_scroll);
  m_scroll->setWidget(m_content);
  v->addWidget(m_title); v->addWidget(m_scroll, 1);
  m_ticker = new pk::Ticker(this, 66);
  m_title->registerAnimation(m_ticker);
  m_ticker->addCursor(m_content->gauge, QRect(200, 4, 160, 26));
  m_ticker->add(m_content->comps, m_content->comps->dotRect());
  connect(&RiskIndex::instance(), &RiskIndex::updated, this, [this] { refresh(); });
  if (RiskIndex::instance().data().ok()) refresh();
  // a page left open would otherwise show the closes of the moment it was opened
  m_reload.setInterval(5 * 60 * 1000);
  connect(&m_reload, &QTimer::timeout, this, [] { RiskIndex::instance().load(240); });   // shorter than the timer (see OverviewData)
  // test hooks
  const QByteArray hv = qgetenv("DW_RISK_HOVER"), sc = qgetenv("DW_RISK_SCROLL");
  if (!hv.isEmpty() || !sc.isEmpty()) {
    QTimer::singleShot(1500, this, [this, hv, sc] {
      if (!sc.isEmpty()) m_scroll->verticalScrollBar()->setValue(sc.toInt());
      const auto xy = QString::fromUtf8(hv).split(',');
      if (xy.size() != 2) return;
      const QPoint pg(xy[0].toInt(), xy[1].toInt());
      for (QWidget* w : {static_cast<QWidget*>(m_content->comps), static_cast<QWidget*>(m_content->history)}) {
        const QPoint lp = w->mapFrom(this, pg);
        if (!w->rect().contains(lp)) continue;
        if (w == m_content->comps) {
          const qreal y = lp.y() - ComponentsPanel::kHead;
          m_content->comps->setHoverRow(y >= 0 ? int(y / m_content->comps->rowH()) : -1);
        } else m_content->history->setHover(lp);
      }
    });
  }
}

void RiskSentimentPage::refresh() {
  const auto& d = RiskIndex::instance().data();
  m_content->setData(d);
  applyBadge();
}
// Phone upright: the bar has room for the title only (the badge would cover it); the footer names the sources.
void RiskSentimentPage::applyBadge() {
  const auto& d = RiskIndex::instance().data();
  int n = 0; for (const auto& c : d.components) if (c.ok()) ++n;
  const QString b = ui::compact(width()) ? QString()
      : QCoreApplication::translate("positioning", "%1 MARKETS · DAILY CLOSES · PERCENTILE VS 1 YEAR").arg(d.ok() ? n : 7);
  if (b != m_badge) { m_badge = b; m_title->setBadge(b, true); }
}
void RiskSentimentPage::resizeEvent(QResizeEvent* e) { QWidget::resizeEvent(e); applyBadge(); }
void RiskSentimentPage::showEvent(QShowEvent*) { m_ticker->setRunning(true); RiskIndex::instance().load(240); m_reload.start(); }
void RiskSentimentPage::hideEvent(QHideEvent*) { m_ticker->setRunning(false); m_reload.stop(); }
}  // namespace positioning

DW_PAGE("positioning/risk-sentiment", positioning::RiskSentimentPage)
