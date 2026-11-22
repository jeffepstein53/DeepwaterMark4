// AI › LLM › Local models: search the Hugging Face Hub for local models (GGUF by default), read a model's licence and
// files, download the files (resumable, checked against the Hub's SHA-256), and manage the models on disk.
// Test hooks: DW_AI_SEARCH=<query> (search at start), DW_AI_OPEN=<owner/repo> (open that model's files),
// DW_AI_DOWNLOAD=<file> (with DW_AI_OPEN: start downloading that file once its details arrive).
// Phones (pages/ai/PhoneScroll.h): one column — search and results, the model's files, then downloads and installed
// models — at fixed heights, the page scrolling; the sort choice in a combo box. Downloads work on iOS, but no model
// can run there (no child process): AI › Agents says so when a cube is started.
#include <QPointer>
#include <QHash>
#include "app/Pages.h"
#include "pages/ai/LlmParts.h"
#include "ai/ModelHub.h"
#include "pages/ai/Fit.h"
#include "pages/ai/PhoneScroll.h"
#include "ui/Kit.h"
#include "ui/Theme.h"
#include "ui/Format.h"
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
#include <QDesktopServices>
#include <QUrl>
#include <QTimer>
#include <QMessageBox>
#include <QFileInfo>
#include <QLocale>

