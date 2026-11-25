#include "pages/news/calendar/RangePanel.h"
#include "ui/Kit.h"
#include "ui/Theme.h"
#include "pages/news/ThemeColor.h"
#include "core/I18n.h"
#include "ui/Touch.h"
#include <QCoreApplication>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QToolTip>
#include <QDesktopServices>
#include <QUrl>
#include <QLocale>
#include <QTimeZone>
#include <cmath>

namespace news::pages {

namespace {
const news::pages::Dw kBg{"#0a0905"}, kBorder{"#242017"}, kDate{"#817f77"}, kLabel{"#e1d6a5"}, kWhite{"#fdfcf9"};
const news::pages::Dw kTrack{"#21201a"}, kPrevMark{"#a3a19b"}, kCons{"#eedd9b"}, kDim{"#52514a"}, kBankName{"#d7d6d2"};
const news::pages::Dw kBankTrack{"#15140d"}, kBankVal{"#eedf9f"}, kBoxBg{"#080605"}, kBoxBorder{"#1a1a17"}, kBoxLbl{"#50514a"};
const news::pages::Dw kLegend{"#807e77"}, kRule{"#191711"}, kSrcLbl{"#5b5a54"}, kSrc{"#807e77"}, kSrcHover{"#cfc392"};
const news::pages::Dw kBelow{"#f69a96"}, kAbove{"#8fccd6"};
}

RangePanel::RangePanel(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  setAttribute(Qt::WA_OpaquePaintEvent);
}

void RangePanel::setEvent(const news::Event& e) { m_e = e; m_hoverLink = -1; update(); }

QList<RangePanel::Link> RangePanel::links() const { return m_links; }

void RangePanel::paintEvent(QPaintEvent*) {
  QPainter p(this);
  const double W = width(), R = W - 17;
  p.fillRect(rect(), kBg);
  p.fillRect(QRectF(0, 0, 1, height()), kBorder);
  m_links.clear();
  p.translate(0, -m_off);   // short windows: the panel scrolls with the wheel
  const news::Event& e = m_e;

  // close glyph + date
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(QPen(m_hoverClose ? kSrcHover : theme::map("#5b5a54"), 1.3));
  p.drawLine(QPointF(19, 10), QPointF(27, 18)); p.drawLine(QPointF(27, 10), QPointF(19, 18));
  p.setRenderHint(QPainter::Antialiasing, false);
  p.setFont(theme::mono(12)); p.setPen(kDate);
  const QLocale en = i18n::locale();
  const QDateTime lt = e.time.toLocalTime();
  // an all-day event is stamped at midnight New York time: its date is New York's, not the local one (one day early
  // west of New York)
  const QDate day = e.allDay ? e.time.toTimeZone(QTimeZone("America/New_York")).date() : lt.date();
  p.drawText(QRectF(40, 4, W - 60, 20), Qt::AlignLeft | Qt::AlignVCenter,
             en.toString(day, "ddd d MMM") + (e.allDay ? "" : ", " + lt.toString("HH:mm")));
  p.fillRect(QRectF(0, 33, W, 1), kRule);

  // scale shared by the range bar and the bank bars
  QList<double> vals;
  for (const auto* v : {&e.low, &e.high, &e.previous, &e.forecast, &e.actual}) if (v->num()) vals << v->scaled();
  for (const auto& b : e.banks) if (b.value.num()) vals << b.value.scaled();
  double lo = 0, hi = 1;
  const bool range = e.hasRange();
  if (range) { lo = e.low.scaled(); hi = e.high.scaled(); }
  else if (!vals.isEmpty()) {
    lo = *std::min_element(vals.begin(), vals.end()); hi = *std::max_element(vals.begin(), vals.end());
  }
  if (hi - lo < 1e-12) { const double d = std::max(std::abs(lo) * 0.1, 0.1); lo -= d; hi += d; }
  const double bx0 = 176, bx1 = std::max(bx0 + 60, W - 102), tx0 = 152, tx1 = bx1 + 26;
  auto X = [&](double v) { return std::clamp(bx0 + (v - lo) / (hi - lo) * (bx1 - bx0), tx0 + 1, tx1 - 1); };
  const news::Value& unitLike = e.forecast.has() ? e.forecast : e.actual.has() ? e.actual : e.previous;
  auto txt = [&](const news::Value& v) { return v.has() ? v.text : QString("—"); };

  // ECONOMISTS' RANGE
  p.setFont(theme::mono(11, QFont::Bold, 2.2)); p.setPen(kLabel);
  p.drawText(QRectF(17, 51, 300, 20), Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("news", "ECONOMISTS' RANGE"));
  p.setFont(theme::mono(14, QFont::Bold)); p.setPen(kWhite);
  p.drawText(QRectF(R - 220, 50, 220, 22), Qt::AlignRight | Qt::AlignVCenter,
             range ? e.low.text + " → " + e.high.text : QString("—"));
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(Qt::NoPen); p.setBrush(kTrack);
  p.drawRoundedRect(QRectF(tx0, 90, tx1 - tx0, 4), 2, 2);
  if (range) {
    QLinearGradient g(bx0, 0, bx1, 0);
    g.setColorAt(0, theme::map("#c46757")); g.setColorAt(0.25, theme::map("#ce9239")); g.setColorAt(0.5, theme::map("#d8af1c"));
    g.setColorAt(0.75, theme::map("#89ae6c")); g.setColorAt(1, theme::map("#22a5b2"));
    p.setBrush(g); p.drawRoundedRect(QRectF(bx0, 87, bx1 - bx0, 10), 5, 5);
  }
  p.setBrush(Qt::NoBrush);
  p.setRenderHint(QPainter::Antialiasing, false);
  auto marker = [&](double v, const QColor& c, bool dashed, double y0, double y1) {
    const double x = std::round(X(v));
    p.setPen(QPen(c, 2, dashed ? Qt::DashLine : Qt::SolidLine, Qt::FlatCap));
    if (dashed) { QPen pen(c, 1.2); pen.setDashPattern({3, 2}); p.setPen(pen); }
    p.drawLine(QPointF(x, y0), QPointF(x, y1));
  };
  if (e.previous.num()) marker(e.previous.scaled(), kPrevMark, true, 78, 108);
  if (e.actual.num()) marker(e.actual.scaled(), kWhite, false, 78, 108);
  if (e.forecast.num()) marker(e.forecast.scaled(), kCons, false, 78, 108);
  p.setFont(theme::mono(10));
  if (range) {
    p.setPen(kDim);
    p.drawText(QRectF(bx0 + 3, 112, 120, 16), Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("news", "low %1").arg(e.low.text));
    p.drawText(QRectF(bx1 - 120, 112, 120, 16), Qt::AlignRight | Qt::AlignVCenter, QCoreApplication::translate("news", "high %1").arg(e.high.text));
  } else {
    p.setPen(kDim);
    p.drawText(QRectF(17, 112, W - 34, 16), Qt::AlignLeft | Qt::AlignVCenter,
               vals.isEmpty() ? QCoreApplication::translate("news", "no figures for this release") : QCoreApplication::translate("news", "no economists' survey range published for free"));
  }
  if (e.actual.has()) {
    const QFont f = theme::mono(10, QFont::Bold); p.setFont(f);
    const QString t = QCoreApplication::translate("news", "actual %1").arg(e.actual.text);
    const double w = QFontMetricsF(f).horizontalAdvance(t);
    const double cx = std::clamp(X(e.actual.scaled()), 17 + w / 2, R - w / 2);
    p.fillRect(QRectF(cx - w / 2 - 3, 112, w + 6, 16), kBg);
    p.setPen(kWhite); p.drawText(QRectF(cx - w / 2 - 3, 112, w + 6, 16), Qt::AlignCenter, t);
  }

  // BANKS' OWN FORECASTS
  p.setFont(theme::mono(11, QFont::Bold, 2.2)); p.setPen(kLabel);
  p.drawText(QRectF(17, 137, 300, 20), Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("news", "BANKS' OWN FORECASTS"));
  double y = 163;
  if (e.banks.isEmpty()) {
    p.setFont(theme::mono(11)); p.setPen(kDim);
    p.drawText(QRectF(17, y, W - 34, 22), Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("news", "No bank forecast found for this release"));
    y += 30;
  }
  for (const auto& b : e.banks) {
    p.setFont(theme::mono(13)); p.setPen(kBankName);
    p.drawText(QRectF(10, y, 132, 22), Qt::AlignRight | Qt::AlignVCenter, b.bank);
    p.fillRect(QRectF(tx0, y, tx1 - tx0, 22), kBankTrack);
    if (b.value.num()) {
      QLinearGradient g(tx0, 0, X(b.value.scaled()), 0);
      g.setColorAt(0, theme::map("#484010")); g.setColorAt(1, theme::map("#cfb210"));
      p.fillRect(QRectF(tx0, y, X(b.value.scaled()) - tx0, 22), g);
    }
    if (e.previous.num()) marker(e.previous.scaled(), theme::map("#8d8a6f"), true, y, y + 22);
    if (e.actual.num()) marker(e.actual.scaled(), kWhite, false, y, y + 22);
    p.setFont(theme::mono(14, QFont::Bold));
    const bool above = b.value.num() && e.forecast.num() && b.value.scaled() > e.forecast.scaled();
    const bool below = b.value.num() && e.forecast.num() && b.value.scaled() < e.forecast.scaled();
    p.setPen(above ? kAbove : below ? kBelow : kBankVal);
    p.drawText(QRectF(R - 80, y, 80, 22), Qt::AlignRight | Qt::AlignVCenter, b.value.has() ? b.value.text : "—");
    y += 30;
  }

  // boxes
  y = std::max(y + 6, 199.0);
  const double bw = (W - 34 - 9) / 2;
  struct Box { QString lbl, val; QColor c; };
  const QList<Box> boxes{{QCoreApplication::translate("news", "CONSENSUS"), txt(e.forecast), theme::map("#eee0a9")},
                         {QCoreApplication::translate("news", "RANGE"), range ? e.low.text + " - " + e.high.text : QString("—"), kWhite},
                         {QCoreApplication::translate("news", "PREVIOUS"), txt(e.previous), theme::map("#a2a29c")}, {QCoreApplication::translate("news", "ACTUAL"), txt(e.actual), kWhite}};
  for (int i = 0; i < 4; ++i) {
    const QRectF r(17 + (i % 2) * (bw + 9), y + (i / 2) * 61, bw, 53);
    p.fillRect(r, kBoxBg); p.setPen(kBoxBorder); p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
    p.setFont(theme::mono(9, QFont::Normal, 2.2)); p.setPen(kBoxLbl);
    p.drawText(QRectF(r.x() + 10, r.y() + 7, r.width(), 14), Qt::AlignLeft | Qt::AlignVCenter, boxes[i].lbl);
    p.setFont(theme::mono(14, QFont::Bold)); p.setPen(boxes[i].val == "—" ? kDim : boxes[i].c);
    p.drawText(QRectF(r.x() + 10, r.y() + 24, r.width() - 14, 22), Qt::AlignLeft | Qt::AlignVCenter, boxes[i].val);
  }
  y += 61 + 53 + 8;   // legend 18 px under the boxes (reference y 331)

  // legend
  {
    p.setFont(theme::mono(W < 440 ? 10 : 11)); const QFontMetricsF fm(p.font());
    double x = 17; const double ly = y + 10;
    auto item = [&](const QString& t, std::function<void(double)> glyph, double gw) {
      glyph(x); x += gw; p.setPen(kLegend); p.drawText(QPointF(x, ly + 4), t); x += fm.horizontalAdvance(t) + (W < 440 ? 6 : 9);
    };
    item(QCoreApplication::translate("news", "consensus"), [&](double gx) { p.fillRect(QRectF(gx, ly - 6, 2.5, 12), theme::map("#fbeaaa")); }, 6);
    item(QCoreApplication::translate("news", "previous"), [&](double gx) { QPen pen(theme::map("#91908d"), 1); pen.setDashPattern({2, 2}); p.setPen(pen); p.drawLine(QPointF(gx + 1, ly - 6), QPointF(gx + 1, ly + 6)); }, 5);
    item(QCoreApplication::translate("news", "actual"), [&](double gx) { p.fillRect(QRectF(gx, ly - 6, 2.5, 12), kWhite); }, 6);
    p.setRenderHint(QPainter::Antialiasing, true);
    item(QCoreApplication::translate("news", "below ·"), [&](double gx) { p.setPen(Qt::NoPen); p.setBrush(kBelow); p.drawEllipse(QPointF(gx + 3, ly), 3, 3); }, 9);
    item(QCoreApplication::translate("news", "above consensus"), [&](double gx) { p.setPen(Qt::NoPen); p.setBrush(kAbove); p.drawEllipse(QPointF(gx + 3, ly), 3, 3); }, 9);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setBrush(Qt::NoBrush);
  }
  y += 30;
  p.fillRect(QRectF(17, y, W - 34, 1), kRule);
  y += 12;
  p.setFont(theme::mono(10, QFont::Normal, 2.2)); p.setPen(kSrcLbl);
  p.drawText(QRectF(17, y, 200, 16), Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("news", "SOURCES"));
  y += 22;
  const QFont sf = theme::mono(12);
  auto source = [&](const QString& text, const QString& url, const QString& right, bool icon) {
    const int idx = int(m_links.size());
    const bool hov = idx == m_hoverLink && !url.isEmpty();
    p.setFont(sf);
    double x = 17;
    if (icon) {
      p.setPen(QPen((hov ? kSrcHover : kSrc).c(), 1));
      p.drawRect(QRectF(x + 0.5, y + 6.5, 7, 7)); p.drawLine(QPointF(x + 4, y + 9.5), QPointF(x + 9, y + 4.5));
      x += 14;
    }
    const double maxW = R - x - (right.isEmpty() ? 0 : 60);
    const QString t = QFontMetricsF(sf).elidedText(text, Qt::ElideRight, maxW);
    p.setPen(hov ? kSrcHover : kSrc);
    p.drawText(QRectF(x, y, maxW, 20), Qt::AlignLeft | Qt::AlignVCenter, t);
    if (!right.isEmpty()) { p.setPen(theme::map("#54534d")); p.drawText(QRectF(R - 60, y, 60, 20), Qt::AlignRight | Qt::AlignVCenter, right); }
    m_links << Link{QRectF(17, y, R - 17, 20), url, t != text ? text : QString()};
    y += 20;
  };
  if (e.forecast.has() || e.previous.has())
    source(QCoreApplication::translate("news", "ForexFactory weekly calendar · %1: %2 — consensus & previous").arg(e.currency, e.title),
           "https://www.forexfactory.com/calendar", {}, true);
  if (e.actual.has())
    source(QCoreApplication::translate("news", "Actual · %1 · %2recorded %3").arg(news::actualSourceText(e), e.actualDetail.isEmpty() ? QString() : news::actualDetailText(e) + " · ",
                                                                     en.toString(e.actualAt.toLocalTime(), "d MMM HH:mm")),
           e.actualSource.startsWith("Myfxbook") ? "https://www.myfxbook.com/forex-economic-calendar" : QString(), {}, true);
  for (const QString& t : {news::alsoText(e), news::otherText(e), news::revisedText(e)}) if (!t.isEmpty()) source(t, QString(), {}, true);
  for (const auto& b : e.banks)
    source(b.bank + " — " + b.source, b.url, en.toString(b.published, "d MMM"), true);
  if (!range) {
    y += 8;
    p.setFont(theme::mono(10)); p.setPen(kDim);
    p.drawText(QRectF(17, y, R - 17, 64), Qt::AlignLeft | Qt::TextWordWrap,
               QCoreApplication::translate("news", "Economists' minimum and maximum come from paid consensus surveys (Econoday, Bloomberg, Reuters). No free source publishes them, so the range stays empty."));
    y += 64;
  }
  Q_UNUSED(unitLike);
  m_contentH = y + 12;
  const double maxOff = std::max(0.0, m_contentH - height());
  if (m_off > maxOff) { m_off = maxOff; update(); }
}

void RangePanel::wheelEvent(QWheelEvent* e) {
  const double maxOff = std::max(0.0, m_contentH - height());
  const double d = !e->pixelDelta().isNull() ? e->pixelDelta().y() : e->angleDelta().y() / 120.0 * 60;
  const double n = std::clamp(m_off - d, 0.0, maxOff);
  if (n != m_off) { m_off = n; update(); }
  e->accept();
}

void RangePanel::updateHover(const QPointF& viewPos) {
  const QPointF pos = viewPos + QPointF(0, m_off);
  int h = -1;
  for (int i = 0; i < m_links.size(); ++i) if (m_links[i].r.contains(pos) && !m_links[i].url.isEmpty()) h = i;
  QString tip;   // elided source lines: full text in a tooltip
  for (const auto& l : m_links) if (l.r.contains(pos) && !l.full.isEmpty()) tip = l.full;
  if (!tip.isEmpty()) QToolTip::showText(mapToGlobal(viewPos.toPoint()), ui::plainTip(tip), this); else QToolTip::hideText();
  const bool hc = closeHit().contains(pos);
  setCursor(h >= 0 || hc ? Qt::PointingHandCursor : Qt::ArrowCursor);
  if (h != m_hoverLink || hc != m_hoverClose) { m_hoverLink = h; m_hoverClose = hc; update(); }
}
QRectF RangePanel::closeHit() const { return width() < 520 ? QRectF(0, 0, 48, 34) : QRectF(12, 4, 22, 20); }
void RangePanel::mouseMoveEvent(QMouseEvent* e) {
  m_pin = false;
  if (m_dragY >= 0 && (e->buttons() & Qt::LeftButton)) {
    const double dy = e->position().y() - m_dragY;
    if (std::abs(dy) > 8) m_dragged = true;
    if (m_dragged) {
      const double n = std::clamp(m_dragOff - dy, 0.0, std::max(0.0, m_contentH - height()));
      if (n != m_off) { m_off = n; update(); }
      return;
    }
  }
  updateHover(e->position());
}
void RangePanel::mouseReleaseEvent(QMouseEvent* e) {
  if (m_dragY < 0) return;
  m_dragY = -1;
  if (m_dragged) { m_dragged = false; return; }
  updateHover(e->position());
  if (m_hoverClose) { emit closeRequested(); return; }
  if (m_hoverLink >= 0) ui::openWebLink(m_links[m_hoverLink].url);
}
void RangePanel::mousePressEvent(QMouseEvent* e) {
  if (ui::touch::enabled() && e->button() == Qt::LeftButton) { m_dragY = e->position().y(); m_dragOff = m_off; m_dragged = false; return; }
  updateHover(e->position());
  if (m_hoverClose) { emit closeRequested(); return; }
  if (m_hoverLink >= 0) ui::openWebLink(m_links[m_hoverLink].url);
}
void RangePanel::leaveEvent(QEvent*) {
  if (m_pin) return; m_hoverLink = -1; m_hoverClose = false; update(); }
}  // namespace news::pages
