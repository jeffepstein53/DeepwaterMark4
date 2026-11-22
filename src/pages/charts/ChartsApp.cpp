// Charts tab — the page and its "app" object. Port of quant-terminal public/js/app.js + index.html + app.css
// (MIT, Copyright (c) 2026 Lewis Jackson — docs/charts/NOTICE.md). Left out (user's choice): the Academy ribbon,
// the Help link and the PTQ tab.
#include "pages/charts/ChartsApp.h"
#include "ui/Kit.h"
#include "charts/core/FootprintSettings.h"
#include "charts/data/CustomFiles.h"
#include "pages/charts/ChartPane.h"
#include "pages/charts/Panels.h"
#if __has_include("pages/charts/panels/SidePanel.h")
#include "pages/charts/panels/SidePanel.h"
#define CHARTS_HAS_SIDE_STEPS 1
#endif
#if __has_include("pages/charts/panels/BottomPanel.h")
#include "pages/charts/panels/BottomPanel.h"
#define CHARTS_HAS_BOTTOM 1
#endif
#include "pages/charts/Drawings.h"
#include "pages/charts/Style.h"
#include "pages/charts/Icons.h"
#include "pages/charts/Format.h"
#include "charts/core/Indicators.h"
#include "charts/core/Series.h"
#include "charts/core/CustomJs.h"
#include "charts/data/Store.h"
#include "charts/data/Providers.h"
#include "charts/data/Alerts.h"
#include "charts/data/ApiServer.h"
#include "core/Paths.h"
#include "core/I18n.h"
#include "ui/Theme.h"
#include "app/Pages.h"
#include "platform/IosNotify.h"
#include <QAbstractButton>
#include <QApplication>
#include <QSystemTrayIcon>
#include <QCoreApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QScrollArea>
#include <QStyle>
#include <functional>
#include <memory>
#include <QScrollBar>
#include <QDateTimeEdit>
#include <QColorDialog>
#include <QStandardItemModel>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QMessageBox>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QPainterPath>
#include <QPainter>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QBuffer>
#include "core/Process.h"
#include <QPointer>
#include <QJsonDocument>
#include <QTextDocument>
#include <QResizeEvent>
#include <cmath>

DW_PAGE("charts/charts", charts::ui::ChartsApp)

namespace charts::ui {

namespace {
const QStringList TF_QUICK{"1m", "5m", "15m", "1h", "4h", "1D", "1W"};
int layoutCount(const QString& l) {
  static const QHash<QString, int> L{{"1", 1}, {"2v", 2}, {"2h", 2}, {"3", 3}, {"4", 4}, {"6", 6}, {"8", 8}, {"16", 16}};
  return L.value(l, 1);
}
QJsonObject defaultWatchlists() {
  return QJsonObject{{"Main", QJsonArray{"BINANCE:BTCUSDT", "BINANCE:ETHUSDT", "BINANCE:SOLUSDT", "US:AAPL", "US:NVDA",
                                         "US:SPY", "US:QQQ", "FX:EURUSD"}}};
}
// chart.js newUid: `i${Date.now().toString(36)}${(uidSeq++).toString(36)}`
QString newUid() {
  static qint64 seq = 0;
  return "i" + QString::number(QDateTime::currentMSecsSinceEpoch(), 36) + QString::number(seq++, 36);
}
QJsonObject barJson(const Bar& b) {
  return QJsonObject{{"time", double(b.time)}, {"open", b.open}, {"high", b.high}, {"low", b.low}, {"close", b.close},
                     {"volume", b.volume}};
}
QString jsString(const QJsonValue& v) {   // String(v) for an <input value="${v}">
  if (v.isDouble()) return ind::numberToString(v.toDouble());
  if (v.isBool()) return v.toBool() ? "true" : "false";
  if (v.isNull() || v.isUndefined()) return v.isNull() ? "null" : "undefined";
  return v.toString();
}

// ---- UI step registry
QHash<QString, UiStep>& steps() { static QHash<QString, UiStep> s; return s; }

// ---- a vertical 1 px separator of the topbar (.sep: 1x22, margin 0 6)
class Sep : public QWidget {
public:
  explicit Sep(QWidget* p) : QWidget(p) { setFixedSize(13, 22); }
protected:
  void paintEvent(QPaintEvent*) override { QPainter(this).fillRect(QRect(6, 0, 1, 22), style::v().border); }
};
// .drawbar hr: 24 px, 1 px border-top, margin 4 0
class Hr : public QWidget {
public:
  explicit Hr(QWidget* p) : QWidget(p) { setFixedSize(24, 9); }
protected:
  void paintEvent(QPaintEvent*) override { QPainter(this).fillRect(QRect(0, 4, 24, 1), style::v().border); }
};
// Brand square (index.html .brand svg 22x22, padding 0 8 0 2)
class Brand : public QWidget {
public:
  explicit Brand(QWidget* p) : QWidget(p) { setFixedSize(32, 32); setToolTip("Deepwater"); }
protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    const auto& s = style::v();
    p.drawPixmap(QPointF(2, 5), icons::brand(22, s.accent, s.onAccent, devicePixelRatioF()));
  }
};
// A plain box that paints a ground and optional 1 px borders (the CSS containers).
class Box : public QWidget {
public:
  enum Edge { None = 0, Left = 1, Right = 2, Top = 4, Bottom = 8 };
  Box(QWidget* p, int edges, bool fillBorder = false) : QWidget(p), edges(edges), fillBorder(fillBorder) {}
  int edges;
  bool fillBorder;   // .grid: background = border (the 1 px gaps)
protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    const auto& s = style::v();
    p.fillRect(rect(), fillBorder ? s.border : s.bg);
    if (edges & Left) p.fillRect(QRect(0, 0, 1, height()), s.border);
    if (edges & Right) p.fillRect(QRect(width() - 1, 0, 1, height()), s.border);
    if (edges & Top) p.fillRect(QRect(0, 0, width(), 1), s.border);
    if (edges & Bottom) p.fillRect(QRect(0, height() - 1, width(), 1), s.border);
  }
};
// The modal overlay (.modal): dim backdrop, card at the top centre (padding-top 9vh); mousedown outside closes.
// The card's box-shadow (0 8px 32px var(--shadow)) is painted by the backdrop from a cached image. A
// QGraphicsDropShadowEffect on the card re-rendered and blurred the whole card on every child update (each caret blink
// of a search field: ~4 % CPU with the indicator picker open).
class ModalCard : public QFrame {
public:
  explicit ModalCard(QWidget* p) : QFrame(p) { setProperty("qtc", "modal-card"); }
protected:
  void mousePressEvent(QMouseEvent* e) override { e->accept(); }
  // the shadow reaches outside the card: the backdrop repaints it when the card moves or resizes
  void moveEvent(QMoveEvent* e) override { QFrame::moveEvent(e); if (parentWidget()) parentWidget()->update(); }
  void resizeEvent(QResizeEvent* e) override { QFrame::resizeEvent(e); if (parentWidget()) parentWidget()->update(); }
};
class Backdrop : public QWidget {
public:
  explicit Backdrop(QWidget* p) : QWidget(p) {}
  std::function<void()> onOutside;
  QPointer<QWidget> card;
protected:
  static constexpr int kBlur = 32, kOffsetY = 8, kRadius = 8;   // the old QGraphicsDropShadowEffect's values
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.fillRect(rect(), style::v().backdrop);
    if (!card || !card->isVisible() || card->size().isEmpty()) return;
    const QColor c = style::v().shadow;
    if (shadowSize_ != card->size() || shadowColor_ != c || shadow_.isNull()) {
      shadowSize_ = card->size();
      shadowColor_ = c;
      shadow_ = makeShadow(shadowSize_, c);
    }
    p.drawImage(card->pos() - QPoint(kBlur, kBlur), shadow_);
  }
  void mousePressEvent(QMouseEvent* e) override { e->accept(); if (onOutside) onOutside(); }
private:
  // Qt's own drop-shadow blur (same look as the effect had), rendered once per card size and colour; the part under
  // the card is cut out (CSS never paints a box-shadow under its box).
  static QImage makeShadow(QSize size, const QColor& color) {
    QPainterPath shape;
    shape.addRoundedRect(QRectF(0, 0, size.width(), size.height()), kRadius, kRadius);
    QGraphicsScene scene;
    auto* item = new QGraphicsPathItem(shape);
    item->setPen(Qt::NoPen);
    item->setBrush(Qt::black);
    auto* eff = new QGraphicsDropShadowEffect;
    eff->setBlurRadius(kBlur);
    eff->setOffset(0, kOffsetY);
    eff->setColor(color);
    item->setGraphicsEffect(eff);
    scene.addItem(item);
    QImage img(size + QSize(2 * kBlur, 2 * kBlur + kOffsetY), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    const QRectF src(-kBlur, -kBlur, img.width(), img.height());
    scene.render(&p, QRectF(img.rect()), src);
    p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(kBlur, kBlur);
    p.fillPath(shape, Qt::black);
    return img;
  }
  QImage shadow_;
  QSize shadowSize_;
  QColor shadowColor_;
};
// <input type="color">: a 28 px field with the colour swatch; click opens the colour dialog.
class ColorField : public QWidget {
public:
  explicit ColorField(const QColor& c, QWidget* p = nullptr) : QWidget(p), color(c) {
    setFixedHeight(28);
    setMinimumWidth(70);
    setCursor(Qt::PointingHandCursor);
  }
  QColor color;
protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto& s = style::v();
    p.setPen(QPen(s.border, 1));
    p.setBrush(s.input);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRect(QRectF(rect()).adjusted(5, 5, -5, -5));
  }
  void mousePressEvent(QMouseEvent*) override {
    auto* dlg = new QColorDialog(color, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setOption(QColorDialog::ShowAlphaChannel);   // translucent fills (a grid, a volume-like body)
    QPointer<ColorField> self(this);
    QObject::connect(dlg, &QColorDialog::colorSelected, dlg, [self](const QColor& c) {
      if (self) { self->color = c; self->update(); }
    });
    dlg->open();
  }
};
}  // namespace

// =================================================================================================== private state
struct ChartsApp::Impl {
  explicit Impl(ChartsApp* app) : q(app) {}
  ChartsApp* q;
  QTimer saveTimer, healthTimer;
  // topbar
  QPushButton* symbolBtn = nullptr;
  QList<QPushButton*> tfButtons;
  QLineEdit* tfCustom = nullptr;
  QComboBox* chartType = nullptr;
  // Topbar compact steps when the window is narrower than the full bar (fitTopbar, called on resize):
  // 0 = full, 1 = labelled buttons show their icon only, 2 = the two selects shrink to their current text too,
  // 3 = no separators and no gap between items, 4 = the Sync box without its word.
  std::function<void()> fitTopbar;
  QComboBox* layoutSel = nullptr;
  QCheckBox* syncSymbol = nullptr;
  QPushButton *replayBtn = nullptr, *logBtn = nullptr;
  QPushButton* fpGear = nullptr;   // footprint settings, shown while the active chart is a footprint
  style::StatusDot* status = nullptr;
  // drawbar
  QHash<QString, QPushButton*> toolButtons;
  // grid
  Box* grid = nullptr;
  QGridLayout* gridLayout = nullptr;
  int count = 1;
  // panels
  Box* center = nullptr;
  Box* sideBox = nullptr;
  Box* bottomBox = nullptr;
  // modal
  Backdrop* backdrop = nullptr;
  QPointer<ModalCard> card;
  QPointer<QWidget> cardBody;
  QScrollArea* cardScroll = nullptr;
  QWidget* cardHead = nullptr;
  QWidget* cardFoot = nullptr;
  bool cardWide = false;
  // toasts
  QList<QPointer<QLabel>> toasts;   // .toasts: bottom-right column, 8 px gaps, oldest on top

  void placeOverlays();
  void restyle();
};