namespace aipage {
namespace {

QString bytes(qint64 b) {
  if (b <= 0) return QStringLiteral("—");
  const double v = double(b);
  if (v >= 1e9) return QString::number(v / 1e9, 'f', 2) + " GB";
  if (v >= 1e6) return QString::number(v / 1e6, 'f', 1) + " MB";
  if (v >= 1e3) return QString::number(v / 1e3, 'f', 0) + " KB";
  return QString::number(b) + " B";
}
QString count(qint64 n) { return n >= 1000 ? fmt::compact(double(n), 1) : QString::number(n); }

QString editQss() {
  const auto& c = theme::pal();
  return QString("QLineEdit { background: %1; border: 1px solid %2; border-radius: 3px; padding: 6px 9px; color: %3;"
                 " font-family: 'JetBrains Mono'; font-size: 12px; } QLineEdit:focus { border-color: %4; }")
      .arg(c.panelAlt.name(), c.border.name(), c.text.name(), c.accent.name());
}
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
void clear(QLayout* l) {
  while (auto* it = l->takeAt(0)) {
    if (it->widget()) it->widget()->deleteLater();
    if (it->layout()) clear(it->layout());
    delete it;
  }
}
QScrollArea* scrollFor(QWidget* inner, QWidget* parent) {
  auto* s = new QScrollArea(parent);
  s->setWidgetResizable(true); s->setFrameShape(QFrame::NoFrame); s->setWidget(inner);
  return s;
}

// ---------------------------------------------------------------------------------------------- result list
class ResultList : public QWidget {
public:
  std::function<void(const ai::HubModel&)> picked;
  explicit ResultList(QWidget* p) : QWidget(p) { setMouseTracking(true); }
  void setModels(const QList<ai::HubModel>& m) { m_models = m; m_sel = -1; updateGeometry(); setMinimumHeight(int(m.size()) * kRow); update(); }
  void select(const QString& id) { for (int i = 0; i < m_models.size(); ++i) if (m_models[i].id == id) { m_sel = i; update(); } }
  QSize sizeHint() const override { return QSize(400, int(m_models.size()) * kRow); }
protected:
  static constexpr int kRow = 54;
  void paintEvent(QPaintEvent*) override {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    const auto& c = theme::pal();
    for (int i = 0; i < m_models.size(); ++i) {
      const auto& m = m_models[i];
      const QRectF r(0, i * kRow, width(), kRow - 1);
      if (i == m_sel) { p.fillRect(r, c.rowSelected); p.fillRect(QRectF(r.left(), r.top(), 2, r.height()), c.accent); }
      else if (i == m_hover) p.fillRect(r, c.rowHover);
      p.fillRect(QRectF(r.left(), r.bottom(), r.width(), 1), c.divider);
      const int slash = int(m.id.indexOf('/'));
      const QString owner = slash > 0 ? m.id.left(slash) : QString(), name = slash > 0 ? m.id.mid(slash + 1) : m.id;
      p.setFont(theme::condensed(15, QFont::Bold)); p.setPen(i == m_sel ? c.accent : c.text);
      p.drawText(QRectF(r.left() + 12, r.top() + 6, r.width() - 140, 22), Qt::AlignLeft | Qt::AlignVCenter,
                 QFontMetricsF(p.font()).elidedText(name, Qt::ElideRight, r.width() - 150));
      QStringList meta{owner};
      if (!m.license.isEmpty()) meta << m.license;
      if (!m.pipeline.isEmpty()) meta << m.pipeline;
      p.setFont(theme::mono(10)); p.setPen(c.textMute);
      p.drawText(QRectF(r.left() + 12, r.top() + 29, r.width() - 140, 18), Qt::AlignLeft | Qt::AlignVCenter,
                 QFontMetricsF(p.font()).elidedText(meta.join("  ·  "), Qt::ElideRight, r.width() - 150));
      p.setFont(theme::mono(10, QFont::Medium)); p.setPen(c.textDim);
      p.drawText(QRectF(r.right() - 130, r.top() + 6, 118, 22), Qt::AlignRight | Qt::AlignVCenter,
                 QStringLiteral("↓ %1  ♥ %2").arg(count(m.downloads), count(m.likes)));
      if (!m.gated.isEmpty()) {
        const QString g = QCoreApplication::translate("ai", "GATED");
        p.setFont(theme::mono(9, QFont::Bold, 1.2));
        const qreal w = QFontMetricsF(p.font()).horizontalAdvance(g) + 12;
        const QRectF b(r.right() - 12 - w, r.top() + 30, w, 16);
        p.setPen(QPen(c.warn, 1)); p.setBrush(theme::alpha(c.warn, 30)); p.drawRoundedRect(b, 2, 2);
        p.setPen(c.warn); p.drawText(b, Qt::AlignCenter, g);
      }
    }
  }
  void mouseMoveEvent(QMouseEvent* e) override {
    const int h = int(e->position().y()) / kRow;
    const int nh = h >= 0 && h < m_models.size() ? h : -1;
    if (nh != m_hover) { m_hover = nh; setCursor(nh >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor); update(); }
  }
  void leaveEvent(QEvent*) override { m_hover = -1; update(); }
  void mousePressEvent(QMouseEvent* e) override {
    const int i = int(e->position().y()) / kRow;
    if (i < 0 || i >= m_models.size()) return;
    m_sel = i; update();
    if (picked) picked(m_models[i]);
  }
private:
  QList<ai::HubModel> m_models;
  int m_sel = -1, m_hover = -1;
};

// ---------------------------------------------------------------------------------------------- page
class AiModelsPage : public QWidget {
public:
  explicit AiModelsPage(QWidget* parent) : QWidget(parent), m_scroll(this) {
    m_scroll.desktopMin = 920;
    const auto& c = theme::pal();
    auto& hub = ai::ModelHub::instance();
    auto* root = new QVBoxLayout(this);
    m_root = root;
    root->setContentsMargins(16, 12, 16, 12); root->setSpacing(10);

    // header: title, disk, token
    auto* head = new QHBoxLayout; head->setSpacing(12);
    head->addWidget(text(QCoreApplication::translate("ai", "LOCAL MODELS"), theme::title(), c.accentText, this));
    head->addWidget(text(QCoreApplication::translate("ai", "from the Hugging Face Hub"), theme::mono(11), c.textDim, this));
    head->addStretch(1);
    m_disk = text(QString(), theme::mono(10), c.textDim, this);
    head->addWidget(m_disk);
    auto* finder = button(QCoreApplication::translate("ai", "OPEN FOLDER"), false, this);
    connect(finder, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(ai::ModelHub::instance().modelsDir())); });
    head->addWidget(finder);
    root->addLayout(head);
    m_head = head; m_finder = finder;

    auto* tokenRow = new QHBoxLayout; tokenRow->setSpacing(8);
    m_token = new QLineEdit(this);
    m_token->setEchoMode(QLineEdit::Password);
    m_token->setStyleSheet(editQss());
    m_token->setMaximumWidth(420);
    auto* saveToken = button(QCoreApplication::translate("ai", "SAVE TOKEN"), false, this);
    m_tokenState = text(QString(), theme::mono(10), c.textMute, this);
    connect(saveToken, &QPushButton::clicked, this, [this] {
      ai::ModelHub::instance().setToken(m_token->text()); m_token->clear(); updateToken();
    });
    connect(m_token, &QLineEdit::returnPressed, saveToken, &QPushButton::click);
    tokenRow->addWidget(text(QCoreApplication::translate("ai", "TOKEN"), theme::microLabel(), c.textMute, this));
    tokenRow->addWidget(m_token, 1); tokenRow->addWidget(saveToken); tokenRow->addWidget(m_tokenState, 1);
    root->addLayout(tokenRow);
    m_tokenRow = tokenRow; m_saveToken = saveToken;

