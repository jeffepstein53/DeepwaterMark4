#include "pages/sources/CheckPanel.h"
#include "app/Watchdog.h"
#include "macro/KeyFigures.h"
#include "ui/Theme.h"
#include "core/I18n.h"
#include <QCoreApplication>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <algorithm>

namespace sourcespage {
namespace {
QString tx(const char* s) { return QCoreApplication::translate("checks", s); }
constexpr int kHead = 38, kPad = 10;
}

CheckPanel::CheckPanel(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  m_open = qEnvironmentVariableIsSet("DW_CHECKS_OPEN");   // test hook
  connect(&app::Watchdog::instance(), &app::Watchdog::changed, this, [this] { if (isVisible()) refresh(); });
  connect(&macro::KeyFigures::instance(), &macro::KeyFigures::changed, this, [this] { if (isVisible()) refresh(); });
  refresh();
}

void CheckPanel::setPhone(bool on) { if (on != m_phone) { m_phone = on; refresh(); } }

void CheckPanel::build() {
  m_issues.clear(); m_figures.clear();
  const auto hm = [](const QDateTime& t) {
    const QDateTime l = t.toLocalTime();
    return i18n::locale().toString(l, l.date() == QDate::currentDate() ? "HH:mm" : "d MMM HH:mm");
  };
  for (const app::Issue& i : app::Watchdog::instance().issues()) {
    Line l;
    l.tone = i.kind == app::Issue::Result || i.kind == app::Issue::Source || i.kind == app::Issue::Network ? 3 : 2;
    l.mark = l.tone == 3 ? QStringLiteral("!") : QStringLiteral("≠");
    l.left = i.title; l.mid = i.detail; l.right = tx("seen at %1").arg(hm(i.since));
    l.tip = i.title + "\n" + i.detail;
    m_issues << l;
  }
  const auto& k = macro::KeyFigures::instance();
  const macro::check::Summary s = k.summary();
  if (s.figures == 0) m_summary = tx("The first figures are being read…");
  else {
    QStringList t{tx("%1 figures").arg(s.figures), tx("%1 confirmed by a second source").arg(s.agree)};
    if (s.differ) t << tx("%1 different").arg(s.differ);
    if (s.late) t << tx("%1 late").arg(s.late);
    if (s.missing) t << tx("%1 missing").arg(s.missing);
    if (s.alone) t << tx("%1 with a single source").arg(s.alone);
    m_summary = t.join(" · ");
  }
  QString ccy;
  for (const macro::check::Item& it : k.items()) {
    if (it.ccy != ccy) { ccy = it.ccy; Line h; h.head = true; h.left = ccy; m_figures << h; }
    Line l;
    using V = macro::check::Verdict;
    l.tone = it.verdict == V::Agree ? 1 : it.verdict == V::Differ ? 2 : it.verdict == V::Alone ? 0 : 3;
    l.mark = macro::checkVerdictName(it.verdict);
    l.left = macro::checkFigureName(it.figure);
    l.mid = macro::checkValue(it, it.value) + (it.freq != 'D' && it.period.isValid() ? "  " + macro::checkPeriod(it) : QString()) +
            (it.source.isEmpty() ? QString() : "  ·  " + macro::checkSource(it.source));
    l.right = macro::checkOthers(it);
    l.tip = l.left + ": " + l.mid + (l.right.isEmpty() ? QString() : "\n" + tx("Other sources: %1").arg(l.right)) +
            (macro::checkLine(it).isEmpty() ? QString() : "\n" + macro::checkLine(it));
    m_figures << l;
  }
}

void CheckPanel::refresh() {
  build();
  int h = kHead + int(m_issues.size()) * lineH() + (m_issues.isEmpty() ? 0 : kPad);
  if (m_open) h += int(m_figures.size()) * lineH() + kPad;
  if (height() != h) setFixedHeight(h);
  update();
}

QRectF CheckPanel::foldRect() const {
  const QString t = m_open ? tx("HIDE THE FIGURES") : tx("SHOW THE FIGURES");
  const qreal w = QFontMetricsF(theme::mono(10, QFont::Bold, 1.0)).horizontalAdvance(t) + 20;
  return QRectF(width() - w - 10, 8, w, 22);
}

void CheckPanel::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const auto& c = theme::pal();
  p.setPen(QPen(m_issues.isEmpty() ? c.border : theme::alpha(c.accent, 140), 1)); p.setBrush(c.panel);
  p.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 3, 3);
  // title, summary, fold button
  const QFont tf = theme::mono(11, QFont::Bold, 1.4);
  p.setFont(tf); p.setPen(c.accentText);
  const QString title = tx("DATA CHECK");
  p.drawText(QPointF(12, kHead / 2.0 + 4.5), title);
  const qreal x = 12 + QFontMetricsF(tf).horizontalAdvance(title) + 16;
  const QRectF f = foldRect();
  p.setFont(theme::sans(12)); p.setPen(c.textDim);
  p.drawText(QRectF(x, 0, f.left() - x - 10, kHead), Qt::AlignVCenter | Qt::AlignLeft,
             QFontMetricsF(p.font()).elidedText(m_summary, Qt::ElideRight, f.left() - x - 10));
  p.setPen(QPen(m_hover ? c.borderHi : c.border, 1)); p.setBrush(m_hover ? c.panelAlt : Qt::transparent);
  p.drawRoundedRect(f.adjusted(.5, .5, -.5, -.5), 3, 3);
  p.setFont(theme::mono(10, QFont::Bold, 1.0)); p.setPen(m_hover ? c.accent : c.textDim);
  p.drawText(f, Qt::AlignCenter, m_open ? tx("HIDE THE FIGURES") : tx("SHOW THE FIGURES"));

  const auto tone = [&c](int t) { return t == 1 ? c.up : t == 2 ? c.accent : t == 3 ? c.down : c.textMute; };
  qreal y = kHead;
  const int lh = lineH();
  for (const Line& l : std::as_const(m_issues)) {
    p.fillRect(QRectF(1, y, width() - 2, 1), c.border);
    const qreal top = y + (m_phone ? 2 : 0), one = m_phone ? lh / 2.0 - 2 : lh;
    p.setFont(theme::mono(13, QFont::Bold)); p.setPen(tone(l.tone));
    p.drawText(QRectF(12, top, 16, one), Qt::AlignVCenter | Qt::AlignLeft, l.mark);
    p.setFont(theme::sans(12, QFont::DemiBold)); p.setPen(c.text);
    const qreal lw = m_phone ? width() - 44 : std::clamp(width() * 0.26, 250.0, 380.0);
    p.drawText(QRectF(32, top, lw, one), Qt::AlignVCenter, QFontMetricsF(p.font()).elidedText(l.left, Qt::ElideRight, lw));
    p.setFont(theme::sans(12)); p.setPen(c.textDim);
    if (m_phone) {
      p.drawText(QRectF(32, top + one, width() - 44, one), Qt::AlignVCenter, QFontMetricsF(p.font()).elidedText(l.mid, Qt::ElideRight, width() - 44));
    } else {
      const qreal rw = 120, mw = width() - 32 - lw - 10 - rw - 12;
      p.drawText(QRectF(32 + lw + 10, top, mw, one), Qt::AlignVCenter, QFontMetricsF(p.font()).elidedText(l.mid, Qt::ElideRight, mw));
      p.setPen(c.textMute);
      p.drawText(QRectF(width() - 12 - rw, top, rw, one), Qt::AlignVCenter | Qt::AlignRight, l.right);
    }
    y += lh;
  }
  if (!m_issues.isEmpty()) y += kPad;
  if (!m_open) return;
  for (const Line& l : std::as_const(m_figures)) {
    p.fillRect(QRectF(1, y, width() - 2, 1), theme::alpha(c.border, 140));
    if (l.head) {
      p.setFont(theme::mono(12, QFont::Bold, 1.0)); p.setPen(c.accentText);
      p.drawText(QRectF(12, y, 200, lh), Qt::AlignVCenter, l.left);
      y += lh; continue;
    }
    const qreal one = m_phone ? lh / 2.0 : lh;
    const qreal mw = 118;
    p.setFont(theme::mono(9, QFont::Bold, 0.8)); p.setPen(tone(l.tone));
    p.drawText(QRectF(12, y, mw, one), Qt::AlignVCenter, l.mark);
    p.setFont(theme::sans(12)); p.setPen(c.textDim);
    const qreal lx = 12 + mw, lw = m_phone ? width() - lx - 12 : 220;
    p.drawText(QRectF(lx, y, lw, one), Qt::AlignVCenter, QFontMetricsF(p.font()).elidedText(l.left, Qt::ElideRight, lw));
    p.setPen(c.text);
    if (m_phone) {
      p.drawText(QRectF(12, y + one, width() - 24, one), Qt::AlignVCenter, QFontMetricsF(p.font()).elidedText(l.mid, Qt::ElideRight, width() - 24));
    } else {
      const qreal rest = width() - lx - lw - 24, w1 = rest * 0.46, w2 = rest - w1 - 12;
      p.drawText(QRectF(lx + lw, y, w1, one), Qt::AlignVCenter, QFontMetricsF(p.font()).elidedText(l.mid, Qt::ElideRight, w1));
      p.setPen(c.textDim);
      p.drawText(QRectF(lx + lw + w1 + 12, y, w2, one), Qt::AlignVCenter, QFontMetricsF(p.font()).elidedText(l.right, Qt::ElideRight, w2));
    }
    y += lh;
  }
}

