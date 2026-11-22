// Charts tab — side panel (Watchlist, Alerts, Calendar, News). Port of quant-terminal public/js/panels.js and
// research.js renderNews (MIT, Copyright (c) 2026 Lewis Jackson — docs/charts/NOTICE.md). See SidePanel.h.
#include "pages/charts/panels/SidePanel.h"
#include "ui/Kit.h"
#include "pages/charts/panels/RuleBuilder.h"
#include "pages/charts/panels/side/Fmt.h"
#include "pages/charts/panels/side/Widgets.h"
#include "pages/charts/ChartPane.h"
#include "pages/charts/ChartsApp.h"
#include "pages/charts/Panels.h"
#include "pages/charts/Style.h"
#include "pages/charts/Format.h"
#include "charts/core/Backtest.h"
#include "charts/core/Indicators.h"
#include "charts/data/Alerts.h"
#include "charts/data/MarketInfo.h"
#include "charts/data/Providers.h"
#include "charts/data/Store.h"
#include "net/Sources.h"
#include "core/I18n.h"

#if __has_include("pages/charts/panels/BottomPanel.h")
#include "pages/charts/panels/BottomPanel.h"
#define CHARTS_HAS_BOTTOM 1
#endif

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFile>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QShowEvent>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace charts::ui {

namespace {


using side::esc;
using side::escv;

QString mutedSpan(const QString& html) {
  return QStringLiteral("<span style=\"color:%1\">%2</span>").arg(style::v().muted.name(), html);
}
QString colorSpan(const QColor& c, const QString& html) {
  return QStringLiteral("<span style=\"color:%1\">%2</span>").arg(c.name(), html);
}

// <div class="pad row-flex">: 10px 12px padding, 8 px gaps, items centred.
QWidget* padRow(const QList<QWidget*>& items, QWidget* parent = nullptr) {
  auto* w = new QWidget(parent);
  auto* h = new QHBoxLayout(w);
  h->setContentsMargins(12, 10, 12, 10);
  h->setSpacing(8);
  for (auto* it : items) {
    if (!it) h->addStretch(1);   // <span class="spacer">
    else h->addWidget(it, 0, Qt::AlignVCenter);
  }
  if (!items.contains(nullptr)) h->addStretch(1);
  return w;
}

// <div class="pad hint-text">
QLabel* padHint(const QString& text, QWidget* parent = nullptr) {
  auto* l = style::hint(text, parent);
  l->setTextFormat(Qt::PlainText);
  l->setContentsMargins(12, 10, 12, 10);
  l->setMinimumHeight(20);
  return l;
}

// <h4 class="pad" style="margin:0;padding-bottom:0">
QLabel* padH4(const QString& text, QWidget* parent = nullptr) {
  auto* l = style::h4(text, parent);
  l->setContentsMargins(12, 10, 12, 0);
  return l;
}

QComboBox* selectIn(const QList<QPair<QString, QString>>& opts, const QString& cur, QWidget* parent = nullptr) {
  auto* c = style::select(parent);
  c->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  for (const auto& [value, label] : opts) c->addItem(label, value);
  const int i = c->findData(cur);
  c->setCurrentIndex(i < 0 ? 0 : i);
  return c;
}

// Deepwater's application style sheet gives every QLineEdit / QComboBox JetBrains Mono 12 px; the CSS inputs use the
// body font (13 px sans). A local sheet restores it for the side panel and the alert dialog.
QString inputFontSheet() {
  return QStringLiteral("QLineEdit[qtc=\"in\"], QComboBox[qtc=\"in\"] { font-family: \"%1\"; font-size: 13px; }")
      .arg(style::font(13).family());
}

double jsRound(double x) { return std::floor(x + 0.5); }

// The calendar's impact ("High", "Medium", "Low", "Holiday" from Forex Factory), shown translated; data stays English.
QString impactLabel(const QString& impact) {
  static const char* const kImpacts[][2] = {
      QT_TRANSLATE_NOOP3("charts", "High", "economic calendar impact"),
      QT_TRANSLATE_NOOP3("charts", "Medium", "economic calendar impact"),
      QT_TRANSLATE_NOOP3("charts", "Low", "economic calendar impact"),
      QT_TRANSLATE_NOOP3("charts", "Holiday", "economic calendar impact"),
  };
  for (const auto& k : kImpacts)
    if (impact == QLatin1String(k[0])) return QCoreApplication::translate("charts", k[0], k[1]);
  return impact;
}

QString invalidDate() { return QCoreApplication::translate("charts", "Invalid Date"); }

// research.js ago(t)
QString ago(double t) {
  if (!(t > 0)) return {};
  const double m = jsRound((double(QDateTime::currentMSecsSinceEpoch()) - t) / 60000);
  if (m < 60) return QCoreApplication::translate("charts", "%1m ago").arg(store::numberToString(m));
  if (m < 1440) return QCoreApplication::translate("charts", "%1h ago").arg(store::numberToString(jsRound(m / 60)));
  const QDate d = QDateTime::fromMSecsSinceEpoch(qint64(t)).toLocalTime().date();
  return i18n::locale().language() == QLocale::English ? QLocale(QLocale::English).toString(d, "M/d/yyyy")
                                                        : i18n::locale().toString(d, "dd/MM/yyyy");
}

// Watchlist key order. QJsonObject sorts its keys; the JS keeps insertion order (Object.keys). The order is read
// once from state.json's text, then kept for the session (new lists at the end).
QStringList& listOrder() {
  static QStringList order;
  static bool seeded = false;
  if (!seeded) {
    seeded = true;
    QFile f(store::dataDir() + "/state.json");
    if (f.open(QIODevice::ReadOnly)) order = data::jsonKeysInOrder(f.readAll(), {"watchlists"});
  }
  return order;
}

QStringList orderedNames(const QJsonObject& lists) {
  QStringList out;
  for (const auto& k : listOrder())
    if (lists.contains(k) && !out.contains(k)) out << k;
  for (auto it = lists.begin(); it != lists.end(); ++it)
    if (!out.contains(it.key())) {
      out << it.key();
      listOrder() << it.key();
    }
  return out;
}

}  // namespace

// ============================== Panels (side part) ==============================

Panels::Panels(ChartsApp* a) : QObject(a), app(a) {
  sidePanel = new SidePanel(a, a);
#ifdef CHARTS_HAS_BOTTOM
  bottomPanel = new BottomPanel(a, a);
#endif
}

QWidget* Panels::side() const { return sidePanel; }
void Panels::showSide(const QString& tab) { sidePanel->showSide(tab); app->showSidePanel(); }   // narrow window: the sheet opens
void Panels::openAlertDialog(const QJsonObject& preset) { sidePanel->openAlertDialog(preset); }
void Panels::onAlertFired(const QJsonObject& event) { sidePanel->onAlertFired(event); }
void Panels::onActiveChange() {
  sidePanel->onActiveChange();
#ifdef CHARTS_HAS_BOTTOM
  if (bottomPanel) bottomPanel->onActiveChange();
#endif
}

// ============================== SidePanel ==============================

SidePanel::SidePanel(ChartsApp* a, QWidget* parent) : QWidget(parent), app(a) {
  setProperty("qtc", "side-bg");
  setAttribute(Qt::WA_StyledBackground);
  setFixedWidth(300);   // .side { width: 300px; flex: none }
  setStyleSheet(inputFontSheet());
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(1, 0, 0, 0);   // border-left
  v->setSpacing(0);
  tabs_ = new style::TabBar(this);
  tabs_->addTab("watchlist", QCoreApplication::translate("charts", "Watchlist"));
  tabs_->addTab("alerts", QCoreApplication::translate("charts", "Alerts"));
  tabs_->addTab("calendar", QCoreApplication::translate("charts", "Calendar"));
  tabs_->addTab("news", QCoreApplication::translate("charts", "News"));
  tabs_->addStretch();
  v->addWidget(tabs_);
  connect(tabs_, &style::TabBar::tabClicked, this, [this](const QString& t) { showSide(t); });
  // alerts.json is shared with any other Deepwater on the same data folder (and the local API): a change seen there
  // redraws the list (the JS page reads /api/alerts each time the tab renders; here a change elsewhere shows at once).
  connect(&alerts::instance(), &alerts::Alerts::changed, this, [this] {
    if (sideTab_ == "alerts" && isVisible()) renderAlerts();
  });
  scroll_ = style::scrollArea(new QWidget, this);
  v->addWidget(scroll_, 1);

  quoteTimer_.setInterval(5000);
  connect(&quoteTimer_, &QTimer::timeout, this, &SidePanel::refreshQuotes);
  // The quotes also price the paper account (fills, positions): they follow the Charts page, not this panel, which a
  // narrow window hides (ChartsApp::resizeEvent).
  app->installEventFilter(this);
  pageShown_ = app->isVisible();
  // next turn: the bottom panel (paper orders) is made after this one; the page may be created hidden, at start, only
  // because orders wait (MainWindow::keepPaperOrdersWatched)
  QTimer::singleShot(0, this, [this] {
    syncQuoteTimer();
    if (quoteTimer_.isActive()) refreshQuotes();
  });
  // Colours inside rich text and local sheets are baked at render time: render the tab again on a theme change.
  connect(app, &ChartsApp::themeChanged, this, [this] { update(); renderSide(); });

  showSide("watchlist");
}

SidePanel::~SidePanel() = default;

void SidePanel::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.fillRect(rect(), style::v().bg);
  p.fillRect(QRect(0, 0, 1, height()), style::v().border);
}