    // body: left = search + results; right = model details, downloads, installed
    auto* body = new QHBoxLayout; body->setSpacing(12);
    root->addLayout(body, 1);
    m_body = body;

    auto* left = new ui::Panel(QCoreApplication::translate("ai", "Search the Hub"), this);
    auto* sr = new QHBoxLayout; sr->setSpacing(8);
    m_search = new QLineEdit(left);
    m_search->setPlaceholderText(QCoreApplication::translate("ai", "Model name: qwen, llama, mistral, phi…"));
    m_search->setStyleSheet(editQss());
    sr->addWidget(m_search, 1);
    m_gguf = new ui::PillBar({QCoreApplication::translate("ai", "GGUF ONLY"), QCoreApplication::translate("ai", "ALL FORMATS")}, left);
    sr->addWidget(m_gguf);
    left->body()->addLayout(sr);
    m_searchRow = sr;
    m_sort = new ui::PillBar({QCoreApplication::translate("ai", "MOST DOWNLOADED"), QCoreApplication::translate("ai", "MOST LIKED"),
                              QCoreApplication::translate("ai", "RECENTLY UPDATED")}, left, ui::PillBar::Underline);
    left->body()->addWidget(m_sort);
    m_sortBox = pillCombo(m_sort, {QCoreApplication::translate("ai", "MOST DOWNLOADED"), QCoreApplication::translate("ai", "MOST LIKED"),
                                   QCoreApplication::translate("ai", "RECENTLY UPDATED")}, left);
    left->body()->addWidget(m_sortBox);
    m_status = text(QString(), theme::mono(10), c.textMute, left);
    left->body()->addWidget(m_status);
    m_results = new ResultList(left);
    left->body()->addWidget(scrollFor(m_results, left), 1);
    const QString note = QCoreApplication::translate("ai",
        "GGUF files run with llama.cpp-based apps (LM Studio, Ollama, llama.cpp). Q4_K_M is a good size / quality trade-off; "
        "a model needs about its file size in free memory to run.");
    m_note = text(note, theme::sans(11), c.textMute, left, true);
    left->body()->addWidget(m_note);
    // on a short window the note takes one line (full text in its tooltip) so the list keeps its rows
    m_noteShort = fit::label(note, theme::sans(11), c.textMute, left);
    m_noteShort->setVisible(false);
    left->body()->addWidget(m_noteShort);
    left->installEventFilter(this);
    m_left = left;
    body->addWidget(left, 5);

    auto* right = new QVBoxLayout; right->setSpacing(12);
    m_detailPanel = new ui::Panel(QCoreApplication::translate("ai", "Model"), this);
    auto* detailInner = new QWidget(m_detailPanel);
    m_detail = new QVBoxLayout(detailInner); m_detail->setContentsMargins(0, 0, 6, 0); m_detail->setSpacing(6);
    m_detailPanel->body()->addWidget(scrollFor(detailInner, m_detailPanel), 1);
    right->addWidget(m_detailPanel, 3);

    auto* local = new ui::Panel(QCoreApplication::translate("ai", "Downloads & installed models"), this);
    auto* localInner = new QWidget(local);
    m_local = new QVBoxLayout(localInner); m_local->setContentsMargins(0, 0, 6, 0); m_local->setSpacing(6);
    local->body()->addWidget(scrollFor(localInner, local), 1);
    right->addWidget(local, 2);
    body->addLayout(right, 6);
    m_localPanel = local;

    // wiring
    m_debounce.setSingleShot(true); m_debounce.setInterval(400);
    connect(&m_debounce, &QTimer::timeout, this, [this] { runSearch(); });
    connect(m_search, &QLineEdit::textChanged, this, [this] { m_debounce.start(); });
    connect(m_gguf, &ui::PillBar::changed, this, [this] { runSearch(); });
    connect(m_sort, &ui::PillBar::changed, this, [this] { runSearch(); });
    m_results->picked = [this](const ai::HubModel& m) { openModel(m.id); };
    connect(&hub, &ai::ModelHub::downloadsChanged, this, [this] { rebuildLocal(); });
    connect(&hub, &ai::ModelHub::installedChanged, this, [this] { rebuildLocal(); if (!m_openRepo.isEmpty()) openModel(m_openRepo); });