// =================================================================================================== construction
ChartsApp::ChartsApp(QWidget* parent) : QWidget(parent), d(new Impl(this)) {
  setObjectName("chartsApp");
  setFocusPolicy(Qt::StrongFocus);
  setFont(style::font(13));

  // ---- init (JS App.init): state with the JS defaults, then the saved state on top
  const QJsonObject saved = store::read("state", QJsonObject()).toObject();
  state = QJsonObject{
      {"layout", "1"},
      {"panes", QJsonArray{QJsonObject{{"symbol", "BINANCE:BTCUSDT"},
                                       {"tf", "1D"},
                                       {"type", "candles"},
                                       {"indicators", QJsonArray{QJsonObject{{"uid", newUid()}, {"id", "ema"},
                                                                             {"params", QJsonObject{{"length", 21}}}},
                                                                 QJsonObject{{"uid", newUid()}, {"id", "rsi"},
                                                                             {"params", QJsonObject{}}}}}}}},
      {"watchlists", defaultWatchlists()},
      {"activeWatchlist", "Main"},
      {"drawings", QJsonObject{}},
      {"settings", QJsonObject{}},
      {"paper", QJsonValue::Null},
      {"strategies", QJsonObject{}}};
  for (auto it = saved.begin(); it != saved.end(); ++it) state.insert(it.key(), it.value());
  settings = QJsonObject{{"theme", "dark"}, {"volume", true}, {"bars", 3000}, {"utc", false},
                         {"stayInDrawMode", false}, {"syncCrosshair", false}, {"countdown", true}};
  const QJsonObject savedSettings = state.value("settings").toObject();
  for (auto it = savedSettings.begin(); it != savedSettings.end(); ++it) settings.insert(it.key(), it.value());
  state["settings"] = settings;
  style::setLight(settings.value("theme").toString() == "light");
  setStyleSheet(style::qss());

  d->saveTimer.setSingleShot(true);
  d->saveTimer.setInterval(400);
  connect(&d->saveTimer, &QTimer::timeout, this, [this] { flushPersist(); });

  // ---- custom indicators (JS loadCustomIndicators): <data>/charts/custom, seeded once with the example
  {
    const QString dir = paths::data("charts/custom");
    const QString marker = dir + "/.seeded";
    if (!QFile::exists(marker)) {
      const QString target = dir + "/ema-ribbon-squeeze.js";
      if (!QFile::exists(target) && QFile::copy(":/charts/custom/ema-ribbon-squeeze.js", target))
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
      QFile m(marker);
      if (m.open(QIODevice::WriteOnly)) m.write("seeded with quant-terminal/custom/ema-ribbon-squeeze.js\n");
    }
    // ApiServer's launch() loads the folder too (in Deepwater, 1.5 s after start): skip what is already registered.
    // .pine files are Pine Script indicators (charts/core/PineIndicators.h); data::loadCustom loads either kind
    QList<QPair<QString, QString>> failed;
    // JavaScript only here: .pine files run the PineTS engine to load (up to seconds each, on the GUI thread), so
    // ApiServer's launch() loads them on a Pine thread (data::loadAllCustomAsync) and announces each through
    // customChanged, whose handler below re-renders the panes and reports a failure
    const QStringList files = QDir(dir).entryList({"*.js"}, QDir::Files, QDir::Name);
    for (const QString& f : files)
      if (!ind::byId(data::customIdOf(f)))
        if (const QString err = data::loadCustom(f); !err.isEmpty()) failed.append({f, err});
    for (const auto& fe : failed) {
        const QString file = fe.first, err = fe.second;
        QTimer::singleShot(0, this, [this, file, err] {
          toast(QCoreApplication::translate("charts", "Custom indicator %1 failed to load: %2").arg(fmt::esc(file), fmt::esc(fmt::tx(err))), "err");
        });
      }
  }

  // ---- page layout: topbar / main (drawbar | center (grid, bottom) | side)
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  // topbar (.topbar: 44 px, gap 2, padding 0 8, border-bottom, horizontal scroll with a hidden bar)
  auto* topScroll = new QScrollArea(this);
  topScroll->setFixedHeight(44);
  topScroll->setFrameShape(QFrame::NoFrame);
  topScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  topScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  topScroll->setWidgetResizable(true);
  auto* topbar = new Box(nullptr, Box::Bottom);
  topbar->setFixedHeight(44);
  topScroll->setWidget(topbar);
  auto* tb = new QHBoxLayout(topbar);
  tb->setContentsMargins(8, 0, 8, 1);
  tb->setSpacing(2);
  root->addWidget(topScroll);

  tb->addWidget(new Brand(topbar));
  d->symbolBtn = style::tbButton("BTCUSDT", {}, QCoreApplication::translate("charts", "Change symbol (or just start typing)"), topbar);
  d->symbolBtn->setProperty("symbol", "1");
  style::repolish(d->symbolBtn);
  tb->addWidget(d->symbolBtn);
  tb->addWidget(new Sep(topbar));
  {
    auto* tfBox = new QWidget(topbar);
    tfBox->setFixedHeight(32);
    auto* h = new QHBoxLayout(tfBox);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(1);
    for (const QString& t : TF_QUICK) {
      auto* b = new QPushButton(t, tfBox);
      b->setProperty("qtc", "tf");
      b->setFixedHeight(32);
      b->setMinimumWidth(34);
      b->setCursor(Qt::PointingHandCursor);
      b->setFocusPolicy(Qt::NoFocus);
      connect(b, &QPushButton::clicked, this, [this, t] { setTf(t); });
      h->addWidget(b);
      d->tfButtons << b;
    }
    tb->addWidget(tfBox, 0, Qt::AlignVCenter);
  }
  tb->addSpacing(2);   // margin-left 4 (2 + the 2 px gap)
  d->tfCustom = new QLineEdit(topbar);
  d->tfCustom->setProperty("qtc", "tf-custom");
  d->tfCustom->setFixedSize(46, 28);
  d->tfCustom->setAlignment(Qt::AlignCenter);
  d->tfCustom->setPlaceholderText("7m");
  d->tfCustom->setToolTip(QCoreApplication::translate("charts", "Any timeframe: 1s, 7m, 2h, 3D, 2W, 3M"));
  connect(d->tfCustom, &QLineEdit::returnPressed, this, [this] {
    static const QRegularExpression dRe("^(\\d+)d$", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression wRe("^(\\d+)w$", QRegularExpression::CaseInsensitiveOption);
    QString v = d->tfCustom->text().trimmed();
    v.replace(dRe, "\\1D");
    v.replace(wRe, "\\1W");
    try {
      series::parseTf(v);
      setTf(v);
      d->tfCustom->clear();
      setFocus();
    } catch (const std::exception& e) {
      toast(fmt::esc(fmt::tx(QString::fromUtf8(e.what()))), "err");   // "Bad timeframe …" (charts/core/Series)
    }
  });
  tb->addWidget(d->tfCustom);
  tb->addWidget(new Sep(topbar));
  d->chartType = style::tbSelect(QCoreApplication::translate("charts", "Chart type"), topbar);
  for (const auto& [id, label] : chartTypes()) d->chartType->addItem(QCoreApplication::translate("charts", label.toUtf8().constData()), id);
  connect(d->chartType, &QComboBox::activated, this, [this](int i) {
    if (!active) return;
    active->cfg["type"] = d->chartType->itemData(i).toString();
    active->render(true);
    syncToolbar();
    persist();
  });
  tb->addWidget(d->chartType);
  d->fpGear = style::iconButton(icons::kGear, QCoreApplication::translate("charts", "Footprint settings"), 14, topbar);
  connect(d->fpGear, &QPushButton::clicked, this, [this] { openFootprintSettings(); });
  d->fpGear->hide();
  tb->addWidget(d->fpGear);
  auto* indBtn = style::tbButton(QCoreApplication::translate("charts", "Indicators"), icons::kIndicators, QCoreApplication::translate("charts", "Indicators"), topbar);
  connect(indBtn, &QPushButton::clicked, this, [this] { openIndicatorPicker(); });
  tb->addWidget(indBtn);
  auto* cmpBtn = style::tbButton(QCoreApplication::translate("charts", "Compare"), icons::kCompare, QCoreApplication::translate("charts", "Compare symbols"), topbar);
  connect(cmpBtn, &QPushButton::clicked, this, [this] {
    openSymbolSearch(
        "",
        [this](const QString& sym) {
          if (!active) return;
          QJsonArray c = active->cfg.value("compare").toArray();
          c.append(sym);
          active->cfg["compare"] = c;
          persist();
          active->load(true);
        },
        QCoreApplication::translate("charts", "Compare with…"));
  });
  tb->addWidget(cmpBtn);
  tb->addWidget(new Sep(topbar));
  d->layoutSel = style::tbSelect(QCoreApplication::translate("charts", "Chart layout"), topbar);
  const QList<QPair<QString, const char*>> layouts{
      {"1", QT_TRANSLATE_NOOP("charts", "1 chart")},   {"2v", QT_TRANSLATE_NOOP("charts", "2 side by side")},
      {"2h", QT_TRANSLATE_NOOP("charts", "2 stacked")}, {"3", QT_TRANSLATE_NOOP("charts", "3 charts")},
      {"4", QT_TRANSLATE_NOOP("charts", "4 charts")},  {"6", QT_TRANSLATE_NOOP("charts", "6 charts")},
      {"8", QT_TRANSLATE_NOOP("charts", "8 charts")},  {"16", QT_TRANSLATE_NOOP("charts", "16 charts")}};
  for (const auto& [id, label] : layouts) d->layoutSel->addItem(QCoreApplication::translate("charts", label), id);
  connect(d->layoutSel, &QComboBox::activated, this, [this](int i) { setLayout(d->layoutSel->itemData(i).toString()); });
  tb->addWidget(d->layoutSel);
  // a <select> is as wide as its widest option + its padding (9 + 22)
  for (QComboBox* c : {d->chartType, d->layoutSel}) {
    int w = 0;
    const QFontMetrics fm(style::font(13));
    for (int i = 0; i < c->count(); ++i) w = std::max(w, fm.horizontalAdvance(c->itemText(i)));
    c->setFixedWidth(w + 9 + 22 + 24);   // + QComboBox's own text margins
  }
  d->syncSymbol = style::tbCheck(QCoreApplication::translate("charts", "Sync"), QCoreApplication::translate("charts", "Change the symbol on every chart at once"), topbar);
  d->syncSymbol->setChecked(settings.value("syncSymbol").toBool());
  connect(d->syncSymbol, &QCheckBox::toggled, this, [this](bool on) {
    settings["syncSymbol"] = on;
    persist();
  });
  tb->addWidget(d->syncSymbol);
  d->replayBtn = style::tbButton(QCoreApplication::translate("charts", "Replay"), icons::kReplay, QCoreApplication::translate("charts", "Bar replay"), topbar);
  connect(d->replayBtn, &QPushButton::clicked, this, [this] {
    if (!active) return;
    if (active->replay) return active->stopReplay();
    setTool("replay-pick");
    toast(QCoreApplication::translate("charts", "Click the bar you want replay to start from."));
  });
  tb->addWidget(d->replayBtn);
  auto* alertBtn = style::tbButton(QCoreApplication::translate("charts", "Alert"), icons::kAlert, QCoreApplication::translate("charts", "Create alert"), topbar);
  connect(alertBtn, &QPushButton::clicked, this, [this] { if (panels) panels->openAlertDialog(); });
  tb->addWidget(alertBtn);
  tb->addStretch(1);
  d->logBtn = style::tbButton(QCoreApplication::translate("charts", "Log"), {}, QCoreApplication::translate("charts", "Logarithmic price scale"), topbar);
  d->logBtn->setProperty("iconOnly", "0");
  connect(d->logBtn, &QPushButton::clicked, this, [this] {
    if (!active) return;
    active->cfg["log"] = !active->cfg.value("log").toBool();
    active->render(false);
    syncToolbar();
    persist();
  });
  tb->addWidget(d->logBtn);
  auto* snapBtn = style::tbButton({}, icons::kCamera, QCoreApplication::translate("charts", "Download chart image"), topbar);
  connect(snapBtn, &QPushButton::clicked, this, [this] {
    if (!active) return;
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    active->screenshot().save(&buf, "PNG");
    fmt::download(this, fmt::ticker(active->cfg.value("symbol").toString()) + "-" + active->cfg.value("tf").toString() +
                            ".png", png);
  });
  tb->addWidget(snapBtn);
  auto* csvBtn = style::tbButton({}, icons::kDownload, QCoreApplication::translate("charts", "Export chart data (CSV)"), topbar);
  connect(csvBtn, &QPushButton::clicked, this, [this] { exportCsv(); });
  tb->addWidget(csvBtn);
  auto* setBtn = style::tbButton({}, icons::kGear, QCoreApplication::translate("charts", "Settings"), topbar);
  connect(setBtn, &QPushButton::clicked, this, [this] { openSettings(); });
  tb->addWidget(setBtn);
  auto* themeBtn = style::tbButton({}, icons::kMoon, QCoreApplication::translate("charts", "Light / dark"), topbar);
  connect(themeBtn, &QPushButton::clicked, this, [this] {
    settings["theme"] = settings.value("theme").toString() == "dark" ? "light" : "dark";
    style::setLight(settings.value("theme").toString() == "light");
    d->restyle();
    for (auto* p : panes) p->applyTheme();
    if (panels) panels->onTheme();
    emit themeChanged();
    persist();
  });
  tb->addWidget(themeBtn);
  tb->addSpacing(6);   // .status margin 0 6 0 8 (8 - the 2 px gap)
  d->status = new style::StatusDot(topbar);
  d->status->setToolTip(QCoreApplication::translate("charts", "Data status"));
  tb->addWidget(d->status, 0, Qt::AlignVCenter);
  tb->addSpacing(4);
  // ≤ 900 px the topbar overflows and scrolls sideways (overflow-x: auto, hidden bar), as in the browser. A closed
  // <select> never takes the wheel there, but Qt's combo box does with some styles (Windows): it would change the
  // chart type and stop the topbar from scrolling. Over the topbar's selects the wheel scrolls the bar instead.
  struct TopbarWheel : QObject {
    QScrollArea* area;
    explicit TopbarWheel(QScrollArea* a) : QObject(a), area(a) {}
    bool eventFilter(QObject*, QEvent* e) override {
      if (e->type() != QEvent::Wheel) return false;
      auto* w = static_cast<QWheelEvent*>(e);
      if (std::abs(w->angleDelta().x()) > std::abs(w->angleDelta().y()))   // QAbstractScrollArea's rule
        QCoreApplication::sendEvent(area->horizontalScrollBar(), w);
      return true;
    }
  };
  auto* topWheel = new TopbarWheel(topScroll);
  for (auto* c : topbar->findChildren<QComboBox*>()) c->installEventFilter(topWheel);
  // the browser's flex items never shrink below their content (.tf-group is flex: none): the bar overflows instead.
  // Qt would squeeze them (the timeframe buttons down to their 34 px minimum, cutting "15m"), so each item keeps
  // its natural width and the scroll area takes the overflow.
  for (int i = 0; i < tb->count(); ++i)
    if (QWidget* w = tb->itemAt(i)->widget()) {
      QSizePolicy sp = w->sizePolicy();
      sp.setHorizontalPolicy(QSizePolicy::Fixed);
      w->setSizePolicy(sp);
    }
  // Narrow windows (Deepwater's own: down to 1024 px, while the full bar is ~1410 px): before the bar overflows, the
  // labelled buttons drop their text (it stays in the tooltip), then the two selects shrink to their current choice.
  // Only when that still doesn't fit does the bar scroll sideways as in the browser. Departs from quant-terminal on
  // purpose (the user's responsive pass); at full width nothing changes.
  {
    struct Labelled { QPushButton* b; QString text; };
    const QList<Labelled> labelled{{indBtn, indBtn->text()}, {cmpBtn, cmpBtn->text()}, {d->replayBtn, d->replayBtn->text()},
                                   {alertBtn, alertBtn->text()}};
    QHash<QComboBox*, int> fullWidth;
    for (QComboBox* c : {d->chartType, d->layoutSel}) fullWidth.insert(c, c->minimumWidth());   // the fixed <select> width
    const QString syncText = d->syncSymbol->text(), syncTip = d->syncSymbol->toolTip();
    d->fitTopbar = [this, topScroll, topbar, tb, labelled, fullWidth, syncText, syncTip] {
      auto apply = [&](int level) {
        for (const auto& l : labelled) {
          const bool iconOnly = level >= 1;
          if ((l.b->text().isEmpty()) == iconOnly) continue;
          l.b->setText(iconOnly ? QString() : l.text);
          l.b->setProperty("iconOnly", iconOnly ? "1" : "0");
          l.b->style()->unpolish(l.b);
          l.b->style()->polish(l.b);
        }
        for (auto it = fullWidth.cbegin(); it != fullWidth.cend(); ++it) {
          QComboBox* c = it.key();
          const int text = QFontMetrics(style::font(13)).horizontalAdvance(c->currentText());
          const int w = level >= 2 ? text + 9 + 22 + 24 : it.value();
          if (c->width() != w) c->setFixedWidth(w);
        }
        for (QObject* o : topbar->children())   // 3: the separators go too (Sep has no Q_OBJECT: no findChildren)
          if (auto* sep = dynamic_cast<Sep*>(o)) sep->setVisible(level < 3);
        tb->setSpacing(level >= 3 ? 0 : 2);   // and the 2 px gap between items
        // 4: the Sync box keeps its box only, its word moves into the tooltip (French "Synchro" is wider)
        d->syncSymbol->setText(level >= 4 ? QString() : syncText);
        d->syncSymbol->setToolTip(level >= 4 ? syncText + " — " + syncTip : syncTip);
        tb->setContentsMargins(level >= 4 ? 4 : 8, 0, level >= 4 ? 4 : 8, 1);   // and the bar's side padding
        tb->invalidate();
        return tb->sizeHint().width();
      };
      const int avail = topScroll->viewport()->width();
      int level = 0;
      while (level < 4 && apply(level) > avail) ++level;
      if (level == 4) apply(4);
      topbar->updateGeometry();
    };
    for (QComboBox* c : {d->chartType, d->layoutSel})
      connect(c, &QComboBox::currentIndexChanged, this, [this] { if (d->fitTopbar) QTimer::singleShot(0, this, [this] { d->fitTopbar(); }); });
  }

  // main
  auto* mainRow = new QHBoxLayout;
  mainRow->setContentsMargins(0, 0, 0, 0);
  mainRow->setSpacing(0);
  root->addLayout(mainRow, 1);

  // drawbar (.drawbar: 44 px, border-right, padding-top 6, gap 2)
  auto* drawbar = new Box(this, Box::Right);
  drawbar->setFixedWidth(44);
  // fixed-size items stacked from the top (padding-top 6, gap 2), centred in the 43 px inside the border
  int dbY = 6;
  auto place = [&](QWidget* w) {
    w->move((43 - w->width()) / 2, dbY);
    dbY += w->height() + 2;
  };
  auto addDrawButton = [&](const QString& svg, const QString& tip) {
    auto* b = new QPushButton(drawbar);
    b->setProperty("qtc", "drawbar");
    b->setFixedSize(34, 34);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);
    b->setToolTip(tip);
    style::setIconSvg(b, svg, 18);
    place(b);
    return b;
  };
  const auto& tl = tools();
  for (int i = 0; i < tl.size(); ++i) {
    if (i == 1) place(new Hr(drawbar));
    const QString id = tl[i].id;
    auto* b = addDrawButton(tl[i].icon, QCoreApplication::translate("charts", tl[i].label.toUtf8().constData()));
    connect(b, &QPushButton::clicked, this, [this, id] { setTool(tool == id ? "cursor" : id); });
    d->toolButtons.insert(id, b);
  }
  place(new Hr(drawbar));
  connect(addDrawButton(icons::kAlert, QCoreApplication::translate("charts", "Alert when price crosses the selected line")), &QPushButton::clicked, this,
          [this] { alertOnSelectedDrawing(); });
  connect(addDrawButton(icons::kTrash, QCoreApplication::translate("charts", "Delete selected drawing (Delete key)")), &QPushButton::clicked, this, [this] {
    if (active && !active->deleteSelectedDrawing()) toast(QCoreApplication::translate("charts", "Click a drawing first to select it."));
  });
  connect(addDrawButton(icons::kClearAll, QCoreApplication::translate("charts", "Remove all drawings on this symbol")), &QPushButton::clicked, this, [this] {
    if (!active) return;
    // the box runs a nested loop: an API layout change meanwhile may delete or replace the active pane
    QPointer<ChartPane> pane(active);
    const QString text = QCoreApplication::translate("charts", "Remove every drawing on %1?").arg(fmt::ticker(pane->cfg.value("symbol").toString()));
    if (::ui::askPlain(this, QString(), text, QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) ==
            QMessageBox::Ok &&
        pane)
      pane->clearDrawings();
  });
  drawbar->setMinimumHeight(dbY);
  // The browser lets a short window cut the drawbar's last buttons off; here it scrolls with the wheel instead (no
  // visible bar, like the topbar), so the tab doesn't hold the whole window at the drawbar's 680 px.
  auto* drawScroll = new QScrollArea(this);
  drawScroll->setFrameShape(QFrame::NoFrame);
  drawScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  drawScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  drawScroll->setWidgetResizable(true);
  drawScroll->setFixedWidth(44);
  drawScroll->viewport()->setAutoFillBackground(false);
  drawScroll->setStyleSheet("QScrollArea { background: transparent; }");
  drawScroll->setWidget(drawbar);
  mainRow->addWidget(drawScroll);

  // center: grid + bottom
  d->center = new Box(this, Box::None);
  auto* cv = new QVBoxLayout(d->center);
  cv->setContentsMargins(0, 0, 0, 0);
  cv->setSpacing(0);
  d->grid = new Box(d->center, Box::None, true);
  d->gridLayout = new QGridLayout(d->grid);
  d->gridLayout->setContentsMargins(0, 0, 0, 0);
  d->gridLayout->setSpacing(1);
  cv->addWidget(d->grid, 1);
  // the bottom panel paints its own border-top and sets its own height (320 px / 39 px collapsed)
  d->bottomBox = new Box(d->center, Box::None);
  auto* bl = new QVBoxLayout(d->bottomBox);
  bl->setContentsMargins(0, 0, 0, 0);
  bl->setSpacing(0);
  cv->addWidget(d->bottomBox, 0);
  mainRow->addWidget(d->center, 1);

  // side (.side: 300 px; the side panel paints its own border-left)
  d->sideBox = new Box(this, Box::None);
  d->sideBox->setFixedWidth(300);
  auto* sl = new QVBoxLayout(d->sideBox);
  sl->setContentsMargins(0, 0, 0, 0);
  sl->setSpacing(0);
  mainRow->addWidget(d->sideBox);

  // overlays: modal, toasts
  d->backdrop = new Backdrop(this);
  d->backdrop->hide();
  d->backdrop->onOutside = [this] { closeModal(); };

  // ---- the JS init order: toolbar, drawbar, layout, panels, keys, alerts, health
  setTool("cursor");
  setLayout(jsString(state.value("layout")), false);   // JS String(layout): a numeric layout (4) stays "4"
  panels = new Panels(this);
  if (QWidget* s = panels->side()) sl->addWidget(s, 1);
  if (QWidget* b = panels->bottom()) bl->addWidget(b, 0);
  panels->onActiveChange();

  // JS listenAlerts (the SSE "alert" event). The checker and the local API are started by charts::api::launch().
  connect(&alerts::Alerts::instance(), &alerts::Alerts::fired, this, [this](const QJsonObject& e) { onAlertEvent(e); });
  // PUT /api/state from outside (an AI agent, a script): take the new state as the JS would after a reload.
  connect(&api::Server::instance(), &api::Server::stateChanged, this,
          [this](const QJsonObject& st, const QJsonObject& patch) { applyExternalState(st, patch); });
  // A custom indicator saved, imported or deleted through the API.
  // saveCustom / importSignal already loaded and registered the file before the API emitted this: only refresh the
  // panes (loading it again ran the file and its trial calc a second time).
  connect(&api::Server::instance(), &api::Server::customChanged, this, [this](const QString& file) {
    if (!QFile::exists(paths::data("charts/custom") + "/" + file)) ind::unregisterIndicator(data::customIdOf(file));
    // a .pine file loaded at start-up on a Pine thread reports its failure here (see the custom folder load above)
    else if (file.endsWith(".pine") && !ind::byId(data::customIdOf(file)))
      if (const QString err = data::customError(file); !err.isEmpty())
        toast(QCoreApplication::translate("charts", "Custom indicator %1 failed to load: %2").arg(fmt::esc(file), fmt::esc(fmt::tx(err))), "err");
    for (auto* p : panes) p->render(false);
  });

  connect(&theme::Notifier::instance(), &theme::Notifier::changed, this, [this] {
    d->restyle();
    for (auto* p : panes) p->applyTheme();
    if (panels) panels->onTheme();
    emit themeChanged();
  });

  d->healthTimer.setInterval(60000);
  connect(&d->healthTimer, &QTimer::timeout, this, [this] { checkHealth(); });
  checkHealth();
}

