#include "pages/news/bank-research/PdfPanel.h"
#include "ui/Kit.h"
#include "pages/news/bank-research/Look.h"
#include "banks/Registry.h"
#include "ui/Theme.h"
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>
#include <QVariantAnimation>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QToolButton>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QIntValidator>
#include <QDesktopServices>
#include <QFileDialog>
#include <QStandardPaths>
#include <QFile>
#include <QUrl>
#include <QLocale>
#include <QCoreApplication>
#include "core/I18n.h"
#include <QMouseEvent>
#include <QScrollArea>
#include <QFileInfo>
#include <QGuiApplication>
#include <QClipboard>
#include <QMenu>
#include <QMessageBox>
#include <QDir>
#include <cmath>

namespace banks::res {


namespace {
constexpr qreal kGap = 10;        // vertical gap between pages
constexpr qreal kSide = 0;        // horizontal margin in fit-width mode (the reference page fills the width)
constexpr double kPt2Px = 96.0 / 72.0;
// Pixels rendered for one page at most (~80 MB as ARGB32). At 400 % on a Retina screen an A4 page is ~57 MP (228 MB)
// and several were kept (over 1 GB): beyond this cap the page renders smaller and paint scales it up.
constexpr double kMaxPagePixels = 20e6;
// Above this size a rendered page is kept only while it is in or near the view (not the ± 2 pages kept at low zoom).
constexpr double kBigPagePixels = 8e6;
}

// ============================================================================================ PdfDocView
PdfDocView::PdfDocView(QWidget* parent) : QAbstractScrollArea(parent) {
  setFrameShape(QFrame::NoFrame);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  setFocusPolicy(Qt::StrongFocus);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);    // no visible scrollbar in the reference (wheel, keys)
  viewport()->setAutoFillBackground(false);
  m_anim = new QVariantAnimation(this);
  m_anim->setDuration(160);
  m_anim->setEasingCurve(QEasingCurve::OutCubic);
  connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { verticalScrollBar()->setValue(v.toInt()); });
  // A window resize or a Ctrl+wheel zoom changes the scale at every step, and each change started full-page renders
  // that cannot be cancelled (their results were then dropped): lay out at once, render when the steps stop.
  m_renderTimer.setSingleShot(true);
  m_renderTimer.setInterval(150);
  connect(&m_renderTimer, &QTimer::timeout, this, [this] { requestVisible(); });
}
void PdfDocView::renderSoon() { m_renderTimer.start(); }

void PdfDocView::setFile(const QString& file) {
  m_file = file;
  m_info = file.isEmpty() ? banks::pdf::Info{} : banks::pdf::info(file);
  m_img.clear(); m_pending.clear(); ++m_generation; m_imgScale = 0; m_rot = 0; m_fit = true;
#if defined(__APPLE__) || defined(_WIN32)
  m_msg = (!file.isEmpty() && !m_info.ok()) ? QCoreApplication::translate("banks", "THIS FILE IS NOT A READABLE PDF") : QString();
#else
  // no PDF engine on this platform (platform/NonApple.cpp): every file read as "not a readable PDF", which blamed the file
  m_msg = !file.isEmpty() ? QCoreApplication::translate("banks", "PDF PAGES ARE SHOWN ON MACOS AND IOS FOR NOW · SAVE THE FILE TO READ IT") : QString();
#endif
  relayout();
  verticalScrollBar()->setValue(0);
  requestVisible();
  viewport()->update();
  emit stateChanged();
}
// renders in flight belong to the previous file: a new generation drops them (they landed in m_img and the
// pending set kept their pages from being asked again once the next note opened)
void PdfDocView::setMessage(const QString& msg) {
  m_msg = msg; m_file.clear(); m_info = {}; m_img.clear(); m_pending.clear(); ++m_generation;
  relayout(); viewport()->update(); emit stateChanged();
}

QSizeF PdfDocView::pageSize(int i) const {
  const QSizeF s = m_info.sizes.value(i, QSizeF(595, 842));
  return (m_rot % 180) ? s.transposed() : s;
}
double PdfDocView::scale() const {
  if (!m_fit) return m_zoom * kPt2Px;
  qreal maxW = 1;
  for (int i = 0; i < m_info.pages; ++i) maxW = qMax(maxW, pageSize(i).width());
  // fit width, but no page wider than 1150 px (big screens: grey gutter on both sides, readable line length)
  return qMax(0.1, qMin<qreal>(viewport()->width() - 2 * kSide, 1150) / maxW);
}

