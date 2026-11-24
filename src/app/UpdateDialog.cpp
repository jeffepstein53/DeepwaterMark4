#include "app/UpdateDialog.h"
#include "app/Updater.h"
#include "core/I18n.h"
#include "ui/Kit.h"
#include "ui/Theme.h"
#include "ui/Touch.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTextBrowser>

namespace updates {
namespace {
QString utr(const char* s) { return QCoreApplication::translate("update", s); }
}
// The download's progress: a thin amber bar with its percentage.
class Bar : public QWidget {
public:
  using QWidget::QWidget;
  double value = 0;
protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto& c = theme::pal();
    const QRectF track(0, 4, width(), 6);
    p.setPen(Qt::NoPen); p.setBrush(c.panelAlt); p.drawRoundedRect(track, 3, 3);
    p.setBrush(c.accent); p.drawRoundedRect(QRectF(track.left(), track.top(), track.width() * std::clamp(value, 0.0, 1.0), track.height()), 3, 3);
  }
};
}  // namespace updates

using updates::utr;

namespace {
QPushButton* button(bool primary, QWidget* parent) {
  auto* b = new QPushButton(parent);
  b->setCursor(Qt::PointingHandCursor);
  b->setFocusPolicy(Qt::NoFocus);
  b->setFont(theme::mono(11, QFont::Bold, 1.2));
  const auto& c = theme::pal();
  b->setStyleSheet(primary
    ? QString("QPushButton { background: %1; color: %2; border: 0; border-radius: 3px; padding: 8px 16px; }"
              "QPushButton:hover { background: %3; } QPushButton:disabled { background: %4; color: %5; }")
          .arg(c.accent.name(), theme::onAccent().name(), theme::mix(c.accent, c.text, 0.2).name(), c.panelAlt.name(), c.textMute.name())
    : QString("QPushButton { background: transparent; color: %1; border: 1px solid %2; border-radius: 3px; padding: 7px 14px; }"
              "QPushButton:hover { color: %3; border-color: %4; } QPushButton:disabled { color: %5; border-color: %2; }")
          .arg(c.textDim.name(), c.border.name(), c.text.name(), c.borderHi.name(), c.textMute.name()));
  return b;
}
constexpr int kTitleH = 58;
}

UpdateDialog::UpdateDialog(QWidget* parent) : QDialog(parent) {
  setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);
  const auto& c = theme::pal();
  m_status = ui::label({}, theme::sans(15, QFont::DemiBold), c.text, this);
  m_status->setWordWrap(true);
  m_detail = ui::label({}, theme::sans(12), c.textDim, this);
  m_detail->setWordWrap(true);
  m_notes = new QTextBrowser(this);
  m_notes->setOpenExternalLinks(true);
  m_notes->setFrameShape(QFrame::NoFrame);
  m_notes->viewport()->setAutoFillBackground(false);
  m_notes->setStyleSheet(QString("QTextBrowser { background: %1; border: 1px solid %2; border-radius: 3px; color: %3; padding: 8px; font-size: 13px; }")
                             .arg(theme::map("#0e0a05").name(), c.border.name(), theme::mix(c.textDim, c.text, 0.45).name()));
  m_bar = new updates::Bar(this);
  m_primary = button(true, this);
  m_secondary = button(false, this);
  m_secondary->setText(utr("Check now"));
  connect(m_secondary, &QPushButton::clicked, this, [] { app::Updater::instance().check(); });
  connect(m_primary, &QPushButton::clicked, this, [] { app::Updater::instance().install(); });
  connect(&app::Updater::instance(), &app::Updater::changed, this, &UpdateDialog::refresh);
  connect(&i18n::Notifier::instance(), &i18n::Notifier::changed, this, &UpdateDialog::refresh);
  refresh();
  fit();
}

