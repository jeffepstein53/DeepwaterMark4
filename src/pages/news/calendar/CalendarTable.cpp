#include "pages/news/calendar/CalendarTable.h"
#include <algorithm>
#include <QTimeZone>
#include "ui/Theme.h"
#include "pages/news/ThemeColor.h"
#include "core/I18n.h"
#include <QCoreApplication>
#include "ui/Kit.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QToolTip>
#include <QLocale>
#include <QTextLayout>
#include <cmath>

namespace news::pages {

namespace {
// measured on the reference frames (docs/reference/News/calendar.md)
const news::pages::Dw kRowBg{"#040302"}, kRowToday{"#110f0b"}, kRowHover{"#251f0b"}, kRowSep{"#1a1b0a"};
const news::pages::Dw kHeadBg{"#0f0a02"}, kHeadLine{"#2b261a"}, kHeadBottom{"#2d2a17"}, kHeadText{"#7c786f"};
const news::pages::Dw kGroupBg{"#140f05"}, kGroupText{"#b4afa6"}, kGroupLine{"#2f2821"}, kGroupGap{"#070301"}, kGroupLine2{"#242017"};
const news::pages::Dw kTime{"#7c7971"}, kCcy{"#b2ada0"}, kName{"#d3d2cb"}, kChevron{"#555451"};
const news::pages::Dw kActual{"#fbfaf3"}, kFcst{"#b4afa4"}, kPrev{"#7f7c75"}, kDash{"#4c4b46"};
const news::pages::Dw kUp{"#73acb0"}, kDown{"#c07a78"}, kSurPos{"#8abfc5"}, kSurNeg{"#d06f6f"};
const news::pages::Dw kRange{"#f5dea3"}, kRangeLine{"#bab093"}, kDotHi{"#f4da29"}, kDotLow{"#5d5c56"};
const news::pages::Dw kTagText{"#49b6d0"}, kTagDim{"#4f4836"}, kTagBorder{"#2a2412"};
const news::pages::Dw kExpBg{"#140d08"}, kDesc{"#8b8681"}, kFocus{"#c9b26a"};
QString dayTitle(const QDate& d) {
  if (i18n::current() == i18n::Lang::French) {   // "Dimanche 8 novembre"
    QString t = i18n::locale().toString(d, "dddd d MMMM");
    if (!t.isEmpty()) t[0] = t[0].toUpper();
    return t;
  }
  return i18n::locale().toString(d, "dddd, MMMM d");
}
// Period tag computed by Deepwater ("ANNUAL"…), shown in the interface language.
QString periodLabel(const QString& p) {
  if (p == "ANNUAL") return QCoreApplication::translate("news", "ANNUAL");
  if (p == "MONTHLY") return QCoreApplication::translate("news", "MONTHLY");
  if (p == "QUARTERLY") return QCoreApplication::translate("news", "QUARTERLY");
  if (p == "3-MONTH") return QCoreApplication::translate("news", "3-MONTH");
  return p;
}
// Lines of a paragraph wrapped at `width` (20 px each in the expansion, like the reference).
QStringList wrapLines(const QString& para, const QFont& f, double width) {
  QStringList out;
  QTextLayout tl(para, f);
  tl.beginLayout();
  for (;;) {
    QTextLine line = tl.createLine();
    if (!line.isValid()) break;
    line.setLineWidth(width);
    out << para.mid(line.textStart(), line.textLength()).trimmed();
  }
  tl.endLayout();
  return out;
}
}

CalendarTable::CalendarTable(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  setAttribute(Qt::WA_OpaquePaintEvent);
  m_anim.setInterval(16);
  connect(&m_anim, &QTimer::timeout, this, [this] {
    const double d = m_target - m_scroll;
    if (std::abs(d) < 0.6) { m_scroll = m_target; m_anim.stop(); }
    else m_scroll += d * 0.3;
    update();
  });
}

// The day an event is listed under: a holiday ("All Day", stamped at midnight New York time) keeps its own date;
// in local time it would fall on the previous day west of New York.
static QDate listedDay(const news::Event& e) {
  static const QTimeZone ny("America/New_York");
  return e.allDay ? e.time.toTimeZone(ny).date() : e.time.toLocalTime().date();
}

void CalendarTable::setEvents(const QList<news::Event>& ev) {
  m_events = ev;
  m_hoverItem = -1;   // an index into the old item list: it may point past the new one (read when a row is pinned)
  // by listed day (time order kept within a day), so a holiday doesn't split a day in two
  std::stable_sort(m_events.begin(), m_events.end(), [](const news::Event& a, const news::Event& b) { return listedDay(a) < listedDay(b); });
  relayout();
  if (!m_expanded.isEmpty() && !expandItem()) m_expanded.clear();
  setTarget(m_target);
  m_scroll = std::clamp(m_scroll, 0.0, maxScroll());
  update();
  emit contentChanged();
  if (m_openToday && !m_items.isEmpty()) {
    m_openToday = false;
    const QDate today = QDate::currentDate();
    for (const auto& it : m_items)
      if (it.kind == Item::Group && it.day >= today) {
        if (it.first) break;                        // the week begins today or later: its top is the place
        if (m_full) emit openAt(int(it.y));
        else setScroll(it.y);
        break;
      }
  }
}

void CalendarTable::setFullHeight(bool on) {
  if (on == m_full) return;
  m_full = on;
  m_scroll = m_target = 0; m_anim.stop();
  m_viewTop = 0; m_viewH = 0;
  update();
}
void CalendarTable::setVisibleBand(double top, double h) {
  top = std::max(0.0, top);
  if (top == m_viewTop && h == m_viewH) return;
  m_viewTop = top; m_viewH = h;
  update();   // the pinned headers move with the band: a blit of the scrolled content would carry them along
}

// Numeric columns are laid out from the right edge with the reference gaps (Max 0, Min 116, Surprise 116,
// Previous 111, Forecast 124, Actual 126 px), scaled with the table width (0.8× … 1.6×). Narrow tables drop the
// low-priority columns first (MAX/MIN, then SURPRISE); their values move to the row tooltip.
bool CalendarTable::shown(int col) const {
  if (col < Actual) return true;
  if (phone()) return col == Actual || col == Forecast || col == Previous;
  if (m_full && (col == Min || col == Max)) return false;   // phone sideways: the names get the room (ranges are rare)
  const double W = width();
  const double room = W - 228 - 250;              // keep ≥ 250 px for the event name, tag and chevron
  const double f = scale();
  if (col == Min || col == Max) return 593 * f <= room;
  if (col == Surprise) return 361 * f <= room;
  return true;
}
double CalendarTable::scale() const {
  const double W = width();
  const double f = W <= 1147 ? std::clamp((W - 228 - 300) / (1147.0 - 228 - 300), 0.8, 1.0)   // reference: 1 at 1147…1600
                             : std::clamp(1.0 + (W - 1920) / 1500.0, 1.0, 1.6);                // wide screens spread out
  // narrow tables: the numeric columns tighten further (down to 0.65) so the event name keeps ≈ 380 px
  // (W - 228 - 10 - (467 + 125) f, see rightOf and the event cell); 1024 px: names were cut to a few letters
  return std::max(0.65, std::min(f, (W - 228 - 10 - 380) / 592.0));
}
double CalendarTable::rightOf(int col) const {
  if (phone()) {   // second line of the two-line rows: three columns from the right edge
    const double x = width() - 10;
    return col == Previous ? x : col == Forecast ? x - 86 : col == Actual ? x - 172 : -1000;
  }
  const double W = width(), f = scale();
  static const int order[] = {Max, Min, Surprise, Previous, Forecast, Actual};
  static const double gap[] = {0, 116, 116, 111, 124, 126};   // distance to the next column on the right
  double x = W - 10; bool first = true;
  for (int k = 0; k < 6; ++k) {
    const int c = order[k];
    if (!shown(c)) continue;
    if (!first) x -= gap[k] * f;
    first = false;
    if (c == col) return x;
  }
  return -1000;
}

QStringList CalendarTable::descParas(const news::Event& e) const {
  QStringList out = news::Calendar::describe(e);
  for (const QString& t : {news::alsoText(e), news::otherText(e), news::revisedText(e), news::detailsText(e)}) if (!t.isEmpty()) out << t;
  return out;
}

void CalendarTable::relayout() {
  m_items.clear();
  double y = 0;
  QDate cur;
  const QFont df = theme::condensed(15, QFont::Medium);
  for (int i = 0; i < m_events.size(); ++i) {
    const QDate d = listedDay(m_events[i]);
    if (d != cur) {
      Item g{Item::Group, y, m_items.isEmpty() ? double(kStickyH) : 31.0, -1, d, m_items.isEmpty()};
      m_items << g; y += g.h; cur = d;
    }
    m_items << Item{Item::Row, y, rowH(), i, d};
    y += rowH();
    if (m_events[i].key == m_expanded) {
      double h = 13;
      for (const auto& para : descParas(m_events[i])) h += wrapLines(para, df, proseWidth()).size() * 20 + 27;
      h += HistoryCard::kHeight - 19 + 11;
      m_items << Item{Item::Expand, y, h, i, d};
      y += h;
    }
  }
}

const CalendarTable::Item* CalendarTable::expandItem() const {
  for (const auto& it : m_items) if (it.kind == Item::Expand) return &it;
  return nullptr;
}

double CalendarTable::maxScroll() const { if (m_full) return 0; return std::max(0.0, contentHeight() - (height() - kHeaderH) + 8); }
void CalendarTable::setTarget(double y) {
  m_target = std::clamp(y, 0.0, maxScroll());
  if (std::abs(m_target - m_scroll) > 0.5 && !m_anim.isActive()) m_anim.start();
}
void CalendarTable::setScroll(double y) { m_target = m_scroll = std::clamp(y, 0.0, maxScroll()); m_anim.stop(); update(); }

void CalendarTable::toggleExpand(const QString& key) {
  m_expanded = m_expanded == key ? QString() : key;
  m_cardPress = false;
  relayout();
  if (const Item* e = expandItem()) {
    const news::Event& ev = m_events[e->ev];
    m_card.setLoading(ev.name, news::Calendar::countryOf(ev.currency));
    news::History::instance().fetch(ev, this, [this, key = ev.key](const news::Series& s) {
      if (m_expanded != key) return;
      m_card.setSeries(s); update();
    });
  }
  setTarget(m_target);
  update();
  emit contentChanged();
}

void CalendarTable::scrollToKey(const QString& key) {
  for (const auto& it : m_items)
    if (it.kind == Item::Row && m_events[it.ev].key == key) {
      // keep the row where it is when visible (reference t014.5: the expansion opens below it); otherwise put it
      // a third down the viewport
      const double top = it.y - m_scroll, view = height() - kHeaderH;
      if (top < kStickyH || top > view - 200) setScroll(it.y - view / 3);
      return;
    }
}

QRectF CalendarTable::cellRect(const Item& it, int col) const {
  if (phone()) {   // values on the second line; the rest of the row opens the release history
    if (col >= Actual) return shown(col) ? QRectF(rightOf(col) - 82, it.y + 29, 86, 28) : QRectF();
    if (col == EventCol) return QRectF(0, it.y, width(), it.h);
    return {};
  }
  if (col >= Actual) return shown(col) ? QRectF(rightOf(col) - 111 * std::min(1.0, scale()), it.y + 5.5, 113 * std::min(1.0, scale()), 25) : QRectF();
  if (col == EventCol) return QRectF(222, it.y, rightOf(Actual) - 125 * std::min(1.0, scale()) - 222, it.h);
  return {};
}
QRectF CalendarTable::cardRect(const Item& e) const {
  return QRectF(14, e.y + e.h - 11 - HistoryCard::kHeight, width() - 28, HistoryCard::kHeight);
}

int CalendarTable::itemAt(double cy) const {
  for (int i = 0; i < m_items.size(); ++i) if (cy >= m_items[i].y && cy < m_items[i].y + m_items[i].h) return i;
  return -1;
}

void CalendarTable::resizeEvent(QResizeEvent*) { relayout(); m_scroll = std::clamp(m_scroll, 0.0, maxScroll()); m_target = m_scroll; }

// ------------------------------------------------------------------------------------------------ painting
void CalendarTable::paintGroup(QPainter& p, const QDate& day, double y, bool sticky) const {
  const double W = width();
  if (sticky) {
    p.fillRect(QRectF(0, y, W, 28), kGroupBg);
    p.fillRect(QRectF(0, y + 28, W, 1), kGroupLine);
    p.fillRect(QRectF(0, y + 29, W, 5), kGroupGap);
    p.fillRect(QRectF(0, y + 34, W, 1), kGroupLine2);
    p.fillRect(QRectF(0, y + 35, W, 1), kRowBg);
  } else {
    p.fillRect(QRectF(0, y, W, 1), theme::map("#312d18"));
    p.fillRect(QRectF(0, y + 1, W, 28), kGroupBg);
    p.fillRect(QRectF(0, y + 29, W, 1), theme::map("#252210"));
    p.fillRect(QRectF(0, y + 30, W, 1), theme::map("#2d291d"));
  }
  p.setFont(theme::condensed(14, QFont::DemiBold)); p.setPen(kGroupText);
  p.drawText(QRectF(10, y + (sticky ? 0 : 1), W, 28), Qt::AlignLeft | Qt::AlignVCenter, dayTitle(day));
}

void CalendarTable::paintRow(QPainter& p, const Item& it, double y, bool pinned) const {
  const news::Event& e = m_events[it.ev];
  const double W = width(), cy = y + 18;
  const int idx = int(&it - m_items.constData());
  const bool hover = idx == m_hoverItem || (pinned && m_hoverItem >= 0 && m_hoverItem < m_items.size() && m_items[m_hoverItem].ev == it.ev && m_items[m_hoverItem].kind == Item::Row);
  // day groups alternate dark / lighter backgrounds (reference: Sunday dark, Monday lighter, Tuesday dark)
  int groupIndex = 0;
  for (int k = 0; k <= idx && k < m_items.size(); ++k) if (m_items[k].kind == Item::Group && k > 0) ++groupIndex;
  const bool lighter = groupIndex % 2 == 1;
  p.fillRect(QRectF(0, y, W, kRowH - 1), hover || e.key == m_expanded ? kRowHover : lighter ? kRowToday : kRowBg);
  p.fillRect(QRectF(0, y + kRowH - 1, W, 1), kRowSep);

  // time, currency
  p.setFont(theme::mono(13)); p.setPen(kTime);
  p.drawText(QRectF(10, y, 90, 36), Qt::AlignLeft | Qt::AlignVCenter,
             e.allDay ? QCoreApplication::translate("news", "All Day") : e.time.toLocalTime().toString("HH:mm"));
  if (e.currency != "ALL") p.drawPixmap(QPointF(103, cy - 6.5), ui::flag(e.currency, QSize(20, 13)));
  else { p.setPen(theme::map("#3a372f")); p.drawRect(QRectF(103.5, cy - 6, 19, 12)); }
  p.setFont(theme::mono(13, QFont::Medium)); p.setPen(kCcy);
  p.drawText(QRectF(130, y, 80, 36), Qt::AlignLeft | Qt::AlignVCenter, e.currency == "ALL" ? "ALL" : e.currency);

  // impact dots + name + tag + chevron
  p.setRenderHint(QPainter::Antialiasing, true);
  const int dots = e.impact == news::Impact::High ? 3 : e.impact == news::Impact::Medium ? 2 : 1;
  const QColor dc = e.impact >= news::Impact::Medium ? kDotHi : kDotLow;
  p.setPen(Qt::NoPen); p.setBrush(dc);
  for (int i = 0; i < dots; ++i) p.drawEllipse(QPointF(231 + i * 9, cy), 3, 3);
  p.setBrush(Qt::NoBrush);
  p.setRenderHint(QPainter::Antialiasing, false);
  double x = 228 + dots * 9 + 4;
  const double maxRight = rightOf(Actual) - 125 * std::min(1.0, scale());
  const QFont nf = theme::condensed(15, QFont::Medium);
  const QFontMetricsF nfm(nf);
  const QFont tf = theme::mono(9, QFont::Bold, 0.8), tf2 = theme::condensed(12, QFont::Medium);
  const QFontMetricsF tfm(tf), tfm2(tf2);
  const QString period = periodLabel(e.period);
  const double tagW = e.period.isEmpty() ? 0 : tfm.horizontalAdvance(period) + tfm2.horizontalAdvance(e.periodRaw) + 18;
  const double room = maxRight - x - (tagW ? tagW + 8 : 0) - 22;
  QString name = e.name;
  if (nfm.horizontalAdvance(name) > room) {
    while (!name.isEmpty() && nfm.horizontalAdvance(name + "...") > room) name.chop(1);
    name = name.trimmed() + "...";
  }
  p.setFont(nf); p.setPen(kName);
  p.drawText(QRectF(x, y, room + 30, 36), Qt::AlignLeft | Qt::AlignVCenter, name);
  x += nfm.horizontalAdvance(name) + 8;
  if (tagW > 0) {
    const QRectF tr(x, cy - 9, tagW, 18);
    p.setPen(kTagBorder); p.drawRect(tr.adjusted(0.5, 0.5, -0.5, -0.5));
    p.setFont(tf); p.setPen(kTagText);
    p.drawText(QRectF(tr.x() + 6, tr.y(), tagW, 18), Qt::AlignLeft | Qt::AlignVCenter, period);
    p.setFont(tf2); p.setPen(kTagDim);
    p.drawText(QRectF(tr.x() + 9 + tfm.horizontalAdvance(period), tr.y(), tagW, 18), Qt::AlignLeft | Qt::AlignVCenter, e.periodRaw);
    x = tr.right() + 9;
  }
  {  // chevron
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(kChevron.c(), 1.2));
    const bool up = e.key == m_expanded;
    const QPointF c(x + 4, cy + (up ? -1 : 1));
    if (up) { p.drawLine(c + QPointF(-3.5, 2), c + QPointF(0, -1.5)); p.drawLine(c + QPointF(0, -1.5), c + QPointF(3.5, 2)); }
    else { p.drawLine(c + QPointF(-3.5, -2), c + QPointF(0, 1.5)); p.drawLine(c + QPointF(0, 1.5), c + QPointF(3.5, -2)); }
    p.setRenderHint(QPainter::Antialiasing, false);
  }

