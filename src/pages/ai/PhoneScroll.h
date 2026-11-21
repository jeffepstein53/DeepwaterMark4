#pragma once
// AI › LLM (Local models, Integration) and AI › Agents on a phone (2026-11-21).
//
// PhoneScroll: on a desktop the page holds its own layouts, untouched (it looks exactly as before). When the page is a
// phone's — narrower than ui::kCompactWidth (held upright), or shorter than 420 px (turned sideways) — its top layout
// moves (with every widget in it) into a content widget inside a vertical global::PageScroll, at the height the page
// asks for, and the page scrolls (finger drag on touch screens); back on the page when it widens again. (A content
// widget kept on desktops too, placed by hand or through a wrapping layout, made Integration's grid come out
// differently.)
//
// pillCombo: a PillBar too wide for a phone (five or six boxed choices) gets a combo box twin with the same choices;
// the page shows one or the other. Choosing in either moves the other.
#include "global/PageScroll.h"
#include "pages/ai/Fit.h"
#include "ui/Kit.h"
#include "ui/Touch.h"
#include <QLayout>
#include <QPointer>
#include <QScrollBar>
#include <functional>

namespace aipage {

class PhoneScroll {
public:
  explicit PhoneScroll(QWidget* page) : m_page(page) {}
  // content height in scroll mode for this width and viewport height (the scroller keeps at least the viewport's)
  std::function<int(int w, int vh)> height;
  int desktopMin = 0;   // the page's desktop width: narrower, it scrolls in its phone layout (ui::compact(w, min))
  static bool wanted(int w, int h, int min = 0) { return ui::compact(w, min) || (w > 0 && h > 0 && h < 420); }
  bool scrolling() const { return m_scroll != nullptr; }
  // from the page's resizeEvent
  void resize() {
    const bool on = wanted(m_page->width(), m_page->height(), desktopMin);
    if (on != scrolling()) {
      if (on) {
        auto* content = new QWidget;
        content->setLayout(m_page->layout());   // moves the layout and its widgets
        m_scroll = new global::PageScroll(content, m_page);
        m_scroll->layout = [this](int w, int vh) { return height ? height(w, vh) : vh; };
        m_scroll->show();
      } else {
        QWidget* content = m_scroll->takeWidget();
        m_page->setLayout(content->layout());   // back on the page, with its widgets
        delete content;
        delete m_scroll;
      }
    }
    if (m_scroll) { m_scroll->setGeometry(m_page->rect()); m_scroll->relayout(); }
  }
  // the content's height changed (a list grew, a view opened)
  void relayout() { if (m_scroll) m_scroll->relayout(); }

private:
  QWidget* m_page;
  QPointer<global::PageScroll> m_scroll;
};

// A phone's stand-in for a PillBar: same items, same current choice, both kept in step. 34 px high (finger target).
inline QComboBox* pillCombo(ui::PillBar* bar, const QStringList& items, QWidget* parent) {
  auto* box = new fit::ElideCombo(parent);
  box->addItems(items);
  box->setCurrentIndex(bar->current());
  box->setMinimumHeight(34);
  box->hide();
  QObject::connect(box, &QComboBox::activated, bar, [bar](int i) { if (bar->current() != i) bar->setCurrent(i, true); });
  QObject::connect(bar, &ui::PillBar::changed, box, [box](int i) { if (box->currentIndex() != i) box->setCurrentIndex(i); });
  return box;
}

// A layout's preferred height at this width (its wrapped labels' heights when it has some), never under its minimum
// (Qt's height-for-width leaves out some minimum heights: 34 px buttons in a grid were squeezed onto each other).
// The nested caches are emptied first: a widget's new minimum (setPhone) reaches only the layouts up to its own
// widget until they activate, and a panel's item keeps the height-for-width it measured before; the measure is
// taken before any of that.
inline void invalidateAll(QLayout* l) {
  l->invalidate();
  for (int i = 0; i < l->count(); ++i) {
    QLayoutItem* it = l->itemAt(i);
    if (QLayout* sub = it->layout()) invalidateAll(sub);
    else if (QWidget* w = it->widget(); w && w->layout()) { invalidateAll(w->layout()); w->updateGeometry(); }
    else it->invalidate();
  }
}
inline int layoutHeight(QLayout* l, int w) {
  invalidateAll(l);
  const int min = l->totalMinimumSize().height();
  return std::max(min, l->hasHeightForWidth() ? l->totalHeightForWidth(w) : l->totalSizeHint().height());
}

// Sets a widget's height to `h` on a phone, or gives it back its free height (no explicit min / max) otherwise.
inline void phoneHeight(QWidget* w, int h) {
  if (h > 0) w->setFixedHeight(h);
  else { w->setMinimumHeight(0); w->setMaximumHeight(QWIDGETSIZE_MAX); }
}

}  // namespace aipage
