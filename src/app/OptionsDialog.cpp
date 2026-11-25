#include "app/OptionsDialog.h"
#include "core/I18n.h"
#include "app/Zoom.h"
#include "app/Background.h"
#include "core/Settings.h"
#include "ui/Kit.h"
#include "ui/Theme.h"
#include "ui/Touch.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCoreApplication>
#include <QScrollArea>
#include <QMainWindow>
#include <QScrollBar>
#include <cmath>
#include <QPointer>
#ifndef Q_OS_IOS
#define DW_CLAUDE 1   // Options > Claude: the MCP server for Claude Code and Claude Desktop (desktop builds)
#include "ai/McpBridge.h"
#include "ai/McpServer.h"
#endif

namespace options {
namespace {
QString otr(const char* s) { return QCoreApplication::translate("options", s); }
}

// The dialog's content, painted: title row, language, themes, footer. Lays itself out for its width (narrow: a phone).
class Body : public QWidget {
public:
  explicit Body(QWidget* parent) : QWidget(parent) { setMouseTracking(true); refreshClaude(); }
  int heightFor(int w) const { return layout(w).height; }
protected:
  void paintEvent(QPaintEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override { m_dragging = false; }
  void leaveEvent(QEvent*) override { if (m_hover != None) { m_hover = None; update(); } }
private:
  enum Hit { None, Close, LangEn, LangFr, Restart, McpToggle = 40, McpJournal, McpCode, McpDesktop, DataToggle = 44, ZoomFirst = 50, ThemeFirst = 100 };
  struct Area { QRectF r; int id; };
  struct Layout {
    bool narrow = false;
    qreal pad = 26;
    QRectF close, langHint, themeHint, footer;
    qreal langLabelY = 0, themeLabelY = 0, zoomLabelY = 0, claudeLabelY = 0, dataLabelY = 0;
    QRectF zoomHint, claudeHint, claudeNote, dataHint;
    QList<Area> areas;
    int height = 0;
  };
  Layout layout(int w) const;
  int hitAt(const QPointF& p) const {
    for (const auto& a : layout(width()).areas) if (a.r.contains(p)) return a.id;
    return None;
  }
  int m_hover = None;
  QPointF m_dragFrom; bool m_dragging = false;
  bool m_inCode = false, m_inDesktop = false;   // Deepwater declared to Claude Code / Claude Desktop (read when the dialog opens)
  QString m_claudeNote;                          // the last answer: done, or what went wrong
  void refreshClaude() {
#ifdef DW_CLAUDE
    m_inCode = ai::mcpbridge::inClaudeCode();
    m_inDesktop = ai::mcpbridge::inClaudeDesktop();
#endif
  }
  static QString claudeHintText() {
    return otr("Claude Code and Claude Desktop can read what Deepwater's pages show (read only, never an order), while Deepwater is open. What Claude reads is sent to Anthropic.");
  }
  static QString dataHintText() {
    return otr("Quotes every minute, news and calendar at each source's own pace, even when their page is not shown. A little more battery and network.");
  }
  static QString zoomHintText() {
    return zoom::saved() == zoom::active()
               ? otr("Size of the whole interface. A new zoom applies when Deepwater restarts (Cmd+= / Cmd+- / Cmd+0).")
               : otr("Size of the whole interface. %1 % is chosen: it applies when Deepwater restarts.").arg(zoom::saved());
  }
};

Body::Layout Body::layout(int w) const {
  Layout L;
  L.narrow = w < 560;
  L.pad = L.narrow ? 16 : 26;
  const qreal pad = L.pad, inner = w - 2 * pad;
  L.close = L.narrow ? QRectF(w - 46, 11, 36, 36) : QRectF(w - 44, 16, 26, 26);
  L.areas << Area{L.close, Close};
  const QFontMetricsF fh(theme::sans(12));
  auto hintH = [&](const QString& s) { return std::ceil(fh.boundingRect(QRectF(0, 0, inner, 200), Qt::TextWordWrap, s).height()); };
  // language
  qreal y = 92;
  L.langLabelY = y;
  const qreal lh = std::max<qreal>(22, hintH(otr("Interface language. Numbers keep the 1,234.56 format.")));
  L.langHint = QRectF(pad, y + 6, inner, lh);
  y = L.langHint.bottom() + 6;
  const qreal half = (inner - 10) / 2.0;
  L.areas << Area{QRectF(pad, y, half, 46), LangEn} << Area{QRectF(pad + half + 10, y, half, 46), LangFr};
  y += 46 + 38;
  // zoom: five levels on one row; RESTART NOW under them while the chosen level waits for a restart
  L.zoomLabelY = y;
  const qreal zh = std::max<qreal>(22, hintH(zoomHintText()));
  L.zoomHint = QRectF(pad, y + 6, inner, zh);
  y = L.zoomHint.bottom() + 6;
  const int nz = int(zoom::levels().size());
  const qreal zw = (inner - 8 * (nz - 1)) / nz;
  for (int i = 0; i < nz; ++i) L.areas << Area{QRectF(pad + i * (zw + 8), y, zw, 40), ZoomFirst + i};
  y += 40;
#ifndef Q_OS_IOS
  if (zoom::saved() != zoom::active()) { L.areas << Area{QRectF(pad, y + 10, std::min<qreal>(inner, 240), 34), Restart}; y += 44; }
#endif
  y += 38;
  // theme
  L.themeLabelY = y;
  const qreal th = std::max<qreal>(22, hintH(otr("Colours of the whole application. Hover a theme for its description.")));
  L.themeHint = QRectF(pad, y + 6, inner, th);
  y = L.themeHint.bottom() + 6;
  const qreal ch = L.narrow ? 58 : 54;
  const auto& themes = theme::themes();
  for (int i = 0; i < themes.size(); ++i)
    L.areas << Area{QRectF(pad + (i % 2) * (half + 10), y + (i / 2) * (ch + 8), half, ch), ThemeFirst + i};
  y += ((themes.size() + 1) / 2) * (ch + 8) - 8;
  // data: one switch (keep the data fresh while its page is not shown)
  y += 38;
  L.dataLabelY = y;
  const qreal dh = std::max<qreal>(22, hintH(dataHintText()));
  L.dataHint = QRectF(pad, y + 6, inner, dh);
  y = L.dataHint.bottom() + 6;
  L.areas << Area{QRectF(pad, y, inner, 46), DataToggle};
  y += 46;
#ifdef DW_CLAUDE
  // Claude: two switches, then one button for each client
  y += 38;
  L.claudeLabelY = y;
  const qreal clh = std::max<qreal>(22, hintH(claudeHintText()));
  L.claudeHint = QRectF(pad, y + 6, inner, clh);
  y = L.claudeHint.bottom() + 6;
  L.areas << Area{QRectF(pad, y, inner, 46), McpToggle};
  y += 46 + 8;
  L.areas << Area{QRectF(pad, y, inner, 46), McpJournal};
  y += 46 + 8;
  L.areas << Area{QRectF(pad, y, half, 46), McpCode} << Area{QRectF(pad + half + 10, y, half, 46), McpDesktop};
  y += 46;
  if (!m_claudeNote.isEmpty()) {
    L.claudeNote = QRectF(pad, y + 8, inner, std::max<qreal>(22, hintH(m_claudeNote)));
    y = L.claudeNote.bottom();
  }
#endif
  // footer
  const QFontMetricsF ff(theme::mono(10, QFont::Normal, 1.0));
  const qreal fh2 = std::ceil(ff.boundingRect(QRectF(0, 0, inner, 200), Qt::TextWordWrap, otr("Changes apply immediately and are saved.  Esc closes.")).height());
  L.footer = QRectF(pad, y + 14, inner, std::max<qreal>(32, fh2));
  L.height = int(L.footer.bottom() + 14);
  return L;
}

void Body::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const auto& c = theme::pal();
  const Layout L = layout(width());
  const qreal pad = L.pad;