void PdfDocView::relayout() {
  const double s = scale();
  if (m_fit) m_zoom = s / kPt2Px;
  m_top.resize(m_info.pages);
  qreal y = 0, maxW = 0;
  for (int i = 0; i < m_info.pages; ++i) {
    m_top[i] = y;
    const QSizeF ps = pageSize(i) * s;
    y += ps.height() + kGap;
    maxW = qMax(maxW, ps.width());
  }
  const int total = int(y);
  verticalScrollBar()->setPageStep(viewport()->height());
  verticalScrollBar()->setSingleStep(40);
  verticalScrollBar()->setRange(0, qMax(0, total - viewport()->height()));
  horizontalScrollBar()->setPageStep(viewport()->width());
  horizontalScrollBar()->setRange(0, qMax(0, int(maxW + 2 * kSide) - viewport()->width()));
}

int PdfDocView::currentPage() const {
  const int y = verticalScrollBar()->value() + 40;
  int cur = 0;
  for (int i = 0; i < m_top.size(); ++i) if (m_top[i] <= y) cur = i;
  return cur;
}
void PdfDocView::goToPage(int i) {
  if (i < 0 || i >= m_top.size()) return;
  m_anim->stop();
  verticalScrollBar()->setValue(int(m_top[i]));
}
void PdfDocView::setZoom(double z) { applyZoom(z, false); }
void PdfDocView::applyZoom(double z, bool deferRender) {
  const double frac = verticalScrollBar()->maximum() > 0 ? double(verticalScrollBar()->value()) / verticalScrollBar()->maximum() : 0;
  m_fit = false; m_zoom = qBound(0.25, z, 4.0);
  relayout();
  if (deferRender) renderSoon();   // before setValue: scrollContentsBy then waits for the timer too
  verticalScrollBar()->setValue(int(frac * verticalScrollBar()->maximum()));
  if (!deferRender) requestVisible();
  viewport()->update(); emit stateChanged();
}
void PdfDocView::fitWidth() { m_fit = true; relayout(); requestVisible(); viewport()->update(); emit stateChanged(); }
void PdfDocView::rotate() { m_rot = (m_rot + 90) % 360; m_img.clear(); ++m_generation; m_imgScale = 0; relayout(); requestVisible(); viewport()->update(); emit stateChanged(); }

void PdfDocView::scrollBy(int dy) {
  const int from = verticalScrollBar()->value();
  m_scrollTarget = qBound(0, (m_anim->state() == QAbstractAnimation::Running ? m_scrollTarget : from) + dy, verticalScrollBar()->maximum());
  m_anim->stop();
  m_anim->setStartValue(from); m_anim->setEndValue(m_scrollTarget);
  m_anim->start();
}

void PdfDocView::wheelEvent(QWheelEvent* e) {
  if (e->modifiers() & Qt::ControlModifier) { applyZoom(m_zoom * (e->angleDelta().y() > 0 ? 1.1 : 1 / 1.1), true); return; }
  if (!e->pixelDelta().isNull()) {   // trackpad: already smooth
    verticalScrollBar()->setValue(verticalScrollBar()->value() - e->pixelDelta().y());
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() - e->pixelDelta().x());
  } else scrollBy(-e->angleDelta().y() * 40 / 120 * 2);
  e->accept();
}

void PdfDocView::resizeEvent(QResizeEvent* e) {
  QAbstractScrollArea::resizeEvent(e);
  renderSoon();
  relayout(); viewport()->update();
  if (m_fit) emit stateChanged();
}
void PdfDocView::scrollContentsBy(int, int) {
  if (!m_renderTimer.isActive()) requestVisible();   // otherwise the pending timer asks for the visible pages
  viewport()->update(); emit stateChanged();
}
double PdfDocView::renderScale(int i, double s, qreal dpr) const {
  const QSizeF ps = pageSize(i);
  const double px = ps.width() * s * dpr * ps.height() * s * dpr;
  return px > kMaxPagePixels ? s * dpr * std::sqrt(kMaxPagePixels / px) : s * dpr;
}

