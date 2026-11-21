// Sources › Data Sources: every outside source declared with sources::Registrar, with its live status
// (active / failing / disabled / not contacted yet), what Deepwater takes from it, the pages using it, its terms,
// a switch to turn it off, and the key or account fields of the sources that need one.
// Keys are stored as settings "keys.<name>" and never shown back; the Myfxbook password goes once to Myfxbook
// through positioning::Retail and is never stored.
// Phones (page narrower than ui::kCompactWidth): one column. The header (title, counts, search, filters, note) scrolls
// with the list, the filter pills become a grid of finger-sized cells, each source card puts its status, access and
// switch on a first line and everything else under it at full width; key and login fields stack. A short window
// (< 420 px) keeps the desktop cards but scrolls the header with the list too.
#include "app/Pages.h"
#include "net/Sources.h"
#include "core/Settings.h"
#include "positioning/Retail.h"
#include "ui/Kit.h"
#include "ui/Theme.h"
#include "ui/Touch.h"
#include <QCoreApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QPainter>
#include <QMouseEvent>
#include <QTimer>
#include <algorithm>

namespace sourcespage {
namespace {

QString statusText(sources::Status s) {
  switch (s) {
    case sources::Status::Active: return QCoreApplication::translate("sources", "ACTIVE");
    case sources::Status::Failing: return QCoreApplication::translate("sources", "FAILING");
    case sources::Status::Disabled: return QCoreApplication::translate("sources", "DISABLED");
    case sources::Status::Waiting: return QCoreApplication::translate("sources", "NOT CONTACTED");
  }
  return {};
}
QColor statusColor(sources::Status s) {
  const auto& c = theme::pal();
  switch (s) {
    case sources::Status::Active: return c.up;
    case sources::Status::Failing: return c.down;
    case sources::Status::Disabled: return c.textMute;
    case sources::Status::Waiting: return c.textDim;
  }
  return c.textDim;
}
QString accessText(sources::Access a) {
  switch (a) {
    case sources::Access::Keyless: return QCoreApplication::translate("sources", "FREE");
    case sources::Access::Key: return QCoreApplication::translate("sources", "API KEY");
    case sources::Access::Account: return QCoreApplication::translate("sources", "ACCOUNT");
    case sources::Access::Bundled: return QCoreApplication::translate("sources", "BUNDLED");
    case sources::Access::Forbidden: return QCoreApplication::translate("sources", "LINK ONLY");
  }
  return {};
}
QString ago(const QDateTime& t) {
  if (!t.isValid()) return QStringLiteral("—");
  const qint64 s = t.secsTo(QDateTime::currentDateTimeUtc());
  if (s < 60) return QCoreApplication::translate("sources", "%1 s ago").arg(qMax<qint64>(0, s));
  if (s < 3600) return QCoreApplication::translate("sources", "%1 min ago").arg(s / 60);
  if (s < 86400) return QCoreApplication::translate("sources", "%1 h ago").arg(s / 3600);
  return QCoreApplication::translate("sources", "%1 d ago").arg(s / 86400);
}
QString pageNames(const QStringList& ids) {
  QStringList out;
  for (const auto& id : ids)
    for (const auto& m : app::tabs()) for (const auto& sub : m.subs)
      if (app::pageId(m.id, sub.id) == id) out << app::label(m.label) + " › " + app::label(sub.label);
  return out.join("  ·  ");
}
// Status shown for a source: the network health, except for account sources whose state is the account's
// (Myfxbook's calendar RSS shares the host and answers even when the account is not connected).
sources::Status effectiveStatus(const sources::Source& s) {
  if (sources::isDisabled(s.id)) return sources::Status::Disabled;
  if (s.id == "myfxbook") {
    using S = positioning::Retail::State;
    switch (positioning::Retail::instance().state()) {
      case S::Connected: return sources::Status::Active;
      case S::Error: case S::Expired: return sources::Status::Failing;
      default: return sources::Status::Waiting;
    }
  }
  return sources::status(s.id);
}
QString shortError(const QString& e) { return e.size() > 140 ? e.left(137) + "…" : e; }

QString lineEditQss() {
  const auto& c = theme::pal();
  return QString("QLineEdit { background: %1; border: 1px solid %2; border-radius: 3px; padding: 5px 8px; color: %3;"
                 " font-family: 'JetBrains Mono'; font-size: 12px; } QLineEdit:focus { border-color: %4; }")
      .arg(c.panelAlt.name(), c.border.name(), c.text.name(), c.accent.name());
}
QPushButton* button(const QString& text, bool primary, QWidget* parent) {
  auto* b = new QPushButton(text, parent);
  const auto& c = theme::pal();
  b->setCursor(Qt::PointingHandCursor);
  b->setFont(theme::mono(11, QFont::Bold, 1.2));
  b->setStyleSheet(primary
    ? QString("QPushButton { background: %1; color: %2; border: 0; border-radius: 3px; padding: 6px 14px; }"
              "QPushButton:hover { background: %3; } QPushButton:disabled { background: %4; color: %5; }")
          .arg(c.accent.name(), theme::onAccent().name(), theme::mix(c.accent, c.text, 0.2).name(), c.panelAlt.name(), c.textMute.name())
    : QString("QPushButton { background: transparent; color: %1; border: 1px solid %2; border-radius: 3px; padding: 5px 12px; }"
              "QPushButton:hover { color: %3; border-color: %4; }")
          .arg(c.textDim.name(), c.border.name(), c.text.name(), c.borderHi.name()));
  return b;
}

// ---------------------------------------------------------------------------------------------- status pill
class StatusPill : public QWidget {
public:
  explicit StatusPill(QWidget* p) : QWidget(p) { setFixedSize(132, 22); }
  void set(sources::Status s) { m_s = s; update(); }
protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    const QColor col = statusColor(m_s);
    const QString t = statusText(m_s);
    const QFont f = theme::mono(10, QFont::Bold, 1.4);
    const qreal w = QFontMetricsF(f).horizontalAdvance(t) + 26;
    const QRectF r(0.5, 2.5, w, height() - 5);
    p.setPen(QPen(theme::alpha(col, 120), 1)); p.setBrush(theme::alpha(col, 34));
    p.drawRoundedRect(r, 3, 3);
    p.setPen(Qt::NoPen); p.setBrush(col); p.drawEllipse(QPointF(r.left() + 10, r.center().y()), 3.5, 3.5);
    p.setFont(f); p.setPen(col);
    p.drawText(r.adjusted(18, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, t);
  }
private:
  sources::Status m_s = sources::Status::Waiting;
};

// ---------------------------------------------------------------------------------------------- on/off switch
class Switch : public QWidget {
public:
  std::function<void(bool)> toggled;
  explicit Switch(QWidget* p) : QWidget(p) { setFixedSize(38, 20); setCursor(Qt::PointingHandCursor); }
  void setOn(bool on) { m_on = on; update(); }
protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    const auto& c = theme::pal();
    // the pill keeps its 38 x 20 look; on phones the widget is larger (finger target) and the pill sits in its middle
    const QRectF r((width() - 38) / 2.0 + 1, (height() - 20) / 2.0 + 2, 36, 16);
    p.setPen(QPen(m_on ? c.accent : c.border, 1)); p.setBrush(m_on ? theme::alpha(c.accent, 70) : c.panelAlt);
    p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
    const qreal d = r.height() - 4;
    const QPointF k(m_on ? r.right() - 2 - d / 2 : r.left() + 2 + d / 2, r.center().y());
    p.setPen(Qt::NoPen); p.setBrush(m_on ? c.accent : c.textMute); p.drawEllipse(k, d / 2, d / 2);
  }
  void mousePressEvent(QMouseEvent*) override { m_on = !m_on; update(); if (toggled) toggled(m_on); }
private:
  bool m_on = true;
};

// ---------------------------------------------------------------------------------------------- one source row
class Row : public QFrame {
public:
  sources::Source src;
  explicit Row(const sources::Source& s, QWidget* parent) : QFrame(parent), src(s) {
    const auto& c = theme::pal();
    setObjectName("row");
    auto* g = m_grid = new QGridLayout(this);
    g->setContentsMargins(14, 10, 14, 10); g->setHorizontalSpacing(16); g->setVerticalSpacing(4);
    m_pill = new StatusPill(this);
    place(m_pill, 0, 0, 1, 1, Qt::AlignTop);

    auto* name = m_name = ui::label(s.publisher, theme::condensed(16, QFont::Bold), c.text, this);
    auto* meta = ui::label(s.id + "  ·  " + (s.hosts.isEmpty() ? QString("—") : s.hosts.join(", ")) + "  ·  " +
                               QCoreApplication::translate("sources", "1 request / %1").arg(paceText(s.paceMs)),
                           theme::mono(10), c.textMute, this);
    meta->setTextInteractionFlags(Qt::TextSelectableByMouse);
    meta->setWordWrap(true);   // a source with many hosts (news-images) must not hold every card wider than the window
    auto* nameBox = new QVBoxLayout; nameBox->setSpacing(2);
    nameBox->addWidget(name); nameBox->addWidget(meta);
    place(nameBox, 0, 1, 1, 1);

    auto* access = ui::label(accessText(s.access), theme::mono(10, QFont::Bold, 1.4),
                             s.access == sources::Access::Keyless ? c.textDim : c.accentText, this);
    access->setAlignment(Qt::AlignRight | Qt::AlignTop);
    place(access, 0, 2, 1, 1, Qt::AlignTop | Qt::AlignRight);

    m_switch = new Switch(this);
    m_switch->setToolTip(QCoreApplication::translate("sources", "Turn the source off: Deepwater stops contacting it."));
    m_switch->toggled = [this](bool on) { sources::setDisabled(src.id, !on); };
    place(m_switch, 0, 3, 1, 1, Qt::AlignTop | Qt::AlignRight);

    auto* data = ui::label(s.data, theme::sans(12), c.textDim, this);
    data->setWordWrap(true);
    place(data, 1, 1, 1, 3);
    if (!s.pages.isEmpty()) {
      auto* pages = ui::label(QCoreApplication::translate("sources", "Used by: %1").arg(pageNames(s.pages)),
                              theme::sans(11), c.textMute, this);
      pages->setWordWrap(true);
      place(pages, 2, 1, 1, 3);
    }
    if (!s.terms.isEmpty()) {
      auto* terms = ui::label(QCoreApplication::translate("sources", "Terms: %1").arg(s.terms), theme::sans(11), c.textMute, this);
      terms->setWordWrap(true);
      place(terms, 3, 1, 1, 3);
    }
    m_health = ui::label(QString(), theme::mono(10), c.textDim, this);
    m_health->setWordWrap(true);
    place(m_health, 4, 1, 1, 3);

    if (!s.keyName.isEmpty()) addKeyField();   // keyed sources, and optional tokens (Hugging Face)
    if (s.id == "myfxbook") addAccountFields();
    g->setColumnStretch(1, 1);
    refresh();
  }