  // values
  const QFont vf = theme::mono(14), vb = theme::mono(14, QFont::Bold);
  auto put = [&](int col, const QString& t, const QColor& c, bool bold, double rightPad = 0) {
    if (!shown(col)) return;
    p.setFont(bold ? vb : vf); p.setPen(c);
    p.drawText(QRectF(rightOf(col) - 200 - rightPad, y, 200, 36), Qt::AlignRight | Qt::AlignVCenter, t);
  };
  // actual + arrow
  if (e.actual.has()) {
    put(Actual, e.actual.text + (e.other.isEmpty() ? "" : "?"), kActual, true, 12);
    const int dir = e.direction();
    if (dir) {
      p.setRenderHint(QPainter::Antialiasing, true);
      p.setPen(QPen((dir > 0 ? kUp : kDown).c(), 1.2));
      const double ax = rightOf(Actual) - 4;
      if (dir > 0) { p.drawLine(QPointF(ax, cy + 4), QPointF(ax, cy - 3)); p.drawLine(QPointF(ax - 2.5, cy - 0.5), QPointF(ax, cy - 3)); p.drawLine(QPointF(ax + 2.5, cy - 0.5), QPointF(ax, cy - 3)); }
      else { p.drawLine(QPointF(ax, cy - 3), QPointF(ax, cy + 4)); p.drawLine(QPointF(ax - 2.5, cy + 1.5), QPointF(ax, cy + 4)); p.drawLine(QPointF(ax + 2.5, cy + 1.5), QPointF(ax, cy + 4)); }
      p.setRenderHint(QPainter::Antialiasing, false);
    }
  } else put(Actual, "—", kActual, true, 12);
  put(Forecast, e.forecast.has() ? e.forecast.text : "—", e.forecast.has() ? kFcst : kDash, false);
  put(Previous, e.revised.has() ? e.revised.text + "*" : e.previous.has() ? e.previous.text : "—", e.previous.has() ? kPrev : kDash, false);
  const double s = e.surprise();
  put(Surprise, std::isfinite(s) ? e.surpriseText() : "—", !std::isfinite(s) ? kDash : s > 0 ? kSurPos : s < 0 ? kSurNeg : kFcst, false);
  for (int col : {Min, Max}) {
    if (!shown(col)) continue;
    const news::Value& v = col == Min ? e.low : e.high;
    if (!v.has()) { put(col, "—", kDash, false); continue; }
    put(col, v.text, kRange, true);
    const double w = QFontMetricsF(vb).horizontalAdvance(v.text);
    p.setPen(QPen(kRangeLine.c(), 1, Qt::DotLine));
    p.drawLine(QPointF(rightOf(col) - w, cy + 10.5), QPointF(rightOf(col), cy + 10.5));
  }
  if (e.key == m_focusKey && m_focusCol >= Actual && shown(m_focusCol)) {
    p.setPen(kFocus); p.drawRect(QRectF(rightOf(m_focusCol) - 111, y + 5.5, 113, 25).adjusted(0.5, 0.5, -0.5, -0.5));
  }
}

