// AI › LLM › Integration: Claude Code (ai::claudecode). Left: is it installed, signed in, which model and effort it uses.
// Middle: the four AI › Agents environments, to load Claude Code into one (or eject it), and what it may do there.
// Right (2026-11-21): Deepwater in Claude — the MCP server for Claude Code and Claude Desktop started by the user
// (ai::McpServer external endpoint + ai/McpBridge.h): on/off, and adding it to each client.
// It can also be dragged onto a cube from the list of AI › Agents.
// Phones (pages/ai/PhoneScroll.h): one column (Claude Code, then the environments), the model and effort choices in
// combo boxes, 34 px buttons; the page scrolls. On iOS no program can be started: Claude Code shows "not found".
#include "pages/ai/LlmParts.h"
#include "pages/ai/PhoneScroll.h"
#include "ai/ClaudeCode.h"
#include "ai/Slots.h"
#include "ai/McpServer.h"
#include "ai/McpBridge.h"
#include "ui/Kit.h"
#include "ui/Theme.h"
#include <QCoreApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QDir>
#include <QFileInfo>
#include <QPointer>

namespace aipage {
namespace {

QPushButton* button(const QString& text, bool primary, QWidget* parent) {
  auto* b = new QPushButton(text, parent);
  const auto& c = theme::pal();
  b->setCursor(Qt::PointingHandCursor);
  b->setFont(theme::mono(10, QFont::Bold, 1.2));
  b->setStyleSheet(primary
    ? QString("QPushButton { background: %1; color: %2; border: 0; border-radius: 3px; padding: 5px 12px; }"
              "QPushButton:hover { background: %3; } QPushButton:disabled { background: %4; color: %5; }")
          .arg(c.accent.name(), theme::onAccent().name(), theme::mix(c.accent, c.text, 0.2).name(), c.panelAlt.name(), c.textMute.name())
    : QString("QPushButton { background: transparent; color: %1; border: 1px solid %2; border-radius: 3px; padding: 4px 10px; }"
              "QPushButton:hover { color: %3; border-color: %4; } QPushButton:disabled { color: %5; border-color: %2; }")
          .arg(c.textDim.name(), c.border.name(), c.text.name(), c.borderHi.name(), c.textMute.name()));
  return b;
}
QLabel* text(const QString& s, const QFont& f, const QColor& c, QWidget* p, bool wrap = false) {
  auto* l = ui::label(s, f, c, p);
  l->setWordWrap(wrap);
  return l;
}

class IntegrationPage : public QWidget {
public:
  explicit IntegrationPage(QWidget* parent) : QWidget(parent), m_scroll(this) {
    m_scroll.desktopMin = kDesktop;
    const auto& c = theme::pal();
    auto* root = new QVBoxLayout(this);
    m_root = root;
    root->setContentsMargins(16, 12, 16, 12); root->setSpacing(10);

    auto* head = new QHBoxLayout; head->setSpacing(12);
    head->addWidget(text(QCoreApplication::translate("ai", "INTEGRATION"), theme::title(), c.accentText, this));
    m_sub = text(QCoreApplication::translate("ai", "AI tools installed on this computer that a cube can hold"), theme::mono(11), c.textDim, this);
    head->addWidget(m_sub);
    head->addStretch(1);
    root->addLayout(head);
    m_head = head;

    auto* body = new QHBoxLayout; body->setSpacing(12);
    m_body = body;
    root->addLayout(body, 1);

    // ---------------------------------------------------------------- left: Claude Code itself
    auto* cc = new ui::Panel(QStringLiteral("Claude Code"), this);
    auto* grid = new QGridLayout; grid->setHorizontalSpacing(14); grid->setVerticalSpacing(8);
    auto row = [&](int r, const QString& name, QLabel** value) {
      grid->addWidget(text(name, theme::microLabel(), c.textMute, cc), r, 0, Qt::AlignLeft | Qt::AlignVCenter);
      *value = text(QString(), theme::mono(11), c.text, cc, true);
      (*value)->setTextInteractionFlags(Qt::TextSelectableByMouse);
      grid->addWidget(*value, r, 1);
    };
    row(0, QCoreApplication::translate("ai", "PROGRAM"), &m_path);
    row(1, QCoreApplication::translate("ai", "VERSION"), &m_version);
    row(2, QCoreApplication::translate("ai", "ACCOUNT"), &m_account);
    grid->setColumnStretch(1, 1);
    cc->body()->addLayout(grid);
    auto* recheckRow = new QHBoxLayout;
    m_recheck = button(QCoreApplication::translate("ai", "RECHECK"), false, cc);
    connect(m_recheck, &QPushButton::clicked, this, [] { ai::claudecode::Probe::instance().refresh(); });
    recheckRow->addWidget(m_recheck);
    recheckRow->addStretch(1);
    cc->body()->addLayout(recheckRow);
    m_install = text(QString(), theme::sans(12), c.textDim, cc, true);
    m_install->setTextFormat(Qt::RichText);
    m_install->setOpenExternalLinks(true);
    cc->body()->addWidget(m_install);

    cc->body()->addSpacing(8);
    cc->body()->addWidget(text(QCoreApplication::translate("ai", "MODEL"), theme::microLabel(), c.textMute, cc));
    m_model = new ui::PillBar({QCoreApplication::translate("ai", "DEFAULT"), "OPUS", "SONNET", "FABLE", "HAIKU"}, cc, ui::PillBar::Boxed);
    m_model->setCurrent(int(qMax<qsizetype>(0, ai::claudecode::modelAliases().indexOf(ai::claudecode::model()))));
    connect(m_model, &ui::PillBar::changed, this, [](int i) { ai::claudecode::setModel(ai::claudecode::modelAliases().value(i)); });
    cc->body()->addWidget(m_model, 0, Qt::AlignLeft);
    m_modelBox = pillCombo(m_model, {QCoreApplication::translate("ai", "DEFAULT"), "OPUS", "SONNET", "FABLE", "HAIKU"}, cc);
    cc->body()->addWidget(m_modelBox);
    cc->body()->addSpacing(4);
    cc->body()->addWidget(text(QCoreApplication::translate("ai", "EFFORT"), theme::microLabel(), c.textMute, cc));
    m_effort = new ui::PillBar({QCoreApplication::translate("ai", "DEFAULT"), QCoreApplication::translate("ai", "LOW"),
                                QCoreApplication::translate("ai", "MEDIUM"), QCoreApplication::translate("ai", "HIGH"),
                                QCoreApplication::translate("ai", "EXTRA HIGH"), QCoreApplication::translate("ai", "MAX")}, cc, ui::PillBar::Boxed);
    m_effort->setCurrent(int(qMax<qsizetype>(0, ai::claudecode::effortLevels().indexOf(ai::claudecode::effort()))));
    connect(m_effort, &ui::PillBar::changed, this, [](int i) { ai::claudecode::setEffort(ai::claudecode::effortLevels().value(i)); });
    cc->body()->addWidget(m_effort, 0, Qt::AlignLeft);
    m_effortBox = pillCombo(m_effort, {QCoreApplication::translate("ai", "DEFAULT"), QCoreApplication::translate("ai", "LOW"),
                                       QCoreApplication::translate("ai", "MEDIUM"), QCoreApplication::translate("ai", "HIGH"),
                                       QCoreApplication::translate("ai", "EXTRA HIGH"), QCoreApplication::translate("ai", "MAX")}, cc);
    cc->body()->addWidget(m_effortBox);
    cc->body()->addWidget(text(QCoreApplication::translate("ai", "DEFAULT keeps Claude Code's own choice. A change applies from the next question."),
                               theme::sans(11), c.textMute, cc, true));
    cc->body()->addStretch(1);
    body->addWidget(cc, 5);

    // ---------------------------------------------------------------- right: the cubes
    auto* cubes = new ui::Panel(QCoreApplication::translate("ai", "Load into an environment"), this);
    auto* cg = new QGridLayout; cg->setHorizontalSpacing(12); cg->setVerticalSpacing(8);
    for (int i = 0; i < ai::Slots::kCount; ++i) {
      cg->addWidget(text(QCoreApplication::translate("ai", "ENVIRONMENT %1").arg(i + 1), theme::mono(11, QFont::Bold, 1.6), c.accentText, cubes), i, 0);
      m_holds << text(QString(), theme::mono(11), c.textDim, cubes);
      cg->addWidget(m_holds.last(), i, 1);
      m_load << button(QString(), false, cubes);
      connect(m_load.last(), &QPushButton::clicked, this, [i] {
        auto& s = ai::Slots::instance();
        const auto& sl = s.slot(i);
        if (ai::claudecode::isKey(sl.repo, sl.file)) s.eject(i);
        else s.load(i, ai::claudecode::kRepo, ai::claudecode::kFile);
      });
      cg->addWidget(m_load.last(), i, 2, Qt::AlignRight);
    }
    cg->setColumnStretch(1, 1);
    cubes->body()->addLayout(cg);
    cubes->body()->addWidget(text(QCoreApplication::translate("ai", "Or drag Claude Code from the list of AI › Agents onto a cube. "
                                                                    "Its skill boxes and its chat are on the cube."),
                                  theme::sans(11), c.textMute, cubes, true));
    cubes->body()->addSpacing(10);
    cubes->body()->addWidget(text(QCoreApplication::translate("ai", "WHAT IT CAN DO IN A CUBE"), theme::microLabel(), c.textMute, cubes));
    cubes->body()->addWidget(text(QCoreApplication::translate("ai",
        "It answers in the cube's chat. Its own tools are switched off: no shell, no access to your files, no web search. "
        "It reads Deepwater's data only when a skill box of the cube is ticked, through Deepwater's tools served on this "
        "computer (127.0.0.1, a key per cube): each call is shown in the chat and written to the cube's journal."),
        theme::sans(12), c.textDim, cubes, true));
    cubes->body()->addSpacing(4);
    cubes->body()->addWidget(text(QCoreApplication::translate("ai",
        "Unlike a local model, it runs on Anthropic's servers with your own Claude sign-in: your questions and what its tools "
        "read are sent to Anthropic, and count against your Claude plan or API usage."),
        theme::sans(12), c.warn, cubes, true));
    cubes->body()->addStretch(1);
    body->addWidget(cubes, 5);

    // ---------------------------------------------------------------- right: Deepwater in Claude (MCP)
    auto* ext = new ui::Panel(QCoreApplication::translate("ai", "Deepwater in Claude"), this);
    ext->body()->addWidget(text(QCoreApplication::translate("ai",
        "Your own Claude Code or Claude Desktop can read Deepwater's data while the app is open: markets, news, calendar, "
        "economy, positioning (the \"Read Deepwater's data\" tools, never the Sources and AI pages, keys or passwords)."),
        theme::sans(12), c.textDim, ext, true));
    auto* swRow = new QHBoxLayout;
    m_extSwitch = button(QString(), true, ext);
    connect(m_extSwitch, &QPushButton::clicked, this, [] {
      auto& m = ai::McpServer::instance();
      m.setExternalEnabled(!m.externalEnabled());
    });
    swRow->addWidget(m_extSwitch);
    m_extState = text(QString(), theme::mono(11), c.textDim, ext, true);
    swRow->addWidget(m_extState, 1);
    ext->body()->addLayout(swRow);
    ext->body()->addSpacing(6);
    auto* clients = new QGridLayout; clients->setHorizontalSpacing(12); clients->setVerticalSpacing(8);
    clients->addWidget(text(QStringLiteral("CLAUDE CODE"), theme::mono(11, QFont::Bold, 1.6), c.accentText, ext), 0, 0);
    m_ccState = text(QString(), theme::mono(11), c.textDim, ext);
    clients->addWidget(m_ccState, 0, 1);
    m_ccButton = button(QString(), false, ext);
    connect(m_ccButton, &QPushButton::clicked, this, [this] {
      m_ccButton->setEnabled(false);
      m_ccError.clear();
      ai::mcpbridge::setInClaudeCode(!ai::mcpbridge::inClaudeCode(), [self = QPointer<IntegrationPage>(this)](bool ok, QString err) {
        if (!self) return;
        if (!ok) self->m_ccError = err;
        self->m_ccButton->setEnabled(true);
        self->syncExternal();
      });
    });
    clients->addWidget(m_ccButton, 0, 2, Qt::AlignRight);
    clients->addWidget(text(QStringLiteral("CLAUDE DESKTOP"), theme::mono(11, QFont::Bold, 1.6), c.accentText, ext), 1, 0);
    m_cdState = text(QString(), theme::mono(11), c.textDim, ext);
    clients->addWidget(m_cdState, 1, 1);
    m_cdButton = button(QString(), false, ext);
    connect(m_cdButton, &QPushButton::clicked, this, [this] {
      QString err;
      m_cdError.clear();
      if (!ai::mcpbridge::setInClaudeDesktop(!ai::mcpbridge::inClaudeDesktop(), &err)) m_cdError = err;
      else m_cdError = QCoreApplication::translate("ai", "Restart Claude Desktop for the change to apply.");
      syncExternal();
    });
    clients->addWidget(m_cdButton, 1, 2, Qt::AlignRight);
    clients->setColumnStretch(1, 1);
    ext->body()->addLayout(clients);
    m_extNote = text(QString(), theme::sans(11), c.warn, ext, true);
    ext->body()->addWidget(m_extNote);
    ext->body()->addSpacing(6);
    ext->body()->addWidget(text(QCoreApplication::translate("ai",
        "The clients start Deepwater's bridge (%1 --mcp-stdio), which talks to this open app on 127.0.0.1 with a key it "
        "reads from a private file: no key is written into their settings. At most %2 calls a minute; each one is noted in "
        "the journal (%3), not the data. ML and backtests stay with the cubes.")
        .arg(QFileInfo(ai::mcpbridge::program()).fileName()).arg(ai::McpServer::kExtPerMinute)
        .arg(QDir::toNativeSeparators(QFileInfo(ai::McpServer::connectionFile()).absolutePath() + "/journal.jsonl")),
        theme::sans(11), c.textMute, ext, true));
    ext->body()->addSpacing(4);
    ext->body()->addWidget(text(QCoreApplication::translate("ai",
        "What the tools read is sent to Anthropic with your questions, as with any Claude conversation."),
        theme::sans(12), c.warn, ext, true));
    ext->body()->addStretch(1);
    body->addWidget(ext, 5);
    connect(&ai::McpServer::instance(), &ai::McpServer::externalChanged, this, [this] { syncExternal(); });

    connect(&ai::claudecode::Probe::instance(), &ai::claudecode::Probe::changed, this, [this] { sync(); m_scroll.relayout(); });
    connect(&ai::Slots::instance(), &ai::Slots::changed, this, [this] { sync(); });
    sync();
    syncExternal();
    // phones and short windows: the content's own height (the wrapped texts at this width), the page scrolls
    m_scroll.height = [this](int w, int) { return layoutHeight(m_root, w); };
  }

protected:
  void paintEvent(QPaintEvent*) override { QPainter p(this); p.fillRect(rect(), theme::pal().bg); }
  // A page built hidden gets Qt's default 100 x 30 first: no phone arrangement for that (it would leave the desktop
  // layouts measured differently), the real size arrives with the show.
  void resizeEvent(QResizeEvent*) override {
    if (!isVisible()) return;
    setPhone(ui::compact(width(), kDesktop));
    m_scroll.resize();
  }
  void showEvent(QShowEvent*) override {
    resizeEvent(nullptr);
    const auto& st = ai::claudecode::Probe::instance().status();
    if (!st.checking) ai::claudecode::Probe::instance().refresh();   // re-read each visit: it may have been installed meanwhile
    syncExternal();   // added or removed from a terminal meanwhile
  }

private:
  // One column on a phone: the subtitle under the title, Claude Code above the environments, combo boxes for the
  // five / six choices (the boxed bars are wider than the screen), finger-sized buttons.
  void setPhone(bool on) {
    if (on == m_phone) return;
    m_phone = on;
    m_head->setDirection(on ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    m_sub->setWordWrap(on);
    m_body->setDirection(on ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    m_model->setVisible(!on); m_modelBox->setVisible(on);
    m_effort->setVisible(!on); m_effortBox->setVisible(on);
    QList<QPushButton*> buttons = m_load;
    buttons << m_recheck << m_extSwitch << m_ccButton << m_cdButton;
    for (auto* b : buttons) b->setMinimumHeight(on ? 34 : 0);
    for (auto* l : m_holds) l->setMinimumWidth(on ? 40 : 0);   // a long model name is cut instead of widening the row
  }
  void sync() {
    const auto& c = theme::pal();
    const auto& st = ai::claudecode::Probe::instance().status();
    const bool found = !st.path.isEmpty();
    auto set = [](QLabel* l, const QString& t, const QColor& col) {
      l->setText(t); l->setStyleSheet(QString("color:%1;background:transparent;").arg(col.name()));
    };
    if (st.checking && !st.probed) set(m_path, QCoreApplication::translate("ai", "checking…"), c.textMute);
    else set(m_path, found ? st.path : QCoreApplication::translate("ai", "not found on this computer"), found ? c.text : c.down);
    set(m_version, st.version.isEmpty() ? QStringLiteral("—") : st.version, st.version.isEmpty() ? c.textMute : c.text);
    QString acct;
    QColor ac = c.textMute;
    if (!found || !st.probed) acct = QStringLiteral("—");
    else if (st.loggedIn) {
      acct = st.authMethod == "claude.ai" ? QCoreApplication::translate("ai", "signed in with a Claude account")
           : QCoreApplication::translate("ai", "signed in (%1)").arg(st.authMethod);
      ac = c.up;
    } else { acct = QCoreApplication::translate("ai", "not signed in"); ac = c.warn; }
    set(m_account, acct, ac);
    m_recheck->setEnabled(!st.checking);
    m_recheck->setText(st.checking ? QCoreApplication::translate("ai", "CHECKING…") : QCoreApplication::translate("ai", "RECHECK"));
    if (!found && st.probed)
      m_install->setText(QCoreApplication::translate("ai", "Install Claude Code (<a href='https://claude.com/claude-code' style='color:%1;'>claude.com/claude-code</a>), "
                                                          "sign in once by typing <b>claude</b> in Terminal, then RECHECK.").arg(c.accentText.name()));
    else if (found && st.probed && !st.loggedIn)
      m_install->setText(QCoreApplication::translate("ai", "Sign in once by typing <b>claude auth login</b> in Terminal, then RECHECK."));
    else m_install->clear();
    m_install->setVisible(!m_install->text().isEmpty());

    for (int i = 0; i < ai::Slots::kCount; ++i) {
      const auto& s = ai::Slots::instance().slot(i);
      const bool mine = ai::claudecode::isKey(s.repo, s.file);
      set(m_holds[i], s.isEmpty() ? QCoreApplication::translate("ai", "empty") : s.displayName(), mine ? c.up : s.isEmpty() ? c.textMute : c.textDim);
      m_load[i]->setText(mine ? QCoreApplication::translate("ai", "EJECT")
                              : s.isEmpty() ? QCoreApplication::translate("ai", "LOAD") : QCoreApplication::translate("ai", "REPLACE"));
      m_load[i]->setToolTip(!mine && !s.isEmpty() ? QCoreApplication::translate("ai", "Takes %1 out of this cube and loads Claude Code instead.").arg(s.displayName())
                                                  : QString());
    }
  }

  // three columns need about 1100 px; narrower, one column that scrolls
  static constexpr int kDesktop = 1100;
  void syncExternal() {
    const auto& c = theme::pal();
    auto& m = ai::McpServer::instance();
    const bool on = m.externalEnabled();
    m_extSwitch->setText(on ? QCoreApplication::translate("ai", "SWITCH OFF") : QCoreApplication::translate("ai", "SWITCH ON"));
    m_extState->setText(on ? QCoreApplication::translate("ai", "served while Deepwater is open · %n call(s) since launch", nullptr, m.externalCalls())
                           : QCoreApplication::translate("ai", "off: no Claude outside Deepwater can read its data"));
    m_extState->setStyleSheet(QString("color:%1;background:transparent;").arg((on ? c.up : c.textMute).name()));
    const bool cc = ai::mcpbridge::inClaudeCode(), cdInstalled = !ai::mcpbridge::claudeDesktopConfig().isEmpty(),
               cd = ai::mcpbridge::inClaudeDesktop();
    const bool ccInstalled = !ai::claudecode::binary().isEmpty();
    m_ccState->setText(!ccInstalled ? QCoreApplication::translate("ai", "not installed") : cc ? QCoreApplication::translate("ai", "added")
                                                                                              : QCoreApplication::translate("ai", "not added"));
    m_ccState->setStyleSheet(QString("color:%1;background:transparent;").arg((cc ? c.up : c.textMute).name()));
    m_ccButton->setText(cc ? QCoreApplication::translate("ai", "REMOVE") : QCoreApplication::translate("ai", "ADD"));
    m_ccButton->setVisible(ccInstalled);
    m_cdState->setText(!cdInstalled ? QCoreApplication::translate("ai", "not installed") : cd ? QCoreApplication::translate("ai", "added")
                                                                                             : QCoreApplication::translate("ai", "not added"));
    m_cdState->setStyleSheet(QString("color:%1;background:transparent;").arg((cd ? c.up : c.textMute).name()));
    m_cdButton->setText(cd ? QCoreApplication::translate("ai", "REMOVE") : QCoreApplication::translate("ai", "ADD"));
    m_cdButton->setVisible(cdInstalled);
    QStringList notes;
    if (!m_ccError.isEmpty()) notes << "Claude Code: " + m_ccError;
    if (!m_cdError.isEmpty()) notes << "Claude Desktop: " + m_cdError;
    if (!on && (cc || cd)) notes << QCoreApplication::translate("ai", "Added but switched off: the client will say Deepwater is not reachable.");
    m_extNote->setText(notes.join('\n'));
    m_extNote->setVisible(!notes.isEmpty());
  }
  QPushButton *m_extSwitch, *m_ccButton, *m_cdButton;
  QLabel *m_extState, *m_ccState, *m_cdState, *m_extNote;
  QString m_ccError, m_cdError;
  QLabel *m_path, *m_version, *m_account, *m_install;
  QPushButton* m_recheck;
  ui::PillBar *m_model, *m_effort;
  QList<QLabel*> m_holds;
  QList<QPushButton*> m_load;
  PhoneScroll m_scroll;
  QVBoxLayout* m_root;
  QLabel* m_sub;
  QBoxLayout *m_head, *m_body;
  QComboBox *m_modelBox, *m_effortBox;
  bool m_phone = false;
};
}  // namespace

QWidget* createIntegrationPart(QWidget* parent) { return new IntegrationPage(parent); }
}  // namespace aipage