    updateToken();
    showEmptyDetail();
    rebuildLocal();
    if (qEnvironmentVariableIsSet("DW_AI_SEARCH")) m_search->setText(qEnvironmentVariable("DW_AI_SEARCH"));
    runSearch();
    if (qEnvironmentVariableIsSet("DW_AI_OPEN")) openModel(qEnvironmentVariable("DW_AI_OPEN"));
    // phones and short windows: the panels at the heights setPhone() gave them (sideways: the desktop rows at 560 px)
    m_scroll.height = [this](int w, int vh) { return m_phone ? layoutHeight(m_root, w) : std::max(vh, 560); };
  }

protected:
  void paintEvent(QPaintEvent*) override { QPainter p(this); p.fillRect(rect(), theme::pal().bg); }
  // A page built hidden gets Qt's default 100 x 30 first: no phone arrangement for that (it would leave the desktop
  // layouts measured differently), the real size arrives with the show.
  void resizeEvent(QResizeEvent*) override {
    if (!isVisible()) return;
    setPhone(ui::compact(width(), 920));   // desktop layout from 901 px (layout audit 2026-11-21)
    m_scroll.resize();
  }
  void showEvent(QShowEvent*) override {
    resizeEvent(nullptr);
    if (m_localStale) rebuildLocal();
    updateDisk(); m_search->setFocus();
  }
  bool eventFilter(QObject* o, QEvent* e) override {
    if (o == m_left && e->type() == QEvent::Resize) {   // the panel's height comes from the window, not from the note
      const bool shortPanel = m_left->height() < 560;
      if (m_noteShort->isVisibleTo(m_left) != shortPanel) { m_note->setVisible(!shortPanel); m_noteShort->setVisible(shortPanel); }
    }
    return QWidget::eventFilter(o, e);
  }

private:
  // One column on a phone. The header's disk line and OPEN FOLDER, and the token's state, go to rows of their own;
  // the GGUF choice under the search field; the sort choice in a combo box; the three panels at fixed heights (their
  // lists scroll inside, the page around them).
  void setPhone(bool on) {
    if (on == m_phone) return;
    m_phone = on;
    if (on) {
      m_head2 = new QHBoxLayout; m_head2->setSpacing(12);
      m_head->removeWidget(m_disk); m_head->removeWidget(m_finder);
      m_head2->addWidget(m_disk, 1); m_head2->addWidget(m_finder);
      m_root->insertLayout(1, m_head2);
      m_tokenRow->removeWidget(m_tokenState);
      m_root->insertWidget(3, m_tokenState);
    } else {
      m_head2->removeWidget(m_disk); m_head2->removeWidget(m_finder);
      m_root->removeItem(m_head2);
      delete m_head2; m_head2 = nullptr;
      m_head->addWidget(m_disk); m_head->addWidget(m_finder);
      m_root->removeWidget(m_tokenState);
      m_tokenRow->addWidget(m_tokenState, 1);
    }
    m_disk->setWordWrap(on);
    m_tokenState->setWordWrap(on);
    m_searchRow->setDirection(on ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    m_searchRow->setAlignment(m_gguf, on ? Qt::AlignLeft : Qt::Alignment());
    m_sort->setVisible(!on); m_sortBox->setVisible(on);
    m_body->setDirection(on ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    phoneHeight(m_left, on ? 600 : 0);
    phoneHeight(m_detailPanel, on ? 440 : 0);
    phoneHeight(m_localPanel, on ? 320 : 0);
    for (auto* b : {m_finder, m_saveToken}) b->setMinimumHeight(on ? 34 : 0);
    m_token->setMinimumHeight(on ? 34 : 0);
    m_search->setMinimumHeight(on ? 34 : 0);
  }
  // a file or model name: elided on a phone (a long name would hold its row wider than the screen)
  QLabel* nameLabel(const QString& s, QWidget* parent) {
    const auto& c = theme::pal();
    QLabel* l = m_phone ? static_cast<QLabel*>(fit::label(s, theme::mono(11, QFont::Medium), c.text, parent))
                        : text(s, theme::mono(11, QFont::Medium), c.text, parent);
    return l;
  }
  void updateDisk() {
    auto& hub = ai::ModelHub::instance();
    qint64 used = 0; for (const auto& m : hub.installed()) used += m.size;
    m_disk->setText(QCoreApplication::translate("ai", "%n model(s)", nullptr, int(hub.installed().size())) + " · " +
                    QCoreApplication::translate("ai", "%1 on disk · %2 free").arg(bytes(used), bytes(hub.freeSpace())));
    m_disk->setToolTip(hub.modelsDir());
  }
  void updateToken() {
    const bool has = !ai::ModelHub::instance().token().isEmpty();
    m_token->setPlaceholderText(has ? QCoreApplication::translate("ai", "Token saved (hidden) — type a new one, or save empty to remove")
                                    : QCoreApplication::translate("ai", "Optional: Hugging Face access token, needed only for gated models"));
    m_tokenState->setText(has ? QCoreApplication::translate("ai", "● TOKEN SAVED · sent to huggingface.co only")
                              : QCoreApplication::translate("ai", "Public models download without a token."));
  }
  void runSearch() {
    const auto sort = m_sort->current() == 1 ? ai::ModelHub::Sort::Likes
                    : m_sort->current() == 2 ? ai::ModelHub::Sort::Updated : ai::ModelHub::Sort::Downloads;
    m_status->setText(QCoreApplication::translate("ai", "Searching…"));
    const int seq = ++m_seq;
    ai::ModelHub::instance().search(m_search->text(), m_gguf->current() == 0, sort, this,
      [this, seq](const QList<ai::HubModel>& models, const QString& err) {
        if (seq != m_seq) return;
        if (!err.isEmpty()) { m_status->setText(QCoreApplication::translate("ai", "Hub unreachable: %1").arg(err)); return; }
        m_status->setText(models.isEmpty() ? QCoreApplication::translate("ai", "No model found.")
                                           : QCoreApplication::translate("ai", "%1 models (first 50)").arg(models.size()));
        m_results->setModels(models);
        if (!m_openRepo.isEmpty()) m_results->select(m_openRepo);
      });
  }
  void showEmptyDetail() {
    clear(m_detail);
    m_detail->addWidget(text(QCoreApplication::translate("ai", "Pick a model in the list to see its licence and files."),
                             theme::sans(12), theme::pal().textMute, this, true));
    m_detail->addStretch(1);
  }
  void openModel(const QString& repo) {
    m_openRepo = repo;
    m_detailPanel->setTitle(repo.section('/', 1));
    clear(m_detail);
    m_detail->addWidget(text(QCoreApplication::translate("ai", "Loading…"), theme::mono(11), theme::pal().textMute, this));
    ai::ModelHub::instance().details(repo, this, [this, repo](const ai::ModelDetails& d) {
      if (repo != m_openRepo) return;
      fillDetail(d);
    });
  }
  void fillDetail(const ai::ModelDetails& d) {
    const auto& c = theme::pal();
    auto& hub = ai::ModelHub::instance();
    clear(m_detail);
    if (!d.error.isEmpty() && d.files.isEmpty()) {
      m_detail->addWidget(text(QCoreApplication::translate("ai", "Could not read this model: %1").arg(d.error), theme::sans(12), c.down, this, true));
      m_detail->addStretch(1);
      return;
    }
    auto* idRow = new QHBoxLayout;
    idRow->addWidget(m_phone ? static_cast<QLabel*>(fit::label(d.model.id, theme::mono(11, QFont::Medium), c.textDim, this))
                             : text(d.model.id, theme::mono(11, QFont::Medium), c.textDim, this), m_phone ? 1 : 0);
    idRow->addStretch(1);
    auto* web = button(QCoreApplication::translate("ai", "OPEN ON HUGGING FACE"), false, this);
    const QString repo = d.model.id;
    connect(web, &QPushButton::clicked, this, [repo] { QDesktopServices::openUrl(QUrl("https://huggingface.co/" + repo)); });
    idRow->addWidget(web);
    m_detail->addLayout(idRow);

    QStringList facts;
    facts << QCoreApplication::translate("ai", "Licence: %1").arg(d.model.license.isEmpty() ? QCoreApplication::translate("ai", "not stated") : d.model.license);
    if (!d.model.pipeline.isEmpty()) facts << d.model.pipeline;
    facts << QCoreApplication::translate("ai", "%1 downloads").arg(count(d.model.downloads));
    if (d.model.lastModified.isValid()) facts << QCoreApplication::translate("ai", "updated %1").arg(i18nDate(d.model.lastModified));
    m_detail->addWidget(text(facts.join("  ·  "), theme::sans(12), c.text, this, true));
    if (!d.model.gated.isEmpty())
      m_detail->addWidget(text(QCoreApplication::translate("ai", "Gated model: accept its terms on huggingface.co with your account, then save your token above."),
                               theme::sans(12), c.warn, this, true));
    m_detail->addWidget(text(QCoreApplication::translate("ai", "Read the licence before use: some models forbid commercial use or require attribution."),
                             theme::sans(11), c.textMute, this, true));
#ifdef Q_OS_IOS
    // iOS does not let an app start the model runner (helpers/llm): nothing could load what would be downloaded here
    m_detail->addWidget(text(QCoreApplication::translate("ai", "Local models run on the Mac and on Windows: iOS does not let Deepwater start the program that runs them, so they are not downloaded on this device."),
                             theme::sans(12), c.warn, this, true));
#endif

    int shown = 0;
    for (const auto& f : d.files) {
      if (!f.isGguf() && m_gguf->current() == 0 && f.size < 1'000'000) continue;   // GGUF view: skip README, config…
      auto* row = new QFrame(this);
      row->setStyleSheet(QString("QFrame { background: %1; border: 1px solid %2; border-radius: 3px; }").arg(c.panel.name(), c.border.name()));
      auto* h = new QHBoxLayout(row); h->setContentsMargins(10, 6, 8, 6); h->setSpacing(10);
      auto* name = nameLabel(f.path, row);
      name->setStyleSheet(name->styleSheet() + "border:0;");
      name->setToolTip(ui::plainTip(f.sha256.isEmpty() ? f.path : f.path + "\nSHA-256 " + f.sha256));   // names from Hugging Face
      h->addWidget(name, 1);
      if (!f.quant().isEmpty()) {
        auto* q = text(f.quant(), theme::mono(10, QFont::Bold, 1.0), c.accentText, row);
        q->setStyleSheet(q->styleSheet() + "border:0;");
        h->addWidget(q);
      }
      auto* sz = text(bytes(f.size), theme::mono(11), c.textDim, row);
      sz->setStyleSheet(sz->styleSheet() + "border:0;");
      sz->setMinimumWidth(70); sz->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
      h->addWidget(sz);
      if (hub.isInstalled(d.model.id, f.path)) {
        auto* ok = text(QCoreApplication::translate("ai", "✓ INSTALLED"), theme::mono(10, QFont::Bold, 1.0), c.up, row);
        ok->setStyleSheet(ok->styleSheet() + "border:0;");
        h->addWidget(ok);
      } else {
#ifndef Q_OS_IOS
        auto* dl = button(QCoreApplication::translate("ai", "DOWNLOAD"), true, row);
        const ai::HubFile file = f; const QString lic = d.model.license;
        connect(dl, &QPushButton::clicked, this, [this, repo, file, lic] {
          auto& hub = ai::ModelHub::instance();
          if (file.size > hub.freeSpace()) {
            ui::warnPlain(this, QCoreApplication::translate("ai", "Not enough space"),
                                 QCoreApplication::translate("ai", "%1 needs %2 and only %3 is free.").arg(file.path, bytes(file.size), bytes(hub.freeSpace())));
            return;
          }
          hub.download(repo, file, lic);
        });
        h->addWidget(dl);
#endif
      }
      m_detail->addWidget(row);
      ++shown;
#ifndef Q_OS_IOS
      if (!m_hookUsed && qEnvironmentVariable("DW_AI_DOWNLOAD") == f.path && !hub.isInstalled(d.model.id, f.path)) {
        m_hookUsed = true;
        hub.download(d.model.id, f, d.model.license);
      }
#endif
    }
    if (!shown) m_detail->addWidget(text(QCoreApplication::translate("ai", "No GGUF file in this repository: switch to ALL FORMATS to see every file."),
                                         theme::sans(12), c.textMute, this, true));
    m_detail->addStretch(1);
  }
  static QString i18nDate(const QDateTime& t) { return QLocale().toString(t.toLocalTime().date(), QLocale::ShortFormat); }

  // The download's state line: progress, speed, time left.
  static QString downloadState(const ai::Download& d) {
    switch (d.state) {
      case ai::Download::State::Queued: return QCoreApplication::translate("ai", "QUEUED");
      case ai::Download::State::Running: {
        QString state = QCoreApplication::translate("ai", "%1 / %2 · %3/s").arg(bytes(d.received), bytes(d.total), bytes(qint64(d.bytesPerSec)));
        if (d.bytesPerSec > 0 && d.total > d.received)
          state += " · " + QCoreApplication::translate("ai", "%1 min left").arg(qMax<qint64>(1, qint64((d.total - d.received) / d.bytesPerSec / 60)));
        return state;
      }
      case ai::Download::State::Paused: return QCoreApplication::translate("ai", "PAUSED at %1 / %2").arg(bytes(d.received), bytes(d.total));
      case ai::Download::State::Verifying: return QCoreApplication::translate("ai", "CHECKING SHA-256…");
      case ai::Download::State::Failed: return QCoreApplication::translate("ai", "FAILED: %1").arg(d.error);
    }
    return {};
  }
  static QString barStyle(const ai::Download& d) {
    const auto& c = theme::pal();
    const double frac = d.total > 0 ? double(d.received) / double(d.total) : 0;
    return QString("background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 %1, stop:%2 %1, stop:%3 %4, stop:1 %4); border:0;")
        .arg(c.accent.name()).arg(qBound(0.0, frac, 1.0)).arg(qBound(0.0, frac + 0.0001, 1.0)).arg(c.panelAlt.name());
  }
  // What decides the rows (not the progress): a download's progress updates its row in place.
  static QString rowsKey(const QList<ai::Download>& dls, const QList<ai::LocalModel>& inst) {
    QString k;
    for (const auto& d : dls) k += d.key() + '|' + QString::number(int(d.state)) + '|' + (d.state == ai::Download::State::Failed ? d.error : QString()) + '\n';
    k += "--\n";
    for (const auto& m : inst) k += m.repo + '/' + m.file + '|' + QString::number(m.size) + '|' + m.license + '|' + QString::number(m.verified) + '|' + m.path + '\n';
    return k;
  }

  // ModelHub announces every ~800 ms during a download: hidden, the list is rebuilt when shown; shown, a progress
  // step updates the state line and the bar of its row (the rows were destroyed and rebuilt, ~6 style sheets each).
  void rebuildLocal() {
    if (!isVisible()) { m_localStale = true; return; }
    m_localStale = false;
    auto& hub = ai::ModelHub::instance();
    const auto dls0 = hub.downloads();
    const auto inst0 = hub.installed();
    const QString key = rowsKey(dls0, inst0);
    if (key == m_localKey) {
      for (const auto& d : dls0) {
        const auto it = m_progress.constFind(d.key());
        if (it == m_progress.cend()) continue;
        const QString st = downloadState(d), bs = barStyle(d);
        if (it->state && it->state->text() != st) it->state->setText(st);
        if (it->bar && it->bar->styleSheet() != bs) it->bar->setStyleSheet(bs);
      }
      updateDisk();
      return;
    }
    m_localKey = key;
    m_progress.clear();
    const auto& c = theme::pal();
    clear(m_local);
    const auto dls = dls0;
    for (const auto& d : dls) {
      auto* row = new QFrame(this);
      row->setStyleSheet(QString("QFrame { background: %1; border: 1px solid %2; border-radius: 3px; }").arg(c.panel.name(), c.border.name()));
      auto* g = new QGridLayout(row); g->setContentsMargins(10, 6, 8, 6); g->setHorizontalSpacing(10); g->setVerticalSpacing(4);
      auto* name = nameLabel(d.file, row); name->setStyleSheet(name->styleSheet() + "border:0;");
      g->addWidget(name, 0, 0);
      const QString state = downloadState(d);
      auto* st = text(state, theme::mono(10), d.state == ai::Download::State::Failed ? c.down : c.textDim, row, true);
      st->setStyleSheet(st->styleSheet() + "border:0;");
      g->addWidget(st, 1, 0);
      auto* bar = new QWidget(row);
      bar->setFixedHeight(3);
      bar->setStyleSheet(barStyle(d));
      g->addWidget(bar, 2, 0, 1, 2);
      m_progress.insert(d.key(), {st, bar});
      auto* btns = new QHBoxLayout; btns->setSpacing(6);
      const QString key = d.key();
      if (d.state == ai::Download::State::Running || d.state == ai::Download::State::Queued) {
        auto* b = button(QCoreApplication::translate("ai", "PAUSE"), false, row);
        connect(b, &QPushButton::clicked, this, [key] { ai::ModelHub::instance().pause(key); });
        btns->addWidget(b);
      } else if (d.state == ai::Download::State::Paused || d.state == ai::Download::State::Failed) {
        auto* b = button(QCoreApplication::translate("ai", "RESUME"), true, row);
        connect(b, &QPushButton::clicked, this, [key] { ai::ModelHub::instance().resume(key); });
        btns->addWidget(b);
      }
      auto* x = button(QCoreApplication::translate("ai", "CANCEL"), false, row);
      connect(x, &QPushButton::clicked, this, [key] { ai::ModelHub::instance().cancel(key); });
      btns->addWidget(x);
      g->addLayout(btns, 0, 1, 2, 1, Qt::AlignRight | Qt::AlignVCenter);
      g->setColumnStretch(0, 1);
      m_local->addWidget(row);
    }
    const auto inst = inst0;
    for (const auto& m : inst) {
      auto* row = new QFrame(this);
      row->setStyleSheet(QString("QFrame { background: %1; border: 1px solid %2; border-radius: 3px; }").arg(c.panel.name(), c.border.name()));
      auto* h = new QHBoxLayout(row); h->setContentsMargins(10, 6, 8, 6); h->setSpacing(10);
      auto* col = new QVBoxLayout; col->setSpacing(2);
      auto* name = nameLabel(m.file, row); name->setStyleSheet(name->styleSheet() + "border:0;");
      QStringList meta{m.repo, bytes(m.size)};
      if (!m.license.isEmpty()) meta << m.license;
      meta << (m.verified ? QCoreApplication::translate("ai", "SHA-256 verified") : QCoreApplication::translate("ai", "no checksum published"));
      auto* sub = text(meta.join("  ·  "), theme::mono(10), c.textMute, row, m_phone); sub->setStyleSheet(sub->styleSheet() + "border:0;");
      sub->setToolTip(m.path);
      col->addWidget(name); col->addWidget(sub);
      h->addLayout(col, 1);
      auto* show = button(QCoreApplication::translate("ai", "SHOW"), false, row);
      const QString path = m.path;
      connect(show, &QPushButton::clicked, this, [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath())); });
      auto* del = button(QCoreApplication::translate("ai", "DELETE"), false, row);
      const QString repo = m.repo, file = m.file;
      connect(del, &QPushButton::clicked, this, [this, repo, file, size = m.size] {
        if (ui::askPlain(this, QCoreApplication::translate("ai", "Delete model"),
                                  QCoreApplication::translate("ai", "Delete %1 (%2) from this computer?").arg(file, bytes(size))) != QMessageBox::Yes)
          return;
        ai::ModelHub::instance().remove(repo, file);
      });
      h->addWidget(show); h->addWidget(del);
      m_local->addWidget(row);
    }
    if (dls.isEmpty() && inst.isEmpty())
      m_local->addWidget(text(QCoreApplication::translate("ai", "No model on this computer yet. Downloads resume where they stopped, even after a restart of the download."),
                              theme::sans(12), c.textMute, this, true));
    m_local->addStretch(1);
    updateDisk();
  }