ChartsApp::~ChartsApp() {
  if (d->saveTimer.isActive()) flushPersist();
  closeModal();
  while (!panes.isEmpty()) delete panes.takeLast();
  active = nullptr;
  if (panels) {
    QPointer<QWidget> s = panels->side(), b = panels->bottom();
    delete s.data();
    delete b.data();
    delete panels;
    panels = nullptr;
  }
  delete d;
}

void ChartsApp::Impl::restyle() {
  q->setStyleSheet(style::qss());
  style::refreshIcons(q);
  q->update();
  for (auto* w : q->findChildren<QWidget*>()) w->update();
}

// =================================================================================================== persistence
void ChartsApp::persist() {
  QJsonArray ps;
  for (auto* p : panes) ps.append(p->cfg);
  // the configs of charts a smaller layout removed stay after the live ones (setLayout brings them back)
  const QJsonArray old = state.value("panes").toArray();
  for (qsizetype i = ps.size(); i < old.size(); ++i) ps.append(old[i]);
  state["panes"] = ps;
  state["active"] = int(panes.indexOf(active));
  state["settings"] = settings;
  d->saveTimer.start();
}
void ChartsApp::flushPersist() {
  d->saveTimer.stop();
  state["settings"] = settings;
  QString err;
  store::mergeState(state, &err);
  if (!err.isEmpty()) toast(QCoreApplication::translate("charts", "Couldn't save: %1").arg(fmt::esc(err)), "err");
}
QJsonArray ChartsApp::getDrawings(const QString& symbol) const {
  return state.value("drawings").toObject().value(symbol).toArray();   // value copy = structuredClone
}
void ChartsApp::saveDrawings(const QString& symbol, const QJsonArray& items) {
  QJsonArray out;
  for (const auto& v : items) {
    const QJsonObject o = v.toObject();
    QJsonObject k;
    for (const char* f : {"id", "type", "points", "text", "color", "rr"})
      if (o.contains(f) && !o.value(f).isUndefined()) k.insert(f, o.value(f));
    out.append(k);
  }
  QJsonObject dr = state.value("drawings").toObject();
  dr[symbol] = out;
  state["drawings"] = dr;
  // JS: every other pane on this symbol takes the new list (the caller already has it).
  for (auto* p : panes)
    if (p->cfg.value("symbol").toString() == symbol && p->drawingItems() != out) p->setDrawingItems(out);
  persist();
}
ind::DefPtr ChartsApp::importCustom(const QString& file) {
  const QString error = data::loadCustom(file);   // .js (custom JS) or .pine (Pine Script)
  if (!error.isEmpty()) {
    toast(QCoreApplication::translate("charts", "Custom indicator %1 failed to load: %2").arg(fmt::esc(file), fmt::esc(fmt::tx(error))), "err");
    return nullptr;
  }
  return ind::byId(data::customIdOf(file));
}
QString ChartsApp::theme() const { return settings.value("theme").toString(); }