void PdfDocView::requestVisible() {
  if (m_file.isEmpty() || !m_info.ok()) return;
  const double s = scale();
  if (std::abs(s - m_imgScale) > 1e-3) {             // new zoom: old images stay (scaled) until replaced
    m_imgScale = s; ++m_generation; m_pending.clear();
  }
  const int y0 = verticalScrollBar()->value(), y1 = y0 + viewport()->height();
  QList<int> want;
  for (int i = 0; i < m_info.pages; ++i) {
    const qreal top = m_top[i], bot = top + pageSize(i).height() * s;
    if (bot >= y0 - viewport()->height() / 2 && top <= y1 + viewport()->height() / 2) want << i;
  }
  // drop images far from view; big ones (high zoom) as soon as they leave the wanted pages
  const int cur = currentPage();
  for (auto it = m_img.begin(); it != m_img.end();) {
    const bool big = double(it->width()) * it->height() > kBigPagePixels;
    if (!want.contains(it.key()) && (big || std::abs(it.key() - cur) > 2)) it = m_img.erase(it); else ++it;
  }
  const qreal dpr = devicePixelRatioF();
  for (int i : want) {
    const auto it = m_img.constFind(i);
    const double rs = renderScale(i, s, dpr);   // device pixels per point actually rendered
    const bool fresh = it != m_img.cend() && std::abs(it->width() - pageSize(i).width() * rs) < 2;
    if (fresh || m_pending.contains(i)) continue;
    m_pending.insert(i);
    const QString file = m_file; const int rot = m_rot; const int gen = m_generation;
    auto* w = new QFutureWatcher<QImage>(this);
    connect(w, &QFutureWatcher<QImage>::finished, this, [this, w, i, gen] {
      w->deleteLater();
      if (gen != m_generation) return;
      m_pending.remove(i);
      QImage img = w->result();
      if (!img.isNull()) { m_img.insert(i, img); viewport()->update(); }
    });
    // render() makes scale × dpr device pixels per point: pass the capped ratio; paint draws it into the page's rectangle
    const double rdpr = rs / s;
    w->setFuture(QtConcurrent::run([file, i, s, rdpr, rot] {
      QImage img = banks::pdf::render(file, i, s, rdpr);
      if (rot && !img.isNull()) { img = img.transformed(QTransform().rotate(rot)); img.setDevicePixelRatio(rdpr); }
      return img;
    }));
  }
}

void PdfDocView::paintEvent(QPaintEvent*) {
  QPainter p(viewport());
  p.fillRect(viewport()->rect(), look::pdfGutter);
  if (!m_msg.isEmpty() || !m_info.ok()) {
    p.setFont(theme::mono(11, QFont::Normal, 3)); p.setPen(theme::map("#9a9a96"));
    p.drawText(viewport()->rect(), Qt::AlignCenter | Qt::TextWordWrap, m_msg.isEmpty() ? QCoreApplication::translate("banks", "LOADING") : m_msg);
    return;
  }
  const double s = scale();
  const int y0 = verticalScrollBar()->value(), x0 = horizontalScrollBar()->value();
  const int contentW = viewport()->width();
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  for (int i = 0; i < m_info.pages; ++i) {
    const QSizeF ps = pageSize(i) * s;
    const qreal x = qMax<qreal>(kSide, (contentW - ps.width()) / 2) - x0;
    const QRectF r(x, m_top[i] - y0, ps.width(), ps.height());
    if (r.bottom() < 0 || r.top() > viewport()->height()) continue;
    p.fillRect(r.translated(0, 2), theme::map(0, 0, 0, 70));
    const auto it = m_img.constFind(i);
    if (it != m_img.cend()) p.drawImage(r, *it);
    else p.fillRect(r, Qt::white);
  }
}

