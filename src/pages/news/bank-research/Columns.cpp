#include "pages/news/bank-research/Columns.h"
#include "ui/Kit.h"
#include "pages/news/bank-research/Look.h"
#include "banks/Registry.h"
#include "banks/Forecasts.h"
#include "ui/Theme.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>
#include <QLabel>
#include <QToolTip>
#include <QHelpEvent>
#include <QDesktopServices>
#include <QVariantAnimation>
#include <QLocale>
#include <QUrl>
#include <QCoreApplication>
#include "core/I18n.h"
#include <QWheelEvent>

namespace banks::res {
using namespace look;

static bool g_blink = true;
bool blinkOn() { return g_blink; }
void setBlink(bool on) { g_blink = on; }
void drawBlockCursor(QPainter& p, const QPointF& tl, qreal w, qreal h) {
  if (g_blink) p.fillRect(QRectF(tl, QSizeF(w, h)), cursor);
}

// small line icons -------------------------------------------------------------------------------------------
static void iconBank(QPainter& p, const QRectF& r, const QColor& c) {
  p.save(); p.setPen(QPen(QColor(c), 1.2)); p.setBrush(Qt::NoBrush);
  const qreal x = r.x(), y = r.y(), w = r.width(), h = r.height();
  QPolygonF roof; roof << QPointF(x, y + h * 0.35) << QPointF(x + w / 2, y) << QPointF(x + w, y + h * 0.35);
  p.drawPolyline(roof);
  p.drawLine(QPointF(x, y + h * 0.35), QPointF(x + w, y + h * 0.35));
  for (int i = 0; i < 3; ++i) { const qreal cx = x + w * (0.22 + i * 0.28); p.drawLine(QPointF(cx, y + h * 0.45), QPointF(cx, y + h * 0.85)); }
  p.drawLine(QPointF(x, y + h), QPointF(x + w, y + h));
  p.restore();
}
static void iconDoc(QPainter& p, const QRectF& r, const QColor& c) {
  p.save(); p.setPen(QPen(QColor(c), 1.2)); p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(r.adjusted(1, 0, -1, 0), 1.5, 1.5);
  for (int i = 0; i < 3; ++i) p.drawLine(QPointF(r.x() + 3.5, r.y() + 3.5 + i * 3), QPointF(r.right() - 3.5, r.y() + 3.5 + i * 3));
  p.restore();
}
static void iconNews(QPainter& p, const QRectF& r, const QColor& c) {
  p.save(); p.setPen(QPen(QColor(c), 1.1)); p.setBrush(Qt::NoBrush);
  p.drawRect(r.adjusted(0, 1, 0, -1));
  p.fillRect(QRectF(r.x() + 2, r.y() + 3, r.width() / 2 - 2, r.height() / 2 - 2), c);
  p.drawLine(QPointF(r.center().x() + 1, r.y() + 4), QPointF(r.right() - 2, r.y() + 4));
  p.drawLine(QPointF(r.x() + 2, r.bottom() - 3), QPointF(r.right() - 2, r.bottom() - 3));
  p.restore();
}
static void iconDb(QPainter& p, const QRectF& r, const QColor& c) {
  p.save(); p.setPen(QPen(QColor(c), 1.1)); p.setBrush(Qt::NoBrush);
  const qreal eh = r.height() * 0.3;
  p.drawEllipse(QRectF(r.x(), r.y(), r.width(), eh));
  p.drawLine(QPointF(r.x(), r.y() + eh / 2), QPointF(r.x(), r.bottom() - eh / 2));
  p.drawLine(QPointF(r.right(), r.y() + eh / 2), QPointF(r.right(), r.bottom() - eh / 2));
  p.drawArc(QRectF(r.x(), r.bottom() - eh, r.width(), eh), 180 * 16, 180 * 16);
  p.drawArc(QRectF(r.x(), r.center().y() - eh / 2, r.width(), eh), 180 * 16, 180 * 16);
  p.restore();
}
static void iconSignal(QPainter& p, const QRectF& r, const QColor& c) {
  p.save(); p.setPen(QPen(QColor(c), 1.1)); p.setBrush(c);
  p.drawEllipse(r.center(), 1.4, 1.4); p.setBrush(Qt::NoBrush);
  for (int k = 1; k <= 2; ++k) {
    const qreal rr = 2.5 * k + 0.5;
    p.drawArc(QRectF(r.center().x() - rr, r.center().y() - rr, 2 * rr, 2 * rr), -50 * 16, 100 * 16);
    p.drawArc(QRectF(r.center().x() - rr, r.center().y() - rr, 2 * rr, 2 * rr), 130 * 16, 100 * 16);
  }
  p.restore();
}

static void spacedText(QPainter& p, const QPointF& baseline, const QString& t) { p.drawText(baseline, t); }

// ============================================================================================ Strip
Strip::Strip(QWidget* parent) : QWidget(parent) {}
void Strip::paintEvent(QPaintEvent*) {
  QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
  QLinearGradient g(0, 0, 0, height()); g.setColorAt(0, theme::map("#1a1305")); g.setColorAt(1, strip);
  p.fillRect(rect(), g);
  p.fillRect(QRect(0, height() - 1, width(), 1), stripBorder);
  iconBank(p, QRectF(13, 11, 11, 11), theme::map("#d6c27a"));
  p.setPen(Qt::NoPen); p.setBrush(blinkOn() ? dot : theme::mix(dot, strip, 0.5));
  p.drawEllipse(QPointF(37, 17), 2.6, 2.6);
  QFont f = theme::mono(11, QFont::Normal, 1.8);
  QString title = QCoreApplication::translate("banks", "GLOBAL BANKING NETWORK — LIVE SURVEILLANCE");
  const QFont bf = theme::mono(10, QFont::Bold, 0.4);
  const QString badge = m_badge;
  const qreal bw = QFontMetricsF(bf).horizontalAdvance(badge) + 22;
  // narrow windows (phones): the badge goes when it would run into the title (the registry below says the same),
  // then the title tightens and is elided at the window edge
  const bool showBadge = 50 + QFontMetricsF(f).horizontalAdvance(title) + 20 + 14 + bw <= width();
  if (!showBadge && 50 + QFontMetricsF(f).horizontalAdvance(title) + 14 > width()) {
    f = theme::mono(10, QFont::Normal, 0.6);
    title = QFontMetricsF(f).elidedText(title, Qt::ElideRight, width() - 50 - 14);
  }
  p.setFont(f); p.setPen(dim);
  spacedText(p, QPointF(50, 21), title);
  drawBlockCursor(p, QPointF(50 + QFontMetricsF(f).horizontalAdvance(title) + 2, 10), 6, 14);
  if (!showBadge) return;
  // badge
  const QRectF br(width() - 14 - bw, 8, bw, 20);
  QLinearGradient bg(br.topLeft(), br.bottomLeft()); bg.setColorAt(0, theme::map("#524604")); bg.setColorAt(1, theme::map("#3a3103"));
  p.setPen(QPen(QColor(badgeBorder), 1)); p.setBrush(bg);
  p.drawRoundedRect(br.adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);
  p.setPen(Qt::NoPen); p.setBrush(badgeText);
  p.drawEllipse(QPointF(br.x() + 8, br.center().y()), 1.6, 1.6);
  p.setFont(bf); p.setPen(badgeText);
  p.drawText(br.adjusted(14, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, badge);
}

// ============================================================================================ DropButton
DropButton::DropButton(QWidget* parent) : QWidget(parent) { setCursor(Qt::PointingHandCursor); setAttribute(Qt::WA_Hover); }
void DropButton::setItems(const QStringList& l, const QStringList& k) {
  const QString keep = currentKey();
  m_labels = l; m_keys = k; m_cur = qMax(0, int(k.indexOf(keep)));
  setToolTip(m_labels.value(m_cur));       // full label when the box elides it
  update();
}
void DropButton::setCurrentKey(const QString& key) { const int i = int(m_keys.indexOf(key)); if (i >= 0) { m_cur = i; setToolTip(m_labels.value(i)); update(); } }
void DropButton::paintEvent(QPaintEvent*) {
  QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
  p.fillRect(rect(), dropBg);
  p.setPen(m_hover ? theme::map("#3a3620") : theme::map("#131209")); p.setBrush(Qt::NoBrush);
  p.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
  // the label tightens (letter spacing, then 11 px) before it is elided ("ALL PUBLICATIONS" in French was cut
  // even at 1728); the tooltip keeps the full label
  const QString label = m_labels.value(m_cur);
  QFont f = theme::mono(12, QFont::Medium, 0.9);
  for (const QFont& c : {theme::mono(12, QFont::Medium, 0.9), theme::mono(12, QFont::Medium, 0), theme::mono(11, QFont::Medium, 0)}) {
    f = c;
    if (QFontMetricsF(c).horizontalAdvance(label) <= width() - 36) break;
  }
  p.setFont(f); p.setPen(theme::map("#f8f8f6"));
  const QString t = QFontMetricsF(f).elidedText(label, Qt::ElideRight, width() - 36);
  p.drawText(QRectF(11, 0, width() - 30, height()), Qt::AlignVCenter | Qt::AlignLeft, t);
  QPen pen(theme::map("#f8f8f6"), 1.8); pen.setCapStyle(Qt::RoundCap); pen.setJoinStyle(Qt::RoundJoin);
  p.setPen(pen);
  const QPointF c(width() - 16, height() / 2.0);
  QPolygonF chev; chev << c + QPointF(-4, -2) << c + QPointF(0, 2) << c + QPointF(4, -2);
  p.drawPolyline(chev);
}
void DropButton::mousePressEvent(QMouseEvent*) {
  QMenu m(this);
  for (int i = 0; i < m_labels.size(); ++i) {
    QAction* a = m.addAction(m_labels[i]);
    a->setCheckable(true); a->setChecked(i == m_cur);
    a->setData(m_keys.value(i));   // by key: setItems() may reorder the list while the menu is open
  }
  m.setMinimumWidth(width());
  if (QAction* a = m.exec(mapToGlobal(QPoint(0, height() + 2)))) {
    const int i = int(m_keys.indexOf(a->data().toString()));
    if (i < 0) return;   // the entry went away meanwhile
    m_cur = i; setToolTip(m_labels.value(m_cur)); update();
    if (onChanged) onChanged(currentKey());
  }
}
void DropButton::enterEvent(QEnterEvent*) { m_hover = true; update(); }
void DropButton::leaveEvent(QEvent*) { m_hover = false; update(); }

// ============================================================================================ NoteList
namespace {
QFont titleFont() { return theme::condensed(16, QFont::DemiBold); }
QString ageText(const QDateTime& t) {
  const qint64 s = t.secsTo(QDateTime::currentDateTimeUtc());
  if (s < 3600) return QString("%1m").arg(qMax<qint64>(1, s / 60));
  if (s < 48 * 3600) return QString("%1h").arg(s / 3600);
  return QString("%1d").arg(s / 86400);
}
QString groupOf(const QDateTime& utc) {
  const QDate d = utc.toLocalTime().date(), today = QDate::currentDate();
  if (d == today) return QCoreApplication::translate("banks", "TODAY");
  if (d == today.addDays(-1)) return QCoreApplication::translate("banks", "YESTERDAY");
  return i18n::locale().toString(d, d.year() == today.year() ? "ddd d MMM" : "d MMM yyyy").toUpper();
}
QStringList wrap(const QString& text, const QFontMetricsF& fm, qreal w, int maxLines) {
  QStringList lines; QString cur;
  for (const QString& word : text.split(' ', Qt::SkipEmptyParts)) {
    const QString cand = cur.isEmpty() ? word : cur + ' ' + word;
    if (fm.horizontalAdvance(cand) <= w || cur.isEmpty()) cur = cand;
    else { lines << cur; cur = word; }
  }
  if (!cur.isEmpty()) lines << cur;
  if (lines.size() > maxLines) {
    QString last = lines.mid(maxLines - 1).join(' ');
    lines = lines.mid(0, maxLines - 1);
    lines << fm.elidedText(last, Qt::ElideRight, w);
  }
  return lines;
}
constexpr int kTitleX = 38;
}

NoteList::NoteList(QWidget* parent) : QWidget(parent) { setMouseTracking(true); }
void NoteList::setNotes(const QList<banks::Note>& n) {
  m_notes = n; m_hover = -1; relayout(); update();
}
void NoteList::setEmptyText(const QString& t, const QString& b, const QString& l, const QString& u) {
  m_emptyTitle = t; m_emptyBody = b; m_emptyLink = l; m_emptyUrl = u; update();
}
QSize NoteList::sizeHint() const { return QSize(362, m_items.isEmpty() ? 200 : m_items.last().y + m_items.last().h + 10); }
void NoteList::resizeEvent(QResizeEvent* e) { if (e->oldSize().width() != width()) relayout(); }
void NoteList::relayout() {
  m_items.clear();
  m_layoutDay = QDate::currentDate();
  const QFontMetricsF fm(titleFont());
  const qreal tw = width() - kTitleX - 12;
  int y = 0; QString group;
  for (int i = 0; i < m_notes.size(); ++i) {
    const QString g = groupOf(m_notes[i].published);
    if (g != group) { group = g; m_items << Item{-1, g, y, 24, {}}; y += 24; }
    Item it; it.note = i; it.y = y; it.lines = wrap(m_notes[i].title, fm, tw, 2);
    it.h = 57 + 18 * int(it.lines.size());
    m_items << it; y += it.h;
  }
  setMinimumHeight(qMax(200, y + 10));
}
int NoteList::rowTop(int row) const { for (const auto& it : m_items) if (it.note == row) return it.y; return 0; }
int NoteList::itemAt(const QPointF& p) const {
  for (int i = 0; i < m_items.size(); ++i) if (m_items[i].note >= 0 && p.y() >= m_items[i].y && p.y() < m_items[i].y + m_items[i].h) return m_items[i].note;
  return -1;
}
void NoteList::paintEvent(QPaintEvent* e) {
  // the TODAY / YESTERDAY group labels are set at layout time: past midnight they named the wrong days. Only the
  // labels change (groups stay split by date), so the heights are the same.
  if (m_layoutDay != QDate::currentDate()) relayout();
  QPainter p(this); p.setRenderHint(QPainter::Antialiasing); p.setRenderHint(QPainter::TextAntialiasing);
  p.fillRect(rect(), leftBg);
  if (m_notes.isEmpty()) {
    p.setFont(theme::mono(10, QFont::Bold, 2)); p.setPen(groupLabel);
    p.drawText(QRectF(12, 16, width() - 24, 20), m_emptyTitle);
    p.setFont(theme::sans(12)); p.setPen(theme::map("#8f8b80"));
    QRectF br;
    p.drawText(QRectF(12, 42, width() - 24, 200), Qt::TextWordWrap, m_emptyBody, &br);
    m_linkRect = {};
    if (!m_emptyLink.isEmpty()) {
      p.setFont(theme::mono(10, QFont::Bold, 1.5)); p.setPen(gold);
      m_linkRect = QRectF(12, br.bottom() + 12, width() - 24, 18);
      p.drawText(m_linkRect, Qt::AlignLeft | Qt::AlignVCenter, m_emptyLink);
    }
    return;
  }
  const QFont fBank = theme::display(13, QFont::Bold, 0.7), fCat = theme::mono(10, QFont::Normal, 0.2),
              fAge = theme::mono(10), fTitle = titleFont(), fFoot = theme::mono(9, QFont::Normal, 0.6),
              fGroup = theme::mono(10, QFont::Bold, 1.4);
  const QFontMetricsF mBank(fBank), mCat(fCat);
  const qreal dpr = devicePixelRatioF();
  for (const Item& it : m_items) {
    if (it.y + it.h < e->rect().top() || it.y > e->rect().bottom()) continue;
    if (it.note < 0) {
      p.setFont(fGroup); p.setPen(groupLabel);
      p.drawText(QPointF(8, it.y + 15), it.group);
      continue;
    }
    const banks::Note& n = m_notes[it.note];
    const bool sel = n.id == m_selected, hov = it.note == m_hover || it.note == m_forcedHover, prs = it.note == m_pressed;
    const QRectF row(0, it.y, width(), it.h);
    // measured on the reference: hover = gold wash fading out by ~70 % of the width, selected = brighter wash
    // across the whole row (no separate bar)
    if (sel || hov) {
      QLinearGradient g(row.topLeft(), row.topRight());
      if (sel) { g.setColorAt(0, theme::map("#3e3205")); g.setColorAt(0.42, theme::map("#302a0b")); g.setColorAt(0.75, theme::map("#231b04")); g.setColorAt(1, theme::map("#120802")); }
      else { g.setColorAt(0, theme::map("#282205")); g.setColorAt(0.42, theme::map("#1a1605")); g.setColorAt(0.7, leftBg); g.setColorAt(1, leftBg); }
      p.fillRect(row, g);
    }
    const qreal dx = (prs || (hov && !sel)) ? -3 : (sel ? -3 : 0);
    p.fillRect(QRectF(0, it.y + it.h - 1, width(), 1), rowSep);
    p.save(); p.translate(dx, 0);
    const qreal y = it.y;
    p.drawPixmap(QPointF(8, y + 7), banks::logo(n.bankId, 18, dpr));
    const banks::Bank* b = banks::bank(n.bankId);
    const QString bn = b ? b->shortName : n.bankId.toUpper();
    p.setFont(fBank); p.setPen(white);
    p.drawText(QPointF(33, y + 21), bn);
    const qreal cx = 33 + mBank.horizontalAdvance(bn) + 6;
    const QString cat = (n.topic.isEmpty() || n.type == "FORECASTS") ? banks::typeLabel(n.type) : n.topic.toUpper();
    const qreal kRightX = width() - 11;
    const QString catE = mCat.elidedText(cat, Qt::ElideRight, kRightX - 40 - cx);
    const QRectF cr(cx, y + 7, mCat.horizontalAdvance(catE) + 12, 18);
    if (!sel) { p.setPen(QPen(theme::map("#1a1712"), 1)); p.setBrush(theme::map(0, 0, 0, hov ? 70 : 255)); p.drawRect(cr.adjusted(0.5, 0.5, -0.5, -0.5)); }
    p.setFont(fCat); p.setPen(theme::map("#e6cf72"));
    p.drawText(cr.adjusted(6, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, catE);
    p.setFont(fAge); p.setPen(age);
    const QString a = ageText(n.published);
    p.drawText(QRectF(kRightX - 60, y + 7, 60, 16), Qt::AlignRight | Qt::AlignVCenter, a);
    p.setFont(fTitle); p.setPen(white);
    for (int l = 0; l < it.lines.size(); ++l) p.drawText(QPointF(kTitleX, y + 43 + 18 * l), it.lines[l]);
    const qreal fy = y + 43 + 18 * (it.lines.size() - 1) + 21;
    iconDoc(p, QRectF(kTitleX, fy - 8, 8, 9), dimmer);
    p.setFont(fFoot); p.setPen(dimmer);
    p.drawText(QPointF(kTitleX + 12, fy), n.type == "PODCAST" ? QCoreApplication::translate("banks", "PODCAST · LISTEN IN THE BROWSER ↗")
                                        : n.type == "VIDEO" ? QCoreApplication::translate("banks", "VIDEO · WATCH ON YOUTUBE ↗")
                                        : n.pdfUrl.isEmpty() ? QCoreApplication::translate("banks", "WEB · OPEN IN TERMINAL") : QCoreApplication::translate("banks", "PDF · OPEN IN TERMINAL"));
    p.restore();
  }
}
void NoteList::mouseMoveEvent(QMouseEvent* e) {
  const int h = itemAt(e->position());
  if (m_notes.isEmpty()) { setCursor(m_linkRect.contains(e->position()) ? Qt::PointingHandCursor : Qt::ArrowCursor); return; }
  if (h != m_hover) { m_hover = h; setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor); update(); }
}
void NoteList::mousePressEvent(QMouseEvent* e) {
  if (m_notes.isEmpty()) { if (m_linkRect.contains(e->position()) && !m_emptyUrl.isEmpty()) QDesktopServices::openUrl(QUrl(m_emptyUrl)); return; }
  m_pressed = itemAt(e->position()); update();
}
void NoteList::mouseReleaseEvent(QMouseEvent* e) {
  const int r = itemAt(e->position());
  if (r >= 0 && r == m_pressed && onOpen) onOpen(m_notes[r].id);
  m_pressed = -1; update();
}
void NoteList::leaveEvent(QEvent*) { m_hover = -1; m_pressed = -1; update(); }

// ============================================================================================ LeftColumn
static const char* kTabs[] = {QT_TRANSLATE_NOOP("banks", "RESEARCH"), QT_TRANSLATE_NOOP("banks", "NEWS"), QT_TRANSLATE_NOOP("banks", "DATA"), QT_TRANSLATE_NOOP("banks", "FILINGS")};

LeftColumn::LeftColumn(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  m_banks = new DropButton(this);
  m_types = new DropButton(this);
  m_scroll = new QScrollArea(this);
  m_scroll->setFrameShape(QFrame::NoFrame);
  m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_scroll->setWidgetResizable(true);
  m_list = new NoteList(m_scroll);
  m_scroll->setWidget(m_list);
  m_list->onOpen = [this](const QString& id) { if (onOpen) onOpen(id); };
  m_textScroll = new QScrollArea(this);
  m_textScroll->setFrameShape(QFrame::NoFrame);
  m_textScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_textScroll->setWidgetResizable(true);
  m_text = new QLabel(m_textScroll);
  m_text->setWordWrap(true); m_text->setTextFormat(Qt::RichText);
  // links: the bank registry's research pages and filing URLs built from SEC JSON, opened through the app's checked
  // opener (web links only), never straight by the label
  connect(m_text, &QLabel::linkActivated, m_text, [](const QString& link) {
    if (link.startsWith(QLatin1String("https://"))) ui::openWebLink(link);
  });
  m_text->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  m_text->setContentsMargins(10, 8, 12, 12);
  banks::setQss(m_text, QString("QLabel { background: %1; color: #a6a59f; font-family: '%2'; font-size: 11px; }").arg(leftBg.dw().name(), theme::mono(11).family()));
  m_textScroll->setWidget(m_text);
  m_textScroll->hide();
  // smooth wheel scrolling (≈150 ms)
  m_wheel = new QVariantAnimation(this);
  m_wheel->setDuration(150); m_wheel->setEasingCurve(QEasingCurve::OutCubic);
  QObject::connect(m_wheel, &QVariantAnimation::valueChanged, m_scroll, [this](const QVariant& v) { m_scroll->verticalScrollBar()->setValue(v.toInt()); });
  m_scroll->viewport()->installEventFilter(this);

  m_banks->onChanged = [this](const QString& k) { m_bank = k; refill(); };
  m_types->onChanged = [this](const QString& k) { m_type = k; refill(); };

  QStringList names; for (const auto& b : banks::researchBanks(banks::Use::Full)) names << b.researchName;
  m_footer = names.join(QString::fromUtf8(" · ")) + QCoreApplication::translate("banks", " — THE BANKS' OWN PUBLISHED PDFS, REUSED WITH ATTRIBUTION (© ING BANK N.V.) · ")
           + QCoreApplication::translate("banks", "%1 BANKS LINKED ONLY AND %2 EXCLUDED BY THEIR TERMS: SEE DATA · ASSETS: SEC EDGAR XBRL")
                 .arg(banks::researchBanks(banks::Use::LinkOnly).size()).arg(banks::researchBanks(banks::Use::Excluded).size());
  reload();
}

bool LeftColumn::eventFilter(QObject* o, QEvent* e) {
  if (o == m_scroll->viewport() && e->type() == QEvent::Wheel) {
    auto* we = static_cast<QWheelEvent*>(e);
    QScrollBar* sb = m_scroll->verticalScrollBar();
    if (!we->pixelDelta().isNull()) { sb->setValue(sb->value() - we->pixelDelta().y()); return true; }
    const int target = qBound(0, (m_wheel->state() == QAbstractAnimation::Running ? m_wheel->endValue().toInt() : sb->value())
                                    - we->angleDelta().y() * 80 / 120, sb->maximum());
    m_wheel->stop(); m_wheel->setStartValue(sb->value()); m_wheel->setEndValue(target); m_wheel->start();
    return true;
  }
  return QWidget::eventFilter(o, e);
}

QRectF LeftColumn::tabRect(int i) const {
  // reference positions at 362 px, scaled with the column width (300 … 480 px); when a label does not fit (French),
  // the tabs are laid out from their measured widths with equal gaps
  static const qreal xs[] = {0, 106, 196, 273}, ws[] = {91, 80, 72, 89};
  const qreal k = width() / 362.0;
  const QFont f = theme::mono(width() < 340 ? 11 : 12, QFont::Bold, width() < 340 ? 0.4 : 1.5);
  const QFontMetricsF fm(f);
  qreal need[4], total = 0; bool fits = true;
  for (int t = 0; t < 4; ++t) {
    need[t] = 17 + fm.horizontalAdvance(QCoreApplication::translate("banks", kTabs[t])) + 6;
    total += need[t];
    if (need[t] > ws[t] * k) fits = false;
  }
  if (fits) return QRectF(xs[i] * k, 0, ws[i] * k, 29 + dy());
  // 6 px from the column edges (the first tab's icon touched the window edge, the last tab the border)
  const qreal gap = qMax<qreal>(2, (width() - 12 - total) / 3.0);
  qreal x = 6;
  for (int t = 0; t < i; ++t) x += need[t] + gap;
  return QRectF(x, 0, need[i], 29 + dy());
}
// Short windows (< 760 px): the sources footer keeps 3 lines, the last one elided, full text in the tooltip
// (its 5 lines crowded the list at 1024x600).
QStringList LeftColumn::footerLines() const {
  const QFont f = theme::mono(8, QFont::Normal, 1.0);
  return wrap(m_footer, QFontMetricsF(f), width() - 18, height() < 760 ? 3 : 1000);
}
int LeftColumn::footerHeight() const {
  if (height() < 760) return qMax(36, int(std::ceil(footerLines().size() * QFontMetricsF(theme::mono(8, QFont::Normal, 1.0)).lineSpacing())) + 8);
  const QRectF r = QFontMetricsF(theme::mono(8, QFont::Normal, 1.0)).boundingRect(QRectF(0, 0, width() - 18, 400), Qt::TextWordWrap, m_footer);
  return qMax(46, int(std::ceil(r.height())) + 8);
}

void LeftColumn::resizeEvent(QResizeEvent*) {
  const int w = width();
  const int dw = (w - 9 - 9 - 7) / 2, d = dy(), bh = m_phone ? 34 : 28;
  m_banks->setGeometry(9, 89 + d, dw, bh);
  m_types->setGeometry(9 + dw + 7, 89 + d, w - 9 - (9 + dw + 7), bh);
  const int listTop = m_tab >= 2 ? 37 + d : 123 + d + (bh - 28);
  const int fh = footerHeight();
  m_scroll->setGeometry(0, listTop, w, height() - listTop - fh);
  m_textScroll->setGeometry(0, 82 + d, w, height() - 82 - d - fh);
  m_list->setFixedWidth(w);
}

void LeftColumn::setTab(int t) {
  m_tab = qBound(0, t, 3);
  const bool notes = m_tab <= 1;
  m_banks->setVisible(notes); m_types->setVisible(notes); m_scroll->setVisible(notes);
  m_textScroll->setVisible(!notes);
  if (m_tab == 2) fillData();
  if (m_tab == 3) { banks::Registry::instance().refreshFilings(); fillFilings(); }
  if (notes) refill();
  resizeEvent(nullptr);
  update();
}

void LeftColumn::setBankFilter(const QString& id) {
  if (m_tab > 1) setTab(0);
  m_bank = id;
  m_banks->setCurrentKey(id);
  refill();
}

void LeftColumn::reload() {
  auto& r = banks::Research::instance();
  QStringList bl{QCoreApplication::translate("banks", "ALL BANKS")}, bk{QString()};
  for (const auto& b : banks::all()) {
    if (b.research == banks::Use::Full || !r.notes(b.id).isEmpty()) { bl << (b.researchName.isEmpty() ? b.name : b.researchName); bk << b.id; }
  }
  for (const auto& b : banks::all())   // listed once: a linked bank with podcast or video entries is already above
    if (b.research == banks::Use::LinkOnly && !bk.contains(b.id)) { bl << b.name + QCoreApplication::translate("banks", "  (LINK)"); bk << b.id; }
  if (!m_bank.isEmpty() && !bk.contains(m_bank)) { const banks::Bank* b = banks::bank(m_bank); bl << (b ? b->name : m_bank); bk << m_bank; }
  m_banks->setItems(bl, bk);
  m_banks->setCurrentKey(m_bank);
  QStringList tl{QCoreApplication::translate("banks", "ALL PUBLICATIONS")}, tk{QString()};
  for (const auto& t : r.types()) { tl << banks::typeLabel(t, true); tk << t; }
  m_types->setItems(tl, tk);
  m_types->setCurrentKey(m_type);
  if (m_tab <= 1) refill();
  if (m_tab == 3) fillFilings();
  update();
}

void LeftColumn::refill() {
  auto& r = banks::Research::instance();
  QList<banks::Note> list;
  for (const auto& n : r.notes(m_bank)) {
    if (!m_type.isEmpty() && n.type != m_type) continue;
    if (m_tab == 1 && !(n.type == "SNAP" || n.type == "OPINION")) continue;
    list << n;
  }
  // a refresh of the same list (new notes arriving) keeps the reader's place; only a change of filter goes to the top
  const QString view = m_bank + '|' + m_type + '|' + QString::number(m_tab);
  const bool sameView = view == m_shownView;
  m_shownView = view;
  m_list->setNotes(list);
  if (list.isEmpty()) {
    const banks::Bank* b = banks::bank(m_bank);
    if (b && b->research != banks::Use::Full) {
      m_list->setEmptyText(QCoreApplication::translate("banks", "NO NOTES FROM %1").arg(b->name),
          b->research == banks::Use::None ? QCoreApplication::translate("banks", "%1 is in the registry only: Deepwater reads no research from it.").arg(b->legalName)
                                          : QCoreApplication::translate("banks", "Deepwater does not read this bank's notes. %1.").arg(banks::trs(b->researchWhy)),
          b->researchUrl.isEmpty() ? QString() : QCoreApplication::translate("banks", "OPEN %1 ↗").arg(b->researchName), b->researchUrl);
    } else {
      m_list->setEmptyText(r.librarySize() ? QCoreApplication::translate("banks", "NO NOTE MATCHES THE FILTERS") : QCoreApplication::translate("banks", "LOADING THE BANKS' NOTES"),
                           r.librarySize() ? QString() : QCoreApplication::translate("banks", "Reading ING THINK (RSS and article listings)."), {}, {});
    }
  }
  if (!sameView) m_scroll->verticalScrollBar()->setValue(0);
  update();
}

void LeftColumn::fillData() {
  QString h = "<style>a { color: #d1c497; text-decoration: none; } td { padding: 3px 4px 6px 0; vertical-align: top; }</style>";
  h += "<div style='color:#e1d5a6; font-weight:bold; letter-spacing:2px;'>" + QCoreApplication::translate("banks", "RESEARCH SOURCES") + "</div>";
  h += "<div style='color:#7d7a72;'>" + QCoreApplication::translate("banks", "READ = notes cached and forecasts parsed (terms allow reuse with attribution). "
       "LINK = the bank's terms forbid reproduction: only a link. NONE = terms forbid automated access, deep links or "
       "citing. Terms read 2026-11-14.") + "</div><br>";
  h += "<table cellspacing=0 width='100%'>";
  auto row = [&](const banks::Bank& b, const QString& state, const QString& color) {
    const QString link = b.researchUrl.isEmpty() ? QString()
                       : QString::fromUtf8("<br><a href='%1'>%2 ↗</a>").arg(b.researchUrl, QUrl(b.researchUrl).host());
    h += QString("<tr><td width=64 style='color:%1; font-weight:bold;'>%2</td><td><span style='color:#fefdf8; font-weight:bold;'>%3</span>"
                 "<br><span style='color:#7d7a72;'>%4</span>%5</td></tr>")
             .arg(color, state, b.researchName.toHtmlEscaped(), banks::trs(b.researchWhy).toHtmlEscaped(), link);
  };
  for (const auto& b : banks::researchBanks(banks::Use::Full)) row(b, QCoreApplication::translate("banks", "READ"), "#7eb8ab");
  for (const auto& b : banks::researchBanks(banks::Use::LinkOnly)) row(b, QCoreApplication::translate("banks", "LINK"), "#c7b691");
  for (const auto& b : banks::researchBanks(banks::Use::Excluded)) row(b, QCoreApplication::translate("banks", "NONE"), "#bb7373");
  h += "</table><br><div style='color:#e1d5a6; font-weight:bold; letter-spacing:2px;'>" + QCoreApplication::translate("banks", "PODCASTS AND VIDEOS") + "</div>";
  h += "<div style='color:#7d7a72;'>" + QCoreApplication::translate("banks", "J.P. Morgan, Bank of America, Goldman Sachs and Morgan Stanley: their public podcast feeds "
       "(At Any Rate, Global Data Pod, Global Research Unlocked, Exchanges, The Markets, Thoughts on the Market) and YouTube channels. "
       "Titles, dates, short summaries and a link, as a podcast app lists them; the episode or video opens on its host.") + "</div><br><table cellspacing=0 width='100%'>";
  h += "</table><br><div style='color:#e1d5a6; font-weight:bold; letter-spacing:2px;'>" + QCoreApplication::translate("banks", "REGISTRY") + "</div><br>";
  h += "<div style='color:#7d7a72;'>" + QCoreApplication::translate("banks", "Total assets: latest <i>Assets</i> fact of each bank's SEC filings (10-K / 10-Q, 20-F, 40-F / 6-K) "
       "from <a href='https://www.sec.gov/edgar/sec-api-documentation'>SEC EDGAR XBRL</a>, in the filing currency. "
       "G-SIB flags: FSB list of November 2024, bundled by hand (fsb.org refuses automated requests).") + "</div>";
  m_text->setText(banks::mapHex(h));
}

void LeftColumn::fillFilings() {
  if (m_tab != 3) return;
  const auto fl = banks::Registry::instance().filings();
  QString h = "<style>a { color: #fefdf8; text-decoration: none; } td { padding: 4px 6px 5px 0; vertical-align: top; }</style>";
  h += "<div style='color:#e1d5a6; font-weight:bold; letter-spacing:2px;'>" + QCoreApplication::translate("banks", "SEC EDGAR FILINGS") + "</div><br>";
  if (fl.isEmpty()) h += "<div style='color:#7d7a72;'>" + QCoreApplication::translate("banks", "Loading the registry banks' filings from data.sec.gov…") + "</div>";
  h += "<table cellspacing=0 width='100%'>";
  int n = 0;
  for (const auto& f : fl) {
    if (++n > 120) break;
    const banks::Bank* b = banks::bank(f.bankId);
    h += QString("<tr><td width=48 style='color:#54534c;'>%1</td><td width=58 style='color:#d1c497; font-weight:bold;'>%2</td>"
                 "<td><a href='%3'>%4</a><br><span style='color:#6b685f;'>%5</span></td></tr>")
             .arg(i18n::locale().toString(f.filed, "dd MMM").toUpper(), f.form.toHtmlEscaped(), f.url.toHtmlEscaped(),
                  b ? b->name.toHtmlEscaped() : f.bankId, (f.description.isEmpty() ? f.form : f.description).toHtmlEscaped().left(80));
  }
  h += "</table>";
  m_text->setText(banks::mapHex(h));
}

void LeftColumn::paintEvent(QPaintEvent*) {
  QPainter p(this); p.setRenderHint(QPainter::Antialiasing); p.setRenderHint(QPainter::TextAntialiasing);
  p.fillRect(rect(), leftBg);
  p.fillRect(QRect(width() - 1, 0, 1, height()), colBorder);
  // tabs
  const QFont ft = theme::mono(12, QFont::Bold, 1.5);
  p.setFont(ft);
  for (int i = 0; i < 4; ++i) {
    const QRectF r = tabRect(i);
    const bool on = i == m_tab, hov = i == m_tabHover;
    if (on) {
      QLinearGradient g(r.topLeft(), r.bottomLeft()); g.setColorAt(0, theme::map("#2a1a03")); g.setColorAt(1, theme::map("#4c2d04"));
      p.fillRect(r, g);
      p.fillRect(QRectF(r.x(), r.bottom() - 2, r.width(), 3), goldBright);
    }
    const QColor c = on ? goldTitle : hov ? theme::map("#a39f86") : tabInactive;
    const qreal sp = width() < 340 ? 0.4 : 1.5;
    p.setFont(on ? theme::mono(width() < 340 ? 11 : 12, QFont::Bold, sp) : theme::mono(width() < 340 ? 11 : 12, QFont::Medium, sp));
    const QRectF ir(r.x() + 4, r.center().y() - 5, 9, 10);
    if (i == 0) iconDoc(p, ir, c); else if (i == 1) iconNews(p, ir, c); else if (i == 2) iconDb(p, ir, c); else iconSignal(p, ir, c);
    p.setPen(c);
    p.drawText(QRectF(r.x() + 17, r.y(), r.width(), r.height()), Qt::AlignVCenter | Qt::AlignLeft, QCoreApplication::translate("banks", kTabs[i]));
  }
  // header band
  const int d = dy();
  const QRectF band(0, 29 + d, width() - 1, 51);
  p.fillRect(band, look::band);
  p.fillRect(QRectF(0, band.bottom(), width() - 1, 1), bandBorder);
  iconDoc(p, QRectF(13, 51 + d, 10, 12), theme::map("#d6c27a"));
  const QFont fh = theme::mono(10, QFont::Normal, 1.2);
  p.setFont(fh); p.setPen(theme::map("#67624f"));
  static const char* heads[] = {QT_TRANSLATE_NOOP("banks", "BANK RESEARCH — DAILY PDFS"), QT_TRANSLATE_NOOP("banks", "BANK NEWS — QUICK READS"),
                                 QT_TRANSLATE_NOOP("banks", "SOURCES — WHO IS READ"), QT_TRANSLATE_NOOP("banks", "FILINGS — SEC EDGAR")};
  auto& r = banks::Research::instance();
  // counters "35 today · 412" on one line, right-aligned (the spec's single line; drawn at a fixed x, "412" wrapped)
  const QString counts = QCoreApplication::translate("banks", "%1 today ·").arg(r.publishedToday()) + " " + QString::number(r.librarySize());
  const qreal countsW = QFontMetricsF(theme::mono(10)).horizontalAdvance(counts);
  {
    const QString head = QCoreApplication::translate("banks", heads[m_tab]);
    const qreal avail = (m_tab <= 1 ? width() - 11 - countsW - 12 : width()) - 34 - 10;
    QFont hf2 = fh;
    if (QFontMetricsF(hf2).horizontalAdvance(head) > avail) hf2 = theme::mono(10);   // narrow column: no spacing
    p.setFont(hf2);
    p.drawText(QPointF(34, 52 + d), QFontMetricsF(hf2).elidedText(head, Qt::ElideRight, avail));
  }
  drawBlockCursor(p, QPointF(35, 58 + d), 7, 13);
  p.setFont(theme::mono(10)); p.setPen(theme::map("#5c5a50"));
  if (m_tab <= 1) p.drawText(QPointF(width() - 11 - countsW, 52 + d), counts);
  // footer
  p.setFont(theme::mono(8, QFont::Normal, 1.0)); p.setPen(footer);
  if (height() < 760) {
    const QStringList lines = footerLines();
    const QFontMetricsF fm(p.font());
    qreal y = height() - footerHeight() + 2 + fm.ascent();
    for (const QString& l : lines) { p.drawText(QPointF(9, y), l); y += fm.lineSpacing(); }
  } else
    p.drawText(QRectF(9, height() - footerHeight() + 2, width() - 18, footerHeight() - 4), Qt::TextWordWrap | Qt::AlignTop | Qt::AlignLeft, m_footer);
}

void LeftColumn::mouseMoveEvent(QMouseEvent* e) {
  setToolTip(height() < 760 && e->position().y() > height() - footerHeight() ? m_footer : QString());   // elided footer
  int h = -1; for (int i = 0; i < 4; ++i) if (tabRect(i).contains(e->position())) h = i;
  if (h != m_tabHover) { m_tabHover = h; setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor); update(QRect(0, 0, width(), 32 + dy())); }
}
void LeftColumn::mousePressEvent(QMouseEvent* e) {
  for (int i = 0; i < 4; ++i) if (tabRect(i).contains(e->position())) { setTab(i); return; }
}

// ============================================================================================ RegistryColumn
namespace { constexpr int kRowTop = 49; }
// 35 px rows as in the reference; tighter (≥ 28) when the window is short, then the list scrolls; taller (≤ 46)
// on big screens so the registry fills its column
static qreal rowH(const QWidget* w) {
  const qreal fit = (w->height() - kRowTop - 4) / 21.0;
  return qBound<qreal>(28, fit, w->height() > 1100 ? 46 : 35);
}

RegistryColumn::RegistryColumn(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  m_wheel = new QVariantAnimation(this);
  m_wheel->setDuration(150); m_wheel->setEasingCurve(QEasingCurve::OutCubic);
  QObject::connect(m_wheel, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { m_off = v.toReal(); update(); });
}
int RegistryColumn::fullHeight() { return kRowTop + 21 * 35 + 4; }
qreal RegistryColumn::maxOffset() const { return qMax<qreal>(0, kRowTop + 21 * rowH(this) + 4 - height()); }
void RegistryColumn::wheelEvent(QWheelEvent* e) {
  if (maxOffset() <= 0) { e->ignore(); return; }   // nothing to scroll (phones): the page scrolls instead
  if (!e->pixelDelta().isNull()) { m_wheel->stop(); m_off = qBound<qreal>(0, m_off - e->pixelDelta().y(), maxOffset()); update(); return; }
  const qreal target = qBound<qreal>(0, (m_wheel->state() == QAbstractAnimation::Running ? m_wheel->endValue().toReal() : m_off)
                                          - e->angleDelta().y() * 70 / 120.0, maxOffset());
  m_wheel->stop(); m_wheel->setStartValue(m_off); m_wheel->setEndValue(target); m_wheel->start();
}
void RegistryColumn::resizeEvent(QResizeEvent*) { m_off = qBound<qreal>(0, m_off, maxOffset()); }
int RegistryColumn::rowAt(const QPointF& p) const {
  const int n = int(banks::systemic().size());
  const int i = int((p.y() + m_off - kRowTop) / rowH(this));
  return p.y() >= kRowTop && i >= 0 && i < n ? i : -1;
}
void RegistryColumn::paintEvent(QPaintEvent*) {
  QPainter p(this); p.setRenderHint(QPainter::Antialiasing); p.setRenderHint(QPainter::TextAntialiasing);
  p.fillRect(rect(), rightBg);
  p.fillRect(QRect(0, 0, 1, height()), theme::map("#1f1e1b"));
  // header
  p.setPen(Qt::NoPen); p.setBrush(blinkOn() ? theme::map("#978f57") : theme::map("#5a5536"));
  p.drawEllipse(QPointF(16, 26), 2.6, 2.6);
  const QFont fh = theme::mono(10, QFont::Normal, 2.3);
  p.setFont(fh); p.setPen(regHeader);
  p.drawText(QPointF(29, 19), QCoreApplication::translate("banks", "BANK REGISTRY — SYSTEMIC"));
  const QString l2 = QCoreApplication::translate("banks", "INSTITUTIONS");
  p.drawText(QPointF(29, 35), l2);
  drawBlockCursor(p, QPointF(29 + QFontMetricsF(fh).horizontalAdvance(l2) + 2, 26), 7, 13);
  p.fillRect(QRectF(0, 47, width(), 1), theme::map("#3a3214"));
  // rows
  const QFont fi = theme::mono(12), fn = theme::display(12, QFont::Bold, 0.5), fv = theme::mono(12), fc = theme::mono(10, QFont::Bold, 0.2);
  const auto list = banks::systemic();
  const qreal dpr = devicePixelRatioF();
  auto& reg = banks::Registry::instance();
  const qreal kRowH = rowH(this);
  p.setClipRect(QRectF(0, 48, width(), height() - 48));
  for (int i = 0; i < list.size(); ++i) {
    const auto& b = list[i];
    const qreal y = kRowTop + i * kRowH - m_off;
    if (y + kRowH < 48 || y > height()) continue;
    const bool sel = b.id == m_selected, hov = i == m_hover;
    if (sel || hov) p.fillRect(QRectF(1, y, width() - 1, kRowH), sel ? rowSel : theme::map("#14110a"));
    if (sel) p.fillRect(QRectF(1, y, 3, kRowH), selBar);
    p.fillRect(QRectF(1, y + kRowH - 1, width() - 1, 1), regSep);
    const qreal cy = y + kRowH / 2.0;
    p.setFont(fi); p.setPen(regIndex);
    p.drawText(QRectF(11, y, 24, kRowH), Qt::AlignVCenter | Qt::AlignLeft, QString("%1").arg(i + 1, 2, 10, QChar('0')));
    p.drawPixmap(QPointF(34, cy - 9), banks::logo(b.id, 18, dpr));
    // ticker chip, value, then the name in what is left (elided on narrow columns; full name in the tooltip)
    const QString t = QString::fromUtf8("• ") + b.ticker;
    const qreal cw = qMax<qreal>(30, QFontMetricsF(fc).horizontalAdvance(t) + 9);
    const QRectF chip(width() - 7 - cw, cy - 10, cw, 20);
    const bool on = !b.ticker.isEmpty();
    const banks::Assets a = reg.assets(b.id);
    const QString at = a.text();
    const qreal vw = QFontMetricsF(fv).horizontalAdvance(at);
    p.setFont(fv); p.setPen(regValue);
    p.drawText(QRectF(chip.left() - 9 - vw - 2, y, vw + 2, kRowH), Qt::AlignVCenter | Qt::AlignRight, at);
    p.setFont(fn); p.setPen(white);
    const qreal nw = chip.left() - 9 - vw - 10 - 60;
    p.drawText(QRectF(60, y, nw, kRowH), Qt::AlignVCenter | Qt::AlignLeft, QFontMetricsF(fn).elidedText(b.name, Qt::ElideRight, nw));
    p.setPen(QPen(on ? chipBorder : theme::map("#2a2720"), 1)); p.setBrush(on ? chipBg : chipOffBg);
    p.drawRect(chip.adjusted(0.5, 0.5, -0.5, -0.5));
    p.setFont(fc); p.setPen(on ? chipText : chipOffText);
    p.drawText(chip, Qt::AlignCenter, on ? t : QString::fromUtf8("—"));
  }
}
void RegistryColumn::mouseMoveEvent(QMouseEvent* e) {
  const int h = rowAt(e->position());
  if (h != m_hover) { m_hover = h; setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor); update(); }
}
void RegistryColumn::mousePressEvent(QMouseEvent* e) {
  const int r = rowAt(e->position());
  if (r >= 0 && onClick) onClick(banks::systemic()[r].id);
}
void RegistryColumn::leaveEvent(QEvent*) { m_hover = -1; update(); }
bool RegistryColumn::event(QEvent* e) {
  if (e->type() == QEvent::ToolTip) {
    auto* he = static_cast<QHelpEvent*>(e);
    const int r = rowAt(he->pos());
    if (r < 0) { QToolTip::hideText(); return true; }
    const auto b = banks::systemic()[r];
    const banks::Assets a = banks::Registry::instance().assets(b.id);
    const QLocale en(QLocale::English), loc = i18n::locale();   // numbers stay English, dates follow the interface
    QString tip = QString("<b>%1</b> · %2<br>").arg(b.legalName.toHtmlEscaped(), banks::trs(b.city));
    if (a.valid())
      tip += QCoreApplication::translate("banks", "Total assets %1 %2 bn · balance sheet %3<br>%4 filed %5 · SEC EDGAR XBRL")
                 .arg(a.currency.toHtmlEscaped(), en.toString(a.value / 1e9, 'f', 1), loc.toString(a.asOf, "d MMM yyyy"), a.form.toHtmlEscaped(), loc.toString(a.filed, "d MMM yyyy"));
    else tip += QCoreApplication::translate("banks", "Total assets: loading from SEC EDGAR…");
    tip += "<br>" + banks::trs(b.fsb) + (b.ticker.isEmpty() ? QString() : QString(" · NYSE: %1").arg(b.ticker));
    QToolTip::showText(he->globalPos(), tip, this);
    return true;
  }
  return QWidget::event(e);
}
}
