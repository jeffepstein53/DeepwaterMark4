#include "pages/global/overview/FeedPanels.h"
#include "core/I18n.h"
#include <QCoreApplication>
#include "core/Settings.h"
#include "global/Look.h"
#include "global/FrameClock.h"
#include "ui/Format.h"
#include "ui/Kit.h"
#include "ui/Theme.h"
#include "macro/Registry.h"
#include "macro/Watchlist.h"
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QToolTip>
#include <QWheelEvent>
#include <QtMath>
#include <cmath>

namespace look = global::look;

namespace overview {

// "12m ago" in the interface language (fmt::ago is English only)
static QString ago(const QDateTime& t, const QDateTime& now) {
  const qint64 s = qMax<qint64>(0, t.secsTo(now));
  if (s < 60) return QCoreApplication::translate("global", "%1s ago").arg(s);
  if (s < 3600) return QCoreApplication::translate("global", "%1m ago").arg(s / 60);
  if (s < 86400) return QCoreApplication::translate("global", "%1h ago").arg(s / 3600);
  return QCoreApplication::translate("global", "%1d ago").arg(s / 86400);
}

// Feed columns: time at 16, age at 89, type icon and text after the widest age label ("1h ago" / "il y a 23h").
static qreal feedTextX() {
  const qreal ageW = QFontMetricsF(theme::mono(12)).horizontalAdvance(QCoreApplication::translate("global", "%1h ago").arg(23));
  return qMax<qreal>(179, 89 + ageW + 32);
}

// Greedy word wrap with a fixed line pitch (Qt's own wrap uses the font's line spacing).
static QStringList wrap(const QString& text, const QFont& f, qreal width) {
  QStringList lines;
  const QFontMetricsF fm(f);
  QString cur;
  for (const QString& w : text.split(' ', Qt::SkipEmptyParts)) {
    const QString t = cur.isEmpty() ? w : cur + ' ' + w;
    if (fm.horizontalAdvance(t) <= width || cur.isEmpty()) cur = t;
    else { lines << cur; cur = w; }
  }
  if (!cur.isEmpty()) lines << cur;
  return lines;
}

// ================================================================= Squawk
static const qreal kWireSpeed = 58.0;   // px/s (reference: 348 px/s in a 6x time-lapse)
static const char* kSettingFr = "global.overview.squawkFr";
static const char* kSettingTab = "global.overview.squawkTab";

SquawkPanel::SquawkPanel(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent);
  setMouseTracking(true);
  m_fr = settings::value(kSettingFr, i18n::current() == i18n::Lang::French).toBool();   // default: interface language
  m_tab = qBound(0, settings::value(kSettingTab, 1).toInt(), 3);
  m_feed.status = QCoreApplication::translate("global", "CONNECTING TO THE WIRE…");
  connect(&global::FrameClock::instance(), &global::FrameClock::tick, this, [this](int frame) {
    Q_UNUSED(frame);
    if (!m_animOn) return;
    const qint64 ms = m_clock.elapsed();
    m_wireOff += (ms - m_lastMs) / 1000.0 * kWireSpeed;
    m_lastMs = ms;
    if (m_wireW > 0) m_wireOff = std::fmod(m_wireOff, m_wireW);
    update(wireRect().toAlignedRect());
  });
  m_ages.setInterval(30000);
  connect(&m_ages, &QTimer::timeout, this, [this] { update(listRect().toAlignedRect()); });
  m_clock.start();
}

// Same items as shown (the fields the wire strip and the list draw): the wide strip image is not re-rendered.
static bool sameItems(const QList<SquawkItem>& a, const QList<SquawkItem>& b) {
  if (a.size() != b.size()) return false;
  auto eq = [](double x, double y) { return x == y || (std::isnan(x) && std::isnan(y)); };
  for (int i = 0; i < a.size(); ++i) {
    const auto &x = a[i], &y = b[i];
    if (x.kind != y.kind || x.time != y.time || x.text != y.text || x.textFr != y.textFr || x.name != y.name ||
        !eq(x.changePct, y.changePct) || !eq(x.price, y.price) || x.priceDecimals != y.priceDecimals)
      return false;
  }
  return true;
}
void SquawkPanel::setFeed(const SquawkFeed& f) {
  const bool same = sameItems(m_feed.items, f.items);
  m_feed = f;
  if (!same) { ++m_feedGen; buildWire(); }
  update();
}
void SquawkPanel::showEvent(QShowEvent*) {
  m_lastMs = m_clock.elapsed();
  if (m_wireW > 0) startAnim(true);
  m_ages.start();
}
void SquawkPanel::hideEvent(QHideEvent*) { startAnim(false); m_ages.stop(); }
void SquawkPanel::startAnim(bool on) {
  if (on == m_animOn) return;
  m_animOn = on;
  if (on) { m_lastMs = m_clock.elapsed(); global::FrameClock::instance().acquire(this); }
  else global::FrameClock::instance().release(this);
}
void SquawkPanel::resizeEvent(QResizeEvent*) { buildWire(); }

// Short panels (laptop windows): the description keeps at most 3 lines, the last one elided ("…", full text in the
// tooltip), so the header doesn't take 130 px from the feed; the reference's 6-line column stays on tall panels.
bool SquawkPanel::shortHeader() const { return height() < 850; }
QStringList SquawkPanel::descLines() const {
  const qreal W = width();
  const QString key = QString("%1|%2|%3").arg(W).arg(m_fr).arg(shortHeader());
  if (key != m_descKey) {   // cached: headerH() is asked on every repaint
    m_descKey = key;
    // FR: the AUTO-TRANSLATED label takes room; the column is as wide as "price moves +" (6 lines, as the reference)
    const QFont f = theme::condensed(12, QFont::Medium);
    const QString text = QCoreApplication::translate("global", "DATA headlines + real-time price moves + fresh data releases");
    const qreal frW = look::width(QCoreApplication::translate("global", "price moves +"), f) + 1;
    const qreal w = showAutoLabel() ? qMin(frW, qMax(40.0, W - 109 - 74 - 151)) : qMax(60.0, W - 109 - 8 - 151);
    m_desc = wrap(text, f, w);
    m_descCut = false;
    if (shortHeader() && m_desc.size() > 3) {
      const QString rest = QStringList(m_desc.mid(2)).join(' ');
      m_desc = m_desc.mid(0, 2);
      m_desc << QFontMetricsF(f).elidedText(rest, Qt::ElideRight, w);
      m_descCut = true;
    }
  }
  return m_desc;
}
int SquawkPanel::headerH() const { return qMax(71, int(descLines().size()) * 19 + 15); }

QRectF SquawkPanel::wireRect() const { return QRectF(1, headerH() + 1, width() - 2, 35); }
QRectF SquawkPanel::tabsRect() const { return QRectF(1, headerH() + 37, width() - 2, 42); }
QRectF SquawkPanel::listRect() const { return QRectF(0, headerH() + 80, width(), height() - headerH() - 80 - 31); }
QRectF SquawkPanel::buttonRect() const { return QRectF(width() - 109, headerH() / 2.0 + 1 - 14, 44, 28); }

bool SquawkPanel::compactTabs() const {
  const QStringList labels{QCoreApplication::translate("global", "ALL"), QCoreApplication::translate("global", "DATA"), QCoreApplication::translate("global", "◇ MOVERS"), QCoreApplication::translate("global", "RELEASES")};
  const QFont lf = theme::mono(12, QFont::DemiBold), cf = theme::mono(12);
  qreal x = 11;
  for (const auto& l : labels) x += look::width(l, lf) + 16 + 5 + look::width("000", cf) + 4;
  return x > width() - 6;
}
// "AUTO- / TRANSLATED" beside the description in FR content mode, when the header has room for both
// (narrow panels drop it; the button's tooltip still says the texts are machine translated)
bool SquawkPanel::showAutoLabel() const { return m_fr && !shortHeader() && width() - 109 - 74 - 151 >= 60; }