// The JS refreshes quotes every 5 s for the whole life of the page; here while the Charts page is shown (a hidden
// Deepwater page pauses its timers). A minimised window (spontaneous events) keeps polling, as before.
bool SidePanel::eventFilter(QObject* o, QEvent* e) {
  if (o == app) {
    if (e->type() == QEvent::Show && !e->spontaneous()) {
      pageShown_ = true;
      quoteTimer_.start();   // restarts the 5 s count: refreshQuotes just below asked
      refreshQuotes();
    } else if (e->type() == QEvent::Hide && !e->spontaneous()) {
      pageShown_ = false;
      syncQuoteTimer();
    }
  }
  return QWidget::eventFilter(o, e);
}

QStringList SidePanel::pendingOrderSymbols() const {
#ifdef CHARTS_HAS_BOTTOM
  if (app->panels && app->panels->bottomPanel) return app->panels->bottomPanel->paperOrderSymbols();
#endif
  return {};
}

void SidePanel::syncQuoteTimer() {
  const bool want = pageShown_ || !pendingOrderSymbols().isEmpty();
  if (want && !quoteTimer_.isActive()) quoteTimer_.start();
  else if (!want) quoteTimer_.stop();
}

void SidePanel::showEvent(QShowEvent* e) {
  QWidget::showEvent(e);
  static bool stepsRun = false;   // test hook, see runUiStep
  if (!stepsRun && qEnvironmentVariableIsSet("DW_CHARTS_SIDE")) {
    stepsRun = true;
    QTimer::singleShot(1500, this, [this] {
      for (const QString& s : qEnvironmentVariable("DW_CHARTS_SIDE").split(';', Qt::SkipEmptyParts))
        if (!runUiStep(s.trimmed())) qWarning("side step failed: %s", qPrintable(s));
    });
  }
}

bool SidePanel::runUiStep(const QString& step) {
  const QString arg = step.section(':', 1);
  if (step.startsWith("side:")) { showSide(arg); return true; }
  if (step.startsWith("wl:")) {
    if (!watchlists().contains(arg)) return false;
    QJsonObject st = app->state;
    st["activeWatchlist"] = arg;
    app->state = st;
    showSide("watchlist");
    refreshQuotes();
    return true;
  }
  if (step.startsWith("cal:")) { calView_ = arg; showSide("calendar"); return true; }
  if (step.startsWith("calf:")) { calFilter_ = arg; showSide("calendar"); return true; }
  if (step == "alert") { openAlertDialog(); return true; }
  if (step == "alert-create") {   // alert3, then "Create alert"
    runUiStep("alert3");
    for (auto* b : app->findChildren<QPushButton*>())
      if (b->text() == QCoreApplication::translate("charts", "Create alert") && b->isVisible()) { b->click(); return true; }
    return false;
  }
  if (step == "alert3") {
    openAlertDialog();
    if (auto* rl = app->findChild<rules::RuleList*>()) {
      const double v = rl->rules().at(0).toObject().value("right").toObject().value("v").toDouble();
      rl->addRule(QJsonObject{{"left", rules::operandForKind("i:rsi")}, {"op", ">"},
                              {"right", QJsonObject{{"kind", "value"}, {"v", v}}}});
      rl->addRule(QJsonObject{{"left", QJsonObject{{"kind", "price"}, {"field", "close"}}}, {"op", ">"},
                              {"right", QJsonObject{{"kind", "value"}, {"v", v}}}});
    }
    return true;
  }
  return false;
}

