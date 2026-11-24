#pragma once
// Small widgets that let a page fit the window's minimum size (1024x600) without changing how it looks when there is
// room: a one-line label and a combo box that elide their text with "…" (full text in a tooltip) instead of holding
// the page wider than the window. Used by the Macro data and Sources graph pages.
#include <QComboBox>
#include <QFontMetrics>
#include <QLabel>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QStylePainter>

namespace fit {

// One-line label: full text when it fits, "…" at the end otherwise (tooltip = full text). Its minimum width is
// `minWidth`, not the text's width, so it never holds a row wider than the window.
class ElideLabel : public QLabel {
public:
  ElideLabel(const QString& text, QWidget* parent, int minWidth = 40) : QLabel(text, parent), m_min(minWidth) {}
  QSize minimumSizeHint() const override {
    QSize s = QLabel::minimumSizeHint();
    s.setWidth(qMin(s.width(), m_min));
    return s;
  }
  // width it asks for when there is room, at least `px` (a slice title that changes as a slider moves)
  void setHintWidth(int px) { m_hintW = px; updateGeometry(); }
  QSize sizeHint() const override { QSize s = QLabel::sizeHint(); s.setWidth(qMax(s.width(), m_hintW)); return s; }
  void setOwnToolTip(const QString& t) { m_tip = t; syncTip(); }
protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    const QRect r = contentsRect();
    const QString t = fontMetrics().elidedText(text(), Qt::ElideRight, r.width());
    style()->drawItemText(&p, r, int(alignment()) | Qt::TextSingleLine, palette(), isEnabled(), t, foregroundRole());
  }
  void resizeEvent(QResizeEvent* e) override { QLabel::resizeEvent(e); syncTip(); }
  bool event(QEvent* e) override {
    if (e->type() == QEvent::ToolTip) syncTip();
    return QLabel::event(e);
  }
private:
  void syncTip() {
    const bool cut = fontMetrics().horizontalAdvance(text()) > contentsRect().width();
    const QString want = cut ? (m_tip.isEmpty() ? text() : text() + "\n\n" + m_tip) : m_tip;
    if (toolTip() != want) setToolTip(want);
  }
  int m_min, m_hintW = 0;
  QString m_tip;
};

// Wrapped text in a box whose height follows the text (between `minH` and `maxH`; beyond that the text scrolls).
// Explicit heights, not Qt's height-for-width: that does not reach through a QStackedWidget, so a wrapping label
// there gets clipped mid-line when the window narrows. Call sync() after changing the text.
class TextBox : public QScrollArea {
public:
  TextBox(QWidget* parent, int minH, int maxH) : QScrollArea(parent), m_minH(minH), m_maxH(maxH) {
    m_label = new QLabel(this);
    m_label->setWordWrap(true);
    m_label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    setWidget(m_label);
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    viewport()->setAutoFillBackground(false);
    setFixedHeight(minH);
  }
  QLabel* label() const { return m_label; }
  void setText(const QString& t) { m_label->setText(t); sync(); }
  void setPadding(int h, int v) { setViewportMargins(h, v, h, v); sync(); }
  void sync() {
    const int frame = height() - viewport()->height();
    const int want = qBound(m_minH, m_label->heightForWidth(viewport()->width()) + qMax(0, frame), m_maxH);
    if (want != height()) setFixedHeight(want);
  }
protected:
  void resizeEvent(QResizeEvent* e) override {
    QScrollArea::resizeEvent(e);
    if (e->oldSize().width() != e->size().width()) sync();
  }
  void showEvent(QShowEvent* e) override { QScrollArea::showEvent(e); sync(); }
private:
  QLabel* m_label;
  int m_minH, m_maxH;
};

// Wrapping label that asks for one line when there is room (a plain word-wrapped QLabel asks for a narrower,
// multi-line shape even when the row is wide): wraps only when the row really narrows.
class LineLabel : public QLabel {
public:
  LineLabel(const QString& text, QWidget* parent) : QLabel(text, parent) { setWordWrap(true); }
  QSize sizeHint() const override {
    const QMargins m = contentsMargins();
    const int w = fontMetrics().horizontalAdvance(text()) + m.left() + m.right() + 2 * margin() + 1;
    return QSize(w, heightForWidth(w));
  }
};

// Combo box that keeps its natural width when there is room but may shrink to `minWidth`, eliding the shown item.
class ElideCombo : public QComboBox {
public:
  explicit ElideCombo(QWidget* parent, int minWidth = 90) : QComboBox(parent), m_min(minWidth) {}
  QSize minimumSizeHint() const override {
    QSize s = QComboBox::minimumSizeHint();
    s.setWidth(qMin(s.width(), m_min));
    return s;
  }
protected:
  void paintEvent(QPaintEvent*) override {
    QStylePainter p(this);
    p.setPen(palette().color(QPalette::Text));
    QStyleOptionComboBox opt;
    initStyleOption(&opt);
    const QString full = opt.currentText;
    opt.currentText.clear();
    p.drawComplexControl(QStyle::CC_ComboBox, opt);   // frame and arrow; the text is drawn below, elided before the arrow
    const QRect field = style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxEditField, this);
    const QRect arrow = style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxArrow, this);
    const int right = arrow.isValid() && arrow.left() > field.left() ? arrow.left() - 3 : field.right();
    const QRect tr(field.left() + 1, field.top(), qMax(0, right - field.left() - 1), field.height());
    const QString shown = fontMetrics().elidedText(full, Qt::ElideRight, tr.width());
    setToolTip(shown != full ? full : QString());
    style()->drawItemText(&p, tr, int(Qt::AlignLeft | Qt::AlignVCenter), palette(), isEnabled(), shown);
  }
private:
  int m_min;
};

// Vertical-only scroll area whose minimum width is its content's (plus the scroll bar where it takes room): the
// content scrolls up and down when the window is short but is never cut at the right.
class VScrollArea : public QScrollArea {
public:
  explicit VScrollArea(QWidget* parent) : QScrollArea(parent) {
    setWidgetResizable(true); setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  }
  QSize minimumSizeHint() const override {
    QSize s = QScrollArea::minimumSizeHint();
    if (!widget()) return s;
    const bool overlay = style()->styleHint(QStyle::SH_ScrollBar_Transient, nullptr, this);
    s.setWidth(qMax(s.width(), widget()->minimumSizeHint().width() + 2 * frameWidth() +
                                   (overlay ? 0 : style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, this))));
    return s;
  }
};

// Same look as ui::label (font + colour), eliding.
inline ElideLabel* label(const QString& text, const QFont& f, const QColor& c, QWidget* parent, int minWidth = 40) {
  auto* l = new ElideLabel(text, parent, minWidth);
  l->setFont(f);
  QPalette pl = l->palette(); pl.setColor(QPalette::WindowText, c); l->setPalette(pl);
  l->setStyleSheet(QString("color:%1;background:transparent;").arg(c.name(QColor::HexArgb)));
  return l;
}

}  // namespace fit