QList<QRectF> SquawkPanel::tabRects() const {
  const QStringList labels{QCoreApplication::translate("global", "ALL"), QCoreApplication::translate("global", "DATA"), QCoreApplication::translate("global", "◇ MOVERS"), QCoreApplication::translate("global", "RELEASES")};
  const bool compact = compactTabs();
  const QFont lf = theme::mono(compact ? 11 : 12, QFont::DemiBold), cf = theme::mono(compact ? 11 : 12);
  const qreal pad = compact ? 10 : 16, gap = compact ? 2 : 4;
  QList<int> counts{int(m_feed.items.size()), 0, 0, 0};
  for (const auto& it : m_feed.items) counts[1 + int(it.kind)]++;
  const bool have = !m_feed.items.isEmpty();
  QList<QRectF> out;
  qreal x = compact ? 6 : 11;
  const QRectF tr = tabsRect();
  for (int i = 0; i < 4; ++i) {
    qreal w = look::width(labels[i], lf) + pad;
    if (have) w += 5 + look::width(QString::number(counts[i]), cf);
    out << QRectF(x, tr.center().y() - 13, w, 26);
    x += w + gap;
  }
  return out;
}

static QString itemText(const SquawkItem& it, bool fr) {
  return it.kind == SquawkItem::Mover
      ? it.name + " " + fmt::pct(it.changePct, 2) + QCoreApplication::translate("global", " — now ") + fmt::num(it.price, it.priceDecimals)
      : (fr && !it.textFr.isEmpty() ? it.textFr : it.text);
}
const QVector<QStringList>& SquawkPanel::wrappedLines() const {
  const qreal w = width() - 11 - feedTextX();
  const QString key = QString("%1|%2|%3").arg(w).arg(m_fr).arg(m_feedGen);
  if (key != m_wrapKey) {
    m_wrapKey = key;
    const QFont xf = theme::condensed(15, QFont::Medium);
    m_wrapped.clear();
    m_wrapped.reserve(m_feed.items.size());
    for (const auto& it : m_feed.items) m_wrapped << wrap(itemText(it, m_fr), xf, w);
  }
  return m_wrapped;
}

QList<int> SquawkPanel::filtered() const {
  QList<int> idx;
  for (int i = 0; i < m_feed.items.size(); ++i) {
    const auto k = m_feed.items[i].kind;
    if (m_tab == 0 || (m_tab == 1 && k == SquawkItem::Headline) || (m_tab == 2 && k == SquawkItem::Mover) ||
        (m_tab == 3 && k == SquawkItem::Release))
      idx << i;
  }
  return idx;
}

void SquawkPanel::buildWire() {
  m_wire = QPixmap(); m_wireW = 0;
  if (m_feed.items.isEmpty()) { startAnim(false); return; }
  const QFont f = theme::mono(14), fb = theme::mono(14, QFont::Bold);
  struct Span { QString t; QFont f; QColor c; };
  QList<QList<Span>> items;
  qreal total = 0;
  const QColor up = theme::map("#4fd1da"), down = theme::map("#e0666b"), white = theme::map("#ffffff");
  for (const auto& it : m_feed.items) {
    QList<Span> s;
    s << Span{it.time.toLocalTime().toString("HH:mm:ss"), f, theme::map("#5c5c5c")};
    if (it.kind == SquawkItem::Mover) {
      const QColor c = it.changePct >= 0 ? up : down;
      s << Span{" • ", f, c} << Span{it.name, fb, white} << Span{" " + fmt::pct(it.changePct, 2), f, c}
        << Span{QCoreApplication::translate("global", " — now "), f, white} << Span{fmt::num(it.price, it.priceDecimals), fb, white};
    } else {
      s << Span{" • ", f, white} << Span{m_fr && !it.textFr.isEmpty() ? it.textFr : it.text, f, white};
    }
    qreal w = 0; for (const auto& x : s) w += look::width(x.t, x.f);
    if (total + w + 40 > 14000) break;     // keep the pixmap within limits
    total += w + 40;
    items << s;
  }
  const qreal dpr = 2.0;
  m_wireW = std::ceil(total);
  m_wire = QPixmap(QSize(int(m_wireW * dpr), int(35 * dpr)));
  m_wire.setDevicePixelRatio(dpr);
  m_wire.fill(Qt::transparent);
  QPainter p(&m_wire);
  p.setRenderHint(QPainter::Antialiasing);
  qreal x = 0;
  for (const auto& s : items) {
    for (const auto& sp : s) x += look::text(p, x, 22.5, sp.t, sp.f, sp.c);
    x += 40;
  }
  if (isVisible()) startAnim(true);
}