// =================================================================================================== layout
void ChartsApp::setLayout(const QString& layout, bool save) {
  const int count = layoutCount(layout);
  state["layout"] = layout;
  d->count = count;
  // grid-template-columns / rows of the JS
  int cols = 1, rows = 1;
  switch (count) {
    case 1: cols = 1; rows = 1; break;
    case 2: cols = layout == "2h" ? 1 : 2; rows = layout == "2h" ? 2 : 1; break;
    case 3: cols = 2; rows = 2; break;   // "2fr 1fr" / "1fr 1fr", the first pane spans 2 rows
    case 4: cols = 2; rows = 2; break;
    case 6: cols = 3; rows = 2; break;
    case 8: cols = 4; rows = 2; break;
    case 16: cols = 4; rows = 4; break;
  }
  // JS `cfgs[i] || {...clone of cfgs[0], tf, indicators: []}`: a null (or any non-object) entry counts as missing.
  QList<std::optional<QJsonObject>> cfgs;
  const QJsonArray saved = state.value("panes").toArray();
  for (auto* p : panes) cfgs << p->cfg;
  // after the live panes, the saved configs of charts a smaller layout removed: growing it again brings them back
  // (persist() keeps them after the live ones) instead of clones of the first chart
  for (qsizetype i = cfgs.size(); i < saved.size(); ++i)
    cfgs << (saved[i].isObject() ? std::optional<QJsonObject>(saved[i].toObject()) : std::nullopt);
  if (!panes.isEmpty()) {
    QJsonArray all;
    for (const auto& c : cfgs) all.append(c ? QJsonValue(*c) : QJsonValue());
    state["panes"] = all;
  }
  while (panes.size() > count) {
    ChartPane* p = panes.takeLast();
    if (p == active) active = nullptr;
    // a removed chart leading a synced replay: its followers go back to live instead of staying frozen
    if (p->replay && !p->replay->follower) onReplayStop(p);
    delete p;
  }
  static const QStringList tfs{"1D", "4h", "1h", "15m", "1W", "5m", "30m", "2h"};
  QList<ChartPane*> created;
  for (int i = 0; i < count; ++i) {
    if (i < panes.size()) continue;
    QJsonObject base;
    if (i < cfgs.size() && cfgs[i]) base = *cfgs[i];
    else {
      base = cfgs.isEmpty() ? QJsonObject() : cfgs[0].value_or(QJsonObject());
      base["tf"] = tfs[i % tfs.size()];
      base["indicators"] = QJsonArray();
    }
    auto* pane = new ChartPane(this, base, d->grid);
    panes << pane;
    created << pane;
  }
  // re-place every pane in the grid
  for (auto* p : panes) d->gridLayout->removeWidget(p);
  for (int c = 0; c < 4; ++c) d->gridLayout->setColumnStretch(c, 0);
  for (int r = 0; r < 4; ++r) d->gridLayout->setRowStretch(r, 0);
  for (int c = 0; c < cols; ++c) d->gridLayout->setColumnStretch(c, count == 3 && c == 0 ? 2 : 1);
  for (int r = 0; r < rows; ++r) d->gridLayout->setRowStretch(r, 1);
  for (int i = 0; i < panes.size(); ++i) {
    if (count == 3) {
      if (i == 0) d->gridLayout->addWidget(panes[0], 0, 0, 2, 1);
      else d->gridLayout->addWidget(panes[i], i - 1, 1);
    } else {
      d->gridLayout->addWidget(panes[i], i / cols, i % cols);
    }
    panes[i]->show();
  }
  for (auto* p : created) p->load();
  // the active chart stays active when it survives the change (state.active is only as fresh as the last persist)
  if (!active || !panes.contains(active)) {
    const int want = std::min(state.value("active").toInt(0), count - 1);
    setActive(want >= 0 && want < panes.size() ? panes[want] : panes.value(0));
  }
  d->layoutSel->setCurrentIndex(std::max(0, d->layoutSel->findData(layout)));
  for (auto* p : panes) p->syncActive();   // the outline shows only with several panes
  if (save) persist();
}

void ChartsApp::setActive(ChartPane* pane) {
  if (!pane || active == pane) return;
  QPointer<ChartPane> old = active;
  active = pane;
  if (old) old->syncActive();   // the pane paints .pane.active::after itself
  pane->syncActive();
  syncToolbar();
  if (panels) panels->onActiveChange();
}

void ChartsApp::onPaneLoaded(ChartPane* pane) {
  if (pane == active) {
    syncToolbar();
    if (panels) panels->onActiveChange();
  }
}

// =================================================================================================== toolbar
void ChartsApp::syncToolbar() {
  ChartPane* p = active;
  if (!p) return;
  const QString sym = p->cfg.value("symbol").toString(), tf = p->cfg.value("tf").toString();
  d->symbolBtn->setText(fmt::ticker(sym));
  d->symbolBtn->setToolTip(QCoreApplication::translate("charts", "%1 — change symbol (or just start typing)").arg(sym));
  for (auto* b : d->tfButtons) style::setActive(b, b->text() == tf);
  d->tfCustom->setPlaceholderText(TF_QUICK.contains(tf) ? "7m" : tf);
  style::setOn(d->tfCustom, !TF_QUICK.contains(tf));
  d->chartType->setCurrentIndex(std::max(0, d->chartType->findData(p->cfg.value("type").toString())));
  style::setOn(d->logBtn, p->cfg.value("log").toBool());
  style::setOn(d->replayBtn, p->replay.has_value());
  d->fpGear->setVisible(p->cfg.value("type").toString() == "footprint");
}

void ChartsApp::setTf(const QString& tf) {
  if (!active) return;
  active->cfg["tf"] = tf;
  active->load();
  syncToolbar();
  persist();
}

void ChartsApp::setSymbol(const QString& sym, ChartPane* pane) {
  if (!pane) pane = active;
  if (!pane) return;
  const QList<ChartPane*> targets = settings.value("syncSymbol").toBool() ? panes : QList<ChartPane*>{pane};
  for (auto* p : targets) {
    p->cfg["symbol"] = sym;
    QJsonArray keep;
    for (const auto& c : p->cfg.value("compare").toArray()) if (c.toString() != sym) keep.append(c);
    p->cfg["compare"] = keep;
    p->load();
  }
  syncToolbar();
  persist();
}

void ChartsApp::setTool(const QString& t) {
  tool = t;
  for (auto it = d->toolButtons.begin(); it != d->toolButtons.end(); ++it) style::setActive(it.value(), it.key() == t);
  for (auto* p : panes) p->setInteractive(t == "cursor");
  if (t == "cursor") unsetCursor();
  else setCursor(Qt::CrossCursor);
}

// =================================================================================================== modals
QWidget* ChartsApp::modal(const QString& title, QWidget* body, const QList<QWidget*>& foot, bool wide) {
  if (d->card) { d->card->deleteLater(); d->card = nullptr; }
  auto* card = new ModalCard(d->backdrop);
  d->card = card;
  d->backdrop->card = card;   // the backdrop paints the card's shadow
  d->cardWide = wide;
  auto* v = new QVBoxLayout(card);
  v->setContentsMargins(1, 1, 1, 1);
  v->setSpacing(0);
  // .modal-head
  d->cardHead = new QWidget(card);
  d->cardHead->setProperty("qtc", "modal-head");
  d->cardHead->setAttribute(Qt::WA_StyledBackground);
  auto* hh = new QHBoxLayout(d->cardHead);
  hh->setContentsMargins(14, 12, 14, 12);
  auto* tl = new QLabel(title, d->cardHead);
  tl->setProperty("qtc", "modal-title");
  tl->setTextFormat(Qt::PlainText);
  tl->setMinimumHeight(21);   // 15 px x line-height 1.4
  hh->addWidget(tl, 1);
  auto* x = new QPushButton(d->cardHead);
  x->setProperty("qtc", "icon-x");
  x->setToolTip(QCoreApplication::translate("charts", "Close"));
  x->setCursor(Qt::PointingHandCursor);
  x->setFocusPolicy(Qt::NoFocus);
  x->setFixedSize(22, 22);
  style::setIconSvg(x, icons::kClose, 18);
  connect(x, &QPushButton::clicked, this, [this] { closeModal(); });
  hh->addWidget(x);
  v->addWidget(d->cardHead);
  // .modal-body (scrolls)
  auto* bodyWrap = new QWidget;
  auto* bw = new QVBoxLayout(bodyWrap);
  bw->setContentsMargins(14, 12, 14, 12);
  bw->setSpacing(0);
  body->setParent(bodyWrap);
  bw->addWidget(body);
  d->cardBody = bodyWrap;
  d->cardScroll = style::scrollArea(bodyWrap, card);
  v->addWidget(d->cardScroll, 1);
  bodyWrap->installEventFilter(this);
  body->installEventFilter(this);
  // .modal-foot
  d->cardFoot = nullptr;
  if (!foot.isEmpty()) {
    d->cardFoot = new QWidget(card);
    d->cardFoot->setProperty("qtc", "modal-foot");
    d->cardFoot->setAttribute(Qt::WA_StyledBackground);
    auto* fh = new QHBoxLayout(d->cardFoot);
    fh->setContentsMargins(14, 10, 14, 10);
    fh->setSpacing(8);
    bool left = false;
    for (auto* w : foot) if (w->property("qtc").toString() == "hint") left = true;
    if (!left) fh->addStretch(1);
    for (auto* w : foot) {
      w->setParent(d->cardFoot);
      if (w->property("qtc").toString() == "hint") {   // style="margin-right:auto"
        if (auto* l = qobject_cast<QLabel*>(w)) l->setWordWrap(false);
        fh->addWidget(w);
        fh->addStretch(1);
      } else {
        fh->addWidget(w);
      }
    }
    v->addWidget(d->cardFoot);
  }
  d->backdrop->setGeometry(rect());
  d->backdrop->show();
  d->backdrop->raise();
  for (auto& t : d->toasts) if (t) t->raise();
  card->show();
  relayoutModal();
  QTimer::singleShot(0, this, [this] { relayoutModal(); });
  return card;
}

void ChartsApp::relayoutModal() {
  if (!d->card) return;
  const int W = width(), H = height();
  const int cw = d->cardWide ? std::min(780, int(W * 0.96)) : std::min(560, int(W * 0.94));
  const int maxH = int(H * 0.78), x = (W - cw) / 2, y = int(H * 0.09);
  // lay the card out at its largest, then measure the body at the real width of the scroll area
  d->card->setGeometry(x, y, cw, maxH);
  if (d->card->layout()) d->card->layout()->activate();
  int need = 0;
  if (d->cardBody && d->cardScroll) {
    const int vw = d->cardScroll->width();
    QLayout* bl = d->cardBody->layout();
    need = bl ? (bl->hasHeightForWidth() ? bl->totalHeightForWidth(vw) : bl->totalSizeHint().height())
              : d->cardBody->sizeHint().height();
  }
  const int head = d->cardHead ? d->cardHead->height() : 0;
  const int foot = d->cardFoot ? d->cardFoot->height() : 0;
  int ch = std::min(maxH, head + need + foot + 2);
  d->card->setGeometry(x, y, cw, ch);
  // whatever the estimate missed (wrapped labels measured at another width): grow while the body still scrolls
  for (int i = 0; i < 3 && d->cardScroll && ch < maxH; ++i) {
    if (d->card->layout()) d->card->layout()->activate();
    const int extra = d->cardScroll->verticalScrollBar()->maximum();
    if (extra <= 0) break;
    ch = std::min(maxH, ch + extra);
    d->card->setGeometry(x, y, cw, ch);
  }
}

void ChartsApp::closeModal() {
  if (d->card) {
    d->card->hide();
    d->card->deleteLater();
    d->card = nullptr;
  }
  d->cardBody = nullptr;
  d->cardHead = d->cardFoot = nullptr;
  d->backdrop->hide();
  if (isVisible()) setFocus();
}
bool ChartsApp::modalOpen() const { return !d->card.isNull(); }
QWidget* ChartsApp::modalCard() const { return d->card; }

// ---- symbol search
void ChartsApp::openSymbolSearch(const QString& initial, std::function<void(const QString&)> onPick,
                                 const QString& title) {
  auto* body = new QWidget;
  auto* v = new QVBoxLayout(body);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(0);
  auto* q = new QLineEdit(body);
  q->setProperty("qtc", "search-in");
  q->setFixedHeight(38);
  q->setPlaceholderText(QCoreApplication::translate("charts", "BTC, AAPL, EURUSD, ^FTSE, VOD.L…"));
  q->setText(initial);
  v->addWidget(q);
  v->addSpacing(8);   // .list margin-top
  auto* list = new style::ItemList(body);
  v->addWidget(list);
  v->addSpacing(8);
  v->addWidget(style::hint(QCoreApplication::translate("charts", "Crypto: Binance (falls back to Coinbase and Kraken). US stocks and ETFs: Nasdaq. Forex: "
                              "Kraken. Global indices, futures and non-US stocks use Yahoo symbols (YAHOO:^FTSE, "
                              "YAHOO:GC=F) when Yahoo is reachable."),
                           body));
  modal(title.isEmpty() ? QCoreApplication::translate("charts", "Symbol search") : title, body);

  struct S { int sel = 0; QList<data::SearchHit> results; QString query; QTimer timer; };   // query: what results answer
  auto st = std::make_shared<S>();
  st->timer.setSingleShot(true);
  st->timer.setInterval(180);
  QPointer<QLineEdit> qp(q);
  QPointer<style::ItemList> lp(list);
  auto pick = [this, onPick](const QString& sym) {
    closeModal();
    if (onPick) onPick(sym);
    else setSymbol(sym);
  };
  auto draw = [st, qp, lp] {
    if (!qp || !lp) return;
    QList<style::ListItem> items;
    for (int i = 0; i < st->results.size(); ++i) {
      const auto& r = st->results[i];
      // our own names ("Gold / USD", "Your file, …"): French; names from the sources (companies) stay as they are
      const QString name = r.name.startsWith(QLatin1String("Your file, ")) || r.name.endsWith(QLatin1String(" / USDT perpetual"))
                               ? fmt::tx(r.name)
                               : QCoreApplication::translate("charts", r.name.toUtf8().constData());
      items << style::ListItem{"<b>" + fmt::esc(fmt::ticker(r.symbol)) + "</b> <span class=\"muted\">" + fmt::esc(name) +
                                   "</span>",
                               fmt::tx(r.type) + " · " + r.exchange, r.symbol};
    }
    if (items.isEmpty() && !qp->text().trimmed().isEmpty())
      items << style::ListItem{QCoreApplication::translate("charts", "Open <b>%1</b>").arg(fmt::esc(qp->text().trimmed().toUpper())), QCoreApplication::translate("charts", "press Enter"),
                               QStringLiteral("\x01raw")};
    lp->setItems(items);
    lp->setSelected(st->results.isEmpty() ? -1 : st->sel);
  };
  auto run = [st, qp, draw] {
    if (!qp) return;
    const QString val = qp->text().trimmed();
    if (val.isEmpty()) { st->results.clear(); st->query.clear(); draw(); return; }
    data::search(val, qp, [st, qp, val, draw](const data::Result<QList<data::SearchHit>>& r) {
      // a late answer to an older query (typed "A", then "APL") must not replace the results of the current one
      if (!qp || qp->text().trimmed() != val) return;
      st->results = r.ok() ? r.value : QList<data::SearchHit>{};
      st->query = val;
      st->sel = 0;
      draw();
    });
  };
  connect(&st->timer, &QTimer::timeout, q, run);
  connect(q, &QLineEdit::textEdited, q, [st] { st->timer.start(); });
  connect(q, &QLineEdit::returnPressed, q, [st, qp, pick] {
    if (!qp) return;
    // the results only count when they answer what the field holds now (else: the typed text as it is)
    const QString val = qp->text().trimmed();
    pick(st->query == val && st->sel >= 0 && st->sel < st->results.size() ? st->results[st->sel].symbol : val);
  });
  connect(list, &style::ItemList::activated, list, [st, qp, lp, pick](int i) {
    if (!qp || !lp) return;
    const QString data = lp->items().value(i).data;
    pick(data == QStringLiteral("\x01raw") ? qp->text().trimmed() : data);
  });
  // ArrowDown / ArrowUp
  class Keys : public QObject {
  public:
    Keys(QObject* p, std::function<void(int)> f) : QObject(p), f(std::move(f)) {}
    std::function<void(int)> f;
    bool eventFilter(QObject*, QEvent* e) override {
      if (e->type() != QEvent::KeyPress) return false;
      auto* k = static_cast<QKeyEvent*>(e);
      if (k->key() == Qt::Key_Down) { f(1); return true; }
      if (k->key() == Qt::Key_Up) { f(-1); return true; }
      return false;
    }
  };
  q->installEventFilter(new Keys(q, [st, draw](int dir) {
    st->sel = dir > 0 ? std::min(st->sel + 1, int(st->results.size()) - 1) : std::max(st->sel - 1, 0);
    draw();
  }));
  q->setFocus();
  q->setCursorPosition(q->text().size());
  if (!initial.isEmpty()) run();
}

