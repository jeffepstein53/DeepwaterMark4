// Data › Macro data: the economic facts Deepwater has recorded (macrodata::Store), drawn as chains of blocks.
//
// Left: every chain, by family (rates, macro, calendar, prices, forecasts, positioning), with a search and a tick box to
// show it. Right: the shown chains stacked on one time axis (wheel = zoom at the cursor, drag = pan, range buttons).
// A block is one fact: it sits on its period, joined to the previous and next fact of its chain. A revised value is a
// thin layer on top of the block it corrects (with the number of revisions); forecasts (dashed) sit on the period they
// predict, with a dotted line down to the actual value when its chain is shown. Click a block for its details.
// Import progress (macrodata::Importer) in the header; the first import starts when this page is first shown.
// Test hooks: DW_MACRODATA_SHOW="key;key" (chains shown), DW_MACRODATA_RANGE=<years>, DW_MACRODATA_SELECT=<chain key>|<index>,
// DW_MACRODATA_TICKALL=<family|all> (TICK ALL pressed on that family).
#include "app/Pages.h"
#include "macrodata/MacroDataStore.h"
#include "macro/CentralBanks.h"
#include "macrodata/Importer.h"
#include "pages/data/Landscape.h"
#include "pages/data/MacroCube.h"
#include "ui/Fit.h"
#include "global/PageScroll.h"
#include "ui/Touch.h"
#include <QSlider>
#include <QStackedWidget>
#include <QSignalBlocker>
#include <climits>
#include "core/Settings.h"
#include "ui/Kit.h"
#include "ui/Theme.h"
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimeZone>
#include <QTimer>
#include <QRegularExpression>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <functional>

namespace macrodatapage {
namespace {
QColor familyColour(const QString& f) {
  static const QHash<QString, QPair<const char*, const char*>> h{
      {"rates", {"#e8b923", "#a16207"}}, {"macro", {"#fb923c", "#c2410c"}}, {"calendar", {"#22d3ee", "#0e7490"}},
      {"prices", {"#4ade80", "#15803d"}}, {"forecasts", {"#a78bfa", "#6d28d9"}}, {"positioning", {"#f87171", "#be123c"}}};
  const auto p = h.value(f, {"#94a3b8", "#475569"});
  return QColor(theme::isDark() ? p.first : p.second);
}
QString kindLabel(const QString& k) {
  if (k == "actual") return QCoreApplication::translate("data", "ACTUAL");
  if (k == "forecast") return QCoreApplication::translate("data", "FORECAST");
  if (k == "consensus") return QCoreApplication::translate("data", "CONSENSUS");
  if (k == "implied") return QCoreApplication::translate("data", "MARKET-IMPLIED");
  if (k == "price") return QCoreApplication::translate("data", "PRICE");
  return k.toUpper();
}
bool isForecast(const QString& k) { return k == "forecast" || k == "consensus" || k == "implied"; }
// "Inflation (CPI y/y) · OECD": a chain holds one source, so two chains of one indicator differ by it
QString withSource(const macrodata::ChainDef& d) {
  QString src = d.source;
  static const QRegularExpression note(R"(\s*\(.*\)$)");
  src.remove(note);   // "ForexFactory (\"previous\" of …)" → "ForexFactory"
  return src.isEmpty() ? d.indicator : d.indicator + "  ·  " + src;
}
// a fact's text as shown: decisions are stored in English (stable whatever the language), shown in the interface's
QString shownText(const QString& text, const QString& chainKey) {
  return chainKey.startsWith("decision:") ? macro::decisionTextShown(text) : text;
}
QString fmtValue(double v, const QString& unit) {
  if (!std::isfinite(v)) return QStringLiteral("—");
  if (unit == "%") return QString::number(v, 'f', 2) + "%";   // rates and growth: always 2 decimals
  const double a = std::abs(v);
  QString s = a >= 1e9 ? QString::number(v / 1e9, 'f', 2) + "B" : a >= 1e6 ? QString::number(v / 1e6, 'f', 2) + "M"
            : a >= 10000 ? QLocale(QLocale::English).toString(v, 'f', 0) : a >= 100 ? QString::number(v, 'f', 1)
            : a >= 1 ? QString::number(v, 'f', 2) : QString::number(v, 'f', 4);
  return s;
}
qint64 periodLength(const QString& freq) {
  const qint64 d = 86400000LL;
  if (freq == "D") return d;
  if (freq == "W") return 7 * d;
  if (freq == "M") return 30 * d;
  if (freq == "Q") return 91 * d;
  if (freq == "A") return 365 * d;
  return 0;   // events: up to the next block
}
QString dateText(qint64 ms, const QString& freq) {
  const QDateTime t = macrodata::fromMs(ms);
  if (!t.isValid()) return QStringLiteral("—");
  if (freq == "A") return t.toString("yyyy");
  if (freq == "Q") return QString("Q%1 %2").arg((t.date().month() - 1) / 3 + 1).arg(t.date().year());
  if (freq == "M") return t.toString("MMM yyyy");
  if (t.time() == QTime(0, 0)) return t.toString("yyyy-MM-dd");
  return t.toString("yyyy-MM-dd HH:mm") + " UTC";
}
// The bar row above the views: as wide as the bar on show, not as the widest of the three (the cube's slice bar
// would otherwise hold the 2D view wider than a 1024 px window).
class CurrentStack : public QStackedWidget {
public:
  explicit CurrentStack(QWidget* p) : QStackedWidget(p) { connect(this, &QStackedWidget::currentChanged, this, [this] { updateGeometry(); }); }
  // the height stays the tallest bar's, so the row does not move when the view changes
  QSize sizeHint() const override { return pick(QStackedWidget::sizeHint(), currentWidget() ? currentWidget()->sizeHint() : QSize()); }
  QSize minimumSizeHint() const override { return pick(QStackedWidget::minimumSizeHint(), currentWidget() ? currentWidget()->minimumSizeHint() : QSize()); }
private:
  static QSize pick(QSize all, QSize cur) { return cur.isValid() ? QSize(cur.width(), all.height()) : all; }
};
}  // namespace

// ------------------------------------------------------------------------------------------------ chain list
class ChainList : public QWidget {
public:
  std::function<void(const QString& key, bool on)> onToggle;
  explicit ChainList(QWidget* p) : QWidget(p) { setMouseTracking(true); }
  void setChains(const QList<macrodata::Chain>& all) { m_all = all; refilter(); }
  void setFilter(const QString& text, const QString& family) { m_text = text.trimmed().toLower(); m_family = family; refilter(); }
  void setShown(const QStringList& keys) { m_shown = QSet<QString>(keys.cbegin(), keys.cend()); update(); }
  int visibleCount() const { return int(m_rows.size()); }
  QStringList visibleKeys() const { QStringList k; for (const auto& c : m_rows) k << c.def.key; return k; }
  QSize sizeHint() const override { return QSize(300, int(m_rows.size()) * kRow + 8); }
protected:
  static constexpr int kRow = 40;
  void refilter() {
    m_rows.clear();
    for (const auto& c : m_all) {
      if (!m_family.isEmpty() && c.def.family != m_family) continue;
      if (!m_text.isEmpty()) {
        const QString hay = (c.def.subject + " " + c.def.indicator + " " + c.def.kind + " " + c.def.key + " " + c.def.source).toLower();
        bool all = true;
        for (const auto& w : m_text.split(' ', Qt::SkipEmptyParts)) if (!hay.contains(w)) { all = false; break; }
        if (!all) continue;
      }
      m_rows << c;
    }
    setMinimumHeight(int(m_rows.size()) * kRow + 8);
    updateGeometry();
    update();
  }
  void paintEvent(QPaintEvent* e) override {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    const auto& c = theme::pal();
    const int first = qMax(0, e->rect().top() / kRow), last = qMin(int(m_rows.size()) - 1, e->rect().bottom() / kRow);
    for (int i = first; i <= last; ++i) {
      const auto& ch = m_rows[i];
      const QRectF r(2, i * kRow + 2, width() - 4, kRow - 4);
      const bool on = m_shown.contains(ch.def.key);
      p.setPen(QPen(i == m_hover ? c.borderHi : c.border, 1)); p.setBrush(i == m_hover ? c.panelAlt : c.panel);
      p.drawRoundedRect(r.adjusted(.5, .5, -.5, -.5), 3, 3);
      // tick box
      const QRectF box(r.left() + 8, r.center().y() - 6.5, 13, 13);
      const QColor fc = familyColour(ch.def.family);
      p.setPen(QPen(on ? fc : c.textMute, 1)); p.setBrush(on ? theme::alpha(fc, 60) : Qt::transparent); p.drawRoundedRect(box, 2, 2);
      if (on) { p.setPen(QPen(fc, 1.8, Qt::SolidLine, Qt::RoundCap)); p.drawLine(box.left() + 3, box.center().y(), box.left() + 5.6, box.bottom() - 3.2);
                p.drawLine(box.left() + 5.6, box.bottom() - 3.2, box.right() - 2.8, box.top() + 3.2); }
      p.setFont(theme::sans(11)); p.setPen(c.text);
      const QFontMetrics fm(p.font());
      p.drawText(QRectF(r.left() + 28, r.top() + 3, r.width() - 140, 17), Qt::AlignLeft | Qt::AlignVCenter,
                 fm.elidedText(ch.def.subject, Qt::ElideRight, int(r.width() - 142)));
      p.setFont(theme::sans(10)); p.setPen(c.textDim);
      p.drawText(QRectF(r.left() + 28, r.top() + 19, r.width() - 90, 15), Qt::AlignLeft | Qt::AlignVCenter,
                 QFontMetrics(p.font()).elidedText(withSource(ch.def), Qt::ElideRight, int(r.width() - 92)));
      p.setFont(theme::mono(8, QFont::Bold, 1.0)); p.setPen(isForecast(ch.def.kind) ? familyColour("forecasts") : fc);
      p.drawText(QRectF(r.right() - 110, r.top() + 3, 104, 15), Qt::AlignRight | Qt::AlignVCenter, kindLabel(ch.def.kind));
      p.setFont(theme::mono(9)); p.setPen(c.textMute);
      p.drawText(QRectF(r.right() - 82, r.top() + 19, 76, 15), Qt::AlignRight | Qt::AlignVCenter,
                 QLocale(QLocale::English).toString(ch.count) + (ch.revisions ? QString(" · %1R").arg(ch.revisions) : QString()));
    }
    if (m_rows.isEmpty()) {
      p.setFont(theme::sans(11)); p.setPen(c.textMute);
      p.drawText(rect().adjusted(8, 8, -8, -8), Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, QCoreApplication::translate("data", "No chain matches."));
    }
  }
  void mouseMoveEvent(QMouseEvent* e) override {
    const int i = int(e->position().y()) / kRow;
    const int h = i >= 0 && i < m_rows.size() ? i : -1;
    if (h != m_hover) { m_hover = h; setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor); update(); }
  }
  void leaveEvent(QEvent*) override { m_hover = -1; update(); }
  void mousePressEvent(QMouseEvent* e) override {
    const int i = int(e->position().y()) / kRow;
    if (i < 0 || i >= m_rows.size() || !onToggle) return;
    const QString key = m_rows[i].def.key;
    onToggle(key, !m_shown.contains(key));
  }
private:
  QList<macrodata::Chain> m_all, m_rows;
  QSet<QString> m_shown;   // per row per paint: a hash, not a list scan
  QString m_text, m_family;
  int m_hover = -1;
};