// ============================================================================================ ThumbStrip
ThumbStrip::ThumbStrip(PdfDocView* view, QWidget* parent) : QWidget(parent), m_view(view) { setCursor(Qt::PointingHandCursor); }
void ThumbStrip::setFile(const QString& file, const banks::pdf::Info& info) {
  m_file = file; m_info = info; m_img.clear(); m_pending.clear(); ++m_gen; m_cur = 0;
  setFixedHeight(sizeHint().height()); update();
  if (isVisible()) request();
}
QRectF ThumbStrip::thumbRect(int i) const {
  qreal y = 16;
  for (int k = 0; k < i; ++k) { const QSizeF s = m_info.sizes.value(k); y += 120.0 * s.height() / qMax(1.0, s.width()) + 34; }
  const QSizeF s = m_info.sizes.value(i, QSizeF(595, 842));
  return QRectF((width() - 120) / 2.0, y, 120, 120.0 * s.height() / qMax(1.0, s.width()));
}
QSize ThumbStrip::sizeHint() const { return QSize(196, m_info.pages ? int(thumbRect(m_info.pages - 1).bottom() + 34) : 100); }
void ThumbStrip::showEvent(QShowEvent*) { request(); }
void ThumbStrip::request() {
  if (m_file.isEmpty()) return;
  const qreal dpr = devicePixelRatioF();
  for (int i = 0; i < m_info.pages && i < 60; ++i) {
    if (m_img.contains(i) || m_pending.contains(i)) continue;
    m_pending.insert(i);
    const QString f = m_file; const int gen = m_gen;
    const double sc = 120.0 / qMax(1.0, m_info.sizes.value(i, QSizeF(595, 842)).width());
    auto* w = new QFutureWatcher<QImage>(this);
    connect(w, &QFutureWatcher<QImage>::finished, this, [this, w, i, gen] {
      w->deleteLater();
      if (gen != m_gen) return;
      m_pending.remove(i); m_img.insert(i, w->result()); update();
    });
    w->setFuture(QtConcurrent::run([f, i, sc, dpr] { return banks::pdf::render(f, i, sc, dpr); }));
  }
}
void ThumbStrip::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.fillRect(rect(), theme::map("#3b3b3b"));
  p.setFont(theme::sans(12)); 
  for (int i = 0; i < m_info.pages; ++i) {
    const QRectF r = thumbRect(i);
    if (i == m_cur) { p.setPen(Qt::NoPen); p.setBrush(theme::map("#8ab4f8")); p.drawRoundedRect(r.adjusted(-4, -4, 4, 4), 3, 3); }
    const auto it = m_img.constFind(i);
    if (it != m_img.cend()) p.drawImage(r, *it); else p.fillRect(r, Qt::white);
    p.setPen(theme::map("#e8eaed"));
    p.drawText(QRectF(r.left(), r.bottom() + 6, r.width(), 16), Qt::AlignCenter, QString::number(i + 1));
  }
}
void ThumbStrip::mousePressEvent(QMouseEvent* e) {
  for (int i = 0; i < m_info.pages; ++i) if (thumbRect(i).adjusted(-4, -4, 4, 20).contains(e->position())) { m_view->goToPage(i); setCurrent(i); return; }
}

// ============================================================================================ PdfPanel
namespace {
// Toolbar glyphs drawn like the browser viewer's icons (white 1.6 px strokes on 20 px).
QIcon glyph(const QString& kind) {
  const qreal dpr = 2.0;
  QPixmap pm(QSize(20, 20) * dpr); pm.setDevicePixelRatio(dpr); pm.fill(Qt::transparent);
  QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing);
  QPen pen(theme::map("#f1f1f1"), 1.7); pen.setCapStyle(Qt::RoundCap); pen.setJoinStyle(Qt::RoundJoin);
  p.setPen(pen); p.setBrush(Qt::NoBrush);
  if (kind == "menu") { for (int i = 0; i < 3; ++i) p.drawLine(QPointF(3, 5 + i * 5), QPointF(17, 5 + i * 5)); }
  else if (kind == "minus") p.drawLine(QPointF(4, 10), QPointF(16, 10));
  else if (kind == "plus") { p.drawLine(QPointF(4, 10), QPointF(16, 10)); p.drawLine(QPointF(10, 4), QPointF(10, 16)); }
  else if (kind == "fit") { p.drawRoundedRect(QRectF(5, 2.5, 10, 15), 1.5, 1.5); p.drawLine(QPointF(10, 6), QPointF(10, 8.5)); p.drawLine(QPointF(10, 11.5), QPointF(10, 14)); }
  else if (kind == "rotate") {
    p.drawArc(QRectF(4, 4, 12, 12), 100 * 16, 260 * 16);
    p.drawLine(QPointF(8.2, 4.2), QPointF(5.5, 2.5)); p.drawLine(QPointF(8.2, 4.2), QPointF(6.5, 6.8));
    p.drawRect(QRectF(8.5, 8.5, 3, 3));
  }
  else if (kind == "download") { p.drawLine(QPointF(10, 3), QPointF(10, 13)); p.drawLine(QPointF(6, 9), QPointF(10, 13)); p.drawLine(QPointF(14, 9), QPointF(10, 13)); p.drawLine(QPointF(4, 17), QPointF(16, 17)); }
  else if (kind == "print") { p.drawRect(QRectF(6, 3, 8, 4)); p.drawRoundedRect(QRectF(3, 7, 14, 7), 1.5, 1.5); p.drawRect(QRectF(6, 11.5, 8, 5.5)); }
  else if (kind == "more") { p.setPen(Qt::NoPen); p.setBrush(theme::map("#f1f1f1")); for (int i = 0; i < 3; ++i) p.drawEllipse(QPointF(10, 4.5 + i * 5.5), 1.6, 1.6); }
  p.end();
  return QIcon(pm);
}
QToolButton* toolBtn(const QString& kind, const QString& tip, QWidget* parent) {
  auto* b = new QToolButton(parent);
  b->setIcon(glyph(kind)); b->setIconSize(QSize(20, 20)); b->setToolTip(tip); b->setCursor(Qt::PointingHandCursor);
  b->setFixedSize(36, 36);
  banks::setQss(b, "QToolButton { background: transparent; border: none; border-radius: 18px; }"
                   "QToolButton:hover { background: #5f5f5f; } QToolButton:checked { background: #5f5f5f; }");
  return b;
}
QPushButton* headBtn(const QString& text, QWidget* parent) {
  auto* b = new QPushButton(text, parent);
  b->setCursor(Qt::PointingHandCursor);
  b->setFixedHeight(22);
  banks::setQss(b, QString("QPushButton { color: %1; background: transparent; border: 1px solid %2; border-radius: 2px;"
                           " padding: 0 7px; font-family: '%3'; font-size: 10px; letter-spacing: 1px; }"
                           "QPushButton:hover { color: #e8e2c8; border-color: #5a5440; }")
                       .arg(look::btnText.dw().name(), look::btnBorder.dw().name(), theme::mono(10).family()));
  return b;
}
}