// ---- indicator picker
void ChartsApp::openIndicatorPicker() {
  auto* body = new QWidget;
  auto* v = new QVBoxLayout(body);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(0);
  auto* q = new QLineEdit(body);
  q->setProperty("qtc", "search-in");
  q->setFixedHeight(38);
  q->setPlaceholderText(QCoreApplication::translate("charts", "Search %1 indicators…").arg(ind::all().size()));
  v->addWidget(q);
  v->addSpacing(8);
  auto* list = new style::ItemList(body);
  v->addWidget(list);
  auto* hintL = style::hint(QCoreApplication::translate("charts", "No limit per chart. Write your own in the Indicator editor tab."));
  auto* newBtn = style::btn(QCoreApplication::translate("charts", "New custom indicator"));
  modal(QCoreApplication::translate("charts", "Indicators"), body, {hintL, newBtn});

  QPointer<QLineEdit> qp(q);
  QPointer<style::ItemList> lp(list);
  auto draw = [qp, lp] {
    if (!qp || !lp) return;
    const QString term = qp->text().trimmed().toLower();
    QStringList order;
    QHash<QString, QList<const ind::Def*>> groups;
    // a local copy: groups keeps pointers into it until the end of this function (all() returns by value)
    const QVector<ind::Def> defs = ind::all();
    for (const auto& def : defs) {
      if (!term.isEmpty() && !(def.name + " " + def.id + " " + def.group + " " + fmt::tx(def.name) + " " + fmt::tx(def.group))
                                  .toLower()
                                  .contains(term))
        continue;   // the English name or the shown (translated) one
      const QString g = def.group.isEmpty() ? QString::fromLatin1(QT_TRANSLATE_NOOP("charts", "Other")) : def.group;
      if (!groups.contains(g)) order << g;
      groups[g] << &def;
    }
    std::stable_sort(order.begin(), order.end(), [](const QString& a, const QString& b) {
      return (a == "Quant (bonus)") > (b == "Quant (bonus)");
    });
    QList<style::ListItem> items;
    for (const QString& g : order) {
      style::ListItem h;
      h.left = fmt::tx(g);
      h.group = true;
      h.gold = g == "Quant (bonus)";
      items << h;
      for (const auto* def : groups[g])
        items << style::ListItem{fmt::esc(fmt::tx(def->name)), def->overlay ? QCoreApplication::translate("charts", "on price") : QCoreApplication::translate("charts", "own pane"), def->id};
    }
    lp->setItems(items);
  };
  connect(q, &QLineEdit::textEdited, q, draw);
  connect(list, &style::ItemList::activated, list, [this, lp](int i) {
    if (!lp || !active) return;
    const QString id = lp->items().value(i).data;
    const ind::DefPtr def = ind::byId(id);
    if (!def) return;
    const QString uid = active->addIndicator(id);
    QString anchorKey;
    for (const auto& in : def->inputs) if (in.type == "time") { anchorKey = in.key; break; }
    if (!anchorKey.isEmpty()) {
      closeModal();
      pickAnchor(active, uid, anchorKey);
    } else {
      toast(QCoreApplication::translate("charts", "Added %1").arg(fmt::esc(fmt::tx(def->name))));
    }
  });
  connect(newBtn, &QPushButton::clicked, this, [this] {
    closeModal();
    if (panels) panels->showBottom("editor");
  });
  draw();
  q->setFocus();
}

void ChartsApp::pickAnchor(ChartPane* pane, const QString& uid, const QString& key) {
  pendingAnchor = PendingAnchor{pane, uid, key};
  setTool("anchor-pick");
  toast(QCoreApplication::translate("charts", "Click the bar to anchor it to."));
}

// ---- indicator settings
void ChartsApp::openIndicatorSettings(ChartPane* pane, const QString& uid) {
  if (!pane) return;
  QJsonArray inds = pane->cfg.value("indicators").toArray();
  int idx = -1;
  for (int i = 0; i < inds.size(); ++i) if (inds[i].toObject().value("uid").toString() == uid) { idx = i; break; }
  if (idx < 0) return;
  const QJsonObject ind = inds[idx].toObject();
  const ind::DefPtr def = ind::byId(ind.value("id").toString());
  if (!def) return;
  const QJsonObject params = ind.value("params").toObject(), colors = ind.value("colors").toObject();

  auto* body = new QWidget;
  auto* v = new QVBoxLayout(body);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(0);
  struct Getter { QString key; std::function<QJsonValue()> get; };
  auto getters = std::make_shared<QList<Getter>>();
  auto pickKeys = std::make_shared<QList<QPair<QPushButton*, QString>>>();
  QList<QWidget*> fields;
  static const QRegularExpression numRe("^-?(\\d+\\.?\\d*|\\.\\d+)?([eE][-+]?\\d*)?$");
  for (const auto& in : def->inputs) {
    const QJsonValue val = params.contains(in.key) && !params.value(in.key).isNull() ? params.value(in.key) : in.def;
    const QString label = fmt::tx(in.label);
    if (in.type == "source") {
      // Indicators on indicators: any output of another indicator on this chart can be the source.
      auto* sel = style::select();
      auto* model = new QStandardItemModel(sel);
      auto header = [&](const QString& t) {
        auto* it = new QStandardItem(t);
        it->setFlags(Qt::NoItemFlags);
        QFont f = style::font(13, QFont::Bold);
        it->setFont(f);
        model->appendRow(it);
      };
      int selRow = -1;
      header(QCoreApplication::translate("charts", "Price"));
      for (const char* s : {"close", "open", "high", "low", "hl2", "hlc3", "ohlc4", "hlcc4"}) {
        auto* it = new QStandardItem(QString::fromLatin1(s));
        it->setData(QString::fromLatin1(s), Qt::UserRole);
        model->appendRow(it);
        if (val.isString() && val.toString() == s && selRow < 0) selRow = model->rowCount() - 1;
      }
      QList<QPair<QString, QString>> others;
      for (const auto& xv : pane->cfg.value("indicators").toArray()) {
        const QJsonObject x = xv.toObject();
        const ind::DefPtr xd = ind::byId(x.value("id").toString());
        if (x.value("uid").toString() == ind.value("uid").toString()) continue;
        if (jsString(x.value("params").toObject().value("source")).contains(ind.value("uid").toString()) &&
            x.value("params").toObject().contains("source"))
          continue;
        if (!xd || xd->outputs.isEmpty() || xd->extra.value("drawnOnly").toBool()) continue;   // drawnOnly: no values
        static const QRegularExpression paren("\\(([^)]+)\\)$");
        const QString xname = fmt::tx(xd->name);
        const auto m = paren.match(xname);
        QString shortName = m.hasMatch() ? m.captured(1).split(" / ").value(0) : QString();
        if (shortName.isEmpty()) shortName = xname;
        for (const auto& o : xd->outputs)
          others << qMakePair("ind:" + x.value("uid").toString() + ":" + o.key,
                              shortName + " " + pane->indicatorParamsLabel(x) + ": " + fmt::tx(o.label));
      }
      if (!others.isEmpty()) {
        header(QCoreApplication::translate("charts", "Another indicator"));
        for (const auto& [value, text] : others) {
          auto* it = new QStandardItem(text);
          it->setData(value, Qt::UserRole);
          model->appendRow(it);
          if (val.isString() && val.toString() == value && selRow < 0) selRow = model->rowCount() - 1;
        }
      }
      sel->setModel(model);
      sel->setCurrentIndex(selRow >= 0 ? selRow : 1);
      fields << style::field(label, sel);
      QPointer<QComboBox> sp(sel);
      getters->append({in.key, [sp] { return sp ? QJsonValue(sp->currentData(Qt::UserRole).toString()) : QJsonValue(); }});
    } else if (in.type == "select") {
      auto* sel = style::select();
      int selRow = 0;
      for (int i = 0; i < in.options.size(); ++i) {
        sel->addItem(fmt::tx(in.options[i]), in.options[i]);   // shown translated, saved as is
        if (val.isString() && val.toString() == in.options[i]) selRow = i;
      }
      sel->setCurrentIndex(selRow);
      fields << style::field(label, sel);
      QPointer<QComboBox> sp(sel);
      getters->append({in.key, [sp] { return sp ? QJsonValue(sp->currentData().toString()) : QJsonValue(); }});
    } else if (in.type == "time") {
      auto* dt = new QDateTimeEdit;
      dt->setProperty("qtc", "in");
      dt->setFixedHeight(28);
      const bool fr = i18n::current() == i18n::Lang::French;
      dt->setDisplayFormat(fr ? "dd/MM/yyyy HH:mm" : "MM/dd/yyyy, hh:mm AP");
      dt->setCalendarPopup(true);
      const QDateTime minDt = QDateTime::fromSecsSinceEpoch(0);
      dt->setMinimumDateTime(minDt);
      dt->setSpecialValueText(fr ? QStringLiteral("jj/mm/aaaa --:--") : QStringLiteral("mm/dd/yyyy, --:-- --"));
      const double secs = val.toDouble();
      dt->setDateTime(secs ? QDateTime::fromSecsSinceEpoch(qint64(secs)) : minDt);
      auto* pick = style::btn(QCoreApplication::translate("charts", "Pick on chart"), "small");
      fields << style::field(label, QList<QWidget*>{dt, pick});
      pickKeys->append({pick, in.key});
      QPointer<QDateTimeEdit> dp(dt);
      getters->append({in.key, [dp, minDt] {
                         if (!dp || dp->dateTime() == minDt) return QJsonValue(0);
                         QDateTime t = dp->dateTime();
                         t.setTime(QTime(t.time().hour(), t.time().minute()));
                         return QJsonValue(double(t.toSecsSinceEpoch()));
                       }});
    } else {
      auto* e = style::input();
      e->setValidator(new QRegularExpressionValidator(numRe, e));
      e->setText(jsString(val));
      fields << style::field(label, e);
      QPointer<QLineEdit> ep(e);
      getters->append({in.key, [ep] {
                         if (!ep) return QJsonValue();
                         const QString t = ep->text().trimmed();
                         if (t.isEmpty()) return QJsonValue(0);   // Number("") = 0
                         bool ok = false;
                         const double d = t.toDouble(&ok);
                         return ok ? QJsonValue(d) : QJsonValue(QJsonValue::Null);   // NaN → null in JSON
                       }});
    }
  }
  // Timeframe
  // parented to the dialog's body: a drawn-only indicator never puts it in a layout, and a parentless combo box leaked
  // at every opening of its settings
  auto* tfSel = style::select(body);
  for (const char* t : {"", "5m", "15m", "1h", "4h", "1D", "1W", "1M"}) {
    const QString s = QString::fromLatin1(t);
    tfSel->addItem(s.isEmpty() ? QCoreApplication::translate("charts", "Same as chart (%1)").arg(pane->cfg.value("tf").toString()) : s, s);
  }
  tfSel->setCurrentIndex(std::max(0, tfSel->findData(ind.value("tf").toString())));
  if (fields.isEmpty()) fields << style::hint(QCoreApplication::translate("charts", "No inputs."));
  const bool drawnOnly = def->extra.value("drawnOnly").toBool();   // drawn from trades, not bars: no timeframe of its own
  if (!drawnOnly) fields << style::field(QCoreApplication::translate("charts", "Timeframe"), tfSel);
  else tfSel->hide();
  v->addWidget(style::formGrid(fields));
  if (!drawnOnly) {
    auto* p = style::hint(QCoreApplication::translate("charts", "A higher timeframe shows its value once each of its bars has closed, so history never peeks "
                             "ahead."));
    p->setContentsMargins(0, 13, 0, 13);   // <p> margins
    v->addWidget(p);
  }
  QList<QPair<QString, ColorField*>> colorFields;
  if (!def->outputs.isEmpty()) {
    auto* h = style::h4(QCoreApplication::translate("charts", "Colours"));
    v->addWidget(h);
    QList<QWidget*> cf;
    for (const auto& o : def->outputs) {
      QString c = (colors.value(o.key).toString().isEmpty() ? o.color : colors.value(o.key).toString()).left(7);
      QColor qc(c);
      if (c.isEmpty() && o.extra.contains("themeColor"))   // a theme colour until the user picks one
        qc = o.extra.value("themeColor").toString() == "down" ? style::v().down : style::v().up;
      else if (!c.startsWith('#') || !qc.isValid()) qc = QColor(0, 0, 0);   // <input type=color> with an invalid value
      auto* f = new ColorField(qc);
      colorFields << qMakePair(o.key, f);
      cf << style::field(fmt::tx(o.label), f);
    }
    v->addWidget(style::formGrid(cf));
  }
  auto* reset = style::btn(QCoreApplication::translate("charts", "Defaults"));
  auto* ok = style::btn(QCoreApplication::translate("charts", "Apply"), "primary");
  modal(fmt::tx(def->name), body, {reset, ok});

  QPointer<ChartPane> pp(pane);
  QList<QPair<QString, QPointer<ColorField>>> cfs;
  for (const auto& [k, f] : colorFields) cfs << qMakePair(k, QPointer<ColorField>(f));
  QPointer<QComboBox> tfp(tfSel);
  auto writeBack = [pp, uid](const std::function<void(QJsonObject&)>& edit) {
    if (!pp) return;
    QJsonArray a = pp->cfg.value("indicators").toArray();
    for (int i = 0; i < a.size(); ++i) {
      QJsonObject o = a[i].toObject();
      if (o.value("uid").toString() != uid) continue;
      edit(o);
      a[i] = o;
    }
    pp->cfg["indicators"] = a;
  };
  const QString idOf = def->id;
  connect(ok, &QPushButton::clicked, this, [this, getters, cfs, tfp, writeBack, pp, idOf] {
    QJsonObject np, nc;
    for (const auto& g : *getters) np[g.key] = g.get();
    for (const auto& [k, f] : cfs) {
      if (!f) continue;
      // a colour left at the theme's own (outputs drawn in theme colours) stays unset, so a theme change follows
      const ind::DefPtr dd = pp ? ind::byId(idOf) : nullptr;
      const ind::Output* out = nullptr;
      if (dd) for (const auto& o : dd->outputs) if (o.key == k) out = &o;
      if (out && out->extra.contains("themeColor") &&
          f->color == (out->extra.value("themeColor").toString() == "down" ? style::v().down : style::v().up))
        continue;
      nc[k] = f->color.name();
    }
    const QString tf = tfp ? tfp->currentData().toString() : QString();
    writeBack([&](QJsonObject& o) {
      o["params"] = np;
      o["colors"] = nc;
      if (tf.isEmpty()) o.remove("tf");
      else o["tf"] = tf;
    });
    closeModal();
    if (pp) pp->render(false);
    persist();
  });
  for (const auto& [b, key] : *pickKeys) {
    const QString k = key;
    connect(b, &QPushButton::clicked, this, [this, pp, uid, k] {
      closeModal();
      if (pp) pickAnchor(pp, uid, k);
    });
  }
  connect(reset, &QPushButton::clicked, this, [this, writeBack, pp] {
    writeBack([](QJsonObject& o) {
      o["params"] = QJsonObject();
      o["colors"] = QJsonObject();
    });
    closeModal();
    if (pp) pp->render(false);
    persist();
  });
}