QWidget* SidePanel::newBody() {
  ++renderSeq_;
  auto* w = new QWidget;
  w->setFont(style::font(13));
  if (QWidget* old = scroll_->takeWidget()) old->deleteLater();
  scroll_->setWidget(w);
  w->setAutoFillBackground(false);
  body_ = w;
  rows_ = nullptr;
  return w;
}

void SidePanel::showSide(const QString& t) {
  sideTab_ = t;
  tabs_->setCurrent(t);
  renderSide();
}

void SidePanel::renderSide() {
  if (sideTab_ == "watchlist") renderWatchlist();
  else if (sideTab_ == "alerts") renderAlerts();
  else if (sideTab_ == "calendar") renderCalendar();
  else if (sideTab_ == "news") renderNews();
}

void SidePanel::onActiveChange() {
  if (sideTab_ == "watchlist") drawRows();
  const QString sym = app->active ? app->active->cfg.value("symbol").toString() : QString();
  if (sym != lastSymbol_) {
    lastSymbol_ = sym;
    if (sideTab_ == "news") renderNews();
  }
}

void SidePanel::onAlertFired(const QJsonObject&) {
  if (sideTab_ == "alerts") renderAlerts();
}

// ======================= WATCHLIST =======================

QJsonObject SidePanel::watchlists() const { return app->state.value("watchlists").toObject(); }

QStringList SidePanel::wlSymbols() const {
  QStringList out;
  for (const auto& s : watchlists().value(app->state.value("activeWatchlist").toString()).toArray())
    out << s.toString();
  return out;
}

void SidePanel::renderWatchlist() {
  QWidget* body = newBody();
  auto* v = new QVBoxLayout(body);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(0);

  // <div class="wl-head">: select (flex 1), "+ List", "Delete"; padding 8, gap 6, border-bottom.
  auto* head = new QWidget(body);
  head->setObjectName("wlHead");
  head->setAttribute(Qt::WA_StyledBackground);
  head->setStyleSheet(QStringLiteral("#wlHead { border-bottom: 1px solid %1; }").arg(style::v().border.name()));
  auto* hl = new QHBoxLayout(head);
  hl->setContentsMargins(8, 8, 8, 9);
  hl->setSpacing(6);
  const QStringList names = orderedNames(watchlists());
  const QString activeName = app->state.value("activeWatchlist").toString();
  auto* sel = style::select(head);
  sel->addItems(names);
  sel->setCurrentIndex(std::max(0, int(names.indexOf(activeName))));
  auto* bNew = style::btn(QCoreApplication::translate("charts", "+ List"), "small", head);
  bNew->setToolTip(QCoreApplication::translate("charts", "New list"));
  bNew->setFixedHeight(24);
  auto* bDel = style::btn(QCoreApplication::translate("charts", "Delete"), "small", head);
  bDel->setToolTip(QCoreApplication::translate("charts", "Delete this list"));
  bDel->setFixedHeight(24);
  hl->addWidget(sel, 1);
  hl->addWidget(bNew, 0, Qt::AlignTop);
  hl->addWidget(bDel, 0, Qt::AlignTop);
  v->addWidget(head);

  // <div style="padding:8px"><input class="in" placeholder="Add symbol (Enter)" style="width:100%">
  auto* addBox = new QWidget(body);
  auto* al = new QHBoxLayout(addBox);
  al->setContentsMargins(8, 8, 8, 8);
  auto* add = style::input(QCoreApplication::translate("charts", "Add symbol (Enter)"), addBox);
  al->addWidget(add);
  v->addWidget(addBox);

  rows_ = new side::WatchRows(body);
  v->addWidget(rows_);
  v->addStretch(1);

  connect(sel, &QComboBox::activated, this, [this, sel](int) {
    QJsonObject st = app->state;
    st["activeWatchlist"] = sel->currentText();
    app->state = st;
    app->persist();
    // the select's own change event, after which the JS re-renders; deferred so the combo is not deleted inside
    // its own signal.
    QMetaObject::invokeMethod(this, [this] { renderWatchlist(); refreshQuotes(); }, Qt::QueuedConnection);
  });
  connect(bNew, &QPushButton::clicked, this, &SidePanel::newList);
  connect(bDel, &QPushButton::clicked, this, &SidePanel::deleteList);
  connect(add, &QLineEdit::returnPressed, this, [this, add] {
    if (add->text().trimmed().isEmpty()) return;
    const QString raw = add->text().trimmed();
    QPointer<QLineEdit> in(add);
    data::resolve(raw, this, [this, in](const data::Result<data::Symbol>& r) {
      if (!r.ok()) {
        app->toast(esc(fmt::tx(r.error->message)), "err");
        return;
      }
      QJsonObject st = app->state;
      QJsonObject lists = st.value("watchlists").toObject();
      const QString name = st.value("activeWatchlist").toString();
      QJsonArray list = lists.value(name).toArray();
      if (!list.contains(QJsonValue(r.value.id))) list.append(r.value.id);
      lists[name] = list;
      st["watchlists"] = lists;
      app->state = st;
      if (in) in->clear();
      app->persist();
      drawRows();
      refreshQuotes();
    });
  });
  connect(rows_, &side::WatchRows::picked, this, [this](const QString& s) { app->setSymbol(s); });
  connect(rows_, &side::WatchRows::removed, this, &SidePanel::removeSymbol);
  drawRows();
}

void SidePanel::drawRows() {
  if (!rows_) return;
  const QString cur = app->active ? app->active->cfg.value("symbol").toString() : QString();
  QList<side::WatchRows::Row> rows;
  for (const QString& s : wlSymbols()) {
    const Q q = quotes_.value(s);
    const double live = app->prices.contains(s) ? app->prices.value(s) : q.price;
    side::WatchRows::Row r;
    r.sym = s;
    r.ticker = side::ticker(s);
    r.exchange = side::exchange(s);
    r.price = side::fmtPrice(live);
    r.change = side::fmtPct(q.changePct);
    r.up = q.changePct >= 0;   // NaN >= 0 is false: "down" like the JS for a missing change
    r.current = s == cur;
    rows << r;
  }
  rows_->setRows(rows);
}

void SidePanel::removeSymbol(const QString& sym) {
  QJsonObject st = app->state;
  QJsonObject lists = st.value("watchlists").toObject();
  const QString name = st.value("activeWatchlist").toString();
  QJsonArray out;
  for (const auto& s : lists.value(name).toArray())
    if (s.toString() != sym) out.append(s);
  lists[name] = out;
  st["watchlists"] = lists;
  app->state = st;
  app->persist();
  drawRows();
}