void CheckPanel::mousePressEvent(QMouseEvent* e) {
  if (foldRect().contains(e->position()) || (m_phone && e->position().y() < kHead)) { m_open = !m_open; refresh(); }
}
void CheckPanel::mouseMoveEvent(QMouseEvent* e) {
  const bool h = foldRect().contains(e->position());
  if (h != m_hover) { m_hover = h; setCursor(h ? Qt::PointingHandCursor : Qt::ArrowCursor); update(); }
}
void CheckPanel::leaveEvent(QEvent*) { m_hover = false; update(); }
void CheckPanel::resizeEvent(QResizeEvent*) { update(); }

bool CheckPanel::event(QEvent* e) {
  if (e->type() == QEvent::ToolTip) {
    auto* he = static_cast<QHelpEvent*>(e);
    const qreal y = he->pos().y() - kHead;
    QString tip;
    if (y >= 0) {
      const int i = int(y / lineH());
      if (i < m_issues.size()) tip = m_issues[i].tip;
      else if (m_open) {
        const int k = int((y - m_issues.size() * lineH() - (m_issues.isEmpty() ? 0 : kPad)) / lineH());
        if (k >= 0 && k < m_figures.size()) tip = m_figures[k].tip;
      }
    }
    if (tip.isEmpty()) QToolTip::hideText(); else QToolTip::showText(he->globalPos(), tip, this);
    return true;
  }
  return QWidget::event(e);
}
}