// ---- settings
void ChartsApp::openSettings() {
  const QJsonObject& s = settings;
  auto* body = new QWidget;
  auto* v = new QVBoxLayout(body);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(0);
  auto* bars = style::input();
  bars->setValidator(new QRegularExpressionValidator(QRegularExpression("^\\d{0,6}$"), bars));
  bars->setText(jsString(s.value("bars")));
  auto pair = [](const char* a, const char* b, bool second) {
    auto* c = style::select();
    c->addItem(QCoreApplication::translate("charts", a), "0");
    c->addItem(QCoreApplication::translate("charts", b), "1");
    c->setCurrentIndex(second ? 1 : 0);
    return c;
  };
  auto* utc = pair(QT_TRANSLATE_NOOP("charts", "My local time"), QT_TRANSLATE_NOOP("charts", "UTC"), s.value("utc").toBool());
  auto* vol = new QComboBox;
  vol->setProperty("qtc", "in");
  vol->setFixedHeight(28);
  vol->addItem(QCoreApplication::translate("charts", "Show"), "1");
  vol->addItem(QCoreApplication::translate("charts", "Hide"), "0");
  vol->setCurrentIndex(s.value("volume").toBool() ? 0 : 1);
  auto* stay = pair(QT_TRANSLATE_NOOP("charts", "Back to cursor"), QT_TRANSLATE_NOOP("charts", "Keep the tool"), s.value("stayInDrawMode").toBool());
  auto* sync = pair(QT_TRANSLATE_NOOP("charts", "Independent"), QT_TRANSLATE_NOOP("charts", "Synced"), s.value("syncCrosshair").toBool());
  auto* countdown = pair(QT_TRANSLATE_NOOP("charts", "Hide"), QT_TRANSLATE_NOOP("charts", "Show"), s.value("countdown").toBool());
  v->addWidget(style::formGrid({style::field(QCoreApplication::translate("charts", "Bars to load per chart"), bars), style::field(QCoreApplication::translate("charts", "Time axis"), utc),
                                style::field(QCoreApplication::translate("charts", "Volume on the price chart"), vol),
                                style::field(QCoreApplication::translate("charts", "After placing a drawing"), stay),
                                style::field(QCoreApplication::translate("charts", "Crosshair across charts"), sync),
                                style::field(QCoreApplication::translate("charts", "Countdown to bar close"), countdown)}));

  // ---- chart style (TradingView's chart settings › Symbol and Canvas): saved in settings.chartStyle, only what
  // differs from the theme, so the light / dark switch still recolours what was left alone
  const auto& th = style::v();
  const QJsonObject cs = s.value("chartStyle").toObject();
  auto stored = [&cs](const char* k, const QColor& def) { const QColor c(cs.value(QLatin1String(k)).toString()); return c.isValid() ? c : def; };
  auto flag = [&cs](const char* k, bool def) { const QJsonValue x = cs.value(QLatin1String(k)); return x.isBool() ? x.toBool() : def; };
  const QColor upBody = stored("upBody", th.up), downBody = stored("downBody", th.down);
  struct Col { const char* key; ColorField* f; QColor def; };
  auto mk = [](const QColor& c) { auto* f = new ColorField(c); f->setFixedWidth(64); return f; };
  QList<Col> cols{{"upBody", mk(upBody), th.up}, {"downBody", mk(downBody), th.down},
                  {"upBorder", mk(stored("upBorder", upBody)), upBody}, {"downBorder", mk(stored("downBorder", downBody)), downBody},
                  {"upWick", mk(stored("upWick", upBody)), upBody}, {"downWick", mk(stored("downWick", downBody)), downBody},
                  {"background", mk(stored("background", th.bg)), th.bg}, {"gridV", mk(stored("gridV", th.grid)), th.grid},
                  {"gridH", mk(stored("gridH", th.grid)), th.grid}, {"crosshair", mk(stored("crosshair", QColor("#9598A1"))), QColor("#9598A1")},
                  {"scaleText", mk(stored("scaleText", th.text)), th.text}};
  auto col = [&cols](const char* k) { for (const auto& c : cols) if (qstrcmp(c.key, k) == 0) return c.f; return static_cast<ColorField*>(nullptr); };
  for (const auto& c : cols) c.f->setToolTip(QCoreApplication::translate("charts", "Click to choose a colour"));
  col("upBody")->setToolTip(QCoreApplication::translate("charts", "Up")); col("downBody")->setToolTip(QCoreApplication::translate("charts", "Down"));
  struct Flag { const char* key; QCheckBox* box; bool def; };
  auto check = [](bool on) { auto* b = style::tbCheck(QString()); b->setChecked(on); return b; };
  QList<Flag> flags{{"borders", check(flag("borders", false)), false}, {"wicks", check(flag("wicks", true)), true},
                    {"gridVVisible", check(flag("gridVVisible", true)), true}, {"gridHVisible", check(flag("gridHVisible", true)), true}};
  auto box = [&flags](const char* k) { for (const auto& f : flags) if (qstrcmp(f.key, k) == 0) return f.box; return static_cast<QCheckBox*>(nullptr); };
  auto* fontSize = new QComboBox;
  fontSize->setProperty("qtc", "in");
  fontSize->setFixedHeight(28);
  for (int n = 9; n <= 16; ++n) fontSize->addItem(QString::number(n), n);
  fontSize->setCurrentIndex(qBound(9, cs.value("fontSize").toInt(11), 16) - 9);
  // one line per field: the check box and the swatches side by side (style::field stacks a list of controls)
  auto row = [](const QList<QWidget*>& ws) {
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(8);
    for (QWidget* x : ws) h->addWidget(x);
    h->addStretch(1);
    return w;
  };
  fontSize->setFixedWidth(72);
  auto* styleTitle = style::h4(QCoreApplication::translate("charts", "Chart style"));
  styleTitle->setContentsMargins(0, 16, 0, 8);
  v->addWidget(styleTitle);
  v->addWidget(style::formGrid({
      style::field(QCoreApplication::translate("charts", "Candle body (up, down)"), row({col("upBody"), col("downBody")})),
      style::field(QCoreApplication::translate("charts", "Candle borders"), row({box("borders"), col("upBorder"), col("downBorder")})),
      style::field(QCoreApplication::translate("charts", "Candle wicks"), row({box("wicks"), col("upWick"), col("downWick")})),
      style::field(QCoreApplication::translate("charts", "Background"), row({col("background")})),
      style::field(QCoreApplication::translate("charts", "Vertical grid lines"), row({box("gridVVisible"), col("gridV")})),
      style::field(QCoreApplication::translate("charts", "Horizontal grid lines"), row({box("gridHVisible"), col("gridH")})),
      style::field(QCoreApplication::translate("charts", "Crosshair"), row({col("crosshair")})),
      style::field(QCoreApplication::translate("charts", "Scale text (colour, size)"), row({col("scaleText"), fontSize}))}));
  // The JS says "lives in the data folder next to the app": here the data lives in Deepwater's data folder.
  auto* p = style::hint(QCoreApplication::translate("charts", "Everything you save lives in the <code>charts</code> folder of Deepwater's data folder "
                           "(<code>%1</code>), on your machine.")
                            .arg(fmt::esc(QDir::toNativeSeparators(store::dataDir()))));
  p->setContentsMargins(0, 13, 0, 13);
  p->setTextInteractionFlags(Qt::TextSelectableByMouse);
  v->addWidget(p);
  auto* reset = style::btn(QCoreApplication::translate("charts", "Reset chart style"));
  reset->setToolTip(QCoreApplication::translate("charts", "Back to the theme's colours and size"));
  auto* ok = style::btn(QCoreApplication::translate("charts", "Save"), "primary");
  modal(QCoreApplication::translate("charts", "Settings"), body, {reset, ok}, true);
  QPointer<QLineEdit> bp(bars);
  QPointer<QComboBox> up(utc), vp(vol), sp(stay), cp(sync), cdp(countdown), fsp(fontSize);
  // the style controls live as long as the modal's body; captured by value as plain pointers guarded by `bp`
  QList<QPair<QString, QPair<ColorField*, QColor>>> colorKeys;
  for (const auto& c : cols) colorKeys.append({QString::fromLatin1(c.key), {c.f, c.def}});
  QList<QPair<QString, QPair<QCheckBox*, bool>>> flagKeys;
  for (const auto& f : flags) flagKeys.append({QString::fromLatin1(f.key), {f.box, f.def}});
  auto applyStyle = [this] {
    style::setLight(settings.value("theme").toString() == "light");
    for (auto* pane : panes) pane->applyTheme();
  };
  connect(reset, &QPushButton::clicked, this, [this, applyStyle] {
    settings.remove("chartStyle");
    persist();
    applyStyle();
    closeModal();
    QTimer::singleShot(0, this, [this] { openSettings(); });   // the dialog again, showing the theme's values
  });
  connect(ok, &QPushButton::clicked, this, [this, bp, up, vp, sp, cp, cdp, fsp, colorKeys, flagKeys, applyStyle] {
    if (!bp) return;
    double n = bp->text().trimmed().isEmpty() ? 0 : bp->text().toDouble();
    if (!n) n = 3000;
    const bool reload = settings.value("bars") != QJsonValue(std::max(200.0, std::min(20000.0, n))) ||
                        settings.value("utc").toBool() != (up->currentData().toString() == "1") ||
                        settings.value("volume").toBool() != (vp->currentData().toString() == "1");
    settings["bars"] = std::max(200.0, std::min(20000.0, n));
    settings["utc"] = up->currentData().toString() == "1";
    settings["volume"] = vp->currentData().toString() == "1";
    settings["stayInDrawMode"] = sp->currentData().toString() == "1";
    settings["syncCrosshair"] = cp->currentData().toString() == "1";
    settings["countdown"] = cdp->currentData().toString() == "1";
    QJsonObject cs;
    // borders and wicks are compared with the body chosen now (their default), the rest with the theme
    QHash<QString, QColor> chosen;
    for (const auto& [k, fc] : colorKeys) chosen.insert(k, fc.first->color);
    for (const auto& [k, fc] : colorKeys) {
      QColor def = fc.second;
      if (k == "upBorder" || k == "upWick") def = chosen.value("upBody");
      if (k == "downBorder" || k == "downWick") def = chosen.value("downBody");
      if (fc.first->color.rgba() != def.rgba()) cs.insert(k, fc.first->color.name(QColor::HexArgb));
    }
    for (const auto& [k, fb] : flagKeys)
      if (fb.first->isChecked() != fb.second) cs.insert(k, fb.first->isChecked());
    if (fsp->currentData().toInt() != 11) cs.insert("fontSize", fsp->currentData().toInt());
    if (cs.isEmpty()) settings.remove("chartStyle"); else settings["chartStyle"] = cs;
    closeModal();
    persist();
    applyStyle();
    if (reload) for (auto* pane : panes) pane->load(true);   // bars, time axis or volume changed: the data again
  });
}