void SidePanel::newList() {
  auto* dlg = new QInputDialog(window());
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->setWindowTitle(QCoreApplication::translate("charts", "Watchlist"));
  dlg->setLabelText(QCoreApplication::translate("charts", "Name for the new list:"));
  dlg->setInputMode(QInputDialog::TextInput);
  connect(dlg, &QInputDialog::textValueSelected, this, [this](const QString& n) {
    QJsonObject st = app->state;
    QJsonObject lists = st.value("watchlists").toObject();
    if (n.isEmpty() || lists.contains(n)) return;
    lists[n] = QJsonArray{};
    listOrder().removeAll(n);
    listOrder() << n;
    st["watchlists"] = lists;
    st["activeWatchlist"] = n;
    app->state = st;
    app->persist();
    renderWatchlist();
  });
  dlg->open();
}

void SidePanel::deleteList() {
  const QJsonObject lists = watchlists();
  if (lists.size() < 2) {
    app->toast(QCoreApplication::translate("charts", "Keep at least one list."));
    return;
  }
  const QString name = app->state.value("activeWatchlist").toString();
  auto* box = new QMessageBox(QMessageBox::Question, QCoreApplication::translate("charts", "Watchlist"),
                              QCoreApplication::translate("charts", "Delete the list \"%1\"?").arg(name), QMessageBox::Ok | QMessageBox::Cancel, window());
  box->setTextFormat(Qt::PlainText);   // the list's name is the user's text
  box->setAttribute(Qt::WA_DeleteOnClose);
  connect(box, &QMessageBox::finished, this, [this, name](int r) {
    if (r != QMessageBox::Ok) return;
    QJsonObject st = app->state;
    QJsonObject l = st.value("watchlists").toObject();
    l.remove(name);
    st["watchlists"] = l;
    st["activeWatchlist"] = orderedNames(l).value(0);
    app->state = st;
    app->persist();
    renderWatchlist();
  });
  box->open();
}

void SidePanel::refreshQuotes() {
  QStringList syms;
  if (pageShown_) {
    syms = wlSymbols();
    // paperSymbols(): the paper account's positions and pending orders also get prices from here.
#ifdef CHARTS_HAS_BOTTOM
    if (app->panels && app->panels->bottomPanel) syms << app->panels->bottomPanel->paperSymbols();
#endif
  } else {
    // page hidden: only what can fill (the watchlist and the positions' marks wait for the page to be shown)
    syms = pendingOrderSymbols();
    if (syms.isEmpty()) { quoteTimer_.stop(); return; }
  }
  syms.removeDuplicates();
  syms.removeAll(QString());
  if (syms.isEmpty()) return;
  if (syms.size() > 200) syms = syms.mid(0, 200);   // the server's slice(0, 200)
  data::getQuotes(syms, this, [this](const data::Result<data::Quotes>& r) {
    if (!r.ok()) return;   // offline
    for (auto it = r.value.begin(); it != r.value.end(); ++it) {
      Q q;
      if (it->error.isEmpty()) {
        q.price = it->price;
        q.changePct = it->changePct;
      }
      quotes_[it.key()] = q;
      if (it->error.isEmpty() && it->price != 0 && std::isfinite(it->price)) app->onPrice(it.key(), it->price);
    }
    if (sideTab_ == "watchlist" && pageShown_) drawRows();   // the page's Show asks again and redraws
  });
}

// ======================= ALERTS =======================

QString SidePanel::describeAlert(const QJsonObject& a) const {
  if (bt::jsTruthy(a.value("message"))) return bt::jsString(a.value("message"));
  const QJsonObject inds = a.value("indicators").toObject();
  auto name = [&](const QJsonValue& x) -> QString {
    if (x.isDouble()) return side::fmtPrice(x.toDouble());
    const QString s = bt::jsString(x);
    if (x.isString() && rules::priceFields().contains(s))
      return s == "close" ? QCoreApplication::translate("charts", "Price") : rules::fieldLabel(s);
    const QStringList parts = s.split('.');
    const QString k = parts.value(0), out = parts.size() > 1 ? parts.value(1) : QString();
    const QJsonObject si = inds.value(k).toObject();
    const ind::DefPtr def = inds.contains(k) ? ind::byId(si.value("id").toString()) : nullptr;
    if (!def) return s;
    QString nm = fmt::tx(def->name);
    static const QRegularExpression note(" \\(.*\\)$");
    nm.remove(note);
    QStringList vals;
    const QJsonObject params = si.value("params").toObject();
    // Object.values in the JS insertion order (the indicator's input order, see RuleBuilder compileRules).
    QStringList keys;
    for (const auto& in : def->inputs)
      if (params.contains(in.key)) keys << in.key;
    for (auto it = params.begin(); it != params.end(); ++it)
      if (!keys.contains(it.key())) keys << it.key();
    for (const auto& kk : keys) vals << bt::jsString(params.value(kk));
    return QStringLiteral("%1 %2%3").arg(nm, vals.join(' '),
                                         !out.isEmpty() && rules::outputsOf(*def).size() > 1 ? " " + rules::outputLabel(*def, out) : QString());
  };
  auto nm = [&](const QJsonValue& x) -> QString {
    if (x.isObject() && bt::jsTruthy(x.toObject().value("line"))) return QCoreApplication::translate("charts", "your drawn line");
    return name(x);
  };
  QJsonArray conds = a.value("conditions").toArray();
  if (conds.isEmpty()) conds = QJsonArray{QJsonObject{{"left", a.value("left")}, {"op", a.value("op")}, {"right", a.value("right")}}};
  QStringList parts;
  for (const auto& cv : conds) {
    const QJsonObject c = cv.toObject();
    const QString op = bt::jsString(c.value("op"));
    const QString label = rules::opLabel(op);
    parts << QStringLiteral("%1 %2 %3").arg(nm(c.value("left")), label, nm(c.value("right")));
  }
  return parts.join(a.value("match").toString() == "any" ? QCoreApplication::translate("charts", " or ")
                                                          : QCoreApplication::translate("charts", " and "));
}