void UpdateDialog::refresh() {
  auto& u = app::Updater::instance();
  const auto& c = theme::pal();
  using S = app::Updater::State;
  const auto r = u.latest();
  const QString cur = u.current().toString();
  QString status, detail;
  bool notes = false, bar = false, primary = false, secondary = true;
  switch (u.state()) {
    case S::Idle:
    case S::Checking:
      status = utr("Looking for a new version…");
      detail = utr("Installed: version %1.").arg(cur);
      secondary = false;
      break;
    case S::UpToDate:
      status = utr("Deepwater is up to date.");
      detail = utr("Version %1 · checked at %2.").arg(cur, i18n::locale().toString(u.lastCheck().toLocalTime(), "HH:mm"));
      break;
    case S::Available:
      status = utr("Version %1 is available.").arg(r.version.toString());
      detail = utr("Installed: version %1.").arg(cur);
      if (r.published.isValid()) detail += " " + utr("Published on %1.").arg(i18n::locale().toString(r.published.toLocalTime().date(), QLocale::LongFormat));
      if (!app::Updater::canInstallItself())
        detail += "\n" + utr("iOS does not let an app install itself: install the new version from its release page (Sideloadly, AltStore, or TestFlight).");
      notes = primary = true;
      break;
    case S::Downloading:
      status = utr("Downloading version %1… %2 %").arg(r.version.toString()).arg(qRound(u.progress() * 100));
      detail = utr("Deepwater starts again by itself once it is installed.");
      notes = bar = true; secondary = false;
      break;
    case S::Installing:
      status = utr("Installing version %1…").arg(r.version.toString());
      detail = utr("Deepwater starts again by itself once it is installed.");
      notes = true; secondary = false;
      break;
    case S::Failed:
      status = utr("The check did not work.");
      detail = utr("Installed: version %1.").arg(cur);
      break;
  }
  if (!u.error().isEmpty()) detail += "\n" + u.error();
  m_status->setText(status);
  m_detail->setText(detail);
  m_detail->setStyleSheet(QString("color: %1; background: transparent;").arg((!u.error().isEmpty() ? c.warn : c.textDim).name()));
  if (notes) m_notes->setMarkdown(r.notes.trimmed().isEmpty() ? utr("No notes for this version.") : r.notes);
  m_notes->setVisible(notes);
  m_bar->setVisible(bar);
  m_bar->value = u.progress();
  m_bar->update();
  m_primary->setVisible(primary);
  m_primary->setText(app::Updater::canInstallItself() ? utr("Install and restart") : utr("Open the release page"));
  m_secondary->setVisible(secondary);
  m_secondary->setEnabled(u.state() != S::Checking);
  fit();
  resizeEvent(nullptr);   // the texts' heights may have changed without the size
  update();
}

void UpdateDialog::fit() {
  // the main window's content (its central widget: clear of a phone's status bar and home bar), in screen coordinates
  const QWidget* host = parentWidget() ? parentWidget()->window() : nullptr;
  QRect area = host ? host->geometry() : QRect(0, 0, 600, 600);
  if (auto* mw = qobject_cast<const QMainWindow*>(host); mw && mw->centralWidget())
    area = QRect(mw->centralWidget()->mapToGlobal(QPoint(0, 0)), mw->centralWidget()->size());
  const int w = std::min(560, area.width() - (ui::compact(area.width()) ? 16 : 48));
  const int want = m_notes->isVisible() ? 560 : 250;
  const int h = std::min(want, area.height() - 16);
  if (size() != QSize(w, h)) setFixedSize(w, h);
  if (host) move(area.center() - QPoint(w / 2, h / 2));
}

void UpdateDialog::resizeEvent(QResizeEvent*) {
  const int pad = (width() < 520) ? 16 : 26, inner = width() - 2 * pad;
  int y = kTitleH + 18;
  m_status->setGeometry(pad, y, inner, m_status->heightForWidth(inner)); y += m_status->height() + 6;
  m_detail->setGeometry(pad, y, inner, m_detail->heightForWidth(inner)); y += m_detail->height() + 14;
  const int bh = 38, by = height() - pad - bh;
  if (m_bar->isVisible()) { m_bar->setGeometry(pad, y, inner, 14); y += 26; }
  m_notes->setGeometry(pad, y, inner, std::max(40, by - 14 - y));
  // buttons on the right; the check button left of the main one
  int x = width() - pad;
  if (m_primary->isVisible()) { const int bw = m_primary->sizeHint().width(); m_primary->setGeometry(x - bw, by, bw, bh); x -= bw + 10; }
  if (m_secondary->isVisible()) { const int bw = m_secondary->sizeHint().width(); m_secondary->setGeometry(x - bw, by, bw, bh); }
}

QRectF UpdateDialog::closeRect() const { return (width() < 520) ? QRectF(width() - 46, 11, 36, 36) : QRectF(width() - 44, 16, 26, 26); }

void UpdateDialog::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const auto& c = theme::pal();
  p.setPen(QPen(c.borderHi, 1)); p.setBrush(theme::map("#0b0804"));
  p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
  ui::drawBrackets(p, QRectF(rect()).adjusted(4, 4, -4, -4), c.accent, 12);
  const qreal pad = (width() < 520) ? 16 : 26;
  p.setPen(Qt::NoPen); p.setBrush(c.accent); p.drawEllipse(QPointF(pad + 3, 31), 3, 3);
  p.setFont(theme::mono(13, QFont::Bold, 3.0)); p.setPen(c.accentText);
  p.drawText(QPointF(pad + 14, 36), utr("UPDATE"));
  const QRectF xr = closeRect();
  p.setPen(QPen(c.textDim, 1.6, Qt::SolidLine, Qt::RoundCap));
  p.drawLine(xr.center() + QPointF(-6, -6), xr.center() + QPointF(6, 6));
  p.drawLine(xr.center() + QPointF(6, -6), xr.center() + QPointF(-6, 6));
  p.fillRect(QRectF(pad, kTitleH, width() - 2 * pad, 1), c.divider);
}

void UpdateDialog::mousePressEvent(QMouseEvent* e) {
  if (closeRect().contains(e->position())) { hide(); return; }
  QDialog::mousePressEvent(e);
}

void UpdateDialog::keyPressEvent(QKeyEvent* e) {
  if (e->key() == Qt::Key_Escape) { hide(); return; }
  QDialog::keyPressEvent(e);
}