// Footprint settings of the active chart (cfg "footprint", charts::flow::FootprintSettings).
void ChartsApp::openFootprintSettings() {
  ChartPane* pane = active;
  if (!pane) return;
  using FS = ::charts::flow::FootprintSettings;
  const FS s = FS::fromJson(pane->cfg.value("footprint").toObject());
  auto tr = [](const char* t) { return QCoreApplication::translate("charts", t); };
  auto* body = new QWidget;
  auto* v = new QVBoxLayout(body);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(0);
  auto* mode = style::select();
  mode->addItem(tr(QT_TRANSLATE_NOOP("charts", "Bid x Ask")), "bidask");
  mode->addItem(tr(QT_TRANSLATE_NOOP("charts", "Delta")), "delta");
  mode->addItem(tr(QT_TRANSLATE_NOOP("charts", "Volume profile")), "profile");
  mode->setCurrentIndex(std::max(0, mode->findData(FS::modeKey(s.mode))));
  auto* rows = style::select();
  rows->addItem(tr(QT_TRANSLATE_NOOP("charts", "Auto (follows the zoom)")), 0);
  const auto tick = pane->footprintTick();
  for (int k : {1, 2, 4, 5, 10, 20, 25, 50, 100, 200, 500, 1000, 2000, 5000, 10000}) {
    QString label = QCoreApplication::translate("charts", "%n tick(s)", nullptr, k);
    if (tick) label += QStringLiteral(" · ") + fmt::price(*tick * k);
    rows->addItem(label, k);
  }
  rows->setCurrentIndex(std::max(0, rows->findData(int(s.rows))));
  auto number = [](double val) {
    auto* e = style::input();
    e->setValidator(new QRegularExpressionValidator(QRegularExpression("^\\d{0,9}([.,]\\d{0,8})?$"), e));
    e->setText(QString::number(val, 'g', 10));
    return e;
  };
  auto* ratio = number(s.ratio);
  auto* minVol = number(s.minVolume);
  auto* stack = style::select();
  for (int k = 2; k <= 8; ++k) stack->addItem(QString::number(k), k);
  stack->setCurrentIndex(std::max(0, stack->findData(s.stackMin)));
  auto showHide = [&](bool on) {
    auto* c = style::select();
    c->addItem(tr(QT_TRANSLATE_NOOP("charts", "Show")), true);
    c->addItem(tr(QT_TRANSLATE_NOOP("charts", "Hide")), false);
    c->setCurrentIndex(on ? 0 : 1);
    return c;
  };
  auto* poc = showHide(s.showPoc);
  auto* totals = showHide(s.showTotals);
  const auto& sv = style::v();
  auto* buyC = new ColorField(s.buyColor.isEmpty() ? sv.up : QColor(s.buyColor));
  auto* sellC = new ColorField(s.sellColor.isEmpty() ? sv.down : QColor(s.sellColor));
  auto* pocC = new ColorField(s.pocColor.isEmpty() ? sv.warn : QColor(s.pocColor));
  v->addWidget(style::formGrid({style::field(tr(QT_TRANSLATE_NOOP("charts", "Display")), mode),
                                style::field(tr(QT_TRANSLATE_NOOP("charts", "Price rows")), rows),
                                style::field(tr(QT_TRANSLATE_NOOP("charts", "Imbalance ratio")), ratio),
                                style::field(tr(QT_TRANSLATE_NOOP("charts", "Minimum volume for an imbalance")), minVol),
                                style::field(tr(QT_TRANSLATE_NOOP("charts", "Stacked imbalances: rows in a row")), stack),
                                style::field(tr(QT_TRANSLATE_NOOP("charts", "Point of control (POC)")), poc),
                                style::field(tr(QT_TRANSLATE_NOOP("charts", "Delta and volume under the candle")), totals),
                                style::field(tr(QT_TRANSLATE_NOOP("charts", "Colours: buyers, sellers, POC")), QList<QWidget*>{buyC, sellC, pocC})}));
  auto* h = style::hint(tr(QT_TRANSLATE_NOOP("charts",
      "A buy imbalance at a price: buyers there are at least <i>ratio</i> times the sellers one row below "
      "(sell imbalance: sellers at least <i>ratio</i> times the buyers one row above). Rows with less than the minimum "
      "volume never count. The candle's lowest row has no buy imbalance and its highest no sell imbalance.")));
  h->setContentsMargins(0, 13, 0, 13);
  v->addWidget(h);
  auto* reset = style::btn(tr(QT_TRANSLATE_NOOP("charts", "Defaults")));
  auto* ok = style::btn(tr(QT_TRANSLATE_NOOP("charts", "Save")), "primary");
  modal(tr(QT_TRANSLATE_NOOP("charts", "Footprint settings")), body, {reset, ok});
  QPointer<ChartPane> pp(pane);
  connect(reset, &QPushButton::clicked, this, [this, pp] {
    if (!pp) return;
    pp->cfg.remove("footprint");
    closeModal();
    pp->render(false);
    persist();
  });
  QPointer<QComboBox> mp(mode), rp(rows), sp(stack), pcp(poc), tp(totals);
  QPointer<QLineEdit> ratp(ratio), mvp(minVol);
  QPointer<ColorField> bcp(buyC), scp(sellC), ocp(pocC);
  connect(ok, &QPushButton::clicked, this, [=, this] {
    if (!pp || !mp) return;
    QJsonObject o{{"mode", mp->currentData().toString()}, {"rows", rp->currentData().toInt()},
                  {"ratio", QString(ratp->text()).replace(',', '.')}, {"minVolume", QString(mvp->text()).replace(',', '.')},
                  {"stack", sp->currentData().toInt()}, {"poc", pcp->currentData().toBool()}, {"totals", tp->currentData().toBool()}};
    const auto& now = style::v();
    // a colour left at the theme's own stays "" so the next theme change follows
    if (bcp->color != now.up) o["buyColor"] = bcp->color.name();
    if (scp->color != now.down) o["sellColor"] = scp->color.name();
    if (ocp->color != now.warn) o["pocColor"] = ocp->color.name();
    FS saved = FS::fromJson(o);
    if (ratp->text().trimmed().isEmpty()) saved.ratio = 3;
    pp->cfg["footprint"] = saved.toJson();
    closeModal();
    pp->render(false);
    persist();
  });
}

// =================================================================================================== crosshair, csv
void ChartsApp::syncCrosshair(ChartPane* src, double logical) {
  if (!src) return;
  const auto utc = src->timeOfLogical(logical);
  if (!utc) return;
  for (auto* pane : panes) {
    if (pane == src || pane->shown.isEmpty()) continue;
    const auto l = pane->logicalOf(*utc);
    if (!l || !std::isfinite(*l)) continue;
    // a position on that chart's time scale; setCrosshairAtLogical finds the bar (its scale may hold other times)
    pane->setCrosshairAtLogical(*l);
  }
}

void ChartsApp::exportCsv() {
  ChartPane* p = active;
  if (!p) return;
  series::CsvColumns extra;
  for (const auto& iv : p->cfg.value("indicators").toArray()) {
    const QJsonObject in = iv.toObject();
    const ind::DefPtr def = ind::byId(in.value("id").toString());
    if (!def) continue;
    const auto it = p->indResults.constFind(in.value("uid").toString());
    if (it == p->indResults.cend()) continue;
    for (const auto& o : def->outputs) {
      if (!it->series.contains(o.key)) continue;
      const QString key = in.value("id").toString() + "_" + o.key;
      bool replaced = false;
      for (auto& [k, s] : extra) if (k == key) { s = it->series.value(o.key); replaced = true; }
      if (!replaced) extra << qMakePair(key, it->series.value(o.key));
    }
  }
  fmt::download(this, fmt::ticker(p->cfg.value("symbol").toString()) + "-" + p->cfg.value("tf").toString() + ".csv",
                series::toCsv(p->shown, extra).toUtf8());
}

// =================================================================================================== keys
void ChartsApp::keyPressEvent(QKeyEvent* e) {
  if (e->key() == Qt::Key_Escape) {
    if (modalOpen()) return closeModal();
    if (tool != "cursor") return setTool("cursor");
    if (active && active->selectedDrawing()) active->clearSelection();
    return;
  }
  if (e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier)) return QWidget::keyPressEvent(e);
  if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && active && active->deleteSelectedDrawing()) {
    e->accept();
    return;
  }
  if (modalOpen()) return QWidget::keyPressEvent(e);
  static const QRegularExpression letter("^[a-z0-9^]$", QRegularExpression::CaseInsensitiveOption);
  const QString t = e->text();
  if (letter.match(t).hasMatch()) {
    e->accept();
    openSymbolSearch(t.toUpper());
    return;
  }
  QWidget::keyPressEvent(e);
}

// =================================================================================================== drawings → alerts
// A drawn line as an alert condition: trend lines and rays are checked at the line's price on each bar, in the data
// layer, so they fire with this tab closed.
QJsonObject ChartsApp::lineOperandFor(const QJsonObject& dr) const {
  const QJsonArray pts = dr.value("points").toArray();
  const QJsonObject a = pts.at(0).toObject(), b = pts.at(1).toObject();
  auto line = [&](const QString& extend, const QString& label) {
    return QJsonObject{{"kind", "line"}, {"label", label},
                       {"line", QJsonObject{{"t1", a.value("time")}, {"p1", a.value("price")}, {"t2", b.value("time")},
                                            {"p2", b.value("price")}, {"extend", extend}}}};
  };
  const QString type = dr.value("type").toString();
  if (type == "hline")
    return QJsonObject{{"kind", "value"}, {"v", QString::number(a.value("price").toDouble(), 'g', 8).toDouble()}};
  if (type == "hray")
    return QJsonObject{{"kind", "line"}, {"label", QT_TRANSLATE_NOOP("charts", "Horizontal ray")},
                       {"line", QJsonObject{{"t1", a.value("time")}, {"p1", a.value("price")},
                                            {"t2", a.value("time").toDouble() + 1}, {"p2", a.value("price")},
                                            {"extend", "right"}}}};
  if (type == "trend") return line("none", QT_TRANSLATE_NOOP("charts", "Trend line (between its two points)"));
  if (type == "ray") return line("right", QT_TRANSLATE_NOOP("charts", "Ray"));
  if (type == "extended") return line("both", QT_TRANSLATE_NOOP("charts", "Extended line"));
  if (type == "channel") return line("none", QT_TRANSLATE_NOOP("charts", "Channel's first line"));
  return {};
}

void ChartsApp::alertOnSelectedDrawing() {
  const auto dr = active ? active->selectedDrawing() : std::nullopt;
  if (!dr) return toast(QCoreApplication::translate("charts", "Click a line on the chart first to select it."));
  const QJsonObject right = lineOperandFor(*dr);
  if (right.isEmpty()) return toast(QCoreApplication::translate("charts", "Alerts work on trend lines, rays, extended and horizontal lines."));
  if (panels)
    panels->openAlertDialog(QJsonObject{
        {"rules", QJsonArray{QJsonObject{{"left", QJsonObject{{"kind", "price"}, {"field", "close"}}},
                                         {"op", "crosses"},
                                         {"right", right}}}}});
}

// =================================================================================================== replay
void ChartsApp::syncReplayFrom(ChartPane* pane, const QJsonObject& bar) {
  if (!pane || bar.isEmpty()) return;
  double secs = 0;
  try { secs = series::parseTf(pane->cfg.value("tf").toString()).seconds; } catch (...) { return; }
  const qint64 end = qint64(bar.value("time").toDouble() + secs);
  for (auto* p : panes)
    if (p != pane && !p->bars.isEmpty() && !(p->replay && !p->replay->follower)) p->replayTo(end);
}
void ChartsApp::onReplayStart(ChartPane* pane) {
  if (settings.value("syncReplay").toBool() && pane->replay && !pane->replay->follower &&
      pane->replay->index >= 0 && pane->replay->index < pane->bars.size())
    syncReplayFrom(pane, barJson(pane->bars[pane->replay->index]));
  syncToolbar();
  if (panels) panels->onReplayChange();
}
void ChartsApp::onReplayBar(ChartPane* pane, const QJsonObject& bar) {
  if (settings.value("syncReplay").toBool() && !(pane->replay && pane->replay->follower)) syncReplayFrom(pane, bar);
  if (panels) panels->onReplayBar(pane, bar);
}
void ChartsApp::onReplayStop(ChartPane* pane) {
  for (auto* p : panes) if (p != pane && p->replay && p->replay->follower) p->stopReplay();
  syncToolbar();
  if (panels) panels->onReplayChange();
}