void SquawkPanel::paintEvent(QPaintEvent* e) {
  QPainter p(this);
  p.setClipRegion(e->region());
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF R(rect());
  look::panel(p, R, theme::map("#020202"));
  const qreal W = width(), hh = headerH(), hc = hh / 2.0 + 1;

  // ---- header (each part is painted only when the repaint touches it: the wire scrolls at 30 fps)
  if (e->region().intersects(QRect(0, 0, width(), int(hh) + 1))) {
    QPainterPath wave;
    wave.moveTo(13, hc); wave.cubicTo(15, hc - 3, 16, hc + 3, 18, hc); wave.cubicTo(19, hc - 2, 20, hc + 2, 21, hc);
    p.setPen(QPen(theme::map("#6b6045"), 1)); p.setBrush(Qt::NoBrush); p.drawPath(wave);
    p.setPen(Qt::NoPen); p.setBrush(theme::map("#fcd700")); p.drawEllipse(QPointF(27.5, hc), 2.6, 2.6);
    look::text(p, 38, hc + 6, "SQUAWK", theme::mono(16, QFont::Bold, 1.5), theme::map("#f7db85"));
    const QRectF lt(112, hc - 9, 33, 17);
    p.setPen(Qt::NoPen);   // flat tag, no border (the text call above left a pen set)
    p.setBrush(m_feed.live ? theme::map("#36320f") : theme::map("#1b1a14"));
    p.drawRoundedRect(lt, 3, 3);
    look::textCenter(p, lt.center().x(), lt.center().y() + 3.5, QCoreApplication::translate("global", "LIVE"), theme::mono(10, QFont::Bold),
                     m_feed.live ? theme::map("#e6cb4a") : theme::map("#5e5b50"));
    // description, wrapped with a 19 px pitch
    const QFont df = theme::condensed(12, QFont::Medium);
    const QStringList lines = descLines();
    qreal y = hc - (lines.size() - 1) * 19 / 2.0 + 4.5;
    for (const auto& l : lines) { look::text(p, 151, y, l, df, theme::map("#6b6f71")); y += 19; }
    if (showAutoLabel()) {
      const QFont af = theme::mono(10, QFont::Normal, 0.8);
      look::text(p, W - 182, hc - 5, QCoreApplication::translate("global", "AUTO-"), af, theme::map("#646464"));
      look::text(p, W - 182, hc + 14, QCoreApplication::translate("global", "TRANSLATED"), af, theme::map("#646464"));
    }
    // translate toggle
    const QRectF br = buttonRect();
    p.setPen(QPen(m_fr ? theme::map("#4a3f10") : m_hoverButton ? theme::map("#5c5c5c") : theme::map("#1b1b1b"), 1));
    p.setBrush(m_fr ? theme::map("#2c2508") : theme::map("#060606"));
    p.drawRoundedRect(br.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    const QColor tc = m_fr ? theme::map("#ffe477") : theme::map("#9a9a9a");
    look::text(p, br.left() + 8, br.center().y() + 1, QString::fromUtf8("文"), theme::sans(7), tc);
    look::text(p, br.left() + 12, br.center().y() + 5, "A", theme::mono(8, QFont::Bold), tc);
    look::text(p, br.left() + 22, br.center().y() + 4.5, m_fr ? "FR" : "EN", theme::mono(12, QFont::Bold), tc);
    // item count
    const QFont cf = theme::mono(13, QFont::Normal, 1);
    look::text(p, W - 54, hc - 3, m_feed.items.isEmpty() ? "—" : QString::number(m_feed.items.size()), cf, theme::map("#626564"));
    look::text(p, W - 54, hc + 16, QCoreApplication::translate("global", "ITEMS"), cf, theme::map("#626564"));
    p.fillRect(QRectF(1, hh, W - 2, 1), theme::map("#101010"));
  }

  // ---- wire
  const QRectF wr = wireRect();
  if (e->region().intersects(wr.toAlignedRect())) {
    p.save();
    p.setClipRect(wr, Qt::IntersectClip);
    p.fillRect(wr, theme::map("#020202"));
    if (m_wireW > 0) {
      const qreal dpr = devicePixelRatioF();
      qreal x = wr.left() - std::round(m_wireOff * dpr) / dpr;
      while (x < wr.right()) { p.drawPixmap(QPointF(x, wr.top()), m_wire); x += m_wireW; }
    } else {
      look::text(p, 14, wr.center().y() + 4, m_feed.status.isEmpty() ? QCoreApplication::translate("global", "CONNECTING TO THE WIRE…") : m_feed.status.toUpper(),
                 theme::mono(10, QFont::Normal, 2.5), theme::map("#4c4e4e"));
    }
    p.restore();
  }
  p.fillRect(QRectF(1, wr.bottom(), W - 2, 1), theme::map("#101010"));

  // ---- filter tabs
  if (e->region().intersects(tabsRect().toAlignedRect())) {
    const QStringList labels{QCoreApplication::translate("global", "ALL"), QCoreApplication::translate("global", "DATA"), QCoreApplication::translate("global", "◇ MOVERS"), QCoreApplication::translate("global", "RELEASES")};
    QList<int> counts{int(m_feed.items.size()), 0, 0, 0};
    for (const auto& it : m_feed.items) counts[1 + int(it.kind)]++;
    const auto rs = tabRects();
    const bool compact = compactTabs();
    const QFont lf = theme::mono(compact ? 11 : 12, QFont::DemiBold), cf = theme::mono(compact ? 11 : 12);
    const qreal tpad = compact ? 5 : 8;
    for (int i = 0; i < 4; ++i) {
      const bool on = i == m_tab;
      if (on) { p.setPen(Qt::NoPen); p.setBrush(theme::map("#332b03")); p.drawRoundedRect(rs[i], 3, 3); }
      const qreal base = rs[i].center().y() + 4.5;
      const QColor lc = on ? theme::map("#ffe278") : i == m_hoverTab ? theme::map("#c9c6bc") : theme::map("#83878a");
      const qreal w = look::text(p, rs[i].left() + tpad, base, labels[i], lf, lc);
      if (!m_feed.items.isEmpty())
        look::text(p, rs[i].left() + tpad + w + 5, base, QString::number(counts[i]), cf, on ? theme::map("#66614a") : theme::map("#646367"));
    }
  }

  // ---- feed list
  const QRectF lr = listRect();
  if (e->region().intersects(lr.toAlignedRect())) {
  p.save();
  p.setClipRect(lr, Qt::IntersectClip);
  p.fillRect(QRectF(0, lr.top(), 1, lr.height()), theme::map("#6b6045"));
  p.fillRect(QRectF(1, lr.top(), 1, lr.height()), theme::map("#4e3e03"));
  const auto idx = filtered();
  if (idx.isEmpty()) {
    const QString msg = m_feed.items.isEmpty() ? (m_feed.status.isEmpty() ? QCoreApplication::translate("global", "CONNECTING…") : m_feed.status.toUpper()) : QCoreApplication::translate("global", "NO ITEM IN THIS FILTER");
    look::textCenter(p, lr.center().x(), lr.top() + 60, msg, theme::mono(10, QFont::Normal, 2.5), theme::map("#4c4e4e"));
  }
  const QFont tf = theme::mono(13), af = theme::mono(12), xf = theme::condensed(15, QFont::Medium);
  const QDateTime now = QDateTime::currentDateTime();
  qreal y = lr.top() - m_scroll;
  const auto& wrapped = wrappedLines();
  for (int k = 0; k < idx.size() && y < lr.bottom(); ++k) {
    const auto& it = m_feed.items[idx[k]];
    const QStringList& lines = wrapped[idx[k]];
    const qreal h = lines.size() * 21 + 24;
    if (y + h >= lr.top()) {
      p.fillRect(QRectF(2, y, W - 3, h), k == m_hoverRow ? theme::map("#111111") : theme::map("#010101"));
      const qreal b0 = y + 12 + 15;
      look::text(p, 16, b0, it.time.toLocalTime().toString("HH:mm:ss"), tf, theme::map("#8c8f8e"));
      look::text(p, 89, b0, ago(it.time, now), af, theme::map("#575b5c"));
      const QPointF ic(feedTextX() - 16, b0 - 5);
      p.setPen(Qt::NoPen);
      if (it.kind == SquawkItem::Headline) {
        p.setBrush(theme::map("#f0db8f")); p.drawEllipse(ic, 6, 6);
        p.setPen(QPen(theme::map("#5a4a10"), 1.2)); p.setBrush(Qt::NoBrush); p.drawEllipse(ic, 3, 3);
      } else if (it.kind == SquawkItem::Mover) {
        const QColor c = it.changePct >= 0 ? theme::map("#4fd1da") : theme::map("#e0666b");
        p.setPen(QPen(c, 1.3)); p.setBrush(Qt::NoBrush);
        p.drawPolygon(QPolygonF{ic + QPointF(0, -5), ic + QPointF(5, 0), ic + QPointF(0, 5), ic + QPointF(-5, 0)});
      } else {
        p.setBrush(theme::map("#4fd1da"));
        p.drawRect(QRectF(ic.x() - 5, ic.y() + 1, 2.5, 4)); p.drawRect(QRectF(ic.x() - 1, ic.y() - 2, 2.5, 7));
        p.drawRect(QRectF(ic.x() + 3, ic.y() - 5, 2.5, 10));
      }
      qreal ly = b0;
      for (const auto& l : lines) { look::text(p, feedTextX(), ly, l, xf, theme::map("#e8e6e4")); ly += 21; }
      p.fillRect(QRectF(2, y + h - 1, W - 3, 1), theme::map("#171717"));
    }
    y += h;
  }
  p.restore();
  }

  // ---- footer
  if (e->region().intersects(QRect(0, height() - 32, width(), 32))) {
  const qreal fy = height() - 31;
  p.fillRect(QRectF(1, fy, W - 2, 1), theme::map("#1a1a1a"));
  look::text(p, 7, height() - 12, QCoreApplication::translate("global", "Terminal Data · %1").arg(m_feed.newest.isValid() ? m_feed.newest.toLocalTime().toString("HH:mm:ss") : fmt::dash()),
             theme::sans(10), theme::map("#656867"));
  const QRectF lb(W - 58, height() - 26, 46, 20);
  p.setPen(QPen(m_feed.live ? theme::map("#968612") : theme::map("#3a3833"), 1));
  p.setBrush(m_feed.live ? theme::map("#342a03") : theme::map("#12110d"));
  p.drawRoundedRect(lb.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
  const QColor lc = m_feed.live ? theme::map("#f8e169") : theme::map("#5e5b50");
  p.setPen(Qt::NoPen); p.setBrush(lc); p.drawEllipse(QPointF(lb.left() + 7, lb.center().y()), 2, 2);
  look::text(p, lb.left() + 12, lb.center().y() + 4, QCoreApplication::translate("global", "LIVE"), theme::mono(11, QFont::Bold), lc);
  }
}

void SquawkPanel::mouseMoveEvent(QMouseEvent* e) {
  const QPointF pos = e->position();
  const bool hb = buttonRect().contains(pos);
  int ht = -1; const auto tr = tabRects();
  for (int i = 0; i < tr.size(); ++i) if (tr[i].contains(pos)) ht = i;
  int hr = -1;
  const QRectF lr = listRect();
  if (lr.contains(pos)) {
    const auto idx = filtered();
    const auto& wrapped = wrappedLines();
    qreal y = lr.top() - m_scroll;
    for (int k = 0; k < idx.size() && y < lr.bottom(); ++k) {
      const qreal h = wrapped[idx[k]].size() * 21 + 24;
      if (pos.y() >= y && pos.y() < y + h) { hr = k; break; }
      y += h;
    }
  }
  setCursor(hb || ht >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
  if (hb != m_hoverButton || ht != m_hoverTab || hr != m_hoverRow) {
    m_hoverButton = hb; m_hoverTab = ht; m_hoverRow = hr; update();
  }
}
void SquawkPanel::leaveEvent(QEvent*) { m_hoverButton = false; m_hoverTab = m_hoverRow = -1; update(); }
void SquawkPanel::mousePressEvent(QMouseEvent* e) {
  if (buttonRect().contains(e->position())) {
    m_fr = !m_fr;
    settings::setValue(kSettingFr, m_fr);
    if (onTranslate) onTranslate(m_fr);
    buildWire(); update();
    return;
  }
  const auto tr = tabRects();
  for (int i = 0; i < tr.size(); ++i)
    if (tr[i].contains(e->position()) && i != m_tab) {
      m_tab = i; m_scroll = 0; settings::setValue(kSettingTab, i); update(); return;
    }
}
bool SquawkPanel::event(QEvent* e) {
  if (e->type() == QEvent::ToolTip) {
    auto* he = static_cast<QHelpEvent*>(e);
    QString tip;
    if (buttonRect().contains(he->pos())) tip = QCoreApplication::translate("global", "EN / FR: machine translation of the headlines (on this Mac).") + "\n" + m_feed.translateNote;
    else if (he->pos().y() > height() - 31) tip = m_feed.sourceNote;
    else if (he->pos().y() < headerH() && he->pos().x() >= 151 && he->pos().x() < buttonRect().left()) {
      descLines();
      if (m_descCut || shortHeader())   // cut description; the AUTO-TRANSLATED label is dropped on short panels
        tip = QCoreApplication::translate("global", "DATA headlines + real-time price moves + fresh data releases")
              + (m_fr ? "\n" + QCoreApplication::translate("global", "AUTO-") + QCoreApplication::translate("global", "TRANSLATED") : QString());
    }
    if (tip.trimmed().isEmpty()) QToolTip::hideText(); else QToolTip::showText(he->globalPos(), tip.trimmed(), this);
    return true;
  }
  return QWidget::event(e);
}

void SquawkPanel::wheelEvent(QWheelEvent* e) {
  // at either end of the list the wheel goes on to the page scroller (small windows scroll the page)
  if (!listRect().contains(e->position())) { e->ignore(); return; }
  qreal content = 0;
  const auto& wrapped = wrappedLines();
  for (int k : filtered()) content += wrapped[k].size() * 21 + 24;
  const qreal maxScroll = qMax<qreal>(0, content - listRect().height());
  const qreal d = e->pixelDelta().y() != 0 ? e->pixelDelta().y() : e->angleDelta().y() / 2.0;
  const qreal next = qBound<qreal>(0, m_scroll - d, maxScroll);
  if (next == m_scroll) { e->ignore(); return; }
  m_scroll = next;
  update(listRect().toAlignedRect());
}

// ================================================================= Risk Sentiment
static QColor zoneColor(double v) { return v < 40 ? theme::map("#f2706e") : v <= 60 ? theme::map("#f7d950") : theme::map("#35cde4"); }
static QString zoneWord(double v) { return v < 40 ? QCoreApplication::translate("global", "RISK-OFF") : v <= 60 ? QCoreApplication::translate("global", "NEUTRAL") : QCoreApplication::translate("global", "RISK-ON"); }

RiskPanel::RiskPanel(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent);
  m_r.status = QCoreApplication::translate("global", "CONNECTING…");
}
void RiskPanel::setReading(const RiskReading& r) { m_r = r; update(); }

bool RiskPanel::event(QEvent* e) {
  if (e->type() == QEvent::ToolTip) {
    auto* he = static_cast<QHelpEvent*>(e);
    const qreal w = look::width(QCoreApplication::translate("global", "RISK SENTIMENT"), look::titleFont());
    if (QRectF(11 + w, 9, 14, 16).contains(he->pos()))
      QToolTip::showText(he->globalPos(), m_r.method.isEmpty() ? QCoreApplication::translate("global", "Risk appetite index 0–100 (risk-off < 40, neutral 40–60, risk-on > 60).\nWaiting for the risk index service.") : m_r.method, this);
    else QToolTip::hideText();
    return true;
  }
  return QWidget::event(e);
}

void RiskPanel::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF R(rect());
  const qreal W = width();
  look::panel(p, R, theme::map("#0d0802"));
  look::panelTitle(p, R, QCoreApplication::translate("global", "RISK SENTIMENT"));
  p.fillRect(QRectF(1, 33, W - 2, 1), theme::map("#181714"));
  const bool have = std::isfinite(m_r.value);

  // ---- gauge (designed for a 245 px panel; scaled down on short windows). A phone upright (stacked): the gauge
  // centred on top at full size, the history below it across the panel.
  const bool stacked = W < 520;
  const qreal gs = stacked ? 1.0 : qBound(0.6, (height() - 33) / 212.0, 1.0);
  const qreal hx = stacked ? 11 : 297 * gs;    // the history's left edge (its label)
  const qreal hy = stacked ? 218 : 0;          // and how far down it starts
  const qreal ly = stacked ? 22 : 0;           // stacked: the legend on its own line under the label
  p.save();
  if (stacked) p.translate(W / 2 - 145, 0);
  p.translate(0, 33); p.scale(gs, gs); p.translate(0, -33);
  const QPointF hub(144, 132);
  auto at = [&](qreal r, qreal v) {   // v 0..100 → 240° dial, 50 at the top
    const qreal a = qDegreesToRadians(-120 + v * 2.4);
    return QPointF(hub.x() + r * std::sin(a), hub.y() - r * std::cos(a));
  };
  p.setPen(QPen(theme::map("#3c382f"), 1)); p.setBrush(Qt::NoBrush);
  p.drawArc(QRectF(hub.x() - 86, hub.y() - 86, 172, 172), int((90 + 120) * 16), int(-240 * 16));
  const QFont nf = theme::mono(6);
  for (int v = 0; v <= 100; v += 2) {   // fine graduations every 2 points, yellow ticks every 10
    if (v % 10) { p.setPen(QPen(theme::map("#3e3a2a"), 1)); p.drawLine(at(85, v), at(87.5, v)); continue; }
    p.setPen(QPen(theme::map("#c8a93a"), 1.2));
    p.drawLine(at(83, v), at(89, v));
    const QPointF l = at(96, v);
    look::textCenter(p, l.x(), l.y() + 2, QString::number(v), nf, theme::map("#6e6a5f"));
  }
  for (int i = 0; i < 50; ++i) {
    const double v = i * 2 + 1;
    const bool lit = have && v <= m_r.value;
    QColor c;
    if (lit) c = zoneColor(m_r.value) == theme::map("#f2706e") ? theme::map("#e87f78") : zoneColor(m_r.value);
    else c = v < 40 ? theme::map("#3a1d1a") : v <= 60 ? theme::map("#433e27") : theme::mix(theme::map("#253127"), theme::map("#1c3a3c"), (v - 60) / 40);
    if (lit) { QPen g(theme::alpha(c, 50), 5); g.setCapStyle(Qt::FlatCap); p.setPen(g); p.drawLine(at(56, v), at(78, v)); }
    QPen pen(c, 2.4); pen.setCapStyle(Qt::FlatCap);
    p.setPen(pen);
    p.drawLine(at(56, v), at(78, v));
  }
  // zone words inside the ring, along the arc (tiny, dim), and the 40 / 60 boundaries
  auto arcWord = [&](const QString& w, double v, const QColor& c) {
    p.save();
    const QPointF at50 = at(50, v);
    p.translate(at50);
    p.rotate(-120 + v * 2.4);
    look::textCenter(p, 0, 2, w, theme::mono(5, QFont::Normal, 1), c);
    p.restore();
  };
  arcWord(QCoreApplication::translate("global", "RISK-OFF"), 18, theme::map("#5a3431"));
  arcWord(QCoreApplication::translate("global", "NEUTRAL"), 50, theme::map("#5c5740"));
  arcWord(QCoreApplication::translate("global", "RISK-ON"), 82, theme::map("#2f4a4a"));
  for (int v : {40, 60}) {
    p.setPen(QPen(theme::map("#6e6a5f"), 1));
    p.drawLine(at(53, v), at(49, v));
    const QPointF l = at(44, v);
    look::textCenter(p, l.x(), l.y() + 2, QString::number(v), theme::mono(5), theme::map("#6e6a5f"));
  }
  if (have) {
    const QColor zc = zoneColor(m_r.value);
    const QColor nc = theme::mix(zc, theme::map("#ffffff"), 0.45);
    look::glowLine(p, hub, at(62, m_r.value), nc, 2, 5, 70);
    p.setPen(QPen(nc, 2)); p.drawLine(hub, at(62, m_r.value));
    p.setPen(QPen(theme::mix(zc, theme::map("#ffffff"), 0.2), 2)); p.setBrush(theme::map("#050302"));
    p.drawEllipse(hub, 4.5, 4.5);
    const QFont vf = theme::mono(22, QFont::Bold);
    const QString vs = fmt::whole(m_r.value);
    const QColor vc = theme::mix(zc, theme::map("#ffffff"), 0.25);
    QPainterPath path; const qreal vw = look::width(vs, vf);
    path.addText(QPointF(hub.x() + 1 - vw / 2, hub.y() + 25), vf, vs);
    QRadialGradient halo(QPointF(hub.x() + 1, hub.y() + 17), 26);   // soft glow behind the value
    halo.setColorAt(0, theme::alpha(zc, 70)); halo.setColorAt(1, theme::alpha(zc, 0));
    p.setPen(Qt::NoPen); p.setBrush(halo); p.drawEllipse(QPointF(hub.x() + 1, hub.y() + 17), 26, 26);
    p.setBrush(vc); p.drawPath(path);
    look::textCenter(p, hub.x() + 1, hub.y() + 37, zoneWord(m_r.value), theme::mono(7, QFont::Bold, 1.5), theme::mix(zc, theme::map("#806060"), 0.4));
  } else {
    p.setPen(QPen(theme::map("#3c382f"), 2)); p.setBrush(theme::map("#050302"));
    p.drawEllipse(hub, 4.5, 4.5);
    look::textCenter(p, hub.x() + 1, hub.y() + 25, fmt::dash(), theme::mono(22, QFont::Bold), theme::map("#4c4a44"));
    look::textCenter(p, hub.x() + 1, hub.y() + 37, m_r.status.isEmpty() ? QCoreApplication::translate("global", "NO DATA") : m_r.status.toUpper(), theme::mono(7, QFont::Normal, 1.5), theme::map("#5e5b50"));
  }
  // change chips
  auto chip = [&](const QRectF& r, const QString& tag, double then) {
    p.setPen(QPen(theme::map("#262219"), 1)); p.setBrush(theme::map("#0a0806"));
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);
    const QFont f = theme::mono(12), fw = theme::mono(12, QFont::Normal, 1.5);
    qreal x = r.left() + 13; const qreal b = r.center().y() + 4.5;
    x += look::text(p, x, b, tag, f, theme::map("#908d86")) + 10;
    if (!have || !std::isfinite(then)) { look::text(p, x, b, fmt::dash(), f, theme::map("#676257")); return; }
    const double d = m_r.value - then;
    const bool down = fmt::roundInt(d) < 0;
    const QColor tc = down ? theme::map("#ff908f") : theme::map("#5fd3c8");
    p.setPen(Qt::NoPen); p.setBrush(tc);
    const QPointF c(x + 5, r.center().y());
    if (down) p.drawPolygon(QPolygonF{c + QPointF(-5, -4), c + QPointF(5, -4), c + QPointF(0, 5)});
    else p.drawPolygon(QPolygonF{c + QPointF(-5, 4), c + QPointF(5, 4), c + QPointF(0, -5)});
    x += 18;
    x += look::text(p, x, b, QString::number(std::abs(fmt::roundInt(d))), f, down ? theme::map("#c68285") : theme::map("#6fb7b0")) + 9;
    look::text(p, x, b, QCoreApplication::translate("global", "(WAS %1)").arg(fmt::roundInt(then)), fw, theme::map("#676257"));
  };
  chip(QRectF(72, 189, 146, 21), QCoreApplication::translate("global", "1W"), m_r.weekAgo);
  chip(QRectF(68, 218, 153, 21), QCoreApplication::translate("global", "1M"), m_r.monthAgo);

  p.restore();
  // ---- history · 1 year
  const QRectF pl(hx + 7, 75 + hy + ly, W - 57 - hx - 7, height() - 75 - hy - ly - 32);
  const QFont lf = theme::mono(10, QFont::Normal, 2.5);
  if (stacked) p.fillRect(QRectF(11, 33 + hy - 4, W - 22, 1), theme::map("#181714"));
  const qreal labelRight = stacked ? 0 : hx + look::width(QCoreApplication::translate("global", "HISTORY · 1 YEAR"), lf);
  look::text(p, hx, 44 + hy, QCoreApplication::translate("global", "HISTORY · 1 YEAR"), lf, theme::map("#605c55"));
  {
    struct L { QString t; QColor c; };
    const QList<L> leg{{QCoreApplication::translate("global", "RISK-OFF < 40"), theme::map("#d48a8f")}, {QCoreApplication::translate("global", "NEUTRAL 40-60"), theme::map("#fbde84")}, {QCoreApplication::translate("global", "RISK-ON > 60"), theme::map("#76cad1")}};
    QList<L> legend = leg;
    qreal ls = 2;
    auto total = [&](qreal s) { qreal w = 0; for (const auto& l : legend) w += 11 + look::width(l.t, theme::mono(10, QFont::Normal, s)) + 12; return w - 12; };
    while (ls > 0.4 && W - 15 - total(ls) < labelRight + 12) ls -= 0.4;
    if (W - 15 - total(ls) < labelRight + 12) { legend[0].t = QCoreApplication::translate("global", "RISK-OFF"); legend[1].t = QCoreApplication::translate("global", "NEUTRAL"); legend[2].t = QCoreApplication::translate("global", "RISK-ON"); }
    const auto& leg2 = legend;
    qreal x = W - 15 - total(ls);
    for (const auto& l : leg2) {
      p.fillRect(QRectF(x, 44.5 + hy + ly, 7, 7), l.c);
      x += 11;
      x += look::text(p, x, 51.5 + hy + ly, l.t, theme::mono(10, QFont::Normal, ls), l.c) + 12;
    }
  }
  for (int z = 0; z < 5; ++z) {
    const qreal y0 = pl.bottom() - (z + 1) * 20 / 100.0 * pl.height();
    p.fillRect(QRectF(pl.left(), y0, pl.width(), pl.height() / 5), theme::zoneBand(z));
  }
  p.save(); p.setRenderHint(QPainter::Antialiasing, false);
  for (int v : {40, 60}) {
    const qreal y = std::round(pl.bottom() - v / 100.0 * pl.height()) + 0.5;
    p.setPen(QPen(theme::map("#2a2618"), 1)); p.drawLine(QPointF(pl.left(), y), QPointF(pl.right(), y));
  }
  { const qreal y = std::round(pl.bottom() - 0.5 * pl.height()) + 0.5;
    QPen d(theme::map("#2a2618"), 1); d.setDashPattern({1, 3}); p.setPen(d); p.drawLine(QPointF(pl.left(), y), QPointF(pl.right(), y)); }
  p.restore();
  const QFont yf = theme::sans(11);
  for (int v = 0; v <= 100; v += 20)
    look::textRight(p, W - 27, pl.bottom() - v / 100.0 * pl.height() + 4, QString::number(v), yf, theme::map("#8a8780"));
  // months: the last 12 month starts, at their date position
  const QDate end = m_r.history.isEmpty() ? QDate::currentDate() : m_r.history.last().first;
  const QDate start = m_r.history.isEmpty() ? end.addYears(-1) : m_r.history.first().first;
  const double span = qMax<qint64>(1, start.daysTo(end));
  const QLocale en = i18n::locale();
  auto monthName = [&](const QDate& d) { return en.toString(d, "MMM").remove('.').toUpper(); };
  qreal widest = 0;
  for (int k = 1; k <= 12; ++k) widest = qMax(widest, look::width(monthName(QDate(2026, k, 1)), yf));
  const int every = pl.width() / 12 < widest + 4 ? 2 : 1;      // narrow windows / long names: every other month
  int mi = 0;
  for (QDate m(start.year(), start.month(), 1); m <= end; m = m.addMonths(1)) {
    if (m < start) continue;
    if (mi++ % every) continue;
    const qreal x = pl.left() + start.daysTo(m) / span * pl.width();
    look::textCenter(p, x + 10, pl.bottom() + 19, monthName(m), yf, theme::map("#938f83"));
  }
  if (m_r.history.size() > 1) {
    auto pt = [&](int i) {
      return QPointF(pl.left() + start.daysTo(m_r.history[i].first) / span * pl.width(),
                     pl.bottom() - qBound(0.0, m_r.history[i].second, 100.0) / 100.0 * pl.height());
    };
    for (int pass = 0; pass < 2; ++pass)
      for (int i = 1; i < m_r.history.size(); ++i) {
        const QColor c = zoneColor((m_r.history[i - 1].second + m_r.history[i].second) / 2);
        QPen pen(pass == 0 ? theme::alpha(c, 70) : c, pass == 0 ? 5 : 1.7);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen); p.drawLine(pt(i - 1), pt(i));
      }
  } else {
    look::textCenter(p, pl.center().x(), pl.center().y() + 4, m_r.status.isEmpty() ? QCoreApplication::translate("global", "NO HISTORY") : m_r.status.toUpper(),
                     theme::mono(10, QFont::Normal, 2.5), theme::map("#6b675d"));
  }
}