// ------------------------------------------------------------------------------------------------ canvas
class ChainCanvas : public QWidget {
public:
  struct Block { int fact; qint64 x0, x1; int revisions; };   // x in ms (period or target), fact = index of the latest value
  struct Lane { macrodata::Chain chain; QVector<macrodata::Fact> facts; QVector<Block> blocks; double vmin = 0, vmax = 0; bool loaded = false; };
  std::function<void(int lane, int block)> onSelect;
  std::function<QVector<macrodata::Fact>(qint64 chainId)> loader;   // facts of a chain, read when its lane comes into view

  explicit ChainCanvas(QWidget* p) : QWidget(p) { setMouseTracking(true); setMinimumHeight(200); }
  // Lanes are loaded when they come into view and dropped when far from it: thousands of chains (millions of facts)
  // can be ticked without reading them all.
  void setLanes(QVector<Lane> lanes) {
    // the selection is kept by chain key and slot, not by index: the new lanes are not loaded yet (the inspector
    // emptied after every reload) and unticking a chain above the selected one shifts every index
    QString selKey; QPair<qint64, qint64> selSlot; bool had = false;
    if (m_sel.first >= 0 && m_sel.first < m_lanes.size()) {
      const Lane& o = m_lanes[m_sel.first];
      if (m_sel.second >= 0 && m_sel.second < o.blocks.size()) { selKey = o.chain.def.key; selSlot = slotOf(o, o.blocks[m_sel.second]); had = true; }
    }
    m_lanes = std::move(lanes);
    m_loadedOrder.clear();
    m_sel = {-1, -1};
    setMinimumHeight(kAxis + int(m_lanes.size()) * kLane + 10);
    for (int i = 0; had && i < m_lanes.size(); ++i) {
      if (m_lanes[i].chain.def.key != selKey) continue;
      ensureLoaded(i);
      const Lane& l = m_lanes[i];
      for (int b = 0; b < l.blocks.size(); ++b) if (slotOf(l, l.blocks[b]) == selSlot) { m_sel = {i, b}; break; }
      break;
    }
    update();
  }
  const QVector<Lane>& lanes() const { return m_lanes; }
  void ensureLoaded(int i) {
    if (i < 0 || i >= m_lanes.size() || m_lanes[i].loaded) return;
    Lane& l = m_lanes[i];
    if (loader) l.facts = loader(l.chain.id);
    build(l);
    l.loaded = true;
    m_loadedOrder << i;
    // keep at most 150 lanes and kMaxFacts facts in memory; drop the oldest that are off screen. The count alone was
    // not a bound: a daily price chain holds ~25,000 facts, so 150 of them (TICK ALL on prices) held hundreds of MB.
    qint64 total = 0;
    for (int j : m_loadedOrder) total += m_lanes[j].facts.size();
    const QRect vis = visibleRegion().boundingRect();
    for (int k = 0; (m_loadedOrder.size() > 150 || total > kMaxFacts) && k < m_loadedOrder.size();) {
      const int j = m_loadedOrder[k];
      if (laneRect(j).intersects(vis) || j == m_sel.first) { ++k; continue; }
      total -= m_lanes[j].facts.size();
      m_lanes[j].facts.clear(); m_lanes[j].facts.squeeze(); m_lanes[j].blocks.clear(); m_lanes[j].blocks.squeeze(); m_lanes[j].loaded = false;
      m_loadedOrder.removeAt(k);
    }
  }
  void setRange(qint64 t0, qint64 t1) { m_t0 = t0; m_t1 = qMax(t0 + 3600000, t1); update(); }
  qint64 t0() const { return m_t0; }
  qint64 t1() const { return m_t1; }
  void select(int lane, int block) { ensureLoaded(lane); m_sel = {lane, block}; update(); if (onSelect) onSelect(lane, block); }
  QPair<int, int> selection() const { return m_sel; }
  // brings a block into view (keeps the zoom)
  void reveal(int lane, int block) {
    if (lane < 0 || lane >= m_lanes.size() || block < 0 || block >= m_lanes[lane].blocks.size()) return;
    const auto& b = m_lanes[lane].blocks[block];
    const qint64 span = m_t1 - m_t0;
    if (b.x0 < m_t0 || b.x0 > m_t1) { m_t0 = b.x0 - span / 2; m_t1 = m_t0 + span; }
    update();
  }

protected:
  static constexpr int kAxis = 30, kLane = 58, kLabel = 190;
  // the label strip: narrower on a phone (the timeline keeps two thirds of a 400 px canvas)
  int lw() const { return width() < 520 ? 128 : kLabel; }
  static constexpr qint64 kMaxFacts = 1000000;   // ~150 MB of facts (three strings each) across the loaded lanes

  // the (period, target) slot a block stands for, as build() groups the facts
  static QPair<qint64, qint64> slotOf(const Lane& l, const Block& b) {
    const auto& f = l.facts[b.fact];
    return isForecast(l.chain.def.kind) ? qMakePair(0LL, f.target ? f.target : f.period) : qMakePair(f.period, f.target);
  }

  static void build(Lane& l) {
    l.blocks.clear();
    const bool fc = isForecast(l.chain.def.kind);
    // one block per (period, target) slot: the latest value; earlier values of the same slot are its revisions
    QHash<QPair<qint64, qint64>, int> slot;
    for (int i = 0; i < l.facts.size(); ++i) {
      const auto& f = l.facts[i];
      const auto key = qMakePair(fc ? 0LL : f.period, fc ? (f.target ? f.target : f.period) : f.target);
      auto it = slot.find(key);
      const qint64 x = fc ? (f.target ? f.target : f.period) : f.period;
      if (it == slot.end()) { slot.insert(key, int(l.blocks.size())); l.blocks << Block{i, x, x, 0}; }
      else { auto& b = l.blocks[*it]; if (!fc || f.period >= l.facts[b.fact].period) { b.fact = i; } ++b.revisions; }
    }
    std::sort(l.blocks.begin(), l.blocks.end(), [](const Block& a, const Block& b) { return a.x0 < b.x0; });
    const qint64 len = periodLength(l.chain.def.freq);
    for (int i = 0; i < l.blocks.size(); ++i) {
      auto& b = l.blocks[i];
      const qint64 next = i + 1 < l.blocks.size() ? l.blocks[i + 1].x0 : b.x0 + (len ? len : 30 * 86400000LL);
      b.x1 = len ? qMin(b.x0 + len, next) : next;
      if (fc) b.x1 = b.x0 + (len ? len : 7 * 86400000LL);
    }
    l.vmin = INFINITY; l.vmax = -INFINITY;
    for (const auto& b : l.blocks) { const double v = l.facts[b.fact].value; if (std::isfinite(v)) { l.vmin = qMin(l.vmin, v); l.vmax = qMax(l.vmax, v); } }
  }

  double xOf(qint64 t) const { return lw() + double(t - m_t0) / double(m_t1 - m_t0) * (width() - lw() - 12); }
  qint64 tOf(double x) const { return m_t0 + qint64((x - lw()) / (width() - lw() - 12) * double(m_t1 - m_t0)); }
  QRectF laneRect(int i) const { return QRectF(0, kAxis + i * kLane, width(), kLane); }

  void paintEvent(QPaintEvent*) override {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    const auto& c = theme::pal();
    const QRect vis = visibleRegion().boundingRect();
    p.fillRect(vis, c.bg);
    // only the lanes in view (loaded on demand)
    const int i0 = qMax(0, (vis.top() - kAxis) / kLane), i1 = qMin(int(m_lanes.size()) - 1, (vis.bottom() - kAxis) / kLane);
    for (int i = i0; i <= i1; ++i) ensureLoaded(i);
    drawAxis(p, vis, false);
    for (int i = i0; i <= i1; ++i) drawLane(p, i);
    drawForecastLinks(p, i0, i1);
    drawAxis(p, vis, true);   // the date row stays at the top of the view while scrolling
    if (m_lanes.isEmpty()) {
      p.setFont(theme::sans(12)); p.setPen(c.textMute);
      p.drawText(rect().adjusted(lw(), kAxis + 20, -20, 0), Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap,
                 QCoreApplication::translate("data", "Tick chains on the left to show them here."));
    }
  }

  void drawAxis(QPainter& p, const QRect& vis, bool band) {
    const auto& c = theme::pal();
    const double y0 = vis.top();
    if (band) {
      p.fillRect(QRectF(0, y0, width(), kAxis), c.bg);
      p.setPen(QPen(c.border, 1)); p.drawLine(QPointF(lw(), y0 + kAxis - 0.5), QPointF(width(), y0 + kAxis - 0.5));
    }
    // tick step from the room available: at least ~56 px between two labels
    const double pxPerDay = (width() - lw() - 12) / (double(m_t1 - m_t0) / 86400000.0);
    // the tick loops below never end on invalid dates (an invalid QDate compares below every valid one and
    // addMonths/addDays leave it invalid): both ends are checked
    const QDateTime a = QDateTime::fromMSecsSinceEpoch(m_t0, QTimeZone::UTC), b = QDateTime::fromMSecsSinceEpoch(m_t1, QTimeZone::UTC);
    if (!a.isValid() || !b.isValid() || !(m_t1 > m_t0) || !(pxPerDay > 0)) return;
    struct Tick { QDateTime t; QString label; };
    QList<Tick> ticks;
    const double need = 56;
    if (pxPerDay * 365 * 0.92 < need * 1.0 || pxPerDay * 30 * 3 < need) {   // years
      int step = 1;
      for (int s : {1, 2, 5, 10, 20, 25, 50}) { step = s; if (pxPerDay * 365.25 * s >= need) break; }
      for (int y = (a.date().year() / step) * step; y <= b.date().year() + 1; y += step)
        ticks << Tick{QDateTime(QDate(y, 1, 1), QTime(0, 0), QTimeZone::UTC), QString::number(y)};
    } else if (pxPerDay * 7 < need) {                                          // months
      int step = 1;
      for (int s : {1, 2, 3, 6}) { step = s; if (pxPerDay * 30.4 * s >= need) break; }
      for (QDate d(a.date().year(), 1, 1); d <= b.date().addMonths(1); d = d.addMonths(step))
        if (d >= a.date().addMonths(-step))
          ticks << Tick{QDateTime(d, QTime(0, 0), QTimeZone::UTC), d.month() == 1 ? QString::number(d.year()) : QLocale().monthName(d.month(), QLocale::ShortFormat)};
    } else {                                                                   // days
      int step = 1;
      for (int s : {1, 2, 7, 14}) { step = s; if (pxPerDay * s >= need) break; }
      for (QDate d = a.date(); d <= b.date().addDays(1); d = d.addDays(step))
        ticks << Tick{QDateTime(d, QTime(0, 0), QTimeZone::UTC), d.toString("dd MMM")};
    }
    p.setFont(theme::mono(9));
    for (const auto& t : ticks) {
      const double x = xOf(t.t.toMSecsSinceEpoch());
      if (x < lw() || x > width()) continue;
      if (!band) { p.setPen(QPen(theme::alpha(c.grid, 120), 1)); p.drawLine(QPointF(x, y0 + kAxis), QPointF(x, vis.bottom())); }
      else if (x + 3 + QFontMetricsF(p.font()).horizontalAdvance(t.label) <= width()) {   // no label cut at the right edge
        p.setPen(c.textMute); p.drawText(QRectF(x + 3, y0 + 6, 80, kAxis - 10), Qt::AlignLeft | Qt::AlignVCenter, t.label);
      }
    }
    // today
    const double xn = xOf(QDateTime::currentMSecsSinceEpoch());
    if (!band && xn > lw() && xn < width()) { p.setPen(QPen(theme::alpha(c.accent, 160), 1, Qt::DashLine)); p.drawLine(QPointF(xn, y0 + kAxis), QPointF(xn, vis.bottom())); }
  }