void ChartsApp::applyExternalState(const QJsonObject& st, const QJsonObject& patch) {
  bool rebuild = false, drawings = false, themeChange = false, reload = false;
  for (auto it = patch.begin(); it != patch.end(); ++it) {
    const QString k = it.key();
    if (k == "settings") {
      const QJsonObject old = settings;
      const QJsonObject ns = st.value("settings").toObject();
      for (auto s2 = ns.begin(); s2 != ns.end(); ++s2) settings.insert(s2.key(), s2.value());
      state["settings"] = settings;
      themeChange = old.value("theme") != settings.value("theme");
      // JS openSettings reloads every pane (keepView) after a bars / utc / volume change
      for (const char* key : {"bars", "utc", "volume"})
        if (old.value(key) != settings.value(key)) reload = true;
      style::setLight(settings.value("theme").toString() == "light");
      d->restyle();
      { const QSignalBlocker block(d->syncSymbol); d->syncSymbol->setChecked(settings.value("syncSymbol").toBool()); }
      continue;
    }
    state[k] = st.value(k);
    if (k == "layout" || k == "panes" || k == "active") rebuild = true;
    if (k == "drawings") drawings = true;
  }
  if (rebuild) {
    // a reload builds the panes from state.panes again
    while (!panes.isEmpty()) delete panes.takeLast();
    active = nullptr;
    setLayout(jsString(state.value("layout")), false);
  } else {
    if (drawings)
      for (auto* p : panes) p->setDrawingItems(getDrawings(p->cfg.value("symbol").toString()));
    // what the topbar light/dark button does (the rebuilt panes above are themed when they are created)
    if (themeChange)
      for (auto* p : panes) p->applyTheme();
    if (reload)
      for (auto* p : panes) p->load(true);
  }
  if (themeChange) {
    if (panels) panels->onTheme();
    emit themeChanged();
  }
  syncToolbar();
  if (panels) {
    panels->onActiveChange();
#ifdef CHARTS_HAS_SIDE_STEPS
    if (panels->sidePanel) panels->sidePanel->renderSide();
#endif
#ifdef CHARTS_HAS_BOTTOM
    if (panels->bottomPanel) panels->bottomPanel->renderBottom();
#endif
  }
}

// =================================================================================================== prices, alerts, status
void ChartsApp::onPrice(const QString& symbol, double price) {
  prices[symbol] = price;
  emit priceChanged(symbol, price);
}

void ChartsApp::onAlertEvent(const QJsonObject& e) {
  const QString text = fmt::tx(e.value("text").toString());   // built in English by charts/data/Alerts
  toast(QCoreApplication::translate("charts", "<b>Alert</b><br>%1").arg(fmt::esc(text)), "alert", 12000);
  beep();
  systemNotify(text, QCoreApplication::translate("charts", "Deepwater alert"));
  if (panels) panels->onAlertFired(e);
}

void ChartsApp::beep() { QApplication::beep(); }

void ChartsApp::systemNotify(const QString& text, const QString& title) {
#ifdef Q_OS_MACOS
  if (qEnvironmentVariableIsEmpty("DW_PROFILE"))   // never from tests
    dw::Process::startDetached("/usr/bin/osascript",
                            {"-e", "on run argv", "-e", "display notification (item 1 of argv) with title (item 2 of argv)",
                             "-e", "end run", text, title});
#elif defined(Q_OS_IOS)
  // iOS (native parity, 2026-11-22): a local notification, shown as a banner even with Deepwater in front
  if (qEnvironmentVariableIsEmpty("DW_PROFILE")) dw::ios::notify(title, text);
#elif defined(Q_OS_WIN)
  // Windows (native parity, 2026-11-22): a notification from Deepwater's icon in the notification area (made with the
  // first alert, kept while the app runs: Windows 11 shows it as a toast and keeps it in the notification centre)
  if (!qEnvironmentVariableIsEmpty("DW_PROFILE") || !QSystemTrayIcon::isSystemTrayAvailable()) return;   // never from tests
  static QSystemTrayIcon* tray = nullptr;
  if (!tray) {
    QIcon icon = QApplication::windowIcon();
    if (icon.isNull()) icon = QIcon(QStringLiteral(":/icon/deepwater.svg"));
    tray = new QSystemTrayIcon(icon, qApp);
    tray->setToolTip(QStringLiteral("Deepwater"));
    tray->show();
  }
  tray->showMessage(title, text, QSystemTrayIcon::Information, 10000);
#else
  Q_UNUSED(text);
  Q_UNUSED(title);
#endif
}

void ChartsApp::bigTradeAlert(const QString& text, bool visual, bool sound) {
  if (qEnvironmentVariableIsSet("DW_CHARTS_STEPS")) qInfo("big alert: %s", qPrintable(text));   // screenshots and checks
  if (visual) {
    toast(QCoreApplication::translate("charts", "<b>Big trade</b><br>%1").arg(fmt::esc(text)), "alert", 8000);
    systemNotify(text, QCoreApplication::translate("charts", "Deepwater · Big trade"));
  }
  if (sound) beep();
}

void ChartsApp::checkHealth() {
  QStringList failing;
  const auto& h = data::health();
  for (auto it = h.cbegin(); it != h.cend(); ++it) if (!it->ok) failing << it.key();
  setStatus(failing.isEmpty() ? "ok" : "warn",
            failing.isEmpty() ? QCoreApplication::translate("charts", "All data sources responding")
                              : QCoreApplication::translate("charts", "Some sources are failing (%1); fallbacks are in use.").arg(failing.join(", ")));
}
void ChartsApp::setStatus(const QString& level, const QString& title) {
  d->status->setLevel(level);
  if (!title.isEmpty()) d->status->setToolTip(title);
}

void ChartsApp::toast(const QString& html, const QString& kind, int ms) {
  auto* el = new QLabel(html, this);
  el->setProperty("qtc", "toast");
  el->setProperty("kind", kind);
  el->setTextFormat(Qt::RichText);
  el->setWordWrap(true);
  el->setTextInteractionFlags(Qt::TextSelectableByMouse);
  el->ensurePolished();
  auto* sh = new QGraphicsDropShadowEffect(el);
  sh->setBlurRadius(32);
  sh->setOffset(0, 8);
  sh->setColor(style::v().shadow);
  el->setGraphicsEffect(sh);
  // .toast: padding 10px 12px, border 1px + 3px left, max-width 360px. The .toasts column stretches its items, so
  // every toast is as wide as the widest one (placeOverlays).
  QTextDocument doc;
  doc.setDefaultFont(el->font());
  doc.setDocumentMargin(0);
  doc.setHtml(html);
  el->setProperty("naturalW", std::min(360, int(std::ceil(doc.idealWidth())) + 28 + 10));   // + slack: QLabel wraps a bit early
  d->toasts << el;
  el->show();
  d->placeOverlays();
  QPointer<QLabel> p(el);
  QTimer::singleShot(ms, this, [this, p] {
    if (!p) return;
    d->toasts.removeAll(p);
    delete p.data();
    d->placeOverlays();
  });
}

void ChartsApp::Impl::placeOverlays() {
  if (backdrop->isVisible()) { backdrop->setGeometry(q->rect()); q->relayoutModal(); }
  toasts.removeAll(nullptr);
  int w = 0;
  for (QLabel* t : toasts) w = std::max(w, t->property("naturalW").toInt());
  for (QLabel* t : toasts) {
    QTextDocument doc;
    doc.setDefaultFont(t->font());
    doc.setDocumentMargin(0);
    doc.setHtml(t->text());
    doc.setTextWidth(w - 28 - 10);
    t->setFixedSize(w, int(std::ceil(doc.size().height())) + 22);
  }
  int y = q->height() - 14;
  for (int i = int(toasts.size()) - 1; i >= 0; --i) {
    QLabel* t = toasts[i];
    y -= t->height();
    t->move(q->width() - 14 - t->width(), y);
    t->raise();
    y -= 8;
  }
}

// =================================================================================================== events
bool ChartsApp::eventFilter(QObject* o, QEvent* e) {
  if (d->cardBody && (o == d->cardBody || o->parent() == d->cardBody) && e->type() == QEvent::LayoutRequest)
    QTimer::singleShot(0, this, [this] { relayoutModal(); });
  return QWidget::eventFilter(o, e);
}
void ChartsApp::resizeEvent(QResizeEvent* e) {
  QWidget::resizeEvent(e);
  d->sideBox->setVisible(width() > 900);   // @media (max-width: 900px) { .side { display: none } }
  d->placeOverlays();
  if (d->fitTopbar) d->fitTopbar();
}
void ChartsApp::paintEvent(QPaintEvent*) { QPainter(this).fillRect(rect(), style::v().bg); }
void ChartsApp::showEvent(QShowEvent* e) {
  QWidget::showEvent(e);
  if (d->fitTopbar) d->fitTopbar();
  checkHealth();
  d->healthTimer.start();
  if (!modalOpen()) setFocus();
  // Dev hook (screenshots): DW_CHARTS_STEPS="bottom:tester;wait:3000;bt:run" runs these UI steps (runUiStep) once,
  // 3 s after the page is first shown, 500 ms apart; "wait:<ms>" adds a pause.
  static bool stepsRun = false;
  if (!stepsRun && qEnvironmentVariableIsSet("DW_CHARTS_STEPS")) {
    stepsRun = true;
    auto queue = std::make_shared<QStringList>(qEnvironmentVariable("DW_CHARTS_STEPS").split(';', Qt::SkipEmptyParts));
    auto next = std::make_shared<std::function<void()>>();
    QPointer<ChartsApp> self(this);
    *next = [self, queue, next] {
      if (!self || queue->isEmpty()) return;
      const QString s = queue->takeFirst().trimmed();
      int pause = 500;
      if (s.startsWith("wait:")) pause = s.mid(5).toInt();
      else if (!runUiStep(self, s)) qWarning("charts step failed: %s", qPrintable(s));
      QTimer::singleShot(pause, self, *next);
    };
    QTimer::singleShot(3000, this, *next);
  }
}
void ChartsApp::hideEvent(QHideEvent* e) {
  d->healthTimer.stop();
  if (d->saveTimer.isActive()) flushPersist();
  QWidget::hideEvent(e);
}

// =================================================================================================== UI steps
bool registerUiStep(const QString& prefix, UiStep fn) {
  steps().insert(prefix, std::move(fn));
  return true;
}

bool runUiStep(ChartsApp* app, const QString& step) {
  if (!app) return false;
  const QString name = step.section(':', 0, 0), arg = step.section(':', 1);
  if (auto it = steps().constFind(name); it != steps().cend()) return (*it)(app, arg);
  if (name == "side") { if (!app->panels) return false; app->panels->showSide(arg); return true; }
  if (name == "bottom") { if (!app->panels) return false; app->panels->showBottom(arg); return true; }
  if (name == "modal") {
    const QString what = arg.section(':', 0, 0), rest = arg.section(':', 1);
    if (what == "symbol") { app->openSymbolSearch(rest); return true; }
    if (what == "compare") {
      app->openSymbolSearch(rest, [](const QString&) {}, QCoreApplication::translate("charts", "Compare with…"));
      return true;
    }
    if (what == "indicators") { app->openIndicatorPicker(); return true; }
    if (what == "settings") { app->openSettings(); return true; }
    if (what == "footprint") { app->openFootprintSettings(); return true; }
    if (what == "alert") { if (!app->panels) return false; app->panels->openAlertDialog(); return true; }
    if (what == "indsettings") {
      if (!app->active) return false;
      const QJsonArray inds = app->active->cfg.value("indicators").toArray();
      const int n = rest.isEmpty() ? 0 : rest.toInt();
      if (n < 0 || n >= inds.size()) return false;
      app->openIndicatorSettings(app->active, inds[n].toObject().value("uid").toString());
      return true;
    }
    return false;
  }
  if (name == "close") { app->closeModal(); return true; }
  if (name == "layout") { app->setLayout(arg); return true; }
  if (name == "tool") { app->setTool(arg); return true; }
  if (name == "tf") { app->setTf(arg); return true; }
  if (name == "ind") { if (!app->active) return false; app->active->addIndicator(arg); return true; }   // ind:pine:my-script
  if (name == "symbol") { app->setSymbol(arg); return true; }
  if (name == "type") {
    if (!app->active) return false;
    app->active->cfg["type"] = arg;
    app->active->render(true);
    app->syncToolbar();
    return true;
  }
  if (name == "theme") {
    if ((arg == "light") != (app->theme() == "light")) {
      // the topbar's moon button
      for (auto* b : app->findChildren<QPushButton*>())
        if (b->property("iconSvg").toString() == QLatin1String(icons::kMoon)) { b->click(); break; }
    }
    return true;
  }
  if (name == "toast") { app->toast((step.section(QLatin1Char(':'), 1, 1)), step.section(QLatin1Char(':'), 2)); return true; }
  if (name == "wheel") {   // wheel:x,y,dx,dy turns the wheel (angle deltas, 120 = a notch) over the page point x,y
    const QStringList a = arg.split(',');
    if (a.size() != 4) return false;
    const QPoint pos(a[0].toInt(), a[1].toInt());
    const QPoint delta(a[2].toInt(), a[3].toInt());
    // a posted event is not spontaneous, so Qt would not hand it to the parents: walk them as a real wheel does
    for (QWidget* w = app->childAt(pos) ? app->childAt(pos) : app; w; w = w->parentWidget()) {
      const QPointF local = w->mapFrom(app, QPointF(pos));
      QWheelEvent e(local, w->mapToGlobal(local), QPoint(), delta, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                    false);
      QCoreApplication::sendEvent(w, &e);
      if (e.isAccepted() || w == app) break;
    }
    return true;
  }
  if (name == "click") {   // click:<text> clicks the visible button showing that text (e.g. click:Run on chart)
    for (QWidget* w : QApplication::allWidgets())   // the panels may sit outside the page's own children
      if (auto* b = qobject_cast<QAbstractButton*>(w); b && b->isVisible() && b->isEnabled() && b->text().remove('&').trimmed() == arg) {
        b->click();
        return true;
      }
    return false;
  }
  if (name == "key") {   // key:<text> types a key on the page (e.g. key:a opens the symbol search)
    QKeyEvent e(QEvent::KeyPress, 0, Qt::NoModifier, arg);
    QCoreApplication::sendEvent(app, &e);
    return true;
  }
#ifdef CHARTS_HAS_SIDE_STEPS
  // the side panel's own steps: wl:<list>, cal:<view>, calf:<filter>, alert, alert3, alert-create
  if (app->panels && app->panels->sidePanel && app->panels->sidePanel->runUiStep(step)) return true;
#endif
  return false;
}

}  // namespace charts::ui