  // title row
  p.setPen(Qt::NoPen); p.setBrush(c.accent); p.drawEllipse(QPointF(pad + 3, 31), 3, 3);
  p.setFont(theme::mono(13, QFont::Bold, 3.0)); p.setPen(c.accentText);
  p.drawText(QPointF(pad + 14, 36), otr("OPTIONS"));
  const QRectF xr = L.close;
  p.setPen(QPen(m_hover == Close ? c.text : c.textDim, 1.6, Qt::SolidLine, Qt::RoundCap));
  p.drawLine(xr.center() + QPointF(-6, -6), xr.center() + QPointF(6, 6));
  p.drawLine(xr.center() + QPointF(6, -6), xr.center() + QPointF(-6, 6));
  p.fillRect(QRectF(pad, 58, width() - 2 * pad, 1), c.divider);

  auto section = [&](qreal y, const QString& label, const QRectF& hintRect, const QString& hint) {
    p.setFont(theme::microLabel()); p.setPen(c.textMute);
    p.drawText(QPointF(pad, y), label);
    p.setFont(theme::sans(12)); p.setPen(c.textDim);
    p.drawText(hintRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, hint);
  };

  // language
  section(L.langLabelY, otr("LANGUAGE"), L.langHint, otr("Interface language. Numbers keep the 1,234.56 format."));
  const bool fr = i18n::current() == i18n::Lang::French;
  for (const auto& a : L.areas) {
    if (a.id != LangEn && a.id != LangFr) continue;
    const bool on = (a.id == LangFr) == fr, hov = a.id == m_hover;
    p.setPen(QPen(on ? c.accent : hov ? c.borderHi : c.border, 1));
    p.setBrush(on ? c.accentDim : hov ? c.panelAlt : theme::map("#0e0a05"));
    p.drawRoundedRect(a.r.adjusted(.5, .5, -.5, -.5), 3, 3);
    const qreal in = L.narrow ? 12 : 16;
    p.setFont(theme::mono(13, QFont::Bold, L.narrow ? 1.0 : 1.5)); p.setPen(on ? c.accent : hov ? c.text : c.textDim);
    p.drawText(a.r.adjusted(in, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, a.id == LangEn ? "ENGLISH" : "FRANÇAIS");
    p.setFont(theme::mono(10, QFont::Medium, 1.5)); p.setPen(on ? theme::alpha(c.accent, 170) : c.textMute);
    p.drawText(a.r.adjusted(0, 0, -in, 0), Qt::AlignVCenter | Qt::AlignRight, a.id == LangEn ? "EN" : "FR");
  }

  // zoom
  section(L.zoomLabelY, otr("ZOOM"), L.zoomHint, zoomHintText());
  for (const auto& a : L.areas) {
    if (a.id == Restart) {
      const bool hov = a.id == m_hover;
      p.setPen(QPen(c.accent, 1)); p.setBrush(hov ? c.accent : c.accentDim);
      p.drawRoundedRect(a.r.adjusted(.5, .5, -.5, -.5), 3, 3);
      p.setFont(theme::mono(11, QFont::Bold, 1.5)); p.setPen(hov ? theme::onAccent() : c.accent);
      p.drawText(a.r, Qt::AlignCenter, otr("RESTART NOW"));
      continue;
    }
    if (a.id < ZoomFirst || a.id >= ThemeFirst) continue;
    const int lvl = zoom::levels()[a.id - ZoomFirst];
    const bool on = lvl == zoom::saved(), hov = a.id == m_hover;
    p.setPen(QPen(on ? c.accent : hov ? c.borderHi : c.border, 1));
    p.setBrush(on ? c.accentDim : hov ? c.panelAlt : theme::map("#0e0a05"));
    p.drawRoundedRect(a.r.adjusted(.5, .5, -.5, -.5), 3, 3);
    p.setFont(theme::mono(12, QFont::Bold, 1.0)); p.setPen(on ? c.accent : hov ? c.text : c.textDim);
    p.drawText(a.r, Qt::AlignCenter, QString("%1 %").arg(lvl));
  }

  // theme
  section(L.themeLabelY, otr("THEME"), L.themeHint, otr("Colours of the whole application. Hover a theme for its description."));
  const auto& th = theme::themes();
  const QString cur = theme::currentId();
  for (const auto& a : L.areas) {
    if (a.id < ThemeFirst) continue;
    const auto& info = th[a.id - ThemeFirst];
    const auto& r = info.roles;
    const bool on = info.id == cur, hov = a.id == m_hover;
    // each card is drawn in its own theme's colours: a small preview
    p.setPen(QPen(on ? c.accent : hov ? c.borderHi : c.border, on ? 2 : 1));
    p.setBrush(r.background);
    p.drawRoundedRect(a.r.adjusted(.5, .5, -.5, -.5), 3, 3);
    const QColor sw[] = {r.panel, r.accent, r.up, r.down, r.text};
    const QString name = QCoreApplication::translate("theme", info.name).toUpper();
    const QString kind = info.id == "deepwater" ? otr("DEFAULT") : info.dark ? otr("DARK") : otr("LIGHT");
    if (L.narrow) {
      // a phone: the name on its own row, the swatches and the kind under it
      p.setFont(theme::mono(12, QFont::Bold, 1.0)); p.setPen(r.accent);
      p.drawText(QRectF(a.r.left() + 10, a.r.top() + 8, a.r.width() - 34, 20), Qt::AlignLeft | Qt::AlignVCenter,
                 QFontMetricsF(p.font()).elidedText(name, Qt::ElideRight, a.r.width() - 34));
      qreal x = a.r.left() + 10;
      for (const QColor& col : sw) {
        p.setPen(QPen(r.panelLine, 1)); p.setBrush(col);
        p.drawRoundedRect(QRectF(x, a.r.top() + 34, 10, 10), 2, 2);
        x += 13;
      }
      p.setFont(theme::mono(9, QFont::Medium, 1.0)); p.setPen(r.textDim);
      p.drawText(QRectF(x + 4, a.r.top() + 30, a.r.right() - 8 - x - 4, 18), Qt::AlignRight | Qt::AlignVCenter, kind);
    } else {
      qreal x = a.r.left() + 12;
      for (const QColor& col : sw) {
        p.setPen(QPen(r.panelLine, 1)); p.setBrush(col);
        p.drawRoundedRect(QRectF(x, a.r.top() + 12, 12, 12), 2, 2);
        x += 15;
      }
      p.setFont(theme::mono(12, QFont::Bold, 1.5)); p.setPen(r.accent);
      p.drawText(QPointF(x + 8, a.r.top() + 23), name);
      p.setFont(theme::mono(9, QFont::Medium, 1.2)); p.setPen(r.textDim);
      p.drawText(QRectF(a.r.left() + 12, a.r.top() + 30, a.r.width() - 24, 18), Qt::AlignLeft | Qt::AlignVCenter, kind);
    }
    if (on) {   // check mark
      p.setPen(QPen(r.accent, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.setBrush(Qt::NoBrush);
      const QPointF o(a.r.right() - (L.narrow ? 14 : 20), L.narrow ? a.r.top() + 18 : a.r.center().y());
      p.drawPolyline(QPolygonF{o + QPointF(-6, 0), o + QPointF(-2, 4), o + QPointF(6, -5)});
    }
  }

  // a switch: ON / OFF at the right (the Data and Claude sections use it)
  auto toggle = [&](const Area& a, const QString& text, bool on) {
    const bool hov = a.id == m_hover;
    p.setPen(QPen(on ? c.accent : hov ? c.borderHi : c.border, 1));
    p.setBrush(on ? c.accentDim : hov ? c.panelAlt : theme::map("#0e0a05"));
    p.drawRoundedRect(a.r.adjusted(.5, .5, -.5, -.5), 3, 3);
    const qreal in = L.narrow ? 12 : 16;
    p.setFont(theme::mono(L.narrow ? 11 : 12, QFont::Bold, 1.0)); p.setPen(on ? c.accent : hov ? c.text : c.textDim);
    p.drawText(a.r.adjusted(in, 0, -(in + 44), 0), Qt::AlignVCenter | Qt::AlignLeft,
               QFontMetricsF(p.font()).elidedText(text, Qt::ElideRight, a.r.width() - 2 * in - 44));
    p.setFont(theme::mono(10, QFont::Medium, 1.5)); p.setPen(on ? theme::alpha(c.accent, 200) : c.textMute);
    p.drawText(a.r.adjusted(0, 0, -in, 0), Qt::AlignVCenter | Qt::AlignRight, on ? otr("ON") : otr("OFF"));
  };

  // data
  section(L.dataLabelY, otr("DATA"), L.dataHint, dataHintText());
  for (const auto& a : L.areas)
    if (a.id == DataToggle) toggle(a, otr("Keep data fresh when its page is not shown"), app::Background::enabled());

#ifdef DW_CLAUDE
  // Claude
  section(L.claudeLabelY, otr("CLAUDE"), L.claudeHint, claudeHintText());
  auto& mcp = ai::McpServer::instance();
  for (const auto& a : L.areas) {
    if (a.id == McpToggle) toggle(a, otr("Let Claude read Deepwater's data"), mcp.externalEnabled());
    else if (a.id == McpJournal) toggle(a, otr("Include my trading journal"), mcp.journalIncluded());
    else if (a.id == McpCode || a.id == McpDesktop) {
      const bool code = a.id == McpCode, added = code ? m_inCode : m_inDesktop, hov = a.id == m_hover;
      p.setPen(QPen(added ? c.accent : hov ? c.borderHi : c.border, 1));
      p.setBrush(added ? c.accentDim : hov ? c.panelAlt : theme::map("#0e0a05"));
      p.drawRoundedRect(a.r.adjusted(.5, .5, -.5, -.5), 3, 3);
      p.setFont(theme::mono(L.narrow ? 10 : 11, QFont::Bold, 1.0)); p.setPen(added ? c.accent : hov ? c.text : c.textDim);
      const QString t = code ? (added ? otr("REMOVE FROM CLAUDE CODE") : otr("ADD TO CLAUDE CODE"))
                             : (added ? otr("REMOVE FROM CLAUDE DESKTOP") : otr("ADD TO CLAUDE DESKTOP"));
      p.drawText(a.r.adjusted(8, 0, -8, 0), Qt::AlignCenter, QFontMetricsF(p.font()).elidedText(t, Qt::ElideRight, a.r.width() - 16));
    }
  }
  if (!m_claudeNote.isEmpty()) {
    p.setFont(theme::sans(12)); p.setPen(c.textDim);
    p.drawText(L.claudeNote, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, m_claudeNote);
  }
#endif

  // footer
  p.setFont(theme::mono(10, QFont::Normal, 1.0)); p.setPen(c.textFaint);
  p.drawText(L.footer, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, otr("Changes apply immediately and are saved.  Esc closes."));
}

void Body::mouseMoveEvent(QMouseEvent* e) {
  if (m_dragging) { window()->move(window()->pos() + (e->globalPosition() - m_dragFrom).toPoint()); m_dragFrom = e->globalPosition(); return; }
  const int h = hitAt(e->position());
  if (h != m_hover) {
    m_hover = h; setCursor(h == None ? Qt::ArrowCursor : Qt::PointingHandCursor); update();
    setToolTip(h >= ThemeFirst ? QCoreApplication::translate("theme", theme::themes()[h - ThemeFirst].description) : QString());
  }
}
void Body::mousePressEvent(QMouseEvent* e) {
  const int h = hitAt(e->position());
  // drag the window by its title row (not on a phone: it fills the width there)
  if (h == None && e->position().y() < 58 && !ui::touch::enabled()) { m_dragging = true; m_dragFrom = e->globalPosition(); return; }
  if (h == Close) { if (auto* d = qobject_cast<QDialog*>(window())) d->accept(); return; }
  if (h == LangEn) i18n::setLanguage(i18n::Lang::English);
  else if (h == LangFr) i18n::setLanguage(i18n::Lang::French);
  else if (h == Restart) zoom::restart();
  else if (h == DataToggle) app::Background::setEnabled(!app::Background::enabled());
#ifdef DW_CLAUDE
  else if (h == McpToggle) ai::McpServer::instance().setExternalEnabled(!ai::McpServer::instance().externalEnabled());
  else if (h == McpJournal) ai::McpServer::instance().setJournalIncluded(!ai::McpServer::instance().journalIncluded());
  else if (h == McpDesktop) {
    QString err;
    const bool want = !m_inDesktop;
    m_claudeNote = ai::mcpbridge::setInClaudeDesktop(want, &err)
                       ? (want ? otr("Added to Claude Desktop. Restart Claude Desktop, and keep Deepwater open.") : otr("Removed from Claude Desktop."))
                       : err;
    refreshClaude();
    if (auto* d = qobject_cast<OptionsDialog*>(window())) d->fit();
  }
  else if (h == McpCode) {
    const bool want = !m_inCode;
    m_claudeNote = otr("Working…");
    QPointer<Body> self(this);
    ai::mcpbridge::setInClaudeCode(want, [self, want](bool ok, QString err) {
      if (!self) return;
      self->m_claudeNote = ok ? (want ? otr("Added to Claude Code. Restart Claude Code, then check with /mcp.") : otr("Removed from Claude Code.")) : err;
      self->refreshClaude();
      if (auto* d = qobject_cast<OptionsDialog*>(self->window())) d->fit();
      self->update();
    });
    if (auto* d = qobject_cast<OptionsDialog*>(window())) d->fit();
  }
#endif
  else if (h >= ZoomFirst && h < ThemeFirst) {
    // the choice is saved here; the button under the row restarts (no question while the dialog is open)
    settings::setValue("ui.zoom", zoom::levels()[h - ZoomFirst]);
    if (auto* d = qobject_cast<OptionsDialog*>(window())) d->fit();
  }
  else if (h >= ThemeFirst) theme::setCurrent(theme::themes()[h - ThemeFirst].id);
  update();
}
}  // namespace options

OptionsDialog::OptionsDialog(QWidget* parent) : QDialog(parent) {
  setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);
  setModal(true);
  m_scroll = new QScrollArea(this);
  m_scroll->setFrameShape(QFrame::NoFrame);
  m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_scroll->setStyleSheet("QScrollArea { background: transparent; }");
  m_scroll->viewport()->setAutoFillBackground(false);
  m_body = new options::Body(m_scroll);
  m_scroll->setWidget(m_body);
  m_body->setAutoFillBackground(false);   // setWidget turns it on: the content is painted on the dialog's own fill
  fit();
  // the dialog itself follows a language change at once (a hint may wrap differently)
  connect(&i18n::Notifier::instance(), &i18n::Notifier::changed, this, [this] { fit(); m_body->update(); });
  connect(&theme::Notifier::instance(), &theme::Notifier::changed, this, [this] { update(); m_body->update(); });
}

void OptionsDialog::fit() {
  // the main window's content (its central widget: clear of a phone's status bar and home bar), in screen coordinates
  const QWidget* host = parentWidget() ? parentWidget()->window() : nullptr;
  QRect area = host ? host->geometry() : QRect(0, 0, 640, 612);
  if (auto* mw = qobject_cast<const QMainWindow*>(host); mw && mw->centralWidget())
    area = QRect(mw->centralWidget()->mapToGlobal(QPoint(0, 0)), mw->centralWidget()->size());
  const QSize avail = area.size();
  const int w = std::min(640, avail.width() - (ui::compact(avail.width()) ? 16 : 48));
  const int content = m_body->heightFor(w);
  const int h = std::min(content, avail.height() - 16);
  setFixedSize(w, h);
  m_body->setFixedSize(w - (content > h ? 6 : 0), content);   // 6: the scroll bar, when the content scrolls
  if (host) move(area.center() - QPoint(w / 2, h / 2));
}

void OptionsDialog::resizeEvent(QResizeEvent*) { m_scroll->setGeometry(rect()); }

void OptionsDialog::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const auto& c = theme::pal();
  p.setPen(QPen(c.borderHi, 1)); p.setBrush(theme::map("#0b0804"));
  p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
  ui::drawBrackets(p, QRectF(rect()).adjusted(4, 4, -4, -4), c.accent, 12);
}

void OptionsDialog::keyPressEvent(QKeyEvent* e) {
  if (e->key() == Qt::Key_Escape) { accept(); return; }
  QDialog::keyPressEvent(e);
}