// ================================================================= Central Banks
CentralBanksPanel::CentralBanksPanel(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent);
  // the eight banks of the majors: names only until the rates service answers
  const QList<QStringList> banks{{"Fed", "USD", "us"}, {"ECB", "EUR", "eu"}, {"BoE", "GBP", "gb"}, {"BoJ", "JPY", "jp"},
                                 {"RBA", "AUD", "au"}, {"RBNZ", "NZD", "nz"}, {"BoC", "CAD", "ca"}, {"SNB", "CHF", "ch"}};
  for (const auto& b : banks) { CentralBankRow r; r.name = b[0]; r.currency = b[1]; r.flag = b[2]; m_b.rows << r; }
  m_b.status = QCoreApplication::translate("global", "CONNECTING…");
}
void CentralBanksPanel::setBanks(const CentralBanks& b) { m_b = b; update(); }

bool CentralBanksPanel::event(QEvent* e) {
  if (e->type() == QEvent::ToolTip) {
    auto* he = static_cast<QHelpEvent*>(e);
    const qreal pitch = qMin<qreal>(24.4 + qMax(0, height() - 262) / 8.0, (height() - 88) / qMax(1, int(m_b.rows.size()) - 1));
    const int i = int(std::floor((he->pos().y() - 74 + pitch / 2) / pitch));
    QString tip;
    if (i >= 0 && i < m_b.rows.size() && he->pos().y() > 62) {
      const auto& r = m_b.rows[i];
      const qreal x = he->pos().x();
      if (x >= width() - 190) tip = r.oddsSource;
      else if (x >= (width() < 700 ? 140 : 180) && x < (width() < 700 ? 232 : 290)) tip = r.rateSource;
    }
    if (tip.isEmpty()) QToolTip::hideText(); else QToolTip::showText(he->globalPos(), tip, this);
    return true;
  }
  return QWidget::event(e);
}