  void drawLane(QPainter& p, int li) {
    const auto& c = theme::pal();
    const Lane& l = m_lanes[li];
    const QRectF lr = laneRect(li);
    const QColor fc = familyColour(isForecast(l.chain.def.kind) ? "forecasts" : l.chain.def.family);
    // label strip
    p.fillRect(QRectF(0, lr.top(), lw() - 6, lr.height() - 6), c.panel);
    p.setPen(Qt::NoPen); p.setBrush(fc); p.drawRect(QRectF(0, lr.top(), 3, lr.height() - 6));
    p.setFont(theme::sans(11)); p.setPen(c.text);
    p.drawText(QRectF(10, lr.top() + 3, lw() - 20, 16), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(p.font()).elidedText(l.chain.def.subject, Qt::ElideRight, lw() - 22));
    p.setFont(theme::sans(10)); p.setPen(c.textDim);
    p.drawText(QRectF(10, lr.top() + 19, lw() - 20, 14), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(p.font()).elidedText(withSource(l.chain.def), Qt::ElideRight, lw() - 22));
    p.setFont(theme::mono(8, QFont::Bold, 1.0)); p.setPen(fc);
    p.drawText(QRectF(10, lr.top() + 34, lw() - 20, 13), Qt::AlignLeft | Qt::AlignVCenter,
               kindLabel(l.chain.def.kind) + "  ·  " + QLocale(QLocale::English).toString(l.chain.count));
    p.setPen(QPen(c.divider, 1)); p.drawLine(QPointF(lw(), lr.bottom() - 3), QPointF(width(), lr.bottom() - 3));

    if (l.blocks.isEmpty()) return;
    p.save();
    p.setClipRect(QRectF(lw(), lr.top(), width() - lw(), lr.height()));
    const double top = lr.top() + 12, h = lr.height() - 22;
    const bool fcLane = isForecast(l.chain.def.kind);
    // visible blocks (binary search on x0)
    auto firstIt = std::lower_bound(l.blocks.begin(), l.blocks.end(), m_t0, [](const Block& b, qint64 t) { return b.x1 < t; });
    int i0 = int(firstIt - l.blocks.begin());
    QPointF prevRight; bool hasPrev = false;
    if (i0 > 0) { --i0; }
    const double span = l.vmax - l.vmin;
    for (int i = i0; i < l.blocks.size(); ++i) {
      const Block& b = l.blocks[i];
      if (b.x0 > m_t1) break;
      const macrodata::Fact& f = l.facts[b.fact];
      double x0 = xOf(b.x0), x1 = xOf(b.x1);
      if (fcLane && x1 - x0 < 8) x1 = x0 + 8;   // forecasts stay visible as blocks
      const double w = x1 - x0;
      const bool tiny = w < 4;
      const double gap = tiny ? 0 : qMin(3.0, w * 0.15);
      x1 = qMax(x0 + 1.2, x1 - gap);
      const QRectF r(x0, top, x1 - x0, h);
      // fill: strength from the value's place in the chain's range
      const double t = std::isfinite(f.value) && span > 0 ? (f.value - l.vmin) / span : 0.5;
      QColor fill = fc; fill.setAlphaF(0.20 + 0.55 * t);
      const bool sel = m_sel.first == li && m_sel.second == i;
      const bool hov = m_hover.first == li && m_hover.second == i;
      // chain link to the previous block
      if (hasPrev && !tiny && !fcLane) {
        p.setPen(QPen(theme::alpha(fc, 170), 1.4));
        p.drawLine(prevRight, QPointF(x0, top + h / 2));
      }
      if (tiny) { p.fillRect(r, fill); }
      else {
        p.setPen(QPen(sel ? c.accent : hov ? c.text : theme::alpha(fc, 220), sel ? 2 : 1, fcLane ? Qt::DashLine : Qt::SolidLine));
        p.setBrush(fill);
        p.drawRoundedRect(r.adjusted(.5, .5, -.5, -.5), 2.5, 2.5);
        if (b.revisions > 0) {   // revisions: thin layers stacked on the block
          for (int k = 0; k < qMin(3, b.revisions); ++k) {
            p.setPen(QPen(c.warn, 1)); p.setBrush(theme::alpha(c.warn, 60));
            p.drawRoundedRect(QRectF(r.left() + 2 + k * 2, r.top() - 4 - k * 3, r.width() - 4 - k * 4, 3), 1, 1);
          }
        }
        if (w > 40) {
          p.setFont(theme::mono(9, QFont::Medium)); p.setPen(c.text);
          const QString txt = f.text.isEmpty() || std::isfinite(f.value) ? fmtValue(f.value, l.chain.def.unit) : shownText(f.text, l.chain.def.key);
          p.drawText(r.adjusted(3, 0, -3, 0), Qt::AlignCenter, QFontMetrics(p.font()).elidedText(txt, Qt::ElideRight, int(r.width() - 6)));
        }
        if (f.origin == "live") { p.setPen(Qt::NoPen); p.setBrush(c.up); p.drawEllipse(QPointF(r.right() - 4, r.bottom() - 4), 2, 2); }
      }
      prevRight = QPointF(x1, top + h / 2); hasPrev = true;
    }
    p.restore();
  }

  // dotted line from a forecast block to the actual block of the period it predicts, when that chain is shown
  void drawForecastLinks(QPainter& p, int i0, int i1) {
    for (int li = i0; li <= i1; ++li) {
      const Lane& l = m_lanes[li];
      if (!l.loaded || !isForecast(l.chain.def.kind) || l.chain.def.targetChain.isEmpty()) continue;
      int ti = -1;
      for (int k = i0; k <= i1; ++k) if (m_lanes[k].chain.def.key == l.chain.def.targetChain && m_lanes[k].loaded) ti = k;
      if (ti < 0) continue;
      const Lane& t = m_lanes[ti];
      p.setPen(QPen(theme::alpha(familyColour("forecasts"), 150), 1, Qt::DotLine));
      for (const auto& b : l.blocks) {
        if (b.x0 < m_t0 || b.x0 > m_t1) continue;
        // the actual block covering the target date
        auto it = std::upper_bound(t.blocks.begin(), t.blocks.end(), b.x0, [](qint64 x, const Block& tb) { return x < tb.x0; });
        if (it == t.blocks.begin()) continue;
        --it;
        if (b.x0 >= it->x1 + 86400000LL) continue;
        const double y1 = laneRect(li).center().y(), y2 = laneRect(ti).center().y();
        const double x = xOf(b.x0) + 2;
        p.drawLine(QPointF(x, y1 + (y2 > y1 ? 14 : -14)), QPointF(xOf(it->x0) + 2, y2 + (y2 > y1 ? -14 : 14)));
      }
    }
  }

  QPair<int, int> hit(const QPointF& pos) const {
    if (pos.x() < lw() || pos.y() < visibleRegion().boundingRect().top() + kAxis) return {-1, -1};   // the date row
    const int li = int((pos.y() - kAxis) / kLane);
    if (li < 0 || li >= m_lanes.size() || !m_lanes[li].loaded) return {-1, -1};
    const qint64 t = tOf(pos.x());
    const auto& bl = m_lanes[li].blocks;
    auto it = std::upper_bound(bl.begin(), bl.end(), t, [](qint64 x, const Block& b) { return x < b.x0; });
    if (it == bl.begin()) return {-1, -1};
    --it;
    // tolerance: a few pixels for thin blocks
    const qint64 tol = qint64(4.0 / (width() - lw() - 12) * double(m_t1 - m_t0));
    if (t > it->x1 + tol) return {-1, -1};
    return {li, int(it - bl.begin())};
  }
  void mousePressEvent(QMouseEvent* e) override { m_drag = e->position(); m_dragT0 = m_t0; m_moved = false; }
  void mouseMoveEvent(QMouseEvent* e) override {
    if (e->buttons() & Qt::LeftButton) {
      const double dx = e->position().x() - m_drag.x();
      if (std::abs(dx) > 3) m_moved = true;
      const qint64 span = m_t1 - m_t0;
      const qint64 shift = qint64(dx / (width() - lw() - 12) * double(span));
      m_t0 = m_dragT0 - shift; m_t1 = m_t0 + span;
      update();
      return;
    }
    const auto h = hit(e->position());
    if (h != m_hover) {
      m_hover = h; update();
      if (h.first >= 0) {
        const auto& l = m_lanes[h.first];
        const auto& f = l.facts[l.blocks[h.second].fact];
        QString tip = "<b>" + (l.chain.def.subject + " · " + l.chain.def.indicator).toHtmlEscaped() + "</b><br>" +
                      dateText(isForecast(l.chain.def.kind) ? (f.target ? f.target : f.period) : f.period, l.chain.def.freq) + "  ·  " +
                      (f.text.isEmpty() || std::isfinite(f.value) ? fmtValue(f.value, l.chain.def.unit) : shownText(f.text, l.chain.def.key).toHtmlEscaped());
        if (l.blocks[h.second].revisions) tip += "<br>" + QCoreApplication::translate("data", "%n revision(s)", nullptr, l.blocks[h.second].revisions);
        QToolTip::showText(e->globalPosition().toPoint(), tip, this);
      } else QToolTip::hideText();
    }
  }
  void mouseReleaseEvent(QMouseEvent* e) override {
    if (m_moved) return;
    const auto h = hit(e->position());
    select(h.first, h.second);
  }
  void wheelEvent(QWheelEvent* e) override {
    const double steps = e->angleDelta().y() / 120.0;
    if (steps == 0) return;
    const double x = e->position().x();
    const qint64 at = x > lw() ? tOf(x) : (m_t0 + m_t1) / 2;
    const double k = std::pow(0.82, steps);
    const qint64 span = qBound<qint64>(5 * 86400000LL, qint64((m_t1 - m_t0) * k), 200LL * 365 * 86400000LL);
    const double frac = double(at - m_t0) / double(m_t1 - m_t0);
    m_t0 = at - qint64(frac * span); m_t1 = m_t0 + span;
    update();
  }
  void leaveEvent(QEvent*) override { m_hover = {-1, -1}; update(); }

private:
  QVector<Lane> m_lanes;
  qint64 m_t0 = 0, m_t1 = 1;
  QPair<int, int> m_sel{-1, -1}, m_hover{-1, -1};
  QList<int> m_loadedOrder;
  QPointF m_drag; qint64 m_dragT0 = 0; bool m_moved = false;
};

