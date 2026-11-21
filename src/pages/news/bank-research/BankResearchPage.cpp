#include "pages/news/bank-research/BankResearchPage.h"
#include "ui/Kit.h"
#include "pages/news/bank-research/Columns.h"
#include "pages/news/bank-research/GlobeView.h"
#include "pages/news/bank-research/PdfPanel.h"
#include "pages/news/bank-research/Look.h"
#include "banks/Registry.h"
#include "banks/Research.h"
#include "app/Pages.h"
#include "ui/Touch.h"
#include <QPainter>
#include <QCoreApplication>
#include "core/I18n.h"
#include <QTimer>
#include <QKeyEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <cmath>
#include <algorithm>

DW_PAGE("news/bank-research", banks::res::BankResearchPage)

namespace banks::res {

BankResearchPage::BankResearchPage(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent);
  setFocusPolicy(Qt::StrongFocus);
  m_strip = new Strip(this);
  m_left = new LeftColumn(this);
  m_right = new RegistryColumn(this);
  m_centre = new QWidget(this);
  m_globe = new GlobeView(m_centre);
  m_pdf = new PdfPanel(m_centre);
  m_pdf->hide();

  // badge: institutions and regions from the registry, source of the assets
  QSet<int> regions; for (const auto& b : banks::systemic()) regions.insert(int(b.region));
  m_strip->setBadge(QCoreApplication::translate("banks", "%1 INSTITUTIONS · %2 REGIONS · SEC EDGAR").arg(banks::systemic().size()).arg(regions.size()));

  m_left->onOpen = [this](const QString& id) { openNote(id); };
  m_right->onClick = [this](const QString& id) { selectBank(id); };
  connect(m_globe, &GlobeView::bankClicked, this, &BankResearchPage::selectBank);
  connect(m_pdf, &PdfPanel::closed, this, &BankResearchPage::closeNote);

  auto& research = banks::Research::instance();
  connect(&research, &banks::Research::updated, this, [this] { m_left->reload(); applyTestHooks(); });
  connect(&banks::Registry::instance(), &banks::Registry::updated, m_right, qOverload<>(&QWidget::update));
  connect(&banks::Registry::instance(), &banks::Registry::filingsUpdated, this, [this] { m_left->reload(); });

  // Cursors blink every 500 ms. While the globe turns they toggle on its frames (the frame nearest each 500 ms step),
  // so the blink shares the frame's repaint pass; the timer does it when the globe is still (PDF open, frozen).
  m_blink.setInterval(500);
  auto toggle = [this] {
    m_nextBlink += 500;
    setBlink(!blinkOn());
    m_strip->update(QRect(0, 0, 720, 37));
    m_left->update(QRect(0, 40, 300, 50));
    m_right->update(QRect(0, 0, m_right->width(), 46));
  };
  connect(&m_blink, &QTimer::timeout, this, [this, toggle] {
    if (m_globe->animating()) return;
    m_nextBlink = m_blinkClock.elapsed();
    toggle();
  });
  connect(m_globe, &GlobeView::frame, this, [this, toggle] {
    const qint64 t = m_blinkClock.elapsed();
    if (t - m_nextBlink > 1000) m_nextBlink = t;   // back from a pause: resync
    if (t >= m_nextBlink - 33) toggle();
  });

  bool ok = false;
  const int tab = qEnvironmentVariable("DW_BANKRES_TAB").toInt(&ok);
  if (ok) m_left->setTab(tab);
}

// Moves a column between the page and the phone's scrolling body; setParent() hides, so its state is put back.
void BankResearchPage::reparent(QWidget* w, QWidget* parent) {
  if (w->parentWidget() == parent) return;
  const bool shown = !w->isHidden();
  w->setParent(parent);
  w->setVisible(shown);
}

// Phones (compact width): the strip stays on top and one column scrolls under it: the notes (left column, a little
// shorter than the screen so the globe peeks below and the page can be dragged by it), the globe, then the whole
// registry (no inner scroll). An opened note covers everything under the strip, like the reader of the News Feed.
void BankResearchPage::layoutPhone(int W, int H) {
  if (!m_page) {
    m_page = new QScrollArea(this);
    m_page->setFrameShape(QFrame::NoFrame);
    m_page->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_page->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_page->setWidgetResizable(false);
    m_page->viewport()->setAutoFillBackground(false);   // the page's own paint (look::page) shows through, theme included
    m_body = new QWidget;
    m_page->setWidget(m_body);
  }
  m_page->show();
  reparent(m_left, m_body); reparent(m_centre, m_body); reparent(m_right, m_body);
  reparent(m_pdf, this);
  m_left->setPhone(true);
  m_strip->setGeometry(0, 0, W, 37);
  const int vh = H - 37;
  m_page->setGeometry(0, 37, W, vh);
  const int leftH = std::max(480, vh - 90);
  const int globeH = std::clamp(int(W * 0.85), 300, 420);
  const int regH = RegistryColumn::fullHeight();
  m_left->setGeometry(0, 0, W, leftH);
  m_centre->setGeometry(0, leftH, W, globeH);
  m_globe->setGeometry(m_centre->rect());
  m_right->setGeometry(0, leftH + globeH, W, regH);
  m_body->setFixedSize(W, leftH + globeH + regH + 12);
  m_pdf->setGeometry(0, 37, W, vh);
  m_pdf->raise();
}