void SidePanel::renderAlerts() {
  QWidget* body = newBody();
  auto* v = new QVBoxLayout(body);
  v->setContentsMargins(0, 0, 0, 0);
  auto* blocks = new side::Blocks(body);
  v->addWidget(blocks);

  auto* bNew = style::btn(QCoreApplication::translate("charts", "New alert"), "primary");
  // "Desktop notifications" is only shown by the JS while the browser's notification permission is not granted;
  // a desktop app needs no permission, so the button never shows (as in a browser that granted it).
  blocks->add(padRow({bNew}));
  connect(bNew, &QPushButton::clicked, this, [this] { openAlertDialog(); });

  auto& svc = alerts::instance();
  const QJsonArray list = svc.list();
  const QJsonArray log = svc.history();

  if (list.isEmpty()) {
    blocks->add(padHint(QCoreApplication::translate("charts", "No alerts yet. They run inside the app, so they keep firing with this tab closed as long "
                           "as the app is running.")));
  }
  for (const auto& av : list) {
    const QJsonObject a = av.toObject();
    const QString id = a.value("id").toString();
    auto* card = new side::Card;
    auto* left = new QWidget;
    auto* lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    lv->setSpacing(0);
    const QString title = bt::jsTruthy(a.value("watchlist"))
                              ? QCoreApplication::translate("charts", "Watchlist: %1").arg(escv(a.value("watchlist")))
                              : esc(side::ticker(bt::jsString(a.value("symbol"))));
    lv->addWidget(side::label(QStringLiteral("<b>%1</b> %2").arg(title, mutedSpan(escv(a.value("tf"))))));
    lv->addWidget(side::label(esc(describeAlert(a))));
    const QString trig = bt::jsString(a.value("trigger"));
    const QString trigLabel = trig == "once" ? QCoreApplication::translate("charts", "only once")
                              : trig == "oncePerBar" ? QCoreApplication::translate("charts", "once per bar")
                              : trig == "oncePerBarClose" ? QCoreApplication::translate("charts", "once per bar close")
                              : (a.value("trigger").isUndefined() ? QStringLiteral("undefined") : trig);
    const bool active = bt::jsTruthy(a.value("active"));
    // "&nbsp;·": Qt would break a line before the dot, the browser breaks after it.
    QString sub = QStringLiteral("%1&nbsp;· %2").arg(active ? QCoreApplication::translate("charts", "Active")
                                                            : QCoreApplication::translate("charts", "Stopped"),
                                                     esc(trigLabel));
    if (bt::jsTruthy(a.value("webhook"))) sub += QCoreApplication::translate("charts", "&nbsp;· webhook");
    if (bt::jsTruthy(a.value("fired")))
      sub += QCoreApplication::translate("charts", "&nbsp;· fired %1x").arg(escv(a.value("fired")));
    auto* subL = side::label(sub, 11, "muted");
    subL->setContentsMargins(0, 2, 0, 0);
    lv->addWidget(subL);

    auto* right = new QWidget;
    auto* rh = new QHBoxLayout(right);
    rh->setContentsMargins(0, 0, 0, 0);
    rh->setSpacing(8);
    auto* bToggle = style::btn(active ? QCoreApplication::translate("charts", "Stop", "alert: stop checking it")
                                : QCoreApplication::translate("charts", "Restart"), "small");
    bToggle->setFixedHeight(24);
    auto* bDel = style::btn(QCoreApplication::translate("charts", "Delete"), "small danger");
    bDel->setFixedHeight(24);
    rh->addWidget(bToggle);
    rh->addWidget(bDel);
    card->addTop(left, right);
    blocks->addCard(card);
    connect(bDel, &QPushButton::clicked, this, [this, id] {
      alerts::instance().remove(id);
      QMetaObject::invokeMethod(this, [this] { renderAlerts(); }, Qt::QueuedConnection);
    });
    connect(bToggle, &QPushButton::clicked, this, [this, id, active] {
      alerts::instance().update(id, QJsonObject{{"active", !active}, {"lastBar", QJsonValue::Null}, {"lastBars", QJsonObject{}}});
      QMetaObject::invokeMethod(this, [this] { renderAlerts(); }, Qt::QueuedConnection);
    });
  }

  blocks->add(padH4(QCoreApplication::translate("charts", "Recent")));
  if (log.isEmpty()) blocks->add(padHint(QCoreApplication::translate("charts", "Nothing has fired yet.")));
  for (int i = 0; i < log.size() && i < 30; ++i) {
    const QJsonObject e = log[i].toObject();
    auto* card = new side::Card;
    // the event's text and webhook result are built in English by src/charts/data/Alerts.cpp (alert-log.json, API)
    card->addLine(e.value("text").isString() ? esc(fmt::tx(e.value("text").toString())) : escv(e.value("text")));
    const QDateTime at = side::parseJsDate(e.value("at").toString());
    QString sub = esc(at.isValid() ? side::localeDateTime(at) : invalidDate());
    if (bt::jsTruthy(e.value("webhook")))
      sub += QCoreApplication::translate("charts", "&nbsp;· webhook %1")
                 .arg(e.value("webhook").isString() ? esc(fmt::tx(e.value("webhook").toString())) : escv(e.value("webhook")));
    card->addSub(sub);
    blocks->addCard(card);
  }
  blocks->finish();
}

// The local API's POST /api/webhook-test (server.js): {text, content} to the URL, {ok, status} back.
void SidePanel::testWebhook(const QString& url) {
  static const QRegularExpression ok("^https?://[^\\s]+$", QRegularExpression::CaseInsensitiveOption);
  if (!ok.match(url).hasMatch()) {
    app->toast(esc(QCoreApplication::translate("charts", "Webhook must be an http(s) URL")), "err");
    return;
  }
  const QUrl u(url);
  const QString host = u.host().toLower();
  // A host nobody declared is the user's own webhook: it joins the "webhook" source (declared by Alerts.cpp).
  if (!host.isEmpty() && !sources::forHost(host)) {
    if (auto s = sources::byId("webhook")) {
      sources::Source src = *s;
      src.hosts = {host};
      sources::declare(src);
    }
  }
  net::Request r;
  r.url = u;
  r.method = "POST";
  r.headers = {{"Content-Type", "application/json"}};
  r.body = QJsonDocument(QJsonObject{{"text", "Deepwater test alert"}, {"content", "Deepwater test alert"}})
               .toJson(QJsonDocument::Compact);
  r.ttlSec = 0;
  r.staleOnError = false;
  r.timeoutMs = 10000;
  r.cacheKey = "webhook-test:" + QUuid::createUuid().toString(QUuid::WithoutBraces);
  QPointer<SidePanel> self(this);
  data::send(r, [self](const net::Reply& rep) {
    if (!self) return;
    if (rep.status == 0) {
      self->app->toast(esc(rep.error.isEmpty() ? QCoreApplication::translate("charts", "fetch failed") : fmt::tx(rep.error)), "err");
      return;
    }
    const bool good = rep.status >= 200 && rep.status < 300;
    self->app->toast(good ? QCoreApplication::translate("charts", "Test message sent.") : QCoreApplication::translate("charts", "Webhook replied HTTP %1").arg(rep.status), good ? "" : "err");
  });
}