void CentralBanksPanel::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF R(rect());
  const qreal W = width();
  look::panel(p, R, theme::map("#0d0802"));
  look::panelTitle(p, R, QCoreApplication::translate("global", "CENTRAL BANKS"));
  if (!m_b.status.isEmpty())
    look::textRight(p, W - 14, 21, m_b.status.toUpper(), theme::mono(9, QFont::Normal, 2), theme::map("#5e5b50"));
  const QFont hf = theme::mono(9, QFont::Normal, 0.8);
  const qreal hy = 53;
  const bool narrow = W < 700;   // compact windows: rate and meeting columns move left
  const bool phone = W < 520;    // a phone upright: the meeting without its year, a short header
  const qreal xRate = phone ? 176 : narrow ? 212 : 267, xNext = phone ? 190 : narrow ? 240 : 329, xCut = W - 139, xHold = W - 74, xHike = W - 11;
  const QColor hc = theme::map("#66635b");
  look::text(p, 11, hy, QCoreApplication::translate("global", "CENTRAL BANK"), hf, hc);
  look::textRight(p, xRate - 14, hy, QCoreApplication::translate("global", "RATE"), hf, hc);
  if (!phone) look::text(p, xNext, hy, QCoreApplication::translate("global", "NEXT MEETING"), hf, hc);   // phone: no room, the dates speak
  look::textRight(p, xCut, hy, QCoreApplication::translate("global", "CUT"), hf, hc);
  look::textRight(p, xHold, hy, QCoreApplication::translate("global", "HOLD"), hf, hc);
  look::textRight(p, xHike, hy, QCoreApplication::translate("global", "HIKE"), hf, hc);
  const qreal pitch = qMin<qreal>(24.4 + qMax(0, height() - 262) / 8.0, (height() - 88) / qMax(1, int(m_b.rows.size()) - 1));
  const QFont nf = theme::sans(12), vf = theme::mono(12), bf = theme::mono(12, QFont::Bold);
  const QLocale en = i18n::locale();
  // 1 px divider under the title row, faint separators between table rows (measured #181408 / #1b170d)
  p.fillRect(QRectF(1, 33, W - 2, 1), theme::map("#181408"));
  for (int i = 0; i < m_b.rows.size(); ++i)
    p.fillRect(QRectF(11, std::round(74 + i * pitch - pitch / 2), W - 22, 1), theme::map("#16120a"));
  for (int i = 0; i < m_b.rows.size(); ++i) {
    const auto& r = m_b.rows[i];
    const qreal cy = 74 + i * pitch, b = cy + 4.5;
    p.drawPixmap(QPointF(11, cy - 6), ui::flag(r.flag, QSize(18, 12), devicePixelRatioF()));
    const qreal w = look::text(p, 35, b, r.name, nf, theme::map("#e7e6e1"));
    look::text(p, 35 + w + 2, b, "(" + r.currency + ")", nf, theme::map("#73706a"));
    const QColor dash = theme::map("#4c4a44");
    look::textRight(p, xRate, b, std::isfinite(r.rate) ? fmt::num(r.rate, 2) + "%" : fmt::dash(), bf,
                    std::isfinite(r.rate) ? theme::map("#fffffc") : dash);
    look::text(p, xNext, b, r.nextMeeting.isValid() ? en.toString(r.nextMeeting, phone ? "dd MMM" : "dd MMM yyyy") : fmt::dash(), vf,
               r.nextMeeting.isValid() ? theme::map("#f5f2ed") : dash);
    auto prob = [&](double v, int dp) { return std::isfinite(v) ? fmt::num(v, dp) + "%" : fmt::dash(); };
    // the most likely outcome in bold (ties to hold, as macro::Bank::action()); all three with 0 decimals
    const double cu = std::isfinite(r.cut) ? r.cut : -1, ho = std::isfinite(r.hold) ? r.hold : -1, hi = std::isfinite(r.hike) ? r.hike : -1;
    const bool hikeMax = std::isfinite(r.hike) && hi > ho && hi >= cu;
    const bool cutMax = std::isfinite(r.cut) && !hikeMax && cu > ho && cu > hi;
    const bool holdMax = std::isfinite(r.hold) && !hikeMax && !cutMax;
    look::textRight(p, xCut, b, prob(r.cut, 0), cutMax ? bf : vf,
                    !std::isfinite(r.cut) ? dash : cutMax ? theme::map("#e0666b") : theme::map("#7e6261"));
    look::textRight(p, xHold, b, prob(r.hold, 0), holdMax ? bf : vf,
                    !std::isfinite(r.hold) ? dash : holdMax ? theme::map("#fffffa") : theme::map("#adaaa4"));
    look::textRight(p, xHike, b, prob(r.hike, 0), hikeMax ? bf : vf,
                    !std::isfinite(r.hike) ? dash : hikeMax ? theme::map("#65cfe0") : theme::map("#4f7e81"));
  }
}