// Phone rows (two lines, 60 px): time · flag · currency · impact dots · name (+ chevron) on the first line; the
// period tag on the left of the second, then ACTUAL / FORECAST / PREVIOUS under their header labels. SURPRISE and
// the economists' range stay in the range panel (tap a value).
void CalendarTable::paintRowPhone(QPainter& p, const Item& it, double y) const {
  const news::Event& e = m_events[it.ev];
  const double W = width(), h = it.h, cy1 = y + 16, cy2 = y + 43;
  const int idx = int(&it - m_items.constData());
  int groupIndex = 0;
  for (int k = 0; k <= idx && k < m_items.size(); ++k) if (m_items[k].kind == Item::Group && k > 0) ++groupIndex;
  p.fillRect(QRectF(0, y, W, h - 1), idx == m_hoverItem || e.key == m_expanded ? kRowHover : groupIndex % 2 == 1 ? kRowToday : kRowBg);
  p.fillRect(QRectF(0, y + h - 1, W, 1), kRowSep);

  p.setFont(theme::mono(13)); p.setPen(kTime);
  p.drawText(QRectF(10, cy1 - 10, 46, 20), Qt::AlignLeft | Qt::AlignVCenter, e.allDay ? QStringLiteral("—") : e.time.toLocalTime().toString("HH:mm"));
  if (e.currency != "ALL") p.drawPixmap(QPointF(56, cy1 - 6.5), ui::flag(e.currency, QSize(20, 13)));
  else { p.setPen(theme::map("#3a372f")); p.drawRect(QRectF(56.5, cy1 - 6, 19, 12)); }
  p.setFont(theme::mono(13, QFont::Medium)); p.setPen(kCcy);
  p.drawText(QRectF(82, cy1 - 10, 50, 20), Qt::AlignLeft | Qt::AlignVCenter, e.currency);
  const double dotX = 82 + QFontMetricsF(p.font()).horizontalAdvance(e.currency) + 10;
  p.setRenderHint(QPainter::Antialiasing, true);
  const int dots = e.impact == news::Impact::High ? 3 : e.impact == news::Impact::Medium ? 2 : 1;
  p.setPen(Qt::NoPen); p.setBrush(e.impact >= news::Impact::Medium ? kDotHi : kDotLow);
  for (int i = 0; i < dots; ++i) p.drawEllipse(QPointF(dotX + i * 9, cy1), 3, 3);
  p.setBrush(Qt::NoBrush);
  p.setRenderHint(QPainter::Antialiasing, false);
  const double nx = dotX + dots * 9 + 3;
  const QFont nf = theme::condensed(15, QFont::Medium);
  const QFontMetricsF nfm(nf);
  const QString name = nfm.elidedText(e.name, Qt::ElideRight, W - 10 - 18 - nx);
  p.setFont(nf); p.setPen(kName);
  p.drawText(QRectF(nx, cy1 - 11, W - nx, 22), Qt::AlignLeft | Qt::AlignVCenter, name);
  {  // chevron
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(kChevron.c(), 1.2));
    const bool up = e.key == m_expanded;
    const QPointF c(nx + nfm.horizontalAdvance(name) + 12, cy1 + (up ? -1 : 1));
    if (up) { p.drawLine(c + QPointF(-3.5, 2), c + QPointF(0, -1.5)); p.drawLine(c + QPointF(0, -1.5), c + QPointF(3.5, 2)); }
    else { p.drawLine(c + QPointF(-3.5, -2), c + QPointF(0, 1.5)); p.drawLine(c + QPointF(0, 1.5), c + QPointF(3.5, -2)); }
    p.setRenderHint(QPainter::Antialiasing, false);
  }

  // second line, left: "All Day" for a holiday, else the period tag (without its raw "y/y" when short of room)
  const double leftRoom = rightOf(Actual) - 82 - 6 - 56;
  if (e.allDay) {
    p.setFont(theme::mono(11)); p.setPen(kTime);
    p.drawText(QRectF(56, cy2 - 10, leftRoom, 20), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetricsF(p.font()).elidedText(QCoreApplication::translate("news", "All Day"), Qt::ElideRight, leftRoom));
  } else if (!e.period.isEmpty()) {
    const QFont tf = theme::mono(9, QFont::Bold, 0.8), tf2 = theme::condensed(12, QFont::Medium);
    const QFontMetricsF tfm(tf), tfm2(tf2);
    const QString period = periodLabel(e.period);
    double tagW = tfm.horizontalAdvance(period) + tfm2.horizontalAdvance(e.periodRaw) + 18;
    const bool raw = tagW <= leftRoom;
    if (!raw) tagW = tfm.horizontalAdvance(period) + 13;
    if (tagW <= leftRoom) {
      const QRectF tr(56, cy2 - 9, tagW, 18);
      p.setPen(kTagBorder); p.drawRect(tr.adjusted(0.5, 0.5, -0.5, -0.5));
      p.setFont(tf); p.setPen(kTagText);
      p.drawText(QRectF(tr.x() + 6, tr.y(), tagW, 18), Qt::AlignLeft | Qt::AlignVCenter, period);
      if (raw) {
        p.setFont(tf2); p.setPen(kTagDim);
        p.drawText(QRectF(tr.x() + 9 + tfm.horizontalAdvance(period), tr.y(), tagW, 18), Qt::AlignLeft | Qt::AlignVCenter, e.periodRaw);
      }
    }
  }

  // values
  const QFont vf = theme::mono(14), vb = theme::mono(14, QFont::Bold);
  auto put = [&](int col, const QString& t, const QColor& c, bool bold, double rightPad = 0) {
    p.setFont(bold ? vb : vf); p.setPen(c);
    p.drawText(QRectF(rightOf(col) - 200 - rightPad, cy2 - 12, 200, 24), Qt::AlignRight | Qt::AlignVCenter, t);
  };
  if (e.actual.has()) {
    put(Actual, e.actual.text + (e.other.isEmpty() ? "" : "?"), kActual, true, 12);
    if (const int dir = e.direction()) {
      p.setRenderHint(QPainter::Antialiasing, true);
      p.setPen(QPen((dir > 0 ? kUp : kDown).c(), 1.2));
      const double ax = rightOf(Actual) - 4, cy = cy2;
      if (dir > 0) { p.drawLine(QPointF(ax, cy + 4), QPointF(ax, cy - 3)); p.drawLine(QPointF(ax - 2.5, cy - 0.5), QPointF(ax, cy - 3)); p.drawLine(QPointF(ax + 2.5, cy - 0.5), QPointF(ax, cy - 3)); }
      else { p.drawLine(QPointF(ax, cy - 3), QPointF(ax, cy + 4)); p.drawLine(QPointF(ax - 2.5, cy + 1.5), QPointF(ax, cy + 4)); p.drawLine(QPointF(ax + 2.5, cy + 1.5), QPointF(ax, cy + 4)); }
      p.setRenderHint(QPainter::Antialiasing, false);
    }
  } else put(Actual, "—", kActual, true, 12);
  put(Forecast, e.forecast.has() ? e.forecast.text : "—", e.forecast.has() ? kFcst : kDash, false);
  put(Previous, e.revised.has() ? e.revised.text + "*" : e.previous.has() ? e.previous.text : "—", e.previous.has() ? kPrev : kDash, false);
  if (e.key == m_focusKey && m_focusCol >= Actual && shown(m_focusCol)) {
    const QRectF fr = cellRect(it, m_focusCol).translated(0, y - it.y);
    p.setPen(kFocus); p.drawRect(fr.adjusted(0.5, 2.5, -0.5, -2.5));
  }
}