void SidePanel::openAlertDialog(const QJsonObject& preset) {
  ChartPane* p = app->active;
  const double price = p && !p->shown.isEmpty() ? p->shown.last().close : 0;
  const double px6 = QString::number(price, 'g', 6).toDouble();   // Number(price.toPrecision(6))
  QJsonArray rs;
  if (preset.value("rules").isArray()) rs = preset.value("rules").toArray();
  else if (preset.value("rule").isObject()) rs = QJsonArray{preset.value("rule")};
  else
    rs = QJsonArray{QJsonObject{{"left", QJsonObject{{"kind", "price"}, {"field", "close"}}},
                                {"op", "crossesAbove"},
                                {"right", QJsonObject{{"kind", "value"}, {"v", px6}}}}};

  auto* body = new side::Blocks;
  body->setFont(style::font(13));
  body->setStyleSheet(inputFontSheet());

  // form-grid: Symbol | Check / Timeframe
  auto* aSym = style::input();
  QString symV = preset.value("symbol").toString();
  if (symV.isEmpty() && p) symV = p->cfg.value("symbol").toString();
  aSym->setText(symV);
  auto* aScope = style::select();
  aScope->addItem(QCoreApplication::translate("charts", "Just this symbol"), QString());
  for (const auto& n : orderedNames(watchlists())) aScope->addItem(QCoreApplication::translate("charts", "Every symbol on %1").arg(n), n);
  auto* aTf = style::input();
  QString tfV = p ? p->cfg.value("tf").toString() : QString();
  aTf->setText(tfV.isEmpty() ? QStringLiteral("1h") : tfV);
  body->add(style::formGrid({style::field(QCoreApplication::translate("charts", "Symbol"), aSym), style::field(QCoreApplication::translate("charts", "Check", "alert: which symbols it checks"), aScope),
                             style::field(QCoreApplication::translate("charts", "Timeframe"), aTf)}));

  auto h4 = [](const QString& t) {
    auto* l = style::h4(t);
    l->setContentsMargins(0, 0, 0, 0);
    return l;
  };
  body->add(h4(QCoreApplication::translate("charts", "Conditions")), 10, 4);
  auto* rules = new rules::RuleList(rs);
  rules->setOuterMargins(false);
  body->add(rules, 4, 10);

  // row-flex: + Condition | Fire when [all/any] | Up to 5.
  auto* row1 = new QWidget;
  auto* r1 = new QHBoxLayout(row1);
  r1->setContentsMargins(0, 0, 0, 0);
  r1->setSpacing(8);
  auto* aAdd = style::btn(QCoreApplication::translate("charts", "+ Condition"), "small");
  aAdd->setFixedHeight(24);
  auto* fire = new QWidget;
  auto* fl = new QHBoxLayout(fire);
  fl->setContentsMargins(0, 0, 0, 0);
  fl->setSpacing(6);
  auto* fireL = new QLabel(QCoreApplication::translate("charts", "Fire when"));
  fireL->setProperty("qtc", "field-label");
  auto* aMatch = style::select();
  aMatch->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  aMatch->addItem(QCoreApplication::translate("charts", "all are true"), "all");
  aMatch->addItem(QCoreApplication::translate("charts", "any is true"), "any");
  fl->addWidget(fireL);
  fl->addWidget(aMatch);
  r1->addWidget(aAdd);
  r1->addWidget(fire);
  r1->addWidget(style::hint(QCoreApplication::translate("charts", "Up to 5.")));
  r1->addStretch(1);
  body->add(row1);

  // form-grid: Trigger | Message (optional)
  auto* aTrig = style::select();
  aTrig->addItem(QCoreApplication::translate("charts", "Only once"), "once");
  aTrig->addItem(QCoreApplication::translate("charts", "Once per bar"), "oncePerBar");
  aTrig->addItem(QCoreApplication::translate("charts", "Once per bar close"), "oncePerBarClose");
  auto* aMsg = style::input(QCoreApplication::translate("charts", "BTC broke out"));
  body->add(style::formGrid({style::field(QCoreApplication::translate("charts", "Trigger", "alert: when it fires"), aTrig), style::field(QCoreApplication::translate("charts", "Message (optional)"), aMsg)}));

  body->add(h4(QCoreApplication::translate("charts", "Also send to (optional)")), 10, 4);
  auto* row2 = new QWidget;
  auto* r2 = new QHBoxLayout(row2);
  r2->setContentsMargins(0, 0, 0, 0);
  r2->setSpacing(8);
  auto* aHook = style::input(QCoreApplication::translate("charts", "Discord, Slack or Telegram webhook URL"));
  auto* aTest = style::btn(QCoreApplication::translate("charts", "Test"));
  aTest->setFixedHeight(28);
  r2->addWidget(aHook, 1);
  r2->addWidget(aTest);
  body->add(row2);
  auto* note = style::hint(esc(QCoreApplication::translate("charts", "Telegram: https://api.telegram.org/bot<token>/sendMessage?chat_id=<your id>. Alerts are "
                                  "checked every 15 seconds while the app runs.")));
  note->setOpenExternalLinks(false);
  body->add(note, 12, 12);
  body->finish();

  auto* ok = style::btn(QCoreApplication::translate("charts", "Create alert"), "primary");
  QWidget* card = app->modal(QCoreApplication::translate("charts", "Create alert"), body, {ok}, true);
  Q_UNUSED(card);

  QPointer<rules::RuleList> rl(rules);
  connect(aAdd, &QPushButton::clicked, this, [this, rl, px6] {
    if (!rl) return;
    if (rl->count() >= 5) {
      app->toast(QCoreApplication::translate("charts", "An alert can have up to 5 conditions."));
      return;
    }
    rl->addRule(QJsonObject{{"left", QJsonObject{{"kind", "price"}, {"field", "close"}}},
                            {"op", ">"},
                            {"right", QJsonObject{{"kind", "value"}, {"v", px6}}}});
  });
  connect(aTest, &QPushButton::clicked, this, [this, aHook] { testWebhook(aHook->text().trimmed()); });
  connect(ok, &QPushButton::clicked, this, [=, this] {
    if (!rl) return;
    const QJsonArray uiRules = rl->rules();
    if (uiRules.isEmpty()) {
      app->toast(QCoreApplication::translate("charts", "Add a condition."), "err");
      return;
    }
    const QJsonObject c = rules::compileRules(QJsonObject{{"rules", uiRules}});
    const QJsonArray compiled = c.value("rules").toArray();
    const QJsonObject first = compiled.first().toObject();
    QJsonObject input{{"symbol", aSym->text().trimmed()},
                      {"tf", aTf->text().trimmed()},
                      {"left", first.value("left")},
                      {"op", first.value("op")},
                      {"right", first.value("right")},
                      {"conditions", compiled},
                      {"match", aMatch->currentData().toString()},
                      {"indicators", c.value("indicators")},
                      {"trigger", aTrig->currentData().toString()},
                      {"message", aMsg->text().trimmed()},
                      {"webhook", aHook->text().trimmed()}};
    const QString watchlist = aScope->currentData().toString();
    if (!watchlist.isEmpty()) input.insert("watchlist", watchlist);   // watchlist || undefined
    const auto r = alerts::instance().add(input);
    if (!r.ok()) {
      app->toast(esc(fmt::tx(r.error->message)), "err");
      return;
    }
    app->closeModal();
    app->toast(QCoreApplication::translate("charts", "Alert created."));
    showSide("alerts");
  });
}