PdfPanel::PdfPanel(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_StyledBackground, false);
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(8, 64, 8, 2);
  v->setSpacing(0);

  // header buttons (painted header text is in paintEvent)
  auto* bPdf = headBtn(QCoreApplication::translate("banks", "⤓ PDF"), this);
  auto* bSrc = headBtn(QCoreApplication::translate("banks", "⧉ SOURCE"), this);
  auto* bClose = new QPushButton(QString::fromUtf8("×"), this);
  bClose->setCursor(Qt::PointingHandCursor);
  bClose->setFixedSize(22, 22);
  banks::setQss(bClose, QString("QPushButton { color: %1; background: transparent; border: none; font-size: 17px; }"
                                "QPushButton:hover { color: #f5f2eb; }").arg(look::btnText.dw().name()));
  bPdf->setToolTip(QCoreApplication::translate("banks", "Save a copy of the PDF"));
  bSrc->setToolTip(QCoreApplication::translate("banks", "Open the note on the bank's website"));
  bClose->setToolTip(QCoreApplication::translate("banks", "Close (Esc)"));
  connect(bPdf, &QPushButton::clicked, this, &PdfPanel::savePdf);
  connect(bSrc, &QPushButton::clicked, this, [this] { if (!m_note.url.isEmpty()) ui::openWebLink(m_note.url); });
  connect(bClose, &QPushButton::clicked, this, [this] { hide(); emit closed(); });
  auto* hb = new QWidget(this);
  auto* hl = new QHBoxLayout(hb); hl->setContentsMargins(0, 0, 0, 0); hl->setSpacing(6);
  hl->addWidget(bPdf); hl->addWidget(bSrc); hl->addSpacing(4); hl->addWidget(bClose);

  // toolbar: [≡ title] … [page n / N | − zoom + | fit rotate] … [save print ⋮], middle cluster centred
  m_toolbar = new QWidget(this);
  m_toolbar->setFixedHeight(62);
  m_toolbar->setAutoFillBackground(true);
  QPalette tp = m_toolbar->palette(); tp.setColor(QPalette::Window, theme::isDefault() ? look::pdfToolbar.dw() : theme::roles().hover);   // themes: a neutral bar, not a mapped mid-grey
  m_toolbar->setPalette(tp);
  auto* tl = new QHBoxLayout(m_toolbar); tl->setContentsMargins(10, 0, 14, 0); tl->setSpacing(0);
  auto* left = new QWidget(m_toolbar); auto* ll = new QHBoxLayout(left); ll->setContentsMargins(0, 0, 0, 0); ll->setSpacing(10);
  auto* mid = new QWidget(m_toolbar); auto* ml = new QHBoxLayout(mid); ml->setContentsMargins(0, 0, 0, 0); ml->setSpacing(4);
  auto* right = new QWidget(m_toolbar); auto* rl = new QHBoxLayout(right); rl->setContentsMargins(0, 0, 0, 0); rl->setSpacing(6);
  left->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  right->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  auto* menu = toolBtn("menu", QCoreApplication::translate("banks", "Pages"), left); menu->setCheckable(true);
  m_docTitle = new QLabel(left);
  banks::setQss(m_docTitle, QString("color: %1; font-family: 'Inter'; font-size: 16px; font-weight: 600;").arg(look::pdfText.dw().name()));
  m_docTitle->setMinimumWidth(40);
  m_docTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  ll->addWidget(menu); ll->addWidget(m_docTitle, 1);
  m_pageEdit = new QLineEdit(mid);
  m_pageEdit->setFixedSize(26, 26);
  m_pageEdit->setAlignment(Qt::AlignCenter);
  banks::setQss(m_pageEdit, "QLineEdit { background: #262a29; color: #f1f1f1; border: none; border-radius: 0; padding: 0;"
                            " font-family: 'Inter'; font-size: 15px; }");
  m_pageEdit->setValidator(new QIntValidator(1, 9999, m_pageEdit));
  m_pageCount = new QLabel(mid);
  banks::setQss(m_pageCount, "color: #f1f1f1; font-family: 'Inter'; font-size: 15px;");
  m_zoomLabel = new QLabel(mid);
  m_zoomLabel->setFixedSize(52, 26);
  m_zoomLabel->setAlignment(Qt::AlignCenter);
  banks::setQss(m_zoomLabel, "background: #262a29; color: #f1f1f1; font-family: 'Inter'; font-size: 15px;");
  auto* zOut = toolBtn("minus", QCoreApplication::translate("banks", "Zoom out"), mid);
  auto* zIn = toolBtn("plus", QCoreApplication::translate("banks", "Zoom in"), mid);
  auto* fit = toolBtn("fit", QCoreApplication::translate("banks", "Fit to width"), mid);
  auto* rot = toolBtn("rotate", QCoreApplication::translate("banks", "Rotate counter-clockwise"), mid);
  auto* dl = toolBtn("download", QCoreApplication::translate("banks", "Save a copy"), right);
  auto* pr = toolBtn("print", QCoreApplication::translate("banks", "Print"), right);
  auto* more = toolBtn("more", QCoreApplication::translate("banks", "More"), right);
  auto sep = [](QWidget* parent) { auto* s = new QWidget(parent); s->setFixedSize(1, 22); banks::setQss(s, "background: #8a8a8a;"); return s; };
  ml->addWidget(m_pageEdit); ml->addSpacing(6); ml->addWidget(m_pageCount);
  ml->addSpacing(16); ml->addWidget(sep(mid)); ml->addSpacing(8);
  ml->addWidget(zOut); ml->addWidget(m_zoomLabel); ml->addWidget(zIn);
  ml->addSpacing(8); ml->addWidget(sep(mid)); ml->addSpacing(8);
  ml->addWidget(fit); ml->addWidget(rot);
  rl->addStretch(1); rl->addWidget(dl); rl->addWidget(pr); rl->addWidget(more);
  tl->addWidget(left, 1); tl->addWidget(mid, 0); tl->addWidget(right, 1);
  m_wideOnly = {fit, rot, dl, pr};
  v->addWidget(m_toolbar);

  auto* body = new QWidget(this);
  auto* bl = new QHBoxLayout(body); bl->setContentsMargins(0, 0, 0, 0); bl->setSpacing(0);
  m_view = new PdfDocView(body);
  auto* ts = new QScrollArea(body);
  ts->setFrameShape(QFrame::NoFrame);
  ts->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  ts->setFixedWidth(196);
  ts->setWidgetResizable(true);
  m_thumbs = new ThumbStrip(m_view, ts);
  ts->setWidget(m_thumbs);
  ts->hide();
  m_thumbScroll = ts;
  bl->addWidget(ts); bl->addWidget(m_view, 1);
  v->addWidget(body, 1);

  connect(menu, &QToolButton::toggled, this, [this](bool on) { m_thumbScroll->setVisible(on); });
  connect(zOut, &QToolButton::clicked, this, [this] { m_view->setZoom(m_view->zoom() / 1.1); });
  connect(zIn, &QToolButton::clicked, this, [this] { m_view->setZoom(m_view->zoom() * 1.1); });
  connect(fit, &QToolButton::clicked, m_view, &PdfDocView::fitWidth);
  connect(rot, &QToolButton::clicked, m_view, &PdfDocView::rotate);
  connect(dl, &QToolButton::clicked, this, &PdfPanel::savePdf);
  connect(pr, &QToolButton::clicked, this, [this] { if (!m_file.isEmpty()) banks::pdf::print(m_file); });
  connect(more, &QToolButton::clicked, this, [this, more] {
    QMenu m(this);
    QAction* save = m.addAction(QCoreApplication::translate("banks", "Save a copy…"));
    QAction* print = m.addAction(QCoreApplication::translate("banks", "Print…"));
    m.addSeparator();
    QAction* open = m.addAction(QCoreApplication::translate("banks", "Open in the default PDF viewer"));
    QAction* copy = m.addAction(QCoreApplication::translate("banks", "Copy the PDF link"));
    QAction* src = m.addAction(QCoreApplication::translate("banks", "Open the note on the bank's website"));
    m.addSeparator();
    const auto info = m_file.isEmpty() ? banks::pdf::Info{} : banks::pdf::info(m_file);
    QAction* props = m.addAction(QCoreApplication::translate("banks", "Document: %1 pages · %2 KB").arg(info.pages)
                                   .arg(m_file.isEmpty() ? 0 : QFileInfo(m_file).size() / 1024));
    props->setEnabled(false);
    open->setEnabled(!m_file.isEmpty());
    QAction* a = m.exec(more->mapToGlobal(QPoint(0, more->height())));
    if (a == save) savePdf();
    else if (a == print) { if (!m_file.isEmpty()) banks::pdf::print(m_file); }
    else if (a == open) QDesktopServices::openUrl(QUrl::fromLocalFile(m_file));
    else if (a == copy) QGuiApplication::clipboard()->setText(m_note.pdfUrl);
    else if (a == src) ui::openWebLink(m_note.url);
  });
  connect(m_pageEdit, &QLineEdit::returnPressed, this, [this] { m_view->goToPage(m_pageEdit->text().toInt() - 1); });
  connect(m_view, &PdfDocView::stateChanged, this, &PdfPanel::refreshToolbar);

  m_blink.setInterval(530);
  connect(&m_blink, &QTimer::timeout, this, [this] { m_cursorOn = !m_cursorOn; update(QRect(0, 0, width(), 40)); });

  m_headButtons = hb;
}