// ================================================================= Asset Registry
static const QStringList kRegions{"G20", "AMERICA", "EUROPE", "AFRICA", "ASIA", "OCEANIA", "WATCH"};   // settings values
static const char* const kRegionLabels[] = {QT_TRANSLATE_NOOP("global", "★ G20"), QT_TRANSLATE_NOOP("global", "AMERICA"),
  QT_TRANSLATE_NOOP("global", "EUROPE"), QT_TRANSLATE_NOOP("global", "AFRICA"), QT_TRANSLATE_NOOP("global", "ASIA"),
  QT_TRANSLATE_NOOP("global", "OCEANIA"), QT_TRANSLATE_NOOP("global", "☆ WATCH")};
static QString regionLabel(int i) { return QCoreApplication::translate("global", kRegionLabels[i]); }
static const char* kSettingRegion = "global.overview.registryRegion";

RegistryPanel::RegistryPanel(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent);
  setMouseTracking(true);
  m_tab = qBound(0, int(kRegions.indexOf(settings::value(kSettingRegion, "G20").toString())), int(kRegions.size()) - 1);
  m_reg.status = QCoreApplication::translate("global", "CONNECTING…");
}
void RegistryPanel::setRegistry(const Registry& r) {   // keeps the scroll (a ☆ toggled pushes the registry again)
  m_reg = r;
  m_scroll = qBound<qreal>(0, m_scroll, qMax<qreal>(0, rows().size() * 31.7 - (height() - 64)));
  update();
}