// ======================= CALENDAR =======================

void SidePanel::renderCalendar() {
  QWidget* body = newBody();
  const int seq = renderSeq_;
  auto* v = new QVBoxLayout(body);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(0);

  auto* calV = selectIn({{"economic", QCoreApplication::translate("charts", "Economic")}, {"earnings", QCoreApplication::translate("charts", "Earnings")}, {"dividends", QCoreApplication::translate("charts", "Dividends")}},
                        calView_);
  QWidget* right = nullptr;
  QComboBox* calF = nullptr;
  QCheckBox* mineBox = nullptr;
  if (calView_ == "economic") {
    calF = selectIn({{"all", QCoreApplication::translate("charts", "All", "calendar impact filter")}, {"Medium", QCoreApplication::translate("charts", "Medium + high")}, {"High", QCoreApplication::translate("charts", "High impact")}}, calFilter_);
    right = calF;
  } else {
    mineBox = new QCheckBox(QCoreApplication::translate("charts", "My watchlists"));
    mineBox->setChecked(calMine_);
    mineBox->setFont(style::font(12));
    mineBox->setStyleSheet(QStringLiteral("QCheckBox { color: %1; font-family: \"%2\"; font-size: 12px; }")
                               .arg(style::v().muted.name(), style::font(12).family()));
    right = mineBox;
  }
  v->addWidget(padRow({calV, nullptr, right}));
  connect(calV, &QComboBox::activated, this, [this, calV] {
    calView_ = calV->currentData().toString();
    QMetaObject::invokeMethod(this, [this] { renderCalendar(); }, Qt::QueuedConnection);
  });
  if (calF)
    connect(calF, &QComboBox::activated, this, [this, calF] {
      calFilter_ = calF->currentData().toString();
      QMetaObject::invokeMethod(this, [this] { renderCalendar(); }, Qt::QueuedConnection);
    });
  if (mineBox)
    connect(mineBox, &QCheckBox::toggled, this, [this](bool on) {
      calMine_ = on;
      QMetaObject::invokeMethod(this, [this] { renderCalendar(); }, Qt::QueuedConnection);
    });

  auto* list = new side::Blocks(body);
  auto* loading = padHint(QCoreApplication::translate("charts", "Loading…"));
  list->add(loading);
  v->addWidget(list);
  v->addStretch(1);
  QPointer<side::Blocks> L(list);
  auto stale = [this, seq, L] { return !L || seq != renderSeq_; };

  QSet<QString> mine;
  for (const auto& l : watchlists())
    for (const auto& s : l.toArray()) mine.insert(s.toString());

  if (calView_ == "economic") {
    data::economicCalendar(this, [this, stale, L](const data::Result<QList<data::CalendarEvent>>& r) {
      if (stale()) return;
      L->clear();
      if (!r.ok()) {
        L->add(padHint(QCoreApplication::translate("charts", "Calendar unavailable: %1").arg(fmt::tx(r.error->message))));
        L->finish();
        return;
      }
      QString day;
      int n = 0;
      for (const auto& e : r.value) {
        const QString impact = bt::jsString(e.impact);
        const bool keep = calFilter_ == "all" || impact == "High" || (calFilter_ == "Medium" && impact == "Medium");
        if (!keep) continue;
        ++n;
        const QDateTime d = side::parseJsDate(e.date.toString());
        const QString label = d.isValid() ? side::localeDayLabel(d.date()) : invalidDate();
        if (label != day) {
          day = label;
          L->add(padH4(label));
        }
        auto* card = new side::Card;
        const QColor ic = impact == "High" ? style::v().down : impact == "Medium" ? style::v().warn : style::v().muted;
        auto* imp = side::label(colorSpan(ic, e.impact.isString() ? esc(impactLabel(impact)) : escv(e.impact)));
        imp->setWordWrap(false);
        card->addTop(side::label(QStringLiteral("<b>%1</b> %2").arg(escv(e.country), escv(e.title))), imp);
        QString sub = d.isValid() ? side::localeTime(d) : invalidDate();
        if (bt::jsTruthy(e.forecast)) sub += QCoreApplication::translate("charts", " · forecast %1").arg(escv(e.forecast));
        if (bt::jsTruthy(e.previous)) sub += QCoreApplication::translate("charts", " · previous %1").arg(escv(e.previous));
        card->addSub(sub);
        L->addCard(card);
      }
      if (!n) L->add(padHint(QCoreApplication::translate("charts", "Nothing at this impact level this week.")));
      L->finish();
    });
    return;
  }

  const bool earnings = calView_ == "earnings";
  auto done = [this, stale, L, mine, earnings](const QJsonArray& all) {
    if (stale()) return;
    L->clear();
    QList<QJsonObject> rows;
    for (const auto& x : all) {
      const QJsonObject r = x.toObject();
      if (!calMine_ || mine.contains(r.value("symbol").toString())) rows << r;
    }
    std::stable_sort(rows.begin(), rows.end(), [&mine](const QJsonObject& x, const QJsonObject& y) {
      const int c = QString::compare(x.value("date").toString(), y.value("date").toString());   // localeCompare
      if (c) return c < 0;
      const int mx = mine.contains(x.value("symbol").toString()), my = mine.contains(y.value("symbol").toString());
      if (mx != my) return mx > my;
      return bt::jsNum(x.value("marketCap")) > bt::jsNum(y.value("marketCap"));   // (y.cap || 0) - (x.cap || 0)
    });
    QString day;
    int n = 0;
    for (const auto& r : rows) {
      if (n++ >= 250) break;
      const QString date = r.value("date").toString();
      if (date != day) {
        day = date;
        const QDate dd = QDate::fromString(date, Qt::ISODate);   // new Date(`${iso}T12:00:00Z`)
        L->add(padH4(dd.isValid() ? side::localeDayLabel(dd) : invalidDate()));
      }
      const QString sym = r.value("symbol").toString();
      auto* card = new side::Card(nullptr, true);
      const QString star = mine.contains(sym)
                               ? QStringLiteral(" <span title=\"%1\" style=\"color:%2\">●</span>")
                                     .arg(QCoreApplication::translate("charts", "On your watchlist"), style::v().up.name())
                               : QString();
      card->addTop(side::label(QStringLiteral("<b>%1</b>%2 %3")
                                   .arg(escv(r.value("ticker")), star,
                                        mutedSpan(esc(bt::jsTruthy(r.value("name")) ? bt::jsString(r.value("name")).left(34) : QString())))));
      QString sub;
      if (earnings) {
        // "when" (Before the open / After the close / Time not given) is built in English by MarketInfo.cpp
        sub = r.value("when").isString() ? esc(fmt::tx(r.value("when").toString())) : escv(r.value("when"));
        if (bt::jsTruthy(r.value("epsForecast")))
          sub += QCoreApplication::translate("charts", " · EPS forecast %1").arg(escv(r.value("epsForecast")));
        if (bt::jsTruthy(r.value("lastYearEps")))
          sub += QCoreApplication::translate("charts", " · last year %1").arg(escv(r.value("lastYearEps")));
        if (bt::jsTruthy(r.value("marketCap")))
          sub += QCoreApplication::translate("charts", " · cap %1").arg(side::fmtNum(bt::jsNum(r.value("marketCap"))));
      } else {
        const QJsonValue amt = r.value("amount");
        if (!amt.isNull() && !amt.isUndefined())
          sub = QCoreApplication::translate("charts", "%1 a share").arg(side::fmtPrice(bt::jsNum(amt)));
        if (bt::jsTruthy(r.value("payDate")))
          sub += QCoreApplication::translate("charts", " · paid %1").arg(escv(r.value("payDate")));
        if (bt::jsTruthy(r.value("annual")))
          sub += QCoreApplication::translate("charts", " · %1 a year").arg(side::fmtPrice(bt::jsNum(r.value("annual"))));
      }
      card->addSub(sub);
      connect(card, &side::Card::clicked, this, [this, sym] { app->setSymbol(sym); });
      L->addCard(card);
    }
    if (rows.isEmpty())
      L->add(padHint(calMine_ ? QCoreApplication::translate("charts", "None of your watchlist symbols in the next five trading days.")
                              : QCoreApplication::translate("charts", "Nothing listed for the next five trading days.")));
    L->finish();
  };
  if (earnings) data::earnings(5, this, done);
  else data::dividends(5, this, done);
}