void PdfPanel::resizeEvent(QResizeEvent*) {
  // narrow panels (small laptops): rotate, fit, save and print move to the ⋮ menu / are dropped from the bar
  const bool narrow = width() < 660, tiny = width() < 520;
  for (QWidget* w : m_wideOnly) w->setVisible(!narrow);
  m_docTitle->setVisible(!tiny);
  m_headButtons->adjustSize();
  m_headButtons->move(width() - m_headButtons->width() - 12, 12);
}

void PdfPanel::openNote(const banks::Note& n) {
  m_note = n; m_file.clear();
  m_thumbs->setFile(QString(), {});   // the previous note's page thumbnails stayed in the strip while this one loads
  m_docTitle->setText(n.type == "FORECASTS" ? QCoreApplication::translate("banks", "Monthly Forecasts") : banks::typeLabel(n.type).left(1) + banks::typeLabel(n.type).mid(1).toLower());
  m_view->setMessage(QCoreApplication::translate("banks", "DOWNLOADING THE PDF FROM %1").arg(QUrl(n.pdfUrl).host().toUpper()));
  update();
  if (n.pdfUrl.isEmpty()) { m_view->setMessage(QCoreApplication::translate("banks", "THIS NOTE HAS NO PDF · USE SOURCE TO OPEN IT ON THE BANK'S SITE")); return; }
  const QString id = n.id;
  banks::Research::instance().fetchPdf(id, this, [this, id](const QString& file, const QString& err) {
    if (id != m_note.id) return;
    if (file.isEmpty()) { m_view->setMessage(QCoreApplication::translate("banks", "PDF UNAVAILABLE · %1").arg(err.toUpper())); return; }
    m_file = file;
    m_view->setFile(file);
    const auto info = m_view->info();   // read once by the view (the PDF was opened three times per note)
    m_thumbs->setFile(file, info);
    if (!info.title.isEmpty() && info.title.size() < 40) {
      m_docTitle->setTextFormat(Qt::PlainText);   // the PDF's own Title metadata
      m_docTitle->setText(info.title);
    }
  });
}