QRect RegistryPanel::cursorRect() const {
  const qreal x = 30 + look::width(QCoreApplication::translate("global", "ASSET REGISTRY"), theme::mono(10, QFont::Normal, 2.2));
  return QRectF(x, 10, 9, 14).toAlignedRect();
}
// seven tabs on one line: the labels' padding shrinks (8 → 2 px) when the panel is narrow
QList<QRectF> RegistryPanel::tabRects() const {
  QList<QRectF> out;
  const QFont f = theme::mono(10, QFont::Normal, 1);
  qreal sum = 0;
  for (int i = 0; i < kRegions.size(); ++i) sum += look::width(regionLabel(i), f);
  const qreal pad = qBound<qreal>(2, (width() - 12 - sum - 2 * (kRegions.size() - 1)) / kRegions.size(), 8);
  qreal x = 6;
  for (int i = 0; i < kRegions.size(); ++i) {
    const qreal w = look::width(regionLabel(i), f) + pad;
    out << QRectF(x, 38, w, 18);
    x += w + 2;
  }
  return out;
}
int RegistryPanel::rowAt(const QPointF& pos) const {
  if (pos.y() <= 63) return -1;
  const int k = int(std::floor((pos.y() - 63 + m_scroll) / 31.7));
  return k >= 0 && k < rows().size() ? k : -1;
}
QRectF RegistryPanel::starRect(int k) const {
  const auto rs = rows();
  const QString badge = "• " + rs[k].institution.toUpper();
  const qreal bw = look::width(badge, theme::mono(9, QFont::Bold)) + 10;
  const qreal cy = 63 + k * 31.7 - m_scroll + 31.7 / 2;
  return QRectF(width() - 12 - bw - 6 - 26, cy - 10, 20, 20);
}
QList<RegistryRow> RegistryPanel::rows() const { return m_tab < m_reg.tabs.size() ? m_reg.tabs[m_tab] : QList<RegistryRow>{}; }

