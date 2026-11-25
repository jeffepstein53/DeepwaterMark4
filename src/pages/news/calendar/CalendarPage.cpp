#include "pages/news/calendar/CalendarPage.h"
#include "pages/news/calendar/CalendarTable.h"
#include "pages/news/calendar/CalendarFilters.h"
#include "pages/news/calendar/RangePanel.h"
#include "app/Pages.h"
#include "ui/Theme.h"
#include "pages/news/ThemeColor.h"
#include "core/I18n.h"
#include "global/PageScroll.h"
#include "ui/Touch.h"
#include <QScrollBar>
#include <QCoreApplication>
#include <QVariantAnimation>
#include <QKeyEvent>
#include <QTimer>
#include <QEasingCurve>

DW_PAGE("news/calendar", news::pages::CalendarPage)

namespace news::pages {

CalendarPage::CalendarPage(QWidget* parent) : QWidget(parent) {
  setAutoFillBackground(true);
  QPalette pl = palette(); pl.setColor(QPalette::Window, theme::map("#0f0a02")); setPalette(pl);
  setFocusPolicy(Qt::StrongFocus);
  m_left = new QWidget(this);
  m_filters = new CalendarFilters(m_left);
  m_table = new CalendarTable(m_left);
  m_footer = new NewsFooter(m_left);
  m_panel = new RangePanel(this);
  m_panel->hide();
  m_slide = new QVariantAnimation(this);
  m_slide->setDuration(280);
  m_slide->setEasingCurve(QEasingCurve::OutCubic);
  connect(m_slide, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { m_progress = v.toDouble(); layoutChildren(); });
  connect(m_slide, &QVariantAnimation::finished, this, [this] { if (m_progress <= 0.001) m_panel->hide(); });

  auto& cal = news::Calendar::instance();
  connect(&cal, &news::Calendar::weekLoaded, this, [this](const QDate& w) { if (w == m_week) { m_all = news::Calendar::instance().events(w); refill(); } });
  connect(&cal, &news::Calendar::refreshed, this, [this] { m_footer->setTime(news::Calendar::instance().lastRefresh()); });
  connect(m_filters, &CalendarFilters::changed, this, &CalendarPage::refill);
  connect(m_filters, &CalendarFilters::weekStep, this, [this](int d) { closePanel(); loadWeek(m_week.addDays(7 * d)); });
  connect(m_table, &CalendarTable::valueClicked, this, [this](const news::Event& e, int col) {
    if (m_progress > 0.5 && m_panelKey == e.key && m_panelCol == col) closePanel();
    else openPanel(e, col);
  });
  connect(m_panel, &RangePanel::closeRequested, this, &CalendarPage::closePanel);
  connect(m_table, &CalendarTable::contentChanged, this, [this] { if (m_pageScroll) m_pageScroll->relayout(); });
  connect(m_table, &CalendarTable::openAt, this, [this](int y) {   // after the relayout, when the bar knows its new range
    QTimer::singleShot(0, this, [this, y] { if (m_pageScroll) m_pageScroll->verticalScrollBar()->setValue(m_table->y() + y); });
  });

  QDate w = news::Calendar::currentWeek();
  const QByteArray hw = qgetenv("DW_CAL_WEEK");
  if (!hw.isEmpty()) { const QDate d = QDate::fromString(QString::fromLatin1(hw), Qt::ISODate); if (d.isValid()) w = news::Calendar::weekStart(d); }
  loadWeek(w);
}

void CalendarPage::loadWeek(const QDate& w) {
  m_week = w;
  m_filters->setWeek(w);
  m_all = news::Calendar::instance().events(w);
  m_table->setEmptyNote({});
  if (w == news::Calendar::currentWeek()) m_table->openOnToday();
  refill();
  news::Calendar::instance().load(w);
}

void CalendarPage::refill() {
  const QString q = m_filters->search();
  QList<news::Event> byCountry, shown;
  QStringList others;
  int counts[4] = {0, 0, 0, 0};
  static const QStringList majors{"USD", "EUR", "GBP", "JPY", "CHF", "CAD", "AUD", "NZD"};
  for (const auto& e : m_all) {
    if (!majors.contains(e.currency) && !others.contains(e.currency)) others << e.currency;
    if (!m_filters->accepts(e.currency)) continue;
    if (!q.isEmpty() && !e.title.contains(q, Qt::CaseInsensitive) && !e.currency.contains(q, Qt::CaseInsensitive) &&
        !news::Calendar::countryOf(e.currency).contains(q, Qt::CaseInsensitive)) continue;
    byCountry << e;
    const int k = e.impact == news::Impact::High ? 1 : e.impact == news::Impact::Medium ? 2 : 3;
    counts[0]++; counts[k]++;
    const int want = m_filters->impact();
    if (want == 0 || want == k) shown << e;
  }
  m_filters->setOtherCurrencies(others);
  m_filters->setCounts(counts[0], counts[1], counts[2], counts[3]);
  QString note = news::Calendar::instance().weekNote(m_week);
  if (note.isEmpty() && !m_all.isEmpty() && shown.isEmpty()) note = QCoreApplication::translate("news", "No event matches the filters.");
  m_table->setEmptyNote(note);
  m_table->setEvents(shown);
  m_footer->setTime(news::Calendar::instance().lastRefresh());
  // keep the panel on the same event (its actual may just have arrived)
  if (!m_panelKey.isEmpty())
    for (const auto& e : m_all) if (e.key == m_panelKey) m_panel->setEvent(e);
  if (!m_hooksDone && !m_all.isEmpty()) QTimer::singleShot(50, this, &CalendarPage::applyHooks);
}

void CalendarPage::openPanel(const news::Event& e, int col) {
  m_panelKey = e.key; m_panelCol = col;
  m_panel->setEvent(e);
  m_table->setFocusCell(e.key, col);
  m_panel->show(); m_panel->raise();
  if (m_progress < 1) {
    m_slide->stop(); m_slide->setStartValue(m_progress); m_slide->setEndValue(1.0); m_slide->start();
  }
}

void CalendarPage::closePanel() {
  if (m_progress <= 0) return;
  m_panelKey.clear();
  m_slide->stop(); m_slide->setStartValue(m_progress); m_slide->setEndValue(0.0); m_slide->start();
}

void CalendarPage::layoutChildren() {
  const int W = width(), H = height();
  // phone upright, or a page this short (phone sideways: 382 px): one scrolling column
  setScrollMode(ui::compact(W) || H < 420);
  if (m_pageScroll) {
    const int pw = ui::compact(W) ? W : std::clamp(int(W * 0.2825), 380, 452);
    const int shift = int(std::round(pw * m_progress));
    m_pageScroll->setGeometry(0, 0, W, H);
    m_panel->setGeometry(W - shift, 0, pw, H);
    m_panel->raise();
    return;
  }
  const int pw = std::clamp(int(W * 0.2825), 380, W >= 2400 ? 560 : 452);
  // wide windows: the table reflows beside the panel (reference); below 1300 px the panel slides OVER the table
  // so the table keeps its columns.
  const bool overlay = W < 1300;
  const int shift = int(std::round(pw * m_progress));
  const int lw = overlay ? W : W - shift;
  m_left->setGeometry(0, 0, lw, H);
  // short windows (1024x600, 1280x720): filters on three rows instead of four, one more table row
  m_filters->setCompact(H < 760);
  const int fh = m_filters->height();
  m_filters->setGeometry(0, 0, lw, fh);
  m_table->setGeometry(0, fh, lw, H - fh - 31);
  m_footer->setGeometry(0, H - 31, lw, 31);
  m_panel->setGeometry(W - shift, 0, pw, H);
  m_panel->raise();
}

void CalendarPage::setScrollMode(bool on) {
  if (on == (m_pageScroll != nullptr)) return;
  if (on) {
    m_pageScroll = new global::PageScroll(m_left, this);
    m_pageScroll->layout = [this](int w, int vh) { return layoutScroll(w, vh); };
    // the table pins its column and day headers at the top of the part the page shows
    auto band = [this] {
      if (!m_pageScroll) return;
      m_table->setVisibleBand(m_pageScroll->verticalScrollBar()->value() - m_table->y(), m_pageScroll->viewport()->height());
    };
    connect(m_pageScroll->verticalScrollBar(), &QScrollBar::valueChanged, this, band);
    connect(m_pageScroll->verticalScrollBar(), &QScrollBar::rangeChanged, this, band);
    m_pageScroll->show();
    m_panel->raise();
  } else {
    m_pageScroll->takeWidget();
    m_left->setParent(this);
    m_left->show();
    m_pageScroll->hide();
    m_pageScroll->deleteLater();
    m_pageScroll = nullptr;
    m_table->setFullHeight(false);
    m_filters->setPhone(false);
    m_panel->raise();
  }
}

int CalendarPage::layoutScroll(int w, int vh) {
  const bool phone = ui::compact(w);
  m_filters->setPhone(phone);
  int fh;
  if (phone) { fh = m_filters->phoneHeight(w); m_filters->setFixedHeight(fh); }
  else { m_filters->setCompact(true); fh = m_filters->height(); }
  m_filters->setGeometry(0, 0, w, fh);
  m_table->setFullHeight(true);
  m_table->resize(w, std::max(1, m_table->height()));   // rows laid out at this width first (two-line rows on phones)
  const int th = std::max(m_table->fullHeight(), vh - fh - 31);   // loading / empty: the note fills the screen
  m_table->setGeometry(0, fh, w, th);
  m_footer->setGeometry(0, fh + th, w, 31);
  m_table->setVisibleBand(m_pageScroll->verticalScrollBar()->value() - fh, vh);
  return fh + th + 31;
}

void CalendarPage::resizeEvent(QResizeEvent*) { layoutChildren(); }
void CalendarPage::showEvent(QShowEvent*) {
  news::Calendar::instance().setActive(this, true);
  if (m_all.isEmpty()) news::Calendar::instance().load(m_week);
}
void CalendarPage::hideEvent(QHideEvent*) { news::Calendar::instance().setActive(this, false); }
void CalendarPage::keyPressEvent(QKeyEvent* e) {
  if (e->key() == Qt::Key_Escape) { closePanel(); return; }
  QWidget::keyPressEvent(e);
}

void CalendarPage::applyHooks() {
  if (m_hooksDone) return;
  m_hooksDone = true;
  auto find = [this](const QByteArray& want) -> const news::Event* {
    const QString w = QString::fromUtf8(want);
    for (const auto& e : m_all) if ((e.currency + " " + e.title).contains(w, Qt::CaseInsensitive) && (e.forecast.has() || e.actual.has())) return &e;
    for (const auto& e : m_all) if ((e.currency + " " + e.title).contains(w, Qt::CaseInsensitive)) return &e;
    return nullptr;
  };
  const QByteArray ex = qgetenv("DW_CAL_EXPAND");
  if (!ex.isEmpty()) if (const auto* e = find(ex)) { m_table->toggleExpand(e->key); m_table->scrollToKey(e->key); }
  const QByteArray sc = qgetenv("DW_CAL_SCROLL");
  if (!sc.isEmpty()) QTimer::singleShot(10, this, [this, sc] { m_table->scrollBy(sc.toDouble()); });
  const QByteArray ch = qgetenv("DW_CAL_CHART");
  if (!ch.isEmpty()) {
    const QList<QByteArray> parts = ch.split(',');
    const int mode = parts.value(0) == "line" ? 1 : 0;
    const QByteArray s = parts.value(1);
    const int span = s == "5y" ? 0 : s == "10y" ? 1 : s == "max" ? 3 : 2;
    m_table->setChartMode(mode, span);
    if (parts.size() > 2) { const double f = parts[2].toDouble(); QTimer::singleShot(2500, this, [this, f] { m_table->hoverChart(f); }); }
  }
  const QByteArray pn = qgetenv("DW_CAL_PANEL");
  if (!pn.isEmpty()) if (const auto* e = find(pn)) { const news::Event copy = *e; openPanel(copy, CalendarTable::Min); }
  // DW_CAL_PANEL_CLOSE=1: close the panel again (the clicked cell keeps its focus outline, reference t013)
  if (!qgetenv("DW_CAL_PANEL_CLOSE").isEmpty()) QTimer::singleShot(900, this, &CalendarPage::closePanel);
  // DW_CAL_HOVER=x,y (page coordinates): hover state of the table or of the panel (rows, links)
  const QList<QByteArray> hv = qgetenv("DW_CAL_HOVER").split(',');
  if (hv.size() == 2) QTimer::singleShot(1500, this, [this, hv] {
    const QPoint p(hv[0].toInt(), hv[1].toInt());
    if (m_panel->isVisible() && m_panel->geometry().contains(p)) m_panel->hoverAt(p - m_panel->pos());
    else m_table->hoverAt(p - m_table->mapTo(this, QPoint(0, 0)));
  });
}
}  // namespace news::pages