void PdfPanel::refreshToolbar() {
  const int n = m_view->pageCount();
  if (!m_pageEdit->hasFocus()) m_pageEdit->setText(n ? QString::number(m_view->currentPage() + 1) : QString());
  m_pageCount->setText(n ? QString("/  %1").arg(n) : QString());
  m_zoomLabel->setText(n ? QString("%1%").arg(int(std::round(m_view->zoom() * 100))) : QString());
  m_thumbs->setCurrent(m_view->currentPage());
}

void PdfPanel::savePdf() {
  if (m_file.isEmpty()) return;
  QString name = QUrl(m_note.pdfUrl).fileName();
  if (!name.endsWith(".pdf", Qt::CaseInsensitive)) name += ".pdf";
  const QString dest = QFileDialog::getSaveFileName(this, QCoreApplication::translate("banks", "Save PDF"),
      QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/" + name, "PDF (*.pdf)");
  if (dest.isEmpty()) return;
  // copy beside the destination first and replace it only once the copy is whole: removing it first lost the
  // user's existing file when the copy then failed (disk full, permissions)
  const QString part = dest + ".part";
  QFile::remove(part);
  const bool copied = QFile::copy(m_file, part);
  bool ok = copied;
  if (ok && QFile::exists(dest)) ok = QFile::remove(dest);
  if (ok) ok = QFile::rename(part, dest);
  if (!ok) {
    if (!copied) QFile::remove(part);   // a whole copy is kept as .part when only the swap failed
    QMessageBox::warning(this, QCoreApplication::translate("banks", "Save PDF"),
                         QCoreApplication::translate("banks", "The PDF could not be saved to %1.").arg(QDir::toNativeSeparators(dest)));
  }
}

void PdfPanel::showEvent(QShowEvent*) { m_blink.start(); }
void PdfPanel::hideEvent(QHideEvent*) { m_blink.stop(); }

void PdfPanel::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  // shadow + panel
  for (int i = 6; i >= 1; --i) { p.setPen(Qt::NoPen); p.setBrush(theme::map(0, 0, 0, 18)); p.drawRect(rect().adjusted(-i, -i + 2, i, i + 2)); }
  p.fillRect(rect(), look::panelBg);
  p.setRenderHint(QPainter::Antialiasing, false);
  p.setPen(look::panelBorder); p.setBrush(Qt::NoBrush);
  p.drawRect(rect().adjusted(0, 0, -1, -1));
  // line 1: CATEGORY · 23 OCT 2026, 11:11 ▮
  const QDateTime t = m_note.published.toLocalTime();
  const QString meta = QString("%1 · %2").arg(banks::typeLabel(m_note.type), i18n::locale().toString(t, "dd MMM yyyy, HH:mm").toUpper());
  QFont f1 = theme::mono(10, QFont::Normal, 3.0);
  p.setFont(f1); p.setPen(theme::map("#585750"));
  const QString metaE = QFontMetricsF(f1).elidedText(meta, Qt::ElideRight, m_headButtons->x() - 14 - 24);
  p.drawText(QPointF(14, 22), metaE);
  const qreal mw = QFontMetricsF(f1).horizontalAdvance(metaE);
  if (m_cursorOn) p.fillRect(QRectF(14 + mw + 4, 12, 7, 13), theme::map("#f7e8a6"));
  // line 2: logo + BANK + title
  const banks::Bank* b = banks::bank(m_note.bankId);
  p.drawPixmap(QPointF(14, 32), banks::logo(m_note.bankId, 22, devicePixelRatioF()));
  QFont f2 = theme::display(14, QFont::Bold, 0.8);
  p.setFont(f2); p.setPen(look::white);
  const QString bn = b ? b->shortName : m_note.bankId.toUpper();
  p.drawText(QPointF(44, 49), bn);
  const qreal bw = QFontMetricsF(f2).horizontalAdvance(bn);
  QFont f3 = theme::condensed(18, QFont::DemiBold);
  p.setFont(f3);
  const qreal tx = 44 + bw + 10;
  p.drawText(QRectF(tx, 32, width() - tx - 16, 24), Qt::AlignVCenter | Qt::AlignLeft,
             QFontMetricsF(f3).elidedText(m_note.title, Qt::ElideRight, width() - tx - 16));
}
}  // namespace banks::res