void BankResearchPage::resizeEvent(QResizeEvent*) {
  const int W = width(), H = height();
  if (ui::compact(W)) { layoutPhone(W, H); return; }
  if (m_page && !m_page->isHidden()) {   // back from the phone layout
    reparent(m_left, this); reparent(m_centre, this); reparent(m_right, this);
    reparent(m_pdf, m_centre);
    m_left->setPhone(false);
    m_page->hide();
  }
  // Side columns keep the reference widths (362 / 338) at 1600 px and scale with the window: narrower on laptops
  // (≥ 300 / 270) so the globe and the PDF keep a usable centre, wider on big screens (≤ 480 / 420).
  const int leftW = qBound(300, int(std::lround(W * 0.22625)), 480);
  const int rightW = qBound(270, int(std::lround(W * 0.21125)), 420);
  m_strip->setGeometry(0, 0, W, 37);
  m_left->setGeometry(0, 37, leftW, H - 37);
  m_right->setGeometry(W - rightW, 37, rightW, H - 37);
  m_centre->setGeometry(leftW, 37, W - leftW - rightW, H - 37);
  m_globe->setGeometry(m_centre->rect());
  // the PDF panel fills the centre with 15 px margins, but no wider than 1600 px (the page itself stops at 1150 px:
  // longer text lines are hard to read), centred on ultrawide screens
  QRect pr = m_centre->rect().adjusted(15, 15, -14, -14);
  if (pr.width() > 1600) pr = QRect(pr.center().x() - 800, pr.top(), 1600, pr.height());
  m_pdf->setGeometry(pr);
}

void BankResearchPage::paintEvent(QPaintEvent*) { QPainter p(this); p.fillRect(rect(), look::page); }

void BankResearchPage::showEvent(QShowEvent*) {
  m_blinkClock.start(); m_nextBlink = 500;
  m_blink.start();
  banks::Research::instance().refresh();
  banks::Registry::instance().refresh();
  m_left->reload();
  applyTestHooks();
}
void BankResearchPage::hideEvent(QHideEvent*) { m_blink.stop(); }
void BankResearchPage::preload() {
  banks::Research::instance().refresh();
  banks::Registry::instance().refresh();
}

void BankResearchPage::keyPressEvent(QKeyEvent* e) {
  if (e->key() == Qt::Key_Escape && m_pdf->isVisible()) { closeNote(); return; }
  QWidget::keyPressEvent(e);
}

void BankResearchPage::openNote(const QString& id) {
  const banks::Note* n = banks::Research::instance().note(id);
  if (!n) return;
  m_left->setSelected(id);
  // a podcast episode or a video (the US banks' feeds) plays on its host: the browser, not the PDF reader
  if (n->type == "PODCAST" || n->type == "VIDEO") { ui::openWebLink(n->url); return; }
  m_pdf->openNote(*n);
  m_pdf->show(); m_pdf->raise();
  m_globe->setPaused(true);
  m_globe->hide();
  setFocus();
}

void BankResearchPage::closeNote() {
  m_pdf->hide();
  m_left->setSelected({});
  m_globe->show();
  m_globe->setPaused(false);
}

void BankResearchPage::selectBank(const QString& id) {
  m_right->setSelected(id);
  m_globe->setSelectedBank(id);
  m_left->setBankFilter(id);
}

// Dev hooks for screenshots (documented in the header).
void BankResearchPage::applyTestHooks() {
  if (m_hooksDone) return;
  const auto notes = m_left->list();
  bool ok = false;
  const int hover = qEnvironmentVariable("DW_BANKRES_HOVER").toInt(&ok);
  if (ok && !notes->noteAt(hover).isEmpty()) { notes->setForcedHover(hover); m_hooksDone = true; }
  const int open = qEnvironmentVariable("DW_BANKRES_OPEN").toInt(&ok);
  if (ok && !notes->noteAt(open).isEmpty()) {
    m_hooksDone = true;
    openNote(notes->noteAt(open));
    const int scroll = qEnvironmentVariable("DW_BANKRES_SCROLL").toInt(&ok);
    if (ok) QTimer::singleShot(2500, this, [this, scroll] { m_pdf->view()->verticalScrollBar()->setValue(scroll); });
  }
}
}  // namespace banks::res