void RegistryPanel::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF R(rect());
  const qreal W = width();
  p.fillRect(R, theme::map("#0a0802"));
  // header band
  QLinearGradient hb(0, 0, 0, 64);
  hb.setColorAt(0, theme::map("#231e06")); hb.setColorAt(1, theme::map("#120d02"));
  p.fillRect(QRectF(0, 0, W, 64), hb);
  p.setPen(QPen(theme::map("#433d2c"), 1)); p.setBrush(Qt::NoBrush);
  p.save(); p.setRenderHint(QPainter::Antialiasing, false); p.drawRect(R.adjusted(0.5, 0.5, -0.5, -0.5)); p.restore();
  look::brackets(p, R);
  p.setPen(Qt::NoPen); p.setBrush(theme::map("#6b5f26")); p.drawEllipse(QPointF(17.5, 17), 2.5, 2.5);
  look::text(p, 30, 20.5, QCoreApplication::translate("global", "ASSET REGISTRY"), theme::mono(10, QFont::Normal, 2.2), theme::map("#75705f"));
  if (look::cursorOn()) { const QRect c = cursorRect(); look::cursorBlock(p, c.x() + 1, 11, 6, 12); }
  const auto idx = rows();
  look::textRight(p, W - 15, 20.5, m_reg.tabs.isEmpty() ? fmt::dash() : QString::number(idx.size()), theme::mono(10), theme::map("#645c46"));
  // tabs
  const auto tr = tabRects();
  const QFont tf = theme::mono(10, QFont::Normal, 1), tfb = theme::mono(10, QFont::Bold, 1);
  for (int i = 0; i < tr.size(); ++i) {
    const QString t = regionLabel(i);
    if (i == m_tab) { p.setPen(Qt::NoPen); p.setBrush(theme::map("#fcd700")); p.drawRoundedRect(tr[i], 3, 3); }
    look::text(p, tr[i].left() + 4, tr[i].center().y() + 3.5, t, i == m_tab || i == m_hoverTab ? tfb : tf,
               i == m_tab ? theme::onAccent() : i == m_hoverTab ? theme::map("#fcd700") : theme::map("#98948a"));
  }
  // list
  const QRectF lr(0, 63, W, height() - 63 - 1);
  p.save();
  p.setClipRect(lr);
  p.fillRect(QRectF(0, lr.top(), 2, lr.height()), theme::map("#6b6045"));
  if (idx.isEmpty()) {
    if (m_tab == int(macro::Group::Watch) && !m_reg.tabs.isEmpty()) {
      p.setFont(theme::mono(10)); p.setPen(theme::map("#848178"));
      p.drawText(QRectF(16, lr.top() + 20, W - 32, 120), Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap,
                 QCoreApplication::translate("global", "Your watchlist is empty. Add a country with the ☆ of its row in the other tabs."));
    } else
    look::textCenter(p, W / 2, lr.top() + 60, m_reg.tabs.isEmpty() ? (m_reg.status.isEmpty() ? QCoreApplication::translate("global", "NO DATA") : m_reg.status.toUpper()) : QCoreApplication::translate("global", "NO COUNTRY"),
                     theme::mono(10, QFont::Normal, 2.5), theme::map("#4c4e4e"));
  }
  const qreal pitch = 31.7;
  const QFont xf = theme::mono(11), nf = theme::display(13, QFont::Bold, 0.8), bf = theme::mono(9, QFont::Bold);
  for (int k = 0; k < idx.size(); ++k) {
    const auto& r = idx[k];
    const qreal top = lr.top() + k * pitch - m_scroll, cy = top + pitch / 2;
    if (top > lr.bottom()) break;
    if (top + pitch < lr.top()) continue;
    if (r.primary) {   // highlighted row: olive, a little brighter toward the badge (measured #201901 → #2f2604)
      QLinearGradient rg(2, 0, W, 0);
      rg.setColorAt(0, theme::map("#201901")); rg.setColorAt(1, theme::map("#2f2604"));
      p.fillRect(QRectF(2, top, W - 3, pitch), rg);
      p.fillRect(QRectF(0, top, 2, pitch), theme::map("#fae77c"));
    } else {           // World-Bank-only row: dark band fading in toward the right (#010000 → #120d02)
      QLinearGradient rg(2, 0, W, 0);
      rg.setColorAt(0, theme::map("#030201")); rg.setColorAt(1, theme::map("#140f03"));
      p.fillRect(QRectF(2, top, W - 3, pitch), rg);
    }
    look::text(p, 12, cy + 4, QString("%1").arg(k + 1, 2, 10, QChar('0')), xf, theme::map("#615c45"));
    p.drawPixmap(QPointF(33, cy - 7.5), macro::flag(r.iso2, QSize(22, 15), devicePixelRatioF()));
    look::text(p, 63, cy + 4.5, r.name.toUpper(), nf, theme::map("#fffff1"));
    const QString badge = "• " + r.institution.toUpper();
    const qreal bw = look::width(badge, bf) + 10;
    const QRectF br(W - 12 - bw - 6, cy - 9, bw + 6, 18);
    p.setPen(QPen(r.primary ? theme::map("#8a7a2a") : theme::map("#2a2614"), 1));
    p.setBrush(r.primary ? theme::map("#594c01") : theme::map("#141002"));
    p.drawRoundedRect(br.adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);
    look::text(p, br.left() + 8, br.center().y() + 3.5, badge, bf, r.primary ? theme::map("#fbebaa") : theme::map("#4e4b40"));
    // ☆ watchlist: filled when listed, an outline on the hovered row
    const bool listed = macro::Watchlist::instance().contains(r.iso2);
    if (listed || k == m_hoverRow) {
      const QRectF sr = starRect(k);
      p.setFont(theme::mono(13, QFont::Bold));
      p.setPen(listed ? theme::map("#fcd700") : sr.contains(m_mouse) ? theme::map("#fff09a") : theme::map("#6b6346"));
      p.drawText(sr, Qt::AlignCenter, QString::fromUtf8(listed ? "★" : "☆"));
    }
    p.fillRect(QRectF(2, top + pitch - 1, W - 3, 1), theme::map("#1e1a0f"));
  }
  p.restore();
  look::brackets(p, R);
}

void RegistryPanel::mouseMoveEvent(QMouseEvent* e) {
  int h = -1; const auto tr = tabRects();
  for (int i = 0; i < tr.size(); ++i) if (tr[i].contains(e->position())) h = i;
  const int row = rowAt(e->position());
  const bool star = row >= 0 && starRect(row).contains(e->position());
  setCursor(h >= 0 || star ? Qt::PointingHandCursor : Qt::ArrowCursor);
  m_mouse = e->position();
  if (h != m_hoverTab || row != m_hoverRow || star) { m_hoverTab = h; m_hoverRow = row; update(); }
  else if (row >= 0) update(starRect(row).toAlignedRect().adjusted(-2, -2, 2, 2));
}
void RegistryPanel::leaveEvent(QEvent*) { m_hoverTab = -1; m_hoverRow = -1; m_mouse = {-1, -1}; update(); }
void RegistryPanel::mousePressEvent(QMouseEvent* e) {
  if (const int row = rowAt(e->position()); row >= 0 && starRect(row).contains(e->position())) {
    macro::Watchlist::instance().toggle(rows()[row].iso2);
    return;
  }
  const auto tr = tabRects();
  for (int i = 0; i < tr.size(); ++i)
    if (tr[i].contains(e->position()) && i != m_tab) {
      m_tab = i; m_scroll = 0; settings::setValue(kSettingRegion, kRegions[i]); update(); return;
    }
}
bool RegistryPanel::event(QEvent* e) {
  if (e->type() == QEvent::ToolTip) {
    auto* he = static_cast<QHelpEvent*>(e);
    const auto rs = rows();
    const int k = rowAt(he->pos());
    if (k >= 0 && starRect(k).contains(he->pos()))
      QToolTip::showText(he->globalPos(), macro::Watchlist::instance().contains(rs[k].iso2) ? QCoreApplication::translate("global", "Remove from the watchlist")
                                                                                          : QCoreApplication::translate("global", "Add to the watchlist"), this);
    else if (k >= 0 && !rs[k].bankName.isEmpty()) QToolTip::showText(he->globalPos(), rs[k].bankName, this);
    else QToolTip::hideText();
    return true;
  }
  return QWidget::event(e);
}

void RegistryPanel::wheelEvent(QWheelEvent* e) {
  const qreal content = rows().size() * 31.7, view = height() - 64;
  const qreal d = e->pixelDelta().y() != 0 ? e->pixelDelta().y() : e->angleDelta().y() / 2.0;
  const qreal next = qBound<qreal>(0, m_scroll - d, qMax<qreal>(0, content - view));
  if (next == m_scroll) { e->ignore(); return; }   // at an end: let the page scroll
  m_scroll = next;
  update();
}
}  // namespace overview