  // Phone card: status pill, access and switch on the first line, every other cell moved one line down at full width
  // (the desktop's second column is ~90 px wide on a phone). Fields stack and grow to finger height.
  void setPhone(bool phone) {
    if (phone == m_phone) return;
    m_phone = phone;
    for (const auto& cell : std::as_const(m_cells)) {
      if (cell.w) m_grid->removeWidget(cell.w); else m_grid->removeItem(cell.l);
    }
    for (const auto& cell : std::as_const(m_cells)) {
      int r = cell.r, c = cell.c, rs = cell.rs, cs = cell.cs;
      Qt::Alignment a = cell.a;
      if (phone) {
        if (r == 0 && c != 1) { if (a) a = (a & Qt::AlignHorizontal_Mask) | Qt::AlignVCenter; }   // pill, access, switch
        else { ++r; c = 0; cs = 4; }
      }
      if (cell.w) m_grid->addWidget(cell.w, r, c, rs, cs, a); else m_grid->addLayout(cell.l, r, c, rs, cs, a);
    }
    m_grid->setContentsMargins(phone ? 12 : 14, 10, phone ? 12 : 14, 10);
    m_grid->setHorizontalSpacing(phone ? 10 : 16);
    m_name->setWordWrap(phone);   // a long publisher name must not hold the card wider than the phone
    if (phone) m_switch->setFixedSize(48, 34); else m_switch->setFixedSize(38, 20);
    for (auto* b : {m_fieldBox, m_accountBox}) if (b) b->setDirection(phone ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    // stacked: the buttons keep their own width, at the left
    if (m_fieldBox) m_fieldBox->setAlignment(m_fieldButtons, phone ? Qt::AlignLeft : Qt::Alignment());
    if (m_accountBox) m_accountBox->setAlignment(m_accountButtons, phone ? Qt::AlignLeft : Qt::Alignment());
    for (auto* e : findChildren<QLineEdit*>()) {
      if (phone) { e->setMinimumWidth(0); e->setMinimumHeight(36); }
      else { e->setMinimumWidth(e == m_email || e == m_password ? 200 : 280); e->setMinimumHeight(0); }
    }
    for (auto* b : findChildren<QPushButton*>()) b->setMinimumHeight(phone ? 36 : 0);
    if (m_retailState) m_retailState->setWordWrap(phone);
  }

  void refresh() {
    const auto& c = theme::pal();
    const auto st = effectiveStatus(src);
    m_pill->set(st);
    m_switch->setOn(!sources::isDisabled(src.id));
    const auto h = sources::health(src.id);
    QStringList parts;
    if (h.lastOk.isValid()) parts << QCoreApplication::translate("sources", "Last answer %1").arg(ago(h.lastOk));
    if (h.lastFail.isValid())
      parts << QCoreApplication::translate("sources", "last failure %1 (%2)").arg(ago(h.lastFail), shortError(h.lastError));
    if (h.okCount + h.failCount > 0)
      parts << QCoreApplication::translate("sources", "%1 answers, %2 failures recorded").arg(h.okCount).arg(h.failCount);
    if (parts.isEmpty()) parts << QCoreApplication::translate("sources", "Not contacted yet: it is read when a page that uses it opens.");
    QString text = parts.join("  ·  ");
    const QStringList shared = sources::sharingHosts(src.id);
    if (st == sources::Status::Disabled && !shared.isEmpty())
      text += "\n" + QCoreApplication::translate("sources", "Shares its host with %1, which are stopped too while this one is off.").arg(shared.join(", "));
    if (src.access == sources::Access::Key && settings::apiKey(src.keyName).isEmpty())
      text += "\n" + QCoreApplication::translate("sources", "No key saved: the data it provides stays empty.");
    if (m_health->text() != text) m_health->setText(text);
    m_health->setToolTip(h.lastError.size() > 140 ? h.lastError : QString());
    // a style sheet re-polishes the widget and its children: set only when the colours change
    const QColor hc = st == sources::Status::Failing ? c.down : c.textDim;
    const QString healthQss = QString("color:%1;background:transparent;").arg(hc.name());
    if (healthQss != m_healthQss) { m_healthQss = healthQss; m_health->setStyleSheet(healthQss); }
    const QString rowQss = QString("QFrame#row { background: %1; border: 1px solid %2; border-radius: 3px; }")
                               .arg((st == sources::Status::Disabled ? c.bg : c.panel).name(), c.border.name());
    if (rowQss != m_rowQss) { m_rowQss = rowQss; setStyleSheet(rowQss); }
    if (m_retailState) updateAccount();
  }

private:
  // where each cell sits on a desktop card (setPhone moves them)
  struct Cell { QWidget* w = nullptr; QLayout* l = nullptr; int r = 0, c = 0, rs = 1, cs = 1; Qt::Alignment a; };
  void place(QWidget* w, int r, int c, int rs, int cs, Qt::Alignment a = {}) {
    m_cells.push_back({w, nullptr, r, c, rs, cs, a});
    m_grid->addWidget(w, r, c, rs, cs, a);
  }
  void place(QLayout* l, int r, int c, int rs, int cs) {
    m_cells.push_back({nullptr, l, r, c, rs, cs, {}});
    m_grid->addLayout(l, r, c, rs, cs);
  }

  static QString paceText(int ms) {
    if (ms <= 0) return QCoreApplication::translate("sources", "no limit");
    if (ms < 1000) return QStringLiteral("%1 ms").arg(ms);
    if (ms < 60000) return QStringLiteral("%1 s").arg(ms / 1000.0, 0, 'g', 3);
    return QStringLiteral("%1 min").arg(ms / 60000.0, 0, 'g', 3);
  }

  void addKeyField() {
    // field, buttons (in a box of their own) and state: in a row on a desktop, one under the other on a phone (the
    // three side by side are wider than a phone)
    auto* box = m_fieldBox = new QBoxLayout(QBoxLayout::LeftToRight); box->setSpacing(8);
    auto* buttons = m_fieldButtons = new QHBoxLayout; buttons->setSpacing(8);
    auto* edit = new QLineEdit(this);
    edit->setEchoMode(QLineEdit::Password);
    edit->setStyleSheet(lineEditQss());
    edit->setMinimumWidth(280);
    auto* save = button(QCoreApplication::translate("sources", "SAVE KEY"), true, this);
    auto* clear = button(QCoreApplication::translate("sources", "REMOVE"), false, this);
    auto* state = ui::label(QString(), theme::mono(10), theme::pal().textDim, this);
    auto update = [this, edit, clear, state] {
      const bool has = !settings::apiKey(src.keyName).isEmpty();
      edit->setPlaceholderText(has ? QCoreApplication::translate("sources", "Key saved (hidden) — type a new one to replace it")
                                   : QCoreApplication::translate("sources", "Paste your API key"));
      clear->setVisible(has);
      state->setText(has ? QCoreApplication::translate("sources", "● KEY SAVED") : QCoreApplication::translate("sources", "○ NO KEY"));
    };
    connect(save, &QPushButton::clicked, this, [this, edit, update] {
      const QString k = edit->text().trimmed();
      if (k.isEmpty()) return;
      settings::setValue("keys." + src.keyName, k);
      edit->clear(); update(); refresh();
    });
    connect(edit, &QLineEdit::returnPressed, save, &QPushButton::click);
    connect(clear, &QPushButton::clicked, this, [this, update] { settings::remove("keys." + src.keyName); update(); refresh(); });
    box->addWidget(edit, 1); buttons->addWidget(save); buttons->addWidget(clear); box->addLayout(buttons); box->addWidget(state);
    auto* note = ui::label(QCoreApplication::translate("sources", "Stored on this computer only (settings key keys.%1), never shown back. "
                                                       "Used from the next refresh of the pages that read it.").arg(src.keyName),
                           theme::sans(11), theme::pal().textMute, this);
    note->setWordWrap(true);
    place(box, 5, 1, 1, 3);
    place(note, 6, 1, 1, 3);
    update();
  }

  void addAccountFields() {
    auto& r = positioning::Retail::instance();
    auto* box = m_accountBox = new QBoxLayout(QBoxLayout::LeftToRight); box->setSpacing(8);
    auto* buttons = m_accountButtons = new QHBoxLayout; buttons->setSpacing(8);
    m_email = new QLineEdit(this);
    m_email->setPlaceholderText(QCoreApplication::translate("sources", "Myfxbook email"));
    m_email->setText(r.email());
    m_password = new QLineEdit(this);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(QCoreApplication::translate("sources", "Password"));
    for (auto* e : {m_email, m_password}) { e->setStyleSheet(lineEditQss()); e->setMinimumWidth(200); }
    m_connect = button(QCoreApplication::translate("sources", "CONNECT"), true, this);
    m_logout = button(QCoreApplication::translate("sources", "LOG OUT"), false, this);
    m_retailState = ui::label(QString(), theme::mono(10, QFont::Bold, 1.2), theme::pal().textDim, this);
    connect(m_connect, &QPushButton::clicked, this, [this] {
      const QString pw = m_password->text();
      if (m_email->text().trimmed().isEmpty() || pw.isEmpty()) return;
      positioning::Retail::instance().login(m_email->text().trimmed(), pw);
      m_password->clear();   // the password is sent once and forgotten
    });
    connect(m_password, &QLineEdit::returnPressed, m_connect, &QPushButton::click);
    connect(m_logout, &QPushButton::clicked, this, [] { positioning::Retail::instance().logout(); });
    connect(&r, &positioning::Retail::changed, this, [this] { refresh(); });
    box->addWidget(m_email, 1); box->addWidget(m_password, 1); buttons->addWidget(m_connect); buttons->addWidget(m_logout); box->addLayout(buttons);
    auto* note = ui::label(QCoreApplication::translate("sources", "Your password is sent once to Myfxbook (HTTPS) to open a session and is never stored; "
                                                       "only the session token is kept, until you log out or it expires."),
                           theme::sans(11), theme::pal().textMute, this);
    note->setWordWrap(true);
    place(box, 5, 1, 1, 3);
    place(m_retailState, 6, 1, 1, 3);
    place(note, 7, 1, 1, 3);
    updateAccount();
  }
  void updateAccount() {
    using S = positioning::Retail::State;
    const auto& r = positioning::Retail::instance();
    const auto& c = theme::pal();
    QString t; QColor col = c.textDim;
    switch (r.state()) {
      case S::Connected: t = QCoreApplication::translate("sources", "● CONNECTED"); col = c.up; break;
      case S::LoggingIn: t = QCoreApplication::translate("sources", "… CONNECTING"); break;
      case S::Expired: t = QCoreApplication::translate("sources", "○ SESSION EXPIRED — CONNECT AGAIN"); col = c.warn; break;
      case S::Error: t = QCoreApplication::translate("sources", "✕ %1").arg(r.message()); col = c.down; break;
      case S::NotConnected: t = QCoreApplication::translate("sources", "○ NOT CONNECTED"); break;
    }
    m_retailState->setText(t);
    m_retailState->setStyleSheet(QString("color:%1;background:transparent;").arg(col.name()));
    const bool connected = r.state() == S::Connected;
    m_connect->setVisible(!connected); m_password->setVisible(!connected); m_email->setEnabled(!connected);
    m_logout->setVisible(connected);
  }

  StatusPill* m_pill = nullptr;
  Switch* m_switch = nullptr;
  QLabel* m_health = nullptr;
  QString m_healthQss, m_rowQss;   // the style sheets set last
  QLineEdit *m_email = nullptr, *m_password = nullptr;
  QPushButton *m_connect = nullptr, *m_logout = nullptr;
  QLabel* m_retailState = nullptr;
  QGridLayout* m_grid = nullptr;
  QList<Cell> m_cells;
  QLabel* m_name = nullptr;
  QBoxLayout *m_fieldBox = nullptr, *m_accountBox = nullptr;
  QHBoxLayout *m_fieldButtons = nullptr, *m_accountButtons = nullptr;
  bool m_phone = false;
};

// ---------------------------------------------------------------------------------------------- phone filters
// The filter pills in a grid of finger-sized cells (two columns): the six pills are ~760 px wide in one line.
// Same look as ui::PillBar's Filled style; the PillBar stays the filter's state.
class FilterGrid : public QWidget {
public:
  std::function<void(int)> picked;
  explicit FilterGrid(const QStringList& items, QWidget* p) : QWidget(p), m_items(items) {
    setFixedHeight(rows() * kCell + (rows() - 1) * kGap);
    setCursor(Qt::PointingHandCursor);
  }
  void setCounts(const QList<int>& n) { m_counts = n; update(); }
  void setCurrent(int i) { m_cur = i; update(); }
protected:
  static constexpr int kCell = 36, kGap = 4;
  int rows() const { return (int(m_items.size()) + 1) / 2; }
  QRectF cell(int i) const {
    const qreal w = (width() - kGap) / 2.0;
    return QRectF((i % 2) * (w + kGap), (i / 2) * (kCell + kGap), w, kCell);
  }
  void paintEvent(QPaintEvent*) override {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    const auto& c = theme::pal();
    const QFont f = theme::mono(11, QFont::Medium, 1.0), fc = theme::mono(11);
    const QFontMetricsF fm(f);
    for (int i = 0; i < m_items.size(); ++i) {
      const QRectF r = cell(i);
      const bool on = i == m_cur;
      p.setPen(on ? Qt::NoPen : QPen(c.border, 1)); p.setBrush(on ? c.accentDim : Qt::transparent);
      p.drawRoundedRect(on ? r : r.adjusted(.5, .5, -.5, -.5), 2, 2);
      const QString t = fm.elidedText(m_items[i], Qt::ElideRight, r.width() - 46);
      const qreal base = r.center().y() + fm.ascent() / 2 - 1;
      p.setFont(f); p.setPen(on ? c.accent : c.textDim);
      p.drawText(QPointF(r.left() + 10, base), t);
      if (i < m_counts.size() && m_counts[i] >= 0) {
        p.setFont(fc); p.setPen(on ? theme::alpha(c.accent, 150) : c.textMute);
        p.drawText(QPointF(r.left() + 10 + fm.horizontalAdvance(t) + 6, base), QString::number(m_counts[i]));
      }
    }
  }
  void mouseReleaseEvent(QMouseEvent* e) override {
    for (int i = 0; i < m_items.size(); ++i)
      if (cell(i).contains(e->position())) { m_cur = i; update(); if (picked) picked(i); return; }
  }
private:
  QStringList m_items; QList<int> m_counts; int m_cur = 0;
};

// ---------------------------------------------------------------------------------------------- page
class SourcesPage : public QWidget {
public:
  explicit SourcesPage(QWidget* parent) : QWidget(parent) {
    const auto& c = theme::pal();
    setAutoFillBackground(true);
    auto* v = m_v = new QVBoxLayout(this);
    v->setContentsMargins(16, 12, 16, 12); v->setSpacing(10);
    // header block: above the list on a desktop, inside the list's scroll area on a phone or a short window
    m_head = new QWidget(this);
    auto* hv = m_headV = new QVBoxLayout(m_head);
    hv->setContentsMargins(0, 0, 0, 0); hv->setSpacing(10);

    auto* top = m_top = new QHBoxLayout; top->setSpacing(12);
    auto* title = ui::label(QCoreApplication::translate("sources", "DATA SOURCES"), theme::title(), c.accentText, m_head);
    m_summary = ui::label(QString(), theme::mono(11, QFont::Normal, 1.0), c.textDim, m_head);
    top->addWidget(title); top->addWidget(m_summary); top->addStretch(1);
    m_search = new QLineEdit(m_head);
    m_search->setPlaceholderText(QCoreApplication::translate("sources", "Search a source, host or page…"));
    m_search->setStyleSheet(lineEditQss());
    m_search->setFixedWidth(280);
    top->addWidget(m_search);
    hv->addLayout(top);

    const QStringList filters{QCoreApplication::translate("sources", "ALL"), QCoreApplication::translate("sources", "ACTIVE"),
                              QCoreApplication::translate("sources", "FAILING"), QCoreApplication::translate("sources", "DISABLED"),
                              QCoreApplication::translate("sources", "NOT CONTACTED"),
                              QCoreApplication::translate("sources", "KEY OR ACCOUNT")};
    m_filter = new ui::PillBar(filters, m_head);
    hv->addWidget(m_filter);
    m_grid = new FilterGrid(filters, m_head);
    m_grid->setVisible(false);
    m_grid->picked = [this](int i) { m_filter->setCurrent(i, true); };
    hv->addWidget(m_grid);

    auto* note = ui::label(QCoreApplication::translate("sources",
        "Status comes from Deepwater's own requests (kept across launches): ACTIVE = the source answered; FAILING = it failed "
        "twice in a row or never answered; DISABLED = switched off here, Deepwater no longer contacts it."),
        theme::sans(11), c.textMute, m_head);
    note->setWordWrap(true);
    hv->addWidget(note);
    v->addWidget(m_head);

    auto* scroll = m_scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* inner = new QWidget(scroll);
    m_list = new QVBoxLayout(inner);
    m_list->setContentsMargins(0, 0, 6, 0); m_list->setSpacing(6);
    scroll->setWidget(inner);
    v->addWidget(scroll, 1);

    QList<sources::Source> all = sources::all();
    std::sort(all.begin(), all.end(), [](const sources::Source& a, const sources::Source& b) {
      return a.publisher.compare(b.publisher, Qt::CaseInsensitive) < 0;
    });
    for (const auto& s : all) { auto* r = new Row(s, inner); m_rows << r; m_list->addWidget(r); }
    m_list->addStretch(1);

    connect(m_filter, &ui::PillBar::changed, this, [this](int i) { m_grid->setCurrent(i); applyFilter(); });
    connect(m_search, &QLineEdit::textChanged, this, [this] { applyFilter(); });
    // every answer of every source notifies: a hidden page waits for its showEvent, which refreshes it
    connect(&sources::Notifier::instance(), &sources::Notifier::changed, this, [this] { if (isVisible()) refreshAll(); });
    m_tick.setInterval(30000);   // relative times ("3 min ago")
    connect(&m_tick, &QTimer::timeout, this, [this] { refreshAll(); });
    // test hook: DW_SOURCES_FILTER=<0..5> opens the page on that filter
    if (qEnvironmentVariableIsSet("DW_SOURCES_FILTER")) {
      m_filter->setCurrent(qEnvironmentVariableIntValue("DW_SOURCES_FILTER"));
      m_grid->setCurrent(m_filter->current());
    }
    refreshAll();
  }

protected:
  void showEvent(QShowEvent*) override { refreshAll(); m_tick.start(); }
  void resizeEvent(QResizeEvent* e) override {
    QWidget::resizeEvent(e);
    setMode(ui::compact(width(), 920), ui::compact(width(), 920) || height() < 420);   // desktop from 902 px
  }
  void hideEvent(QHideEvent*) override { m_tick.stop(); }
  void paintEvent(QPaintEvent*) override { QPainter p(this); p.fillRect(rect(), theme::pal().bg); }

private:
  // phone: one-column header and cards; scroll: the header scrolls with the list (phone, or a window too short for both)
  void setMode(bool phone, bool scroll) {
    if (phone != m_phone) {
      m_phone = phone;
      m_top->removeWidget(m_summary); m_top->removeWidget(m_search);
      m_headV->removeWidget(m_summary); m_headV->removeWidget(m_search);
      if (phone) {   // title, then the counts (wrapped), then the search at full width
        m_headV->insertWidget(1, m_summary);
        m_headV->insertWidget(2, m_search);
        m_search->setMinimumWidth(0); m_search->setMaximumWidth(QWIDGETSIZE_MAX); m_search->setFixedHeight(36);
      } else {
        m_top->insertWidget(1, m_summary);
        m_top->insertWidget(3, m_search);   // after the stretch
        m_search->setFixedWidth(280); m_search->setMinimumHeight(0); m_search->setMaximumHeight(QWIDGETSIZE_MAX);
      }
      m_summary->setWordWrap(phone);
      m_filter->setVisible(!phone); m_grid->setVisible(phone);
      // no sideways scrolling on a phone: the cards fit the width (scroll bars hidden, the finger scrolls)
      m_scroll->setHorizontalScrollBarPolicy(phone ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
      m_scroll->setVerticalScrollBarPolicy(phone ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
      m_list->setContentsMargins(0, 0, phone ? 0 : 6, 0);
      for (auto* r : std::as_const(m_rows)) r->setPhone(phone);
    }
    if (scroll != m_scrolling) {
      m_scrolling = scroll;
      if (scroll) { m_v->removeWidget(m_head); m_list->insertWidget(0, m_head); }
      else { m_list->removeWidget(m_head); m_v->insertWidget(0, m_head); }
      m_head->show();   // the layouts reparent it
      m_headV->setContentsMargins(0, 0, 0, scroll ? 4 : 0);   // + the list's spacing: the desktop's 10 under the note
    }
  }

  void refreshAll() {
    int n[5] = {0, 0, 0, 0, 0};
    int keyed = 0;
    for (auto* r : std::as_const(m_rows)) {
      r->refresh();
      n[int(effectiveStatus(r->src))]++;
      if (r->src.access == sources::Access::Key || r->src.access == sources::Access::Account || !r->src.keyName.isEmpty()) ++keyed;
    }
    const int total = int(m_rows.size());
    // enum order: Waiting, Active, Failing, Disabled
    m_filter->setCounts({total, n[1], n[2], n[3], n[0], keyed});
    m_grid->setCounts({total, n[1], n[2], n[3], n[0], keyed});
    m_summary->setText(QCoreApplication::translate("sources", "%1 sources · %2 active · %3 failing · %4 disabled")
                           .arg(total).arg(n[1]).arg(n[2]).arg(n[3]));
    applyFilter();
  }
  void applyFilter() {
    const int f = m_filter->current();
    const QString q = m_search->text().trimmed();
    for (auto* r : std::as_const(m_rows)) {
      const auto st = effectiveStatus(r->src);
      bool ok = f == 0 || (f == 1 && st == sources::Status::Active) || (f == 2 && st == sources::Status::Failing) ||
                (f == 3 && st == sources::Status::Disabled) || (f == 4 && st == sources::Status::Waiting) ||
                (f == 5 && (r->src.access == sources::Access::Key || r->src.access == sources::Access::Account || !r->src.keyName.isEmpty()));
      if (ok && !q.isEmpty())
        ok = r->src.publisher.contains(q, Qt::CaseInsensitive) || r->src.id.contains(q, Qt::CaseInsensitive) ||
             r->src.hosts.join(' ').contains(q, Qt::CaseInsensitive) || r->src.data.contains(q, Qt::CaseInsensitive) ||
             pageNames(r->src.pages).contains(q, Qt::CaseInsensitive);
      r->setVisible(ok);
    }
  }

  QLabel* m_summary;
  QLineEdit* m_search;
  ui::PillBar* m_filter;
  FilterGrid* m_grid;
  QVBoxLayout *m_v, *m_headV;
  QHBoxLayout* m_top;
  QWidget* m_head;
  QScrollArea* m_scroll;
  bool m_phone = false, m_scrolling = false;
  QVBoxLayout* m_list;
  QList<Row*> m_rows;
  QTimer m_tick;
};
}  // namespace
}  // namespace sourcespage

using SourcesPageImpl = sourcespage::SourcesPage;
DW_PAGE("sources/sources", SourcesPageImpl)