  QLabel *m_disk, *m_tokenState, *m_status, *m_note = nullptr, *m_noteShort = nullptr;
  QWidget* m_left = nullptr;
  QLineEdit *m_token, *m_search;
  ui::PillBar *m_gguf, *m_sort;
  ResultList* m_results;
  ui::Panel* m_detailPanel;
  QVBoxLayout *m_detail, *m_local;
  QString m_openRepo;
  bool m_localStale = false;
  QString m_localKey;                                            // rowsKey() of the rows shown
  struct Progress { QPointer<QLabel> state; QPointer<QWidget> bar; };
  QHash<QString, Progress> m_progress;                           // by download key
  QTimer m_debounce;
  int m_seq = 0;
  bool m_hookUsed = false;
  PhoneScroll m_scroll;
  QVBoxLayout* m_root;
  QHBoxLayout *m_head, *m_tokenRow, *m_body, *m_searchRow;
  QHBoxLayout* m_head2 = nullptr;
  QPushButton *m_finder, *m_saveToken;
  QComboBox* m_sortBox;
  QWidget* m_localPanel;
  bool m_phone = false;
};
}  // namespace
}  // namespace aipage

// shown by AI › LLM (pages/ai/LlmPage.cpp) as its "Local models" part
QWidget* aipage::createLocalModelsPart(QWidget* parent) { return new aipage::AiModelsPage(parent); }