// ------------------------------------------------------------------------------------------------ page
class MacroDataPage : public QWidget {
public:
  explicit MacroDataPage(QWidget* parent) : QWidget(parent) {
    const auto& c = theme::pal();
    // the page's content: in the page itself, or (phone, or a page this short) in a vertical page scroller, see setScroll
    m_outer = new QVBoxLayout(this);
    m_outer->setContentsMargins(0, 0, 0, 0); m_outer->setSpacing(0);
    m_content = new QWidget(this);
    m_outer->addWidget(m_content);
    m_content->installEventFilter(this);
    // the content's widgets are made in m_content (not in the page, then moved there by the layouts): a widget moved
    // under another parent is polished again, which changed the 3D bars' cached height and moved the desktop view 5 px
    auto* root = new QVBoxLayout(m_content);
    m_root = root;
    root->setContentsMargins(16, 12, 16, 10); root->setSpacing(8);

    auto* head = new QHBoxLayout; head->setSpacing(12);
    m_head = head;
    head->addWidget(ui::label(QCoreApplication::translate("data", "MACRO DATA"), theme::title(), c.accentText, m_content));
    head->addWidget(fit::label(QCoreApplication::translate("data", "economic facts, linked in chains"), theme::mono(11), c.textDim, m_content));
    head->addStretch(1);
    m_status = fit::label(QString(), theme::mono(10), c.textDim, m_content, 80);   // import progress: elides on a narrow window
    head->addWidget(m_status);
    m_counts = ui::label(QString(), theme::mono(10), c.textDim, m_content);
    head->addWidget(m_counts);
    root->addLayout(head);
    // phones: the import progress and the counts on a second row (hidden, and empty, otherwise)
    m_head2 = new QWidget(m_content);
    m_head2L = new QHBoxLayout(m_head2);
    m_head2L->setContentsMargins(0, 0, 0, 0); m_head2L->setSpacing(12);
    m_head2->hide();
    root->addWidget(m_head2);

    auto* body = new QHBoxLayout; body->setSpacing(12);
    m_body = body;
    root->addLayout(body, 1);

    // left: chains
    auto* left = new ui::Panel(QCoreApplication::translate("data", "Chains"), m_content);
    m_left = left;
    left->setMinimumWidth(290); left->setMaximumWidth(360);
    m_search = new QLineEdit(left);
    m_search->setPlaceholderText(QCoreApplication::translate("data", "Search a chain (country, indicator, market…)"));
    m_search->setClearButtonEnabled(true);
    m_search->setStyleSheet(QString("QLineEdit { background: %1; border: 1px solid %2; border-radius: 3px; padding: 4px 8px; color: %3;"
                                    " font-family: 'JetBrains Mono'; font-size: 12px; } QLineEdit:focus { border-color: %4; }")
                                .arg(c.panelAlt.name(), c.border.name(), c.text.name(), c.accent.name()));
    left->body()->addWidget(m_search);
    // family filter: 2 rows of exclusive buttons (the names do not fit on one row in the side panel)
    auto* famBox = new QWidget(left);
    m_famGrid = new QGridLayout(famBox);
    m_famGrid->setContentsMargins(0, 0, 0, 0); m_famGrid->setSpacing(4);
    left->body()->addWidget(famBox);
    // tick / untick every chain listed (the family and the search above decide which)
    auto* bulk = new QHBoxLayout; bulk->setSpacing(6);
    auto mk = [&](const QString& t) {
      auto* b = new QPushButton(t, left);
      b->setCursor(Qt::PointingHandCursor); b->setFont(theme::mono(8, QFont::Bold, 1.0));
      b->setStyleSheet(QString("QPushButton { background: transparent; color: %1; border: 1px solid %2; border-radius: 3px; padding: 3px 8px; }"
                               "QPushButton:hover { color: %3; border-color: %3; }").arg(c.textDim.name(), c.border.name(), c.accent.name()));
      bulk->addWidget(b);
      m_touchButtons << b;
      return b;
    };
    auto* checkAll = mk(QCoreApplication::translate("data", "✓ TICK ALL"));
    auto* uncheckAll = mk(QCoreApplication::translate("data", "✕ UNTICK ALL"));
    bulk->addStretch(1);
    left->body()->addLayout(bulk);
    m_shownLabel = ui::label(QString(), theme::mono(9), c.textMute, left);
    left->body()->addWidget(m_shownLabel);
    connect(checkAll, &QPushButton::clicked, this, [this] {
      QSet<QString> have(m_shown.cbegin(), m_shown.cend());
      for (const auto& k : m_list->visibleKeys()) if (!have.contains(k)) { have.insert(k); m_shown << k; }
      shownChanged();
    });
    // test hook DW_MACRODATA_TICKALL=<family id or "all">: that family selected, then TICK ALL
    if (qEnvironmentVariableIsSet("DW_MACRODATA_TICKALL"))
      QTimer::singleShot(300, this, [this, checkAll] {
        const QString f = qEnvironmentVariable("DW_MACRODATA_TICKALL");
        const int i = f == "all" ? 0 : int(m_familyIds.indexOf(f));
        if (i >= 0 && i < m_famButtons.size()) m_famButtons[i]->click();
        checkAll->click();
      });
    connect(uncheckAll, &QPushButton::clicked, this, [this] {
      const QStringList keys = m_list->visibleKeys();
      const QSet<QString> drop(keys.cbegin(), keys.cend());
      m_shown.removeIf([&](const QString& k) { return drop.contains(k); });
      shownChanged();
    });
    m_list = new ChainList(left);
    auto* scroll = new QScrollArea(left);
    scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setWidget(m_list);
    left->body()->addWidget(scroll, 1);
    body->addWidget(left, 0);

    // right: canvas + range + inspector
    auto* right = new QVBoxLayout; right->setSpacing(6);
    m_right = right;
    // view switch: the 2D chains (default) or one of the 3D views, each with its own bar
    auto* top = new QHBoxLayout; top->setSpacing(6);
    m_top = top;
    auto viewButton = [&](const QString& t) {
      auto* b = new QPushButton(t, m_content);
      b->setCheckable(true); b->setCursor(Qt::PointingHandCursor); b->setFont(theme::mono(9, QFont::Bold, 1.2));
      b->setStyleSheet(QString("QPushButton { background: transparent; color: %1; border: 1px solid %2; border-radius: 3px; padding: 3px 10px; }"
                               "QPushButton:checked { color: %3; border-color: %3; background: %4; }")
                           .arg(c.textDim.name(), c.border.name(), c.accent.name(), theme::alpha(c.accent, 30).name(QColor::HexArgb)));
      top->addWidget(b);
      m_viewButtons << b;
      return b;
    };
    viewButton(QCoreApplication::translate("data", "2D"));
    viewButton(QCoreApplication::translate("data", "LANDSCAPE 3D"));
    viewButton(QCoreApplication::translate("data", "MACRO CUBE 3D"));
    for (int i = 0; i < m_viewButtons.size(); ++i) connect(m_viewButtons[i], &QPushButton::clicked, this, [this, i] { setView(i); });
    top->addSpacing(10);
    m_bars = new CurrentStack(m_content);
    top->addWidget(m_bars, 1);
    right->addLayout(top);
    auto* bar2d = new QWidget(m_bars);
    auto* ranges = new QHBoxLayout(bar2d); ranges->setSpacing(6); ranges->setContentsMargins(0, 0, 0, 0);
    ranges->addWidget(ui::label(QCoreApplication::translate("data", "Range"), theme::mono(9, QFont::Bold, 1.4), c.textMute, m_content));
    for (const auto& [label, years] : QList<QPair<QString, double>>{{"1M", 1.0 / 12}, {"6M", 0.5}, {"1Y", 1}, {"5Y", 5}, {"20Y", 20}, {QCoreApplication::translate("data", "ALL"), 0}}) {
      auto* b = new QPushButton(label, m_content);
      b->setCursor(Qt::PointingHandCursor); b->setFont(theme::mono(9, QFont::Bold, 1.2));
      b->setStyleSheet(QString("QPushButton { background: transparent; color: %1; border: 1px solid %2; border-radius: 3px; padding: 2px 8px; }"
                               "QPushButton:hover { color: %3; border-color: %3; }").arg(c.textDim.name(), c.border.name(), c.accent.name()));
      connect(b, &QPushButton::clicked, this, [this, y = years] { setYears(y); });
      ranges->addWidget(b);
      m_touchButtons << b;
    }
    ranges->addStretch(1);
    m_rangeHint = fit::label(QCoreApplication::translate("data", "Wheel: zoom · drag: move · click a block: details"), theme::sans(10), c.textMute, m_content, 30);
    ranges->addWidget(m_rangeHint);
    m_bars->addWidget(bar2d);
    buildBars3d();
    m_views = new QStackedWidget(m_content);
    right->addWidget(m_views, 1);
    auto* page2d = new QWidget(m_views);
    auto* p2 = new QVBoxLayout(page2d); p2->setContentsMargins(0, 0, 0, 0); p2->setSpacing(6);
    m_canvas = new ChainCanvas(m_content);
    m_canvas->loader = [](qint64 id) { return macrodata::Store::instance().facts(id); };
    auto* cs = new QScrollArea(m_content);
    cs->setWidgetResizable(true); cs->setFrameShape(QFrame::NoFrame); cs->setWidget(m_canvas);
    // the date row stays on top: repaint the whole view on scroll (lanes coming into view load then)
    connect(cs->verticalScrollBar(), &QScrollBar::valueChanged, m_canvas, qOverload<>(&QWidget::update));
    if (qEnvironmentVariableIsSet("DW_MACRODATA_SCROLL"))   // test hook: scroll the chains view by <px>
      QTimer::singleShot(1500, this, [cs] { cs->verticalScrollBar()->setValue(qEnvironmentVariableIntValue("DW_MACRODATA_SCROLL")); });
    cs->setStyleSheet(QString("QScrollArea { background:%1; }").arg(c.bg.name()));
    p2->addWidget(cs, 1);
    // the box grows with its text (up to 140 px, then scrolls): the idle note takes 4 lines on a 1024 px window
    m_inspBox = textBox(m_content, 86, 140, 8, 12);
    m_inspector = m_inspBox->label();
    m_inspector->setFont(font());
    m_inspector->setTextFormat(Qt::RichText);
    m_inspector->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* nav = new QHBoxLayout; nav->setSpacing(6);
    m_prevBtn = new QPushButton(QCoreApplication::translate("data", "◀ PREVIOUS"), m_content); m_nextBtn = new QPushButton(QCoreApplication::translate("data", "NEXT ▶"), m_content);
    for (auto* b : {m_prevBtn, m_nextBtn}) {
      b->setCursor(Qt::PointingHandCursor); b->setFont(theme::mono(9, QFont::Bold, 1.2));
      b->setStyleSheet(QString("QPushButton { background: transparent; color: %1; border: 1px solid %2; border-radius: 3px; padding: 3px 10px; }"
                               "QPushButton:hover { color: %3; border-color: %3; } QPushButton:disabled { color: %4; border-color: %4; }")
                           .arg(c.textDim.name(), c.border.name(), c.accent.name(), c.divider.name()));
    }
    connect(m_prevBtn, &QPushButton::clicked, this, [this] { step(-1); });
    connect(m_nextBtn, &QPushButton::clicked, this, [this] { step(+1); });
    auto* insp = new QHBoxLayout; insp->setSpacing(8);
    m_insp = insp;
    insp->addWidget(m_inspBox, 1);
    auto* navCol = new QVBoxLayout; navCol->addWidget(m_prevBtn); navCol->addWidget(m_nextBtn); navCol->addStretch(1);
    m_navCol = navCol;
    insp->addLayout(navCol);
    p2->addLayout(insp);
    m_views->addWidget(page2d);
    buildViews3d();
    body->addLayout(right, 1);

    m_shown = settings::value("macrodata.shown", settings::value("facts.shown")).toStringList();   // facts.*: before 2026-11-18
    if (qEnvironmentVariableIsSet("DW_MACRODATA_SHOW")) m_shown = qEnvironmentVariable("DW_MACRODATA_SHOW").split(';', Qt::SkipEmptyParts);
    if (m_shown.isEmpty()) m_shown = {"rate:xm", "rate:us", "implied:fed", "macro:us:inflation", "macro:us:core-inflation", "cal:USD|CPI y/y:actual:myfxbook",
                                      "cal:USD|CPI y/y:consensus", "px:EURUSD:close", "fc:ing:EURUSD"};
    m_list->setShown(m_shown);
    m_list->onToggle = [this](const QString& key, bool on) {
      if (on && !m_shown.contains(key)) m_shown << key; else if (!on) m_shown.removeAll(key);
      shownChanged();
    };
    connect(m_search, &QLineEdit::textChanged, this, [this] { applyFilter(); });

    m_canvas->onSelect = [this](int lane, int block) { inspect(lane, block); };

    auto& store = macrodata::Store::instance();
    // hidden, the tab does no SQL nor 3D load: it notes that something changed and reloads once when shown again
    connect(&store, &macrodata::Store::changed, this, [this] { if (isVisible()) m_reload.start(); else m_stale = true; });
    m_reload.setSingleShot(true); m_reload.setInterval(1500);
    connect(&m_reload, &QTimer::timeout, this, [this] { reloadChains(); reloadLanes(); if (m_view > 0) load3d(); });
    connect(&store, &macrodata::Store::monthlyReady, this, [this] { if (!isVisible()) m_stale = true; else if (m_view > 0) load3d(); });
    auto& imp = macrodata::Importer::instance();
    connect(&imp, &macrodata::Importer::progress, this, [this] { if (isVisible()) syncStatus(); });
    connect(&imp, &macrodata::Importer::finished, this, [this] {
      if (!isVisible()) { m_stale = true; return; }
      syncStatus(); reloadChains(); reloadLanes();
    });

    reloadChains();
    const double years = qEnvironmentVariableIsSet("DW_MACRODATA_RANGE") ? qEnvironmentVariable("DW_MACRODATA_RANGE").toDouble()
                                                                     : settings::value("macrodata.years", settings::value("facts.years", 5.0)).toDouble();
    setYears(years);
    reloadLanes();
    inspect(-1, -1);
    syncStatus();
    setView(0);
    if (qEnvironmentVariableIsSet("DW_MACRODATA_VIEW")) {
      const QString v = qEnvironmentVariable("DW_MACRODATA_VIEW");
      QTimer::singleShot(0, this, [this, v] { setView(v == "cube" ? 2 : v == "landscape" ? 1 : 0); });
    }
  }

protected:
  void paintEvent(QPaintEvent*) override { QPainter p(this); p.fillRect(rect(), theme::pal().bg); }
  void showEvent(QShowEvent*) override {
    if (!macrodata::Importer::instance().running() && !m_started) { m_started = true; macrodata::Importer::instance().start(); }
    syncStatus();
    if (m_stale) {
      m_stale = false;
      reloadChains(); reloadLanes();
      if (m_view > 0) load3d();
    }
  }
  void hideEvent(QHideEvent*) override {
    if (m_reload.isActive()) { m_reload.stop(); m_stale = true; }
  }
  // Phones (ui::compact): one column (header on two rows, the chains above the view, the inspector over its arrows);
  // a phone upright or a page this short (phone sideways) scrolls. Wider and taller, the desktop layout as it was.
  void resizeEvent(QResizeEvent* e) override {
    QWidget::resizeEvent(e);
    const bool phone = ui::compact(width(), 940);   // desktop layout from 925 px (layout audit 2026-11-21)
    setPhone(phone);
    setScroll(phone || height() < 420);
    if (m_pageScroll) m_pageScroll->relayout();
  }
  // the content's layout changed (a text, a view): its height in the page scroller follows, once per event loop pass
  bool eventFilter(QObject* o, QEvent* e) override {
    if (o == m_content && e->type() == QEvent::LayoutRequest && m_pageScroll && !m_relayoutQueued) {
      m_relayoutQueued = true;
      QTimer::singleShot(0, this, [this] { m_relayoutQueued = false; if (m_pageScroll) m_pageScroll->relayout(); });
    }
    return QWidget::eventFilter(o, e);
  }

private:
  void setPhone(bool on) {
    if (on == m_phone) return;
    m_phone = on;
    m_head->removeWidget(m_status); m_head->removeWidget(m_counts);
    m_head2L->removeWidget(m_status); m_head2L->removeWidget(m_counts);
    if (on) { m_head2L->addWidget(m_status, 1); m_head2L->addWidget(m_counts); }
    else { m_head->addWidget(m_status); m_head->addWidget(m_counts); }
    m_head2->setVisible(on);
    m_body->setDirection(on ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    m_left->setMinimumWidth(on ? 0 : 290); m_left->setMaximumWidth(on ? QWIDGETSIZE_MAX : 360);
    // the chains get a box of their own (their list scrolls inside it); the view comes under it
    if (on) m_left->setFixedHeight(520);
    else { m_left->setMinimumHeight(0); m_left->setMaximumHeight(QWIDGETSIZE_MAX); }
    // the view buttons on one row, the range (or slice) bar on the next
    m_top->removeWidget(m_bars); m_right->removeWidget(m_bars);
    if (on) m_right->insertWidget(1, m_bars);
    else m_top->addWidget(m_bars, 1);
    m_rangeHint->setVisible(!on);   // wheel and mouse: not on a phone
    m_sliceBy->setVisible(!on);     // the cube's bar fits a phone without its label (the axis buttons say it)
    m_insp->setDirection(on ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    m_navCol->setDirection(on ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
    if (!on) { m_views->setMinimumHeight(0); m_views->setMaximumHeight(QWIDGETSIZE_MAX); }
    // finger targets
    for (auto* b : std::as_const(m_touchButtons)) b->setMinimumHeight(on ? 34 : 0);
    for (auto* b : std::as_const(m_famButtons)) b->setMinimumHeight(on ? 34 : 0);
    for (auto* b : std::as_const(m_viewButtons)) b->setMinimumHeight(on ? 34 : 0);
    for (auto* b : {m_prevBtn, m_nextBtn}) b->setMinimumHeight(on ? 34 : 0);
    m_search->setMinimumHeight(on ? 36 : 0);
  }
  void setScroll(bool on) {
    if (on == (m_pageScroll != nullptr)) return;
    if (on) {
      m_outer->removeWidget(m_content);
      m_pageScroll = new global::PageScroll(m_content, this);
      m_pageScroll->layout = [this](int w, int vh) { return contentHeight(w, vh); };
      m_outer->addWidget(m_pageScroll);
      m_content->show();
    } else {
      m_outer->removeWidget(m_pageScroll);
      m_pageScroll->takeWidget();
      m_content->setParent(this);
      m_outer->addWidget(m_content);
      m_content->show();
      m_pageScroll->hide();
      m_pageScroll->deleteLater();
      m_pageScroll = nullptr;
    }
  }
  // height of the content in the page scroller: phone sideways, the two columns made taller than the page; phone
  // upright, the column's own height with the view about a screen tall
  int contentHeight(int w, int vh) {
    if (!m_phone) return std::max(vh, 680);
    m_views->setFixedHeight(std::clamp(vh - 110, 420, 640));
    const int h = m_root->hasHeightForWidth() ? m_root->totalHeightForWidth(w) : m_root->totalSizeHint().height();
    return std::max(h, m_root->totalMinimumSize().height());
  }

  // a panel-coloured box of wrapped text whose height follows the text (see fit::TextBox)
  static fit::TextBox* textBox(QWidget* parent, int minH, int maxH, int padV, int padH) {
    const auto& c = theme::pal();
    auto* b = new fit::TextBox(parent, minH, maxH);
    b->setStyleSheet(QString("QScrollArea { background:%1; border:1px solid %2; border-radius:4px; }")
                         .arg(c.panel.name(), c.border.name()));
    b->label()->setFont(theme::sans(11));
    b->label()->setStyleSheet(QString("QLabel { background:transparent; color:%1; }").arg(c.text.name()));
    b->setPadding(padH, padV);
    return b;
  }

  // ---- 3D views
  void buildBars3d() {
    const auto& c = theme::pal();
    auto small = [&](QWidget* parent, const QString& t, bool checkable = false) {
      auto* b = new QPushButton(t, parent);
      b->setCheckable(checkable); b->setCursor(Qt::PointingHandCursor); b->setFont(theme::mono(8, QFont::Bold, 1.0));
      b->setStyleSheet(QString("QPushButton { background: transparent; color: %1; border: 1px solid %2; border-radius: 3px; padding: 2px 8px; }"
                               "QPushButton:hover { color: %3; border-color: %3; } QPushButton:checked { color: %3; border-color: %3; }")
                           .arg(c.textDim.name(), c.border.name(), c.accent.name()));
      m_touchButtons << b;
      return b;
    };
    auto sliderStyle = [&](QSlider* sl) {
      sl->setStyleSheet(QString("QSlider::groove:horizontal { height: 4px; background: %1; border-radius: 2px; }"
                                "QSlider::handle:horizontal { width: 12px; margin: -5px 0; background: %2; border-radius: 6px; }")
                            .arg(c.border.name(), c.accent.name()));
    };
    // landscape: slice date
    auto* bl = new QWidget(m_bars);
    auto* l = new QHBoxLayout(bl); l->setContentsMargins(0, 0, 0, 0); l->setSpacing(8);
    l->addWidget(ui::label(QCoreApplication::translate("data", "Slice"), theme::mono(9, QFont::Bold, 1.4), c.textMute, bl));
    m_landSlider = new QSlider(Qt::Horizontal, bl); sliderStyle(m_landSlider); m_landSlider->setMinimumWidth(80);
    l->addWidget(m_landSlider, 2);
    m_landSliceLabel = ui::label(QCoreApplication::translate("data", "none"), theme::mono(10), c.text, bl);
    m_landSliceLabel->setMinimumWidth(80);
    l->addWidget(m_landSliceLabel);
    auto* clear = small(bl, "✕");
    l->addWidget(clear);
    auto* reset = small(bl, QCoreApplication::translate("data", "RECENTRE"));
    l->addWidget(reset);
    l->addStretch(1);
    reset->setToolTip(QCoreApplication::translate("data", "Drag: turn · right drag: move · wheel: zoom · double-click: 2D"));
    connect(m_landSlider, &QSlider::valueChanged, this, [this](int m) {
      m_land->setSlice(m); m_landSliceLabel->setText(QLocale().toString(macrodata::monthDate(m), "MMM yyyy"));
    });
    connect(clear, &QPushButton::clicked, this, [this] { m_land->setSlice(-1); m_landSliceLabel->setText(QCoreApplication::translate("data", "none")); });
    connect(reset, &QPushButton::clicked, this, [this] { m_land->resetCamera(); });
    m_bars->addWidget(bl);
    // cube: slice axis + position
    auto* bc = new QWidget(m_bars);
    auto* k = new QHBoxLayout(bc); k->setContentsMargins(0, 0, 0, 0); k->setSpacing(6);
    m_sliceBy = ui::label(QCoreApplication::translate("data", "Slice by"), theme::mono(9, QFont::Bold, 1.4), c.textMute, bc);
    k->addWidget(m_sliceBy);
    for (const QString& t : {QCoreApplication::translate("data", "DATE"), QCoreApplication::translate("data", "COUNTRY"),
                             QCoreApplication::translate("data", "INDICATOR")}) {
      auto* b = small(bc, t, true);
      const int axis = int(m_axisButtons.size());
      connect(b, &QPushButton::clicked, this, [this, axis] { setCubeAxis(axis); });
      m_axisButtons << b; k->addWidget(b);
    }
    m_cubeSlider = new QSlider(Qt::Horizontal, bc); sliderStyle(m_cubeSlider); m_cubeSlider->setMinimumWidth(30);
    k->addWidget(m_cubeSlider, 2);
    auto* sliceLabel = fit::label(QString(), theme::mono(10), c.text, bc, 56);   // a month ("août 2026") stays whole
    sliceLabel->setHintWidth(150);
    m_cubeSliceLabel = sliceLabel;
    k->addWidget(m_cubeSliceLabel);
    auto* frame = small(bc, QCoreApplication::translate("data", "RECENTRE"));
    k->addWidget(frame);
    connect(frame, &QPushButton::clicked, this, [this] { m_cube->frame(); });
    connect(m_cubeSlider, &QSlider::valueChanged, this, [this](int v) {
      m_cube->setSlice(m_cube->axis(), v); m_cubeSliceLabel->setText(m_cube->sliceTitle());
    });
    m_bars->addWidget(bc);
  }
  void buildViews3d() {
    // plain text: the hover notes quote chain names and values from imported data (the inspector, which the app writes
    // as escaped HTML, stays rich)
    auto info = [&](QWidget* parent) { auto* t = textBox(parent, 30, 80, 6, 10); t->label()->setTextFormat(Qt::PlainText); return t; };
    auto* pl = new QWidget(m_views);
    auto* lv = new QVBoxLayout(pl); lv->setContentsMargins(0, 0, 0, 0); lv->setSpacing(6);
    m_landLayout = lv;
    m_landInfo = info(pl);
    lv->addWidget(m_landInfo);
    m_views->addWidget(pl);
    auto* pc = new QWidget(m_views);
    auto* cv = new QVBoxLayout(pc); cv->setContentsMargins(0, 0, 0, 0); cv->setSpacing(6);
    m_cubeLayout = cv;
    m_cubeInfo = info(pc);
    cv->addWidget(m_cubeInfo);
    m_views->addWidget(pc);
    const QString idle = QCoreApplication::translate("data", "Height and brightness: the month's rank in its own chain (0 = lowest ever, 100 = highest). Hover a block for its value.")
                         + "  ·  " + QCoreApplication::translate("data", "Drag: turn · right drag: move · wheel: zoom · double-click: 2D");
    m_landInfo->setText(idle);
    const QString cubeIdle = QCoreApplication::translate("data", "Colour: the value's rank in its own series (blue = lowest ever, grey = middle, orange = highest). Hover a cell of the slice.");
    m_cubeInfo->setText(cubeIdle);
  }
  // The two 3D views are OpenGL widgets: the first one created switches the whole window to OpenGL composition for
  // the rest of the session (every page then costs more to draw, Macro 10 → 16 % measured, and +325 MB). They are
  // created when a 3D view is first opened, not with the page.
  void ensure3d() {
    if (m_land) return;
    m_land = new Landscape(m_landLayout->parentWidget());
    m_landLayout->insertWidget(0, m_land, 1);
    m_cube = new MacroCube(m_cubeLayout->parentWidget());
    m_cubeLayout->insertWidget(0, m_cube, 1);
    const QString idle = m_landInfo->label()->text(), cubeIdle = m_cubeInfo->label()->text();   // the idle notes (set in buildViews3d)
    m_land->onInfo = [this, idle](const QString& t) { m_landInfo->setText(t.isEmpty() ? idle : t); };
    m_cube->onInfo = [this, cubeIdle](const QString& t) { m_cubeInfo->setText(t.isEmpty() ? cubeIdle : t); };
    m_land->onOpen2D = [this](const QString& key, int month) { openIn2D(key, month); };
    m_cube->onOpen2D = [this](const QString& key, int month) { openIn2D(key, month); };
  }
  void setView(int v) {
    if (v > 0) ensure3d();
    m_view = v;
    for (int i = 0; i < m_viewButtons.size(); ++i) m_viewButtons[i]->setChecked(i == v);
    m_bars->setCurrentIndex(v);
    m_views->setCurrentIndex(v);
    if (v > 0) load3d();
  }
  // the monthly cells, read in the background, then the active 3D view is filled
  void load3d() {
    // a reload asked while one runs (a store change during the read) is done once the current one lands, not dropped
    if (m_loading3d) { m_pending3d = true; return; }
    m_loading3d = true;
    (m_view == 2 ? m_cubeInfo : m_landInfo)->setText(QCoreApplication::translate("data", "Reading the monthly cells…"));
    macrodata::Store::instance().loadMonthly(this, [this](const macrodata::MonthMap& map) {
      m_loading3d = false;
      m_monthly = map;
      fillLandscape();
      fillCube();
      m_landInfo->setText(QCoreApplication::translate("data", "Height and brightness: the month's rank in its own chain (0 = lowest ever, 100 = highest). Hover a block for its value.")
                          + "  ·  " + QCoreApplication::translate("data", "Drag: turn · right drag: move · wheel: zoom · double-click: 2D"));
      m_cubeInfo->setText(QCoreApplication::translate("data", "Colour: the value's rank in its own series (blue = lowest ever, grey = middle, orange = highest). Hover a cell of the slice."));
      if (m_pending3d) { m_pending3d = false; if (m_view > 0) load3d(); }
    });
  }
  void fillLandscape() {
    const QStringList order = macrodata::Importer::families();
    QList<macrodata::Chain> list;
    const QSet<QString> shown(m_shown.cbegin(), m_shown.cend());
    for (const auto& ch : m_chains) if (shown.contains(ch.def.key)) list << ch;
    std::stable_sort(list.begin(), list.end(), [&](const macrodata::Chain& a, const macrodata::Chain& b) {
      const int fa = order.indexOf(a.def.family), fb = order.indexOf(b.def.family);
      if (fa != fb) return fa < fb;
      if (a.def.subject != b.def.subject) return a.def.subject < b.def.subject;
      return a.def.indicator < b.def.indicator;
    });
    // a forecast goes right after the chain it predicts when that chain is shown
    QList<macrodata::Chain> ordered, later;
    for (const auto& ch : list) {
      const bool fc = isForecast(ch.def.kind);
      bool targetShown = false;
      if (fc) for (const auto& x : list) if (x.def.key == ch.def.targetChain) targetShown = true;
      if (fc && targetShown) later << ch; else ordered << ch;
    }
    for (const auto& f : later) {
      int at = -1;
      for (int i = 0; i < ordered.size(); ++i) if (ordered[i].def.key == f.def.targetChain) at = i;
      ordered.insert(at + 1, f);
    }
    QVector<Landscape::Row> rows;
    for (const auto& ch : ordered) {
      Landscape::Row r;
      r.chain = ch; r.cells = m_monthly.value(ch.id);
      r.forecast = isForecast(ch.def.kind);
      r.colour = familyColour(r.forecast ? "forecasts" : ch.def.family);
      // a forecast placed behind its target stays in the target's plateau
      QString fam = ch.def.family;
      if (r.forecast) for (const auto& x : ordered) if (x.def.key == ch.def.targetChain) fam = x.def.family;
      r.group = macrodata::Importer::familyName(fam).toUpper();
      if (!r.cells.isEmpty()) rows << r;
    }
    m_land->setRows(rows);
    QSignalBlocker b(m_landSlider);
    m_landSlider->setRange(m_land->firstMonth(), m_land->lastMonth());
    // a slice the user set survives a reload (the landscape keeps it): the slider stays on it
    const int slice = m_land->slice();
    m_landSlider->setValue(slice >= m_land->firstMonth() && slice <= m_land->lastMonth() ? slice : m_land->lastMonth());
  }
  void fillCube() {
    static const QStringList indOrder{"inflation", "core-inflation", "gdp-qoq", "gdp-yoy", "unemployment", "retail-sales",
                                      "business-confidence", "consumer-confidence", "yield-10y", "rate-3m", "real-growth",
                                      "inflation-annual", "unemployment-annual", "current-account", "gov-debt", "gdp-usd",
                                      "gdp-per-capita", "fx-reserves", "population"};
    QStringList countries, indicators, indKeys;
    QList<const macrodata::Chain*> macro;
    QHash<QString, const macrodata::Chain*> best;   // "country|indicator" → chain kept (a publisher change starts a second chain)
    for (const auto& ch : m_chains) {
      if (ch.def.family != "macro" || !ch.def.key.startsWith("macro:")) continue;
      const QString cell = ch.def.key.section(':', 1, 2);
      if (best.contains(cell) && best.value(cell)->count >= ch.count) continue;
      best.insert(cell, &ch);
    }
    for (const auto& ch : m_chains) {
      if (ch.def.family != "macro" || !ch.def.key.startsWith("macro:") || best.value(ch.def.key.section(':', 1, 2)) != &ch) continue;
      macro << &ch;
      if (!countries.contains(ch.def.subject)) countries << ch.def.subject;
      const QString ik = ch.def.key.section(':', 2, 2);
      if (!indKeys.contains(ik)) indKeys << ik;
    }
    std::sort(countries.begin(), countries.end());
    std::sort(indKeys.begin(), indKeys.end(), [](const QString& a, const QString& b) {
      const int ia = indOrder.indexOf(a), ib = indOrder.indexOf(b);
      return (ia < 0 ? 99 : ia) < (ib < 0 ? 99 : ib);
    });
    QHash<QString, QString> indName;
    for (const auto* ch : macro) indName.insert(ch->def.key.section(':', 2, 2), ch->def.indicator);
    for (const auto& k : indKeys) indicators << indName.value(k);
    QVector<MacroCube::Series> series;
    for (const auto* ch : macro) {
      MacroCube::Series s;
      s.country = int(countries.indexOf(ch->def.subject));
      s.indicator = int(indKeys.indexOf(ch->def.key.section(':', 2, 2)));
      s.span = ch->def.freq == "A" ? 12 : ch->def.freq == "Q" ? 3 : 1;
      s.key = ch->def.key; s.unit = ch->def.unit;
      s.cells = m_monthly.value(ch->id);
      if (!s.cells.isEmpty()) series << s;
    }
    m_cube->setData(countries, indicators, series);
    if (m_axisButtons.size() == 3) {
      // test hook DW_MACRODATA_CUBE="date:2009-03" | "country:<name>" | "indicator:<index>"
      if (qEnvironmentVariableIsSet("DW_MACRODATA_CUBE") && !m_cubeHookDone) {
        m_cubeHookDone = true;
        const QString h = qEnvironmentVariable("DW_MACRODATA_CUBE");
        const QString kind = h.section(':', 0, 0), val = h.section(':', 1);
        if (kind == "country") { m_cube->setSlice(MacroCube::Country, int(countries.indexOf(val))); }
        else if (kind == "indicator") { m_cube->setSlice(MacroCube::Indicator, val.toInt()); }
        else m_cube->setSlice(MacroCube::Date, macrodata::monthIndex(QDate::fromString(val + "-01", "yyyy-MM-dd")));
      }
      syncCubeBar();
    }
  }
  void setCubeAxis(int axis) {
    int v = 0;
    if (axis == 0) v = macrodata::monthIndex(QDate::currentDate().addMonths(-3));
    m_cube->setSlice(MacroCube::Axis(axis), v);
    syncCubeBar();
  }
  void syncCubeBar() {
    const int axis = int(m_cube->axis());
    for (int i = 0; i < m_axisButtons.size(); ++i) m_axisButtons[i]->setChecked(i == axis);
    QSignalBlocker b(m_cubeSlider);
    if (axis == 0) m_cubeSlider->setRange(m_cube->firstMonth(), m_cube->lastMonth());
    else m_cubeSlider->setRange(0, qMax(0, int((axis == 1 ? m_cubeCountries() : m_cubeIndicators())) - 1));
    m_cubeSlider->setValue(m_cube->value());
    m_cubeSliceLabel->setText(m_cube->sliceTitle());
  }
  int m_cubeCountries() const { QSet<QString> s; for (const auto& ch : m_chains) if (ch.def.family == "macro") s.insert(ch.def.subject); return int(s.size()); }
  int m_cubeIndicators() const { QSet<QString> s; for (const auto& ch : m_chains) if (ch.def.family == "macro") s.insert(ch.def.key.section(':', 2, 2)); return int(s.size()); }
  // double-click in 3D: the chain in the 2D view, around that month
  void openIn2D(const QString& key, int month) {
    if (!m_shown.contains(key)) { m_shown << key; shownChanged(); }
    setView(0);
    const qint64 t = QDateTime(macrodata::monthDate(month), QTime(0, 0), QTimeZone::UTC).toMSecsSinceEpoch();
    const qint64 span = qint64(5 * 365.25 * 86400000.0);
    m_canvas->setRange(t - span / 2, t + span / 2);
    for (int i = 0; i < m_canvas->lanes().size(); ++i) {
      if (m_canvas->lanes()[i].chain.def.key != key) continue;
      m_canvas->ensureLoaded(i);
      const auto& bl = m_canvas->lanes()[i].blocks;
      int best = -1; qint64 bd = LLONG_MAX;
      for (int b = 0; b < bl.size(); ++b) { const qint64 d = std::abs(bl[b].x0 - t); if (d < bd) { bd = d; best = b; } }
      if (best >= 0) m_canvas->select(i, best);
    }
  }

  void syncStatus() {
    auto& imp = macrodata::Importer::instance();
    m_status->setText(imp.running() ? "● " + imp.status() : imp.status());
    m_status->setStyleSheet(QString("color:%1;background:transparent;").arg((imp.running() ? theme::pal().warn : theme::pal().textDim).name()));
  }
  void reloadChains() {
    m_chains = macrodata::Store::instance().chains();
    // families in the tab's order, names translated (keys and stored names stay English)
    const QStringList order = macrodata::Importer::families();
    for (auto& c : m_chains) c.def.indicator = QCoreApplication::translate("data", c.def.indicator.toUtf8().constData());
    std::stable_sort(m_chains.begin(), m_chains.end(), [&](const macrodata::Chain& a, const macrodata::Chain& b) {
      return order.indexOf(a.def.family) < order.indexOf(b.def.family);
    });
    m_list->setChains(m_chains);
    // a ticked chain that no longer exists (2026-11-16: split into one chain per source, e.g. "cal:USD|CPI y/y:actual"
    // → "…:actual:myfxbook") is replaced by the chains that took its place
    {
      QSet<QString> keys;
      for (const auto& c : m_chains) keys.insert(c.def.key);
      QStringList shown;
      bool changed = false;
      for (const QString& k : m_shown) {
        if (keys.contains(k) || keys.isEmpty()) { shown << k; continue; }
        bool found = false;
        for (const auto& c : m_chains) if (c.def.key.startsWith(k + ":")) { if (!shown.contains(c.def.key)) shown << c.def.key; found = true; }
        changed = true;
        if (!found) shown << k;   // not recorded yet (first pass running): kept
      }
      if (changed && shown != m_shown) { m_shown = shown; settings::setValue("macrodata.shown", m_shown); m_list->setShown(m_shown); }
    }
    syncShownLabel();
    qint64 n = 0, r = 0;
    QHash<QString, int> perFamily;
    for (const auto& c : m_chains) { n += c.count; r += c.revisions; ++perFamily[c.def.family]; }
    m_counts->setText(QCoreApplication::translate("data", "%1 chains · %2 facts · %3 revisions").arg(QLocale(QLocale::English).toString(m_chains.size()),
                                                                  QLocale(QLocale::English).toString(n), QLocale(QLocale::English).toString(r)));
    if (m_famButtons.isEmpty()) {
      const auto& c = theme::pal();
      m_familyIds = QStringList{QString()} + macrodata::Importer::families();
      for (int i = 0; i < m_familyIds.size(); ++i) {
        auto* b = new QPushButton(m_content);
        b->setCheckable(true); b->setCursor(Qt::PointingHandCursor); b->setFont(theme::mono(8, QFont::Bold, 1.0));
        const QColor fc = i == 0 ? c.accent : familyColour(m_familyIds[i]);
        b->setStyleSheet(QString("QPushButton { background: transparent; color: %1; border: 1px solid %2; border-radius: 3px; padding: 3px 4px; }"
                                 "QPushButton:checked { color: %3; border-color: %3; background: %4; }")
                             .arg(c.textDim.name(), c.border.name(), fc.name(), theme::alpha(fc, 30).name(QColor::HexArgb)));
        connect(b, &QPushButton::clicked, this, [this, i] { m_family = i; for (int k = 0; k < m_famButtons.size(); ++k) m_famButtons[k]->setChecked(k == i); applyFilter(); });
        m_famGrid->addWidget(b, i / 3, i % 3);
        m_famButtons << b;
        if (m_phone) b->setMinimumHeight(34);
      }
    }
    for (int i = 0; i < m_famButtons.size(); ++i) {
      const int n = i == 0 ? int(m_chains.size()) : perFamily.value(m_familyIds[i]);
      m_famButtons[i]->setText((i == 0 ? QCoreApplication::translate("data", "ALL") : shortFamily(m_familyIds[i])) + " " + QString::number(n));
      m_famButtons[i]->setChecked(i == m_family);
    }
    applyFilter();
  }
  void applyFilter() { m_list->setFilter(m_search->text(), m_familyIds.value(m_family)); }
  void shownChanged() {
    settings::setValue("macrodata.shown", m_shown);
    m_list->setShown(m_shown);
    syncShownLabel();
    reloadLanes();
    if (m_view == 1 && !m_monthly.isEmpty()) fillLandscape();   // the landscape follows the ticks
  }
  void syncShownLabel() {   // chains shown that exist (a key can outlive its chain)
    int n = 0;
    const QSet<QString> shown(m_shown.cbegin(), m_shown.cend());
    for (const auto& c : m_chains) if (shown.contains(c.def.key)) ++n;
    m_shownLabel->setText(QCoreApplication::translate("data", "%n shown", nullptr, n));
  }
  static QString shortFamily(const QString& f) {
    if (f == "rates") return QCoreApplication::translate("data", "RATES");
    if (f == "macro") return QCoreApplication::translate("data", "MACRO");
    if (f == "calendar") return QCoreApplication::translate("data", "CALENDAR");
    if (f == "prices") return QCoreApplication::translate("data", "PRICES");
    if (f == "forecasts") return QCoreApplication::translate("data", "FORECASTS");
    return QCoreApplication::translate("data", "POSITIONING");
  }
  void reloadLanes() {
    QVector<ChainCanvas::Lane> lanes;
    // the shown keys in their order, each with its chains in catalogue order (TICK ALL: 2,195 × 2,195 compares before)
    QHash<QString, QVector<int>> byKey;
    for (int i = 0; i < m_chains.size(); ++i) byKey[m_chains[i].def.key] << i;
    for (const auto& key : m_shown)
      for (int i : byKey.value(key)) {
        ChainCanvas::Lane l; l.chain = m_chains[i];
        lanes << l;
      }
    m_canvas->setLanes(lanes);
    // read after setLanes: it drops a selection whose lane is gone (read before, a selection on a lane that no longer
    // exists reached inspect() and crashed — select a block, then UNTICK ALL)
    const auto sel = m_canvas->selection();
    if (qEnvironmentVariableIsSet("DW_MACRODATA_SELECT") && !m_selectedOnce) {
      m_selectedOnce = true;
      const QString s = qEnvironmentVariable("DW_MACRODATA_SELECT");
      for (int i = 0; i < lanes.size(); ++i) if (lanes[i].chain.def.key == s.section('|', 0, 0)) {
        m_canvas->ensureLoaded(i);
        const int b = s.contains('|') ? s.section('|', 1).toInt() : int(m_canvas->lanes()[i].blocks.size()) - 1;
        // the lanes may have been set again (and unloaded) by then: loaded again, and nothing picked in an empty one
        // (qBound(0, x, -1) asserted)
        QTimer::singleShot(0, this, [this, i, b] {
          if (i >= m_canvas->lanes().size()) return;
          m_canvas->ensureLoaded(i);
          const int n = int(m_canvas->lanes()[i].blocks.size());
          if (n > 0) m_canvas->select(i, qBound(0, b < 0 ? n + b : b, n - 1));
        });
      }
    } else inspect(sel.first, sel.second);   // a selection that is gone resets the inspector and the arrows
  }
  void setYears(double years) {
    settings::setValue("macrodata.years", years);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (years <= 0) {   // everything shown
      qint64 a = now, b = now;
      for (const auto& c : m_chains) if (m_shown.contains(c.def.key) && c.first) { a = qMin(a, c.first); b = qMax(b, c.last); }
      const qint64 pad = (b - a) / 30 + 86400000LL;
      m_canvas->setRange(a - pad, qMax(b, now) + pad);
      return;
    }
    const qint64 span = qint64(years * 365.25 * 86400000.0);
    m_canvas->setRange(now - span, now + qMax<qint64>(span / 4, 120LL * 86400000));   // room for forecasts ahead
  }
  void step(int dir) {
    const auto sel = m_canvas->selection();
    if (sel.first < 0) return;
    const int nb = sel.second + dir;
    if (nb < 0 || nb >= m_canvas->lanes()[sel.first].blocks.size()) return;
    m_canvas->select(sel.first, nb);
    m_canvas->reveal(sel.first, nb);
  }
  void inspect(int lane, int block) {
    const auto& c = theme::pal();
    // the range test first: lanes()[lane] is only read once lane is known to exist
    const bool valid = lane >= 0 && block >= 0 && lane < m_canvas->lanes().size() && block < m_canvas->lanes()[lane].blocks.size();
    m_prevBtn->setEnabled(valid && block > 0);
    m_nextBtn->setEnabled(valid && block + 1 < m_canvas->lanes()[lane].blocks.size());
    if (!valid) {
      m_inspBox->setText(QString("<span style='color:%1'>%2</span>").arg(c.textMute.name(),
          QCoreApplication::translate("data", "Each block is one fact: a value of an indicator for one period, real or forecast. A chain links the facts of one "
            "indicator of one kind, each to the one before. A revised value is a new block layered on the one it corrects "
            "(nothing is overwritten); forecasts are dashed and point at the actual value they predict.").toHtmlEscaped()));
      return;
    }
    const auto& l = m_canvas->lanes()[lane];
    const auto& b = l.blocks[block];
    const auto& f = l.facts[b.fact];
    const QString unit = l.chain.def.unit;
    QStringList lines;
    lines << QString("<b style='color:%1'>%2</b> &nbsp;<span style='color:%3'>%4 · %5</span>")
                 .arg(familyColour(isForecast(l.chain.def.kind) ? "forecasts" : l.chain.def.family).name(),
                      (l.chain.def.subject + " · " + l.chain.def.indicator).toHtmlEscaped(), c.textMute.name(),
                      kindLabel(l.chain.def.kind), l.chain.def.source.toHtmlEscaped());
    const bool fc = isForecast(l.chain.def.kind);
    QString main = (fc ? QCoreApplication::translate("data", "Forecast for %1: %2, made %3") : QCoreApplication::translate("data", "%1: %2"))
                       .arg(dateText(fc ? (f.target ? f.target : f.period) : f.period, l.chain.def.freq),
                            f.text.isEmpty() || std::isfinite(f.value) ? fmtValue(f.value, unit) : shownText(f.text, l.chain.def.key));
    if (fc) main = main.arg(dateText(f.period, "D"));
    if (!f.text.isEmpty() && std::isfinite(f.value)) main += "  (" + f.text + ")";
    lines << "<span style='font-size:14px'>" + main.toHtmlEscaped() + "</span>";
    QStringList meta;
    meta << (f.origin == "live" ? QCoreApplication::translate("data", "seen live, recorded %1") : QCoreApplication::translate("data", "imported from history on %1")).arg(dateText(f.recorded, "D"));
    if (f.at) meta << QCoreApplication::translate("data", "published / observed %1").arg(dateText(f.at, "E"));
    if (!f.note.isEmpty()) meta << f.note;
    lines << QString("<span style='color:%1'>%2</span>").arg(c.textDim.name(), meta.join("  ·  ").toHtmlEscaped());
    // revisions: every value this slot has had, oldest first
    if (b.revisions > 0) {
      QStringList hist;
      for (const auto& x : l.facts) {
        const bool sameSlot = fc ? ((x.target ? x.target : x.period) == (f.target ? f.target : f.period))
                                 : (x.period == f.period && x.target == f.target);
        if (sameSlot) hist << fmtValue(x.value, unit) + " (" + QCoreApplication::translate("data", "recorded %1").arg(dateText(x.recorded, "D")) + ")";
      }
      lines << QString("<span style='color:%1'>%2</span>").arg(c.warn.name(), (QCoreApplication::translate("data", "Values of this period: ") + hist.join(" → ")).toHtmlEscaped());
    }
    // neighbours in the chain
    QStringList near;
    if (block > 0) { const auto& pf = l.facts[l.blocks[block - 1].fact]; near << QCoreApplication::translate("data", "previous %1 (%2)").arg(fmtValue(pf.value, unit), dateText(fc ? (pf.target ? pf.target : pf.period) : pf.period, l.chain.def.freq)); }
    if (block + 1 < l.blocks.size()) { const auto& nf = l.facts[l.blocks[block + 1].fact]; near << QCoreApplication::translate("data", "next %1 (%2)").arg(fmtValue(nf.value, unit), dateText(fc ? (nf.target ? nf.target : nf.period) : nf.period, l.chain.def.freq)); }
    if (block > 0 && std::isfinite(f.value)) {
      const double pv = l.facts[l.blocks[block - 1].fact].value;
      if (std::isfinite(pv)) {
        const double d = f.value - pv;
        const QString txt = unit == "%" ? QString("%1%2 pp").arg(d >= 0 ? "+" : "").arg(d, 0, 'f', std::abs(d) < 1 ? 2 : 1)
                                        : (d >= 0 ? "+" : "") + fmtValue(d, unit);
        near << QCoreApplication::translate("data", "change %1").arg(txt);
      }
    }
    if (!near.isEmpty()) lines << QString("<span style='color:%1'>%2</span>").arg(c.textMute.name(), near.join("  ·  ").toHtmlEscaped());
    m_inspBox->setText(lines.join("<br>"));
  }

  ChainList* m_list;
  ChainCanvas* m_canvas;
  QLineEdit* m_search;
  QGridLayout* m_famGrid = nullptr;
  QList<QPushButton*> m_famButtons;
  int m_family = 0;
  QLabel *m_status, *m_counts, *m_inspector, *m_shownLabel = nullptr;
  QPushButton *m_prevBtn, *m_nextBtn;
  QList<macrodata::Chain> m_chains;
  QStringList m_shown, m_familyIds;
  QTimer m_reload;
  bool m_stale = false;   // the store changed while the tab was hidden
  QList<QPushButton*> m_viewButtons, m_axisButtons;
  QStackedWidget *m_bars = nullptr, *m_views = nullptr;
  Landscape* m_land = nullptr;
  MacroCube* m_cube = nullptr;
  QVBoxLayout* m_landLayout = nullptr; QVBoxLayout* m_cubeLayout = nullptr;   // their pages, filled by ensure3d()
  fit::TextBox *m_landInfo = nullptr, *m_cubeInfo = nullptr, *m_inspBox = nullptr;
  QLabel *m_landSliceLabel = nullptr, *m_cubeSliceLabel = nullptr;
  QSlider *m_landSlider = nullptr, *m_cubeSlider = nullptr;
  macrodata::MonthMap m_monthly;
  int m_view = 0;
  bool m_loading3d = false, m_pending3d = false, m_cubeHookDone = false;
  bool m_started = false, m_selectedOnce = false;
  // phone layout (see setPhone / setScroll)
  QVBoxLayout *m_outer = nullptr, *m_root = nullptr, *m_right = nullptr, *m_navCol = nullptr;
  QHBoxLayout *m_head = nullptr, *m_head2L = nullptr, *m_body = nullptr, *m_top = nullptr, *m_insp = nullptr;
  QWidget *m_content = nullptr, *m_head2 = nullptr;
  ui::Panel* m_left = nullptr;
  QLabel *m_rangeHint = nullptr, *m_sliceBy = nullptr;
  QList<QPushButton*> m_touchButtons;
  global::PageScroll* m_pageScroll = nullptr;
  bool m_phone = false, m_relayoutQueued = false;
};
}  // namespace macrodatapage

using MacroDataPageImpl = macrodatapage::MacroDataPage;
DW_PAGE("data/macro-data", MacroDataPageImpl)