void CalendarTable::paintExpand(QPainter& p, const Item& it, double y) const {
  const news::Event& e = m_events[it.ev];
  p.fillRect(QRectF(0, y, width(), it.h), kExpBg);
  p.fillRect(QRectF(0, y + it.h - 1, width(), 1), kRowSep);
  const QFont df = theme::condensed(15, QFont::Medium);
  p.setFont(df); p.setPen(kDesc);
  double ty = y + 13;
  for (const auto& para : descParas(e)) {
    const QStringList lines = wrapLines(para, df, proseWidth());
    for (int i = 0; i < lines.size(); ++i)
      p.drawText(QRectF(14, ty + i * 20, proseWidth(), 20), Qt::AlignLeft | Qt::AlignVCenter, lines[i]);
    ty += lines.size() * 20 + 27;
  }
  QRectF card = cardRect(it); card.moveTop(card.top() - it.y + y);
  m_card.paint(p, card);
}

void CalendarTable::paintEvent(QPaintEvent*) {
  QPainter p(this);
  const double W = width(), H = height();
  p.fillRect(rect(), kRowBg);
  // full height (page scrolls): the headers are pinned at the top of the visible band, rows outside it are skipped
  const double S = scrollPos(), hdr = headY(), bottom = m_full && m_viewH > 0 ? std::min(H, hdr + m_viewH) : H;
  const double top = hdr + kHeaderH;
  p.save();
  p.setClipRect(QRectF(0, top, W, H - top));
  const double off = top - S;
  for (const auto& it : m_items) {
    const double y = it.y + off;
    if (y + it.h < top || y > bottom) continue;
    if (it.kind == Item::Group) paintGroup(p, it.day, y + (it.first ? 0 : 0), it.first);
    else if (it.kind == Item::Row) { if (phone()) paintRowPhone(p, it, y); else paintRow(p, it, y, false); }
    else paintExpand(p, it, y);
  }
  if (m_items.isEmpty()) {
    p.setFont(theme::mono(11, QFont::Normal, 1)); p.setPen(theme::map("#6b685f"));
    p.drawText(QRectF(60, top, W - 120, H - top), Qt::AlignCenter | Qt::TextWordWrap,
               m_note.isEmpty() ? QCoreApplication::translate("news", "Loading the calendar…") : m_note);
  }
  // sticky: day header (+ pinned expanded row)
  if (!m_items.isEmpty() && S > 0.5) {
    int g = -1, next = -1;
    for (int i = 0; i < m_items.size(); ++i) {
      if (m_items[i].kind != Item::Group) continue;
      if (m_items[i].y <= S) g = i; else { next = i; break; }
    }
    if (g >= 0) {
      double sy = top;
      if (next >= 0) sy = std::min(sy, m_items[next].y + off - kStickyH);
      bool pinned = false;
      if (const Item* ex = expandItem(); ex && ex->day == m_items[g].day && !phone()) {
        const Item& row = m_items[ex->ev >= 0 ? int(ex - m_items.constData()) - 1 : 0];
        const double rowY = row.y + off;
        const double pinY = top + 20;
        if (rowY < pinY && ex->y + ex->h + off > pinY + kRowH + 40) { paintRow(p, row, pinY, true); pinned = true; }
      }
      if (pinned) {   // compact header: the pinned row sits right under it (reference t019)
        p.fillRect(QRectF(0, sy, W, 28), kGroupBg);
        p.fillRect(QRectF(0, sy + 28, W, 1), kGroupLine);
        p.setFont(theme::condensed(14, QFont::DemiBold)); p.setPen(kGroupText);
        p.drawText(QRectF(10, sy, W, 28), Qt::AlignLeft | Qt::AlignVCenter, dayTitle(m_items[g].day));
      } else paintGroup(p, m_items[g].day, sy, true);
    }
  }
  p.restore();

  // column header
  p.save();
  p.translate(0, hdr);
  p.fillRect(QRectF(0, 0, W, kHeaderH), kHeadBg);
  p.fillRect(QRectF(0, 0, W, 1), kHeadLine);
  p.fillRect(QRectF(0, kHeaderH - 2, W, 1), kHeadBottom);
  p.fillRect(QRectF(0, kHeaderH - 1, W, 1), kHeadLine);
  p.setFont(theme::mono(9, QFont::Normal, 0.8)); p.setPen(kHeadText);
  const QRectF hr(0, 1, W, kHeaderH - 3);
  p.drawText(hr.adjusted(10, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("news", "TIME"));
  if (!phone()) p.drawText(hr.adjusted(103, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("news", "CURRENCY"));
  p.drawText(hr.adjusted(phone() ? 56 : 228, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("news", "EVENT"));
  const QList<QPair<int, QString>> cols{{Actual, QCoreApplication::translate("news", "ACTUAL")}, {Forecast, QCoreApplication::translate("news", "FORECAST")}, {Previous, QCoreApplication::translate("news", "PREVIOUS")},
                                        {Surprise, QCoreApplication::translate("news", "SURPRISE")}, {Min, QCoreApplication::translate("news", "MIN")}, {Max, QCoreApplication::translate("news", "MAX")}};
  for (const auto& [c, t] : cols) if (shown(c)) p.drawText(QRectF(rightOf(c) - 150, 1, 150, kHeaderH - 3), Qt::AlignRight | Qt::AlignVCenter, t);
  p.restore();
}

// ------------------------------------------------------------------------------------------------ input
void CalendarTable::updateHover(const QPointF& pos) {
  int hi = -1, hc = -1;
  bool onCard = false;   // the mouse is over the expanded release-history card (hi is only set for rows)
  Qt::CursorShape cur = Qt::ArrowCursor;
  const double S = scrollPos(), hdr = headY();
  if (pos.y() >= hdr + kHeaderH) {
    double cy = contentY(pos.y());
    // pinned expanded row
    if (const Item* ex = expandItem(); ex && !phone()) {
      const int ri = int(ex - m_items.constData()) - 1;
      const double rowY = m_items[ri].y + hdr + kHeaderH - S, pinY = hdr + kHeaderH + 20;
      if (rowY < pinY && ex->y + ex->h + hdr + kHeaderH - S > pinY + kRowH + 40 && pos.y() >= pinY + 9 && pos.y() < pinY + kRowH)
        cy = m_items[ri].y + (pos.y() - pinY);
    }
    // under the sticky day header nothing is clickable
    if (S > 0.5 && pos.y() < hdr + kHeaderH + 29) cy = -1;
    const int i = itemAt(cy);
    if (i >= 0 && m_items[i].kind == Item::Row) {
      hi = i;
      const QPointF c(pos.x(), cy);
      for (int col = Actual; col <= Max; ++col) if (cellRect(m_items[i], col).contains(c)) hc = col;
      if (hc < 0 && cellRect(m_items[i], EventCol).contains(c)) hc = EventCol;
      if (hc >= 0) cur = Qt::PointingHandCursor;
      const news::Event& e = m_events[m_items[i].ev];
      if ((hc == Min || hc == Max) && !e.hasRange())
        QToolTip::showText(mapToGlobal(pos.toPoint()), QCoreApplication::translate("news", "No economists' range: no free source publishes the survey's minimum and maximum (Econoday's terms forbid reuse). Click for the release details."), this);
      else if (hc == Actual && e.released())
        QToolTip::showText(mapToGlobal(pos.toPoint()), QCoreApplication::translate("news", "Actual from %1, recorded %2%3").arg(news::actualSourceText(e),
                           i18n::locale().toString(e.actualAt.toLocalTime(), "ddd d MMM, HH:mm"),
                           e.actualDetail.isEmpty() ? QString() : "\n" + news::actualDetailText(e)) +
                           (e.also.isEmpty() ? QString() : "\n" + news::alsoText(e)) +
                           (e.other.isEmpty() ? QString() : "\n" + news::otherText(e)), this);
      else if (hc == Previous && e.revised.has())
        QToolTip::showText(mapToGlobal(pos.toPoint()), ui::plainTip(news::revisedText(e)), this);
      else if (hc == Forecast && e.forecast.has())
        QToolTip::showText(mapToGlobal(pos.toPoint()), ui::plainTip(QCoreApplication::translate("news", "Consensus forecast: %1").arg(e.forecastSource)), this);
      else if (hc == EventCol && !(!shown(Surprise) || !shown(Min)))   // names may be elided: full title
        QToolTip::showText(mapToGlobal(pos.toPoint()), ui::plainTip(QCoreApplication::translate("news", "%1 · %2  (%3 impact)").arg(e.currency, e.title, news::impactName(e.impact))), this);
      else if (!shown(Surprise) || !shown(Min)) {   // narrow table: the hidden columns' values
        QStringList t;
        if (!shown(Surprise)) t << QCoreApplication::translate("news", "Surprise %1").arg(e.surpriseText().isEmpty() ? QString("—") : e.surpriseText());
        if (!shown(Min)) t << QCoreApplication::translate("news", "Economists' range %1").arg(e.hasRange() ? e.low.text + " – " + e.high.text : QCoreApplication::translate("news", "— (no free source)"));
        QToolTip::showText(mapToGlobal(pos.toPoint()), ui::plainTip(e.title + "\n" + t.join("\n")), this);
      } else QToolTip::hideText();
    } else if (i >= 0 && m_items[i].kind == Item::Expand) {
      QRectF card = cardRect(m_items[i]);
      const QPointF c(pos.x(), cy);
      onCard = true;
      if (m_card.mouseMove(card, c)) update();
      if (m_card.wantsPointer(card, c)) cur = Qt::PointingHandCursor;
      else if (card.contains(c)) cur = Qt::CrossCursor;
      QToolTip::hideText();
    } else QToolTip::hideText();
  }
  // clear the card's hover only when the mouse is on neither a row nor the card: this cleared it on every move over
  // the card itself, so the history chart never showed its hover
  if (!onCard && (hi < 0 || m_items[hi].kind != Item::Row)) { if (m_card.leave()) update(); }
  setCursor(cur);
  if (hi != m_hoverItem || hc != m_hoverCol) { m_hoverItem = hi; m_hoverCol = hc; update(); }
}

void CalendarTable::mouseMoveEvent(QMouseEvent* e) {
  m_pin = false;
  if (m_cardPress) {
    if (const Item* ex = expandItem()) {
      const QPointF c(e->position().x(), contentY(e->position().y()));
      if (m_card.mouseMove(cardRect(*ex), c)) update();
    }
    return;
  }
  updateHover(e->position());
}

void CalendarTable::mousePressEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return;
  if (const Item* ex = expandItem()) {
    const QPointF c(e->position().x(), contentY(e->position().y()));
    if (e->position().y() > headY() + kHeaderH + 36 && cardRect(*ex).contains(c)) {
      if (m_card.mousePress(cardRect(*ex), c)) { m_cardPress = m_card.dragging(); update(); }
      return;
    }
  }
  updateHover(e->position());
  if (m_hoverItem < 0 || m_hoverCol < 0) return;
  const news::Event ev = m_events[m_items[m_hoverItem].ev];
  if (m_hoverCol == EventCol) toggleExpand(ev.key);
  else if (m_hoverCol >= Actual) emit valueClicked(ev, m_hoverCol);
}

void CalendarTable::mouseReleaseEvent(QMouseEvent* e) {
  if (!m_cardPress) return;
  m_cardPress = false;
  if (const Item* ex = expandItem()) {
    const QPointF c(e->position().x(), contentY(e->position().y()));
    if (m_card.mouseRelease(cardRect(*ex), c)) update();
  }
}

void CalendarTable::mouseDoubleClickEvent(QMouseEvent* e) {
  if (const Item* ex = expandItem()) {
    const QPointF c(e->position().x(), contentY(e->position().y()));
    if (m_card.mouseDoubleClick(cardRect(*ex), c)) { update(); return; }
  }
  mousePressEvent(e);
}

void CalendarTable::wheelEvent(QWheelEvent* e) {
  if (m_full) { e->ignore(); return; }   // the page scrolls
  if (!e->pixelDelta().isNull()) { setScroll(m_scroll - e->pixelDelta().y()); }
  else setTarget(m_target - e->angleDelta().y() / 120.0 * 113);
  updateHover(e->position());
  e->accept();
}

void CalendarTable::leaveEvent(QEvent*) {
  if (m_pin) return;
  m_hoverItem = m_hoverCol = -1;
  m_card.leave();
  update();
}

void CalendarTable::hoverAt(const QPoint& p) { m_pin = true; updateHover(p); }
void CalendarTable::hoverChart(double f) {
  if (const Item* ex = expandItem()) { m_card.hoverFraction(cardRect(*ex), f); update(); }
}
void CalendarTable::setChartMode(int mode, int span) {
  m_card.setMode(HistoryCard::Mode(mode)); m_card.setSpan(HistoryCard::Span(span)); update();
}
}  // namespace news::pages