// ======================= NEWS (research.js renderNews) =======================

void SidePanel::renderNews() {
  QWidget* body = newBody();
  const int seq = renderSeq_;
  const QString sym = app->active ? app->active->cfg.value("symbol").toString() : QString();
  auto* v = new QVBoxLayout(body);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(0);
  auto* title = side::label(QStringLiteral("<b>%1</b>").arg(esc(side::ticker(sym))));
  title->setWordWrap(false);
  auto* refresh = style::btn(QCoreApplication::translate("charts", "Refresh"), "small");
  refresh->setFixedHeight(24);
  v->addWidget(padRow({title, nullptr, refresh}));
  auto* note = padHint(QCoreApplication::translate("charts", "Loading headlines…"));
  v->addWidget(note);
  auto* list = new side::Blocks(body);
  v->addWidget(list);
  v->addStretch(1);
  connect(refresh, &QPushButton::clicked, this,
          [this] { QMetaObject::invokeMethod(this, [this] { renderNews(); }, Qt::QueuedConnection); });

  QPointer<QLabel> N(note);
  QPointer<side::Blocks> L(list);
  data::news(sym, this, [this, seq, sym, N, L](const data::Result<QJsonObject>& r) {
    if (!N || !L || seq != renderSeq_) return;
    const QString cur = app->active ? app->active->cfg.value("symbol").toString() : QString();
    if (cur != sym) return;
    if (!r.ok()) {
      N->setText(QCoreApplication::translate("charts", "Headlines unavailable: %1").arg(fmt::tx(r.error->message)));
      return;
    }
    const QJsonValue nv = r.value.value("note");   // textContent = r.note (null / undefined -> "")
    N->setText(nv.isNull() || nv.isUndefined() ? QString() : fmt::tx(bt::jsString(nv)));   // note: English from MarketInfo.cpp
    for (const auto& x : r.value.value("items").toArray()) {
      const QJsonObject n = x.toObject();
      auto* card = new side::Card(nullptr, true, true);
      card->setToolTip(::ui::plainTip(n.value("url").toString()));
      auto* t = side::label(escv(n.value("title")));
      t->setFont(style::font(13, QFont::DemiBold));
      card->body()->addWidget(t);
      QString sub = escv(n.value("source"));
      if (bt::jsTruthy(n.value("time")) || bt::jsTruthy(n.value("ago")))
        sub += " · " + esc(bt::jsTruthy(n.value("ago")) ? bt::jsString(n.value("ago")) : ago(bt::jsNum(n.value("time"))));
      card->addSub(sub);
      if (bt::jsTruthy(n.value("summary"))) {
        auto* s = new side::ClampLabel(bt::jsString(n.value("summary")), 2, card);
        s->setContentsMargins(0, 0, 0, 0);
        card->body()->addSpacing(4);   // .news-sum { margin-top: 4px } (the .sub margin is 2px, this one wins)
        card->body()->addWidget(s);
      }
      const QString url = n.value("url").toString();
      // a feed's <link>: web links only (a file:// or app scheme from a feed is refused)
      connect(card, &side::Card::clicked, this, [url] { ::ui::openWebLink(url); });
      L->addCard(card);
    }
    L->finish();
  });
}

}  // namespace charts::ui
