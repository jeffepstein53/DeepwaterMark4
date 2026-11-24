// Data › Technical data: the settings of one pair. See PairDialog.h.
#include "pages/data/PairDialog.h"
#include "charts/core/Indicators.h"
#include "net/Http.h"
#include "technicaldata/Collector.h"
#include "technicaldata/Dukascopy.h"
#include "technicaldata/FootprintArchive.h"
#include "technicaldata/PerpArchive.h"
#include "technicaldata/TechnicalDataSpec.h"
#include "technicaldata/TechnicalDataStore.h"
#include "ui/Kit.h"
#include "ui/Theme.h"
#include "ui/Touch.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateEdit>
#include <QDateTime>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpressionValidator>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimeZone>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <climits>
#include <cmath>

namespace technicaldatapage {
using namespace technicaldata;

namespace {
QString num(double v, int dec = 0) { return QLocale(QLocale::English).toString(v, 'f', dec); }
QString bars(qint64 n) {
  if (n >= 1000000) return QCoreApplication::translate("data", "%1 M bars").arg(num(n / 1e6, 1));
  if (n >= 10000) return QCoreApplication::translate("data", "%1 k bars").arg(num(n / 1e3, 0));
  return QCoreApplication::translate("data", "%1 bars").arg(QLocale(QLocale::English).toString(n));
}
QString bytes(qint64 b) {
  if (b >= 1000000000) return QCoreApplication::translate("data", "%1 GB").arg(num(b / 1e9, 1));
  return QCoreApplication::translate("data", "%1 MB").arg(num(std::max<qint64>(1, b / 1000000)));
}
QString day(qint64 t) { return QDateTime::fromSecsSinceEpoch(t, QTimeZone::UTC).toString("yyyy-MM-dd"); }
QString tfName(int s) {
  for (const auto& t : timeframes()) if (t.seconds == s) return t.name;
  return QString::number(s);
}
QVector<int> allTfs() {
  QVector<int> v;
  for (const auto& t : timeframes()) v << t.seconds;
  return v;
}

QString qss() {
  const auto& c = theme::pal();
  return QString(
             "QDialog { background: %1; }"
             "QLabel { color: %2; background: transparent; }"
             "QLineEdit, QDateEdit, QComboBox, QSpinBox, QDoubleSpinBox { background: %3; border: 1px solid %4; border-radius: 3px;"
             "  color: %5; font-family: 'JetBrains Mono'; font-size: 12px; padding: 4px 6px; min-height: 18px; }"
             "QLineEdit:focus, QDateEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: %6; }"
             "QLineEdit:read-only { color: %2; }"
             "QComboBox::drop-down, QDateEdit::drop-down { border: none; width: 18px; }"
             "QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; border: none; }"
             "QComboBox QAbstractItemView { background: %1; border: 1px solid %4; color: %5; selection-background-color: %7; outline: 0; }"
             "QRadioButton, QCheckBox { color: %5; font-size: 12px; spacing: 6px; background: transparent; padding-right: 8px; }"
             "QRadioButton:disabled, QCheckBox:disabled { color: %8; }"
             "QRadioButton::indicator, QCheckBox::indicator { width: 11px; height: 11px; border: 1px solid %10; background: %3; }"
             "QRadioButton::indicator { border-radius: 6px; } QCheckBox::indicator { border-radius: 2px; }"
             "QRadioButton::indicator:checked, QCheckBox::indicator:checked { background: %6; border-color: %6; }"
             "QRadioButton::indicator:disabled, QCheckBox::indicator:disabled { background: transparent; border-color: %4; }"
             "QScrollArea { background: transparent; border: none; } QScrollArea > QWidget > QWidget#pairBody { background: %1; }"
             "QPushButton[tf=\"1\"] { background: transparent; color: %2; border: 1px solid %4; border-radius: 3px; padding: 5px 0;"
             "  font-family: 'JetBrains Mono'; font-size: 11px; font-weight: bold; min-width: 46px; }"
             "QPushButton[tf=\"1\"]:checked { background: %9; color: %6; border-color: %6; }"
             "QPushButton[tf=\"1\"]:hover { border-color: %10; }")
      .arg(c.panel.name(), c.textDim.name(), c.panelAlt.name(), c.border.name(), c.text.name(), c.accent.name(),
           c.rowSelected.name(), c.textMute.name(), c.accentDim.name(), c.borderHi.name());
}
QPushButton* button(const QString& text, bool primary, QWidget* parent, bool danger = false) {
  auto* b = new QPushButton(text, parent);
  const auto& c = theme::pal();
  b->setCursor(Qt::PointingHandCursor);
  b->setFont(theme::mono(10, QFont::Bold, 1.2));
  b->setStyleSheet(primary
    ? QString("QPushButton { background: %1; color: %2; border: 0; border-radius: 3px; padding: 7px 16px; }"
              "QPushButton:hover { background: %3; } QPushButton:disabled { background: %4; color: %5; }")
          .arg(c.accent.name(), theme::onAccent().name(), theme::mix(c.accent, c.text, 0.2).name(), c.panelAlt.name(), c.textMute.name())
    : QString("QPushButton { background: transparent; color: %1; border: 1px solid %2; border-radius: 3px; padding: 5px 12px; }"
              "QPushButton:hover { color: %3; border-color: %4; } QPushButton:disabled { color: %5; border-color: %2; }")
          .arg(danger ? c.down.name() : c.textDim.name(), danger ? theme::alpha(c.down, 120).name(QColor::HexArgb) : c.border.name(),
               danger ? c.down.name() : c.text.name(), danger ? c.down.name() : c.borderHi.name(), c.textMute.name()));
  return b;
}
QLabel* section(const QString& t, QWidget* p) {
  auto* l = ui::label(t.toUpper(), theme::microLabel(), theme::pal().accentText, p);
  l->setContentsMargins(0, 8, 0, 0);
  return l;
}
QLabel* note(const QString& t, QWidget* p, const QColor& col = theme::pal().textMute) {
  auto* l = ui::label(t, theme::sans(11), col, p);
  l->setWordWrap(true);
  return l;
}
// Phones (the app window under ui::kCompactWidth): the dialog as wide as the window, one column, fields and buttons a
// finger can hit (36 px), long radio texts wrapped.
QString phoneQss() {
  return "QLineEdit, QDateEdit, QComboBox, QSpinBox, QDoubleSpinBox { min-height: 26px; }"
         "QPushButton[tf=\"1\"] { min-height: 24px; min-width: 40px; }"   // seven timeframes in a phone's width
         "QRadioButton::indicator, QCheckBox::indicator { width: 15px; height: 15px; } QRadioButton::indicator { border-radius: 8px; }";
}
// a label that clicks its radio button (a radio's own text does not wrap)
class RadioText : public QLabel {
public:
  RadioText(QRadioButton* r, QWidget* parent) : QLabel(parent), m_radio(r) {
    setWordWrap(true);
    setCursor(Qt::PointingHandCursor);
    setStyleSheet(QString("QLabel { color: %1; font-size: 12px; }").arg(theme::pal().text.name()));
  }
protected:
  void mousePressEvent(QMouseEvent*) override { if (m_radio->isEnabled()) m_radio->click(); }
private:
  QRadioButton* m_radio;
};

// ------------------------------------------------------------------------------------------------ dialog
class PairDialog : public QDialog {
public:
  PairDialog(QWidget* parent, const QString& market, std::function<void(const QString&)> done)
      : QDialog(parent), m_market(market), m_done(std::move(done)) {
    auto& st = Store::instance();
    m_phone = parent && ui::compact(parent->window()->width());
    const int mx = m_phone ? 16 : 24;   // side margins
    const bool isNew = market.isEmpty();
    m_fx = dukascopy::isForex(market) || (isNew && qEnvironmentVariable("DW_TECHNICALDATA_PAIR_KIND") == "forex");
    if (!isNew) m_spec = specFromJson(st.pairSpec(market));
    else m_spec = m_fx ? defaultForexSpec() : PairSpec{allTfs(), 0, defaultSpec().indicators};
    setWindowTitle(isNew ? QCoreApplication::translate("data", "Add a pair") : QCoreApplication::translate("data", "%1 settings").arg(binanceSymbol(market)));
    setStyleSheet(m_phone ? qss() + phoneQss() : qss());
    setMinimumWidth(m_phone ? 0 : 680);
    const auto& c = theme::pal();

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* inner = new QWidget;
    inner->setObjectName("pairBody");
    scroll->setWidget(inner);
    outer->addWidget(scroll, 1);
    auto* v = new QVBoxLayout(inner);
    v->setContentsMargins(mx, 20, mx, 8);
    v->setSpacing(6);

    v->addWidget(ui::label(isNew ? QCoreApplication::translate("data", "ADD A PAIR") : QCoreApplication::translate("data", "%1 · SETTINGS").arg(binanceSymbol(market)), theme::title(), c.accentText, inner));
    m_intro = note(QString(), inner, c.textDim);
    v->addWidget(m_intro);

    // the market: crypto (Binance) or forex (Dukascopy)
    if (isNew) {
      auto* kindRow = new QHBoxLayout;
      kindRow->setSpacing(6);
      m_kindCrypto = new QPushButton(QCoreApplication::translate("data", "CRYPTO · BINANCE"), inner);
      m_kindFx = new QPushButton(QCoreApplication::translate("data", "FOREX · DUKASCOPY"), inner);
      for (auto* b : {m_kindCrypto, m_kindFx}) {
        b->setProperty("tf", "1");
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setMinimumWidth(m_phone ? 0 : 170);
        kindRow->addWidget(b, m_phone ? 1 : 0);   // phones: the two share the width
      }
      kindRow->addStretch(1);
      (m_fx ? m_kindFx : m_kindCrypto)->setChecked(true);
      connect(m_kindFx, &QPushButton::toggled, this, [this](bool on) { setKind(on); });
      v->addLayout(kindRow);
    }

    // the pair
    v->addWidget(section(QCoreApplication::translate("data", "Pair"), inner));
    auto* symRow = new QBoxLayout(m_phone ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);   // phones: the answer under the field
    m_fxPick = new QComboBox(inner);
    m_fxPick->setMinimumWidth(200);
    for (const auto& i : dukascopy::majors())
      if (!st.pairs().contains(dukascopy::marketOf(i.symbol))) m_fxPick->addItem(i.symbol, i.symbol);
    connect(m_fxPick, &QComboBox::currentIndexChanged, this, [this] { showForex(); sync(); });
    symRow->addWidget(m_fxPick);
    m_symbol = new QLineEdit(inner);
    m_symbol->setPlaceholderText("ETHUSDT");
    m_symbol->setValidator(new QRegularExpressionValidator(QRegularExpression("[A-Za-z0-9]{0,20}"), m_symbol));
    m_symbol->setMaximumWidth(200);
    if (!isNew) { m_symbol->setText(binanceSymbol(market)); m_symbol->setReadOnly(true); }
    symRow->addWidget(m_symbol);
    m_symInfo = ui::label(QString(), theme::mono(11), c.textDim, inner);
    m_symInfo->setWordWrap(true);
    symRow->addWidget(m_symInfo, 1);
    v->addLayout(symRow);
    m_symTimer.setSingleShot(true);
    m_symTimer.setInterval(500);
    connect(&m_symTimer, &QTimer::timeout, this, [this] { checkSymbol(); });
    connect(m_symbol, &QLineEdit::textEdited, this, [this](const QString& t) {
      const int pos = m_symbol->cursorPosition();
      m_symbol->setText(t.toUpper());
      m_symbol->setCursorPosition(pos);
      // an answer still on its way is for the text before this edit: it must not validate this one (ETHUSDT's answer
      // made ETHUSD addable; an emptied field kept ADD enabled and stored "BINANCE:")
      ++m_symGen;
      m_symOk = false;
      m_checkedSym.clear();
      m_listing = 0;
      m_symInfo->setText(t.isEmpty() ? QString() : QCoreApplication::translate("data", "checking with Binance…"));
      m_symTimer.start();
      sync();
    });

    // the first day
    v->addWidget(section(QCoreApplication::translate("data", "History"), inner));
    m_fromFirst = new QRadioButton(inner);
    v->addWidget(m_phone ? radioRow(m_fromFirst, &m_fromFirstText, inner) : static_cast<QWidget*>(m_fromFirst));
    auto* dateRow = new QHBoxLayout;
    m_fromDate = new QRadioButton(QCoreApplication::translate("data", "From"), inner);
    dateRow->addWidget(m_fromDate);
    m_date = new QDateEdit(inner);
    m_date->setDisplayFormat("yyyy-MM-dd");
    m_date->setCalendarPopup(true);
    m_date->setTimeZone(QTimeZone::UTC);
    m_date->setDateRange(firstAllowedDay(), QDate::currentDate());   // before setDate: a forex start before 2017 is kept
    m_date->setDate(m_spec.start ? QDateTime::fromSecsSinceEpoch(m_spec.start, QTimeZone::UTC).date() : QDate(QDate::currentDate().year() - 3, 1, 1));
    m_date->setMaximumWidth(150);
    dateRow->addWidget(m_date);
    dateRow->addStretch(1);
    v->addLayout(dateRow);
    (m_spec.start ? m_fromDate : m_fromFirst)->setChecked(true);
    connect(m_fromFirst, &QRadioButton::toggled, this, [this] { sync(); });
    connect(m_date, &QDateEdit::dateChanged, this, [this] { m_fromDate->setChecked(true); sync(); });

    // the timeframes
    v->addWidget(section(QCoreApplication::translate("data", "Timeframes"), inner));
    auto* tfRow = new QHBoxLayout;
    tfRow->setSpacing(6);
    for (const auto& t : timeframes()) {
      auto* b = new QPushButton(t.name, inner);
      b->setProperty("tf", "1");
      b->setCheckable(true);
      b->setChecked(m_spec.timeframes.contains(t.seconds));
      b->setCursor(Qt::PointingHandCursor);
      connect(b, &QPushButton::toggled, this, [this] { sync(); });
      m_tfButtons << b;
      tfRow->addWidget(b);
    }
    tfRow->addStretch(1);
    v->addLayout(tfRow);
    m_tfNote = note(QString(), inner);
    v->addWidget(m_tfNote);

    // the footprint history (user's request 2026-11-20): Binance's daily trade archives, spot and / or perpetual
    m_fpBox = new QWidget(inner);
    {
      auto* fv = new QVBoxLayout(m_fpBox);
      fv->setContentsMargins(0, 0, 0, 0);
      fv->setSpacing(8);
      fv->addWidget(section(QCoreApplication::translate("data", "Footprint"), m_fpBox));
      fv->addWidget(note(QCoreApplication::translate("data", "Buyers and sellers at every price level, minute by minute, from Binance's daily trade "
                                                     "archives (data.binance.vision, no key): every trade of the day, read once. Levels of one basis "
                                                     "point (0.01 % of the price), the same grid in every year. About 130 KB a day per market."),
                         m_fpBox));
      auto* row = new QHBoxLayout;
      row->setSpacing(6);
      const QStringList ids{"spot", "perp"};
      const QStringList names{QCoreApplication::translate("data", "Spot"), QCoreApplication::translate("data", "Perpetual USD-M")};
      for (int i = 0; i < 2; ++i) {
        auto* b = new QPushButton(names[i], m_fpBox);
        b->setProperty("tf", "1");
        b->setProperty("venue", ids[i]);
        b->setCheckable(true);
        b->setChecked(m_spec.footprint.contains(ids[i]));
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::toggled, this, [this] { sync(); });
        m_fpButtons << b;
        row->addWidget(b);
      }
      row->addStretch(1);
      fv->addLayout(row);
      const QString fpFirst = QCoreApplication::translate("data", "From the first archive (spot 2017-08-17, perpetual 2019-12-31)");
      m_fpFirst = new QRadioButton(m_phone ? QString() : fpFirst, m_fpBox);
      if (m_phone) {
        QLabel* t = nullptr;
        fv->addWidget(radioRow(m_fpFirst, &t, m_fpBox));
        t->setText(fpFirst);
      } else fv->addWidget(m_fpFirst);
      auto* dr = new QHBoxLayout;
      m_fpFrom = new QRadioButton(QCoreApplication::translate("data", "From"), m_fpBox);
      dr->addWidget(m_fpFrom);
      m_fpDate = new QDateEdit(m_fpBox);
      m_fpDate->setDisplayFormat("yyyy-MM-dd");
      m_fpDate->setCalendarPopup(true);
      m_fpDate->setTimeZone(QTimeZone::UTC);
      m_fpDate->setDateRange(QDate(2017, 8, 17), QDate::currentDate());
      // a first footprint: the last three months (user's sample, 2026-11-20)
      m_fpDate->setDate(m_spec.footprintStart ? QDateTime::fromSecsSinceEpoch(m_spec.footprintStart, QTimeZone::UTC).date()
                                              : QDate::currentDate().addMonths(-3));
      m_fpDate->setMaximumWidth(150);
      dr->addWidget(m_fpDate);
      dr->addStretch(1);
      fv->addLayout(dr);
      (m_spec.footprint.isEmpty() || m_spec.footprintStart ? m_fpFrom : m_fpFirst)->setChecked(true);
      connect(m_fpFirst, &QRadioButton::toggled, this, [this] { sync(); });
      connect(m_fpDate, &QDateEdit::dateChanged, this, [this] { m_fpFrom->setChecked(true); sync(); });
    }
    v->addWidget(m_fpBox);

    // the perpetual's order book and positioning, and the live book (user's choices 2026-11-20)
    m_bookBox = new QWidget(inner);
    {
      auto* bv = new QVBoxLayout(m_bookBox);
      bv->setContentsMargins(0, 0, 0, 0);
      bv->setSpacing(8);
      bv->addWidget(section(QCoreApplication::translate("data", "Order book and positions"), m_bookBox));
      // (phones: the three buttons one under the other, too wide side by side)
      bv->addWidget(note(QCoreApplication::translate("data", "From Binance's daily archives of the USD-M perpetual, each from its first day: the order "
                                                     "book's depth out to ±0.2, 1, 2, 3, 4 and 5 % of the price every 30 seconds (from 2023), and "
                                                     "the open interest with the long/short ratios every 5 minutes (from 2020). The live book is "
                                                     "kept in memory while Deepwater runs (spot and perpetual, for live trading): never written "
                                                     "to disk, gone when Deepwater closes."), m_bookBox));
      auto* row = new QBoxLayout(m_phone ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
      row->setSpacing(6);
      auto make = [&](const QString& text, bool on) {
        auto* b = new QPushButton(text, m_bookBox);
        b->setProperty("tf", "1");
        b->setCheckable(true);
        b->setChecked(on);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::toggled, this, [this] { sync(); });
        row->addWidget(b);
        return b;
      };
      m_depthBtn = make(QCoreApplication::translate("data", "Book depth"), m_spec.depth);
      m_oiBtn = make(QCoreApplication::translate("data", "Open interest"), m_spec.openInterest);
      m_liveBtn = make(QCoreApplication::translate("data", "Live book (memory)"), m_spec.liveBook);
      m_liveBtn->setToolTip(QCoreApplication::translate("data", "The order book and the trades of the spot market and the perpetual, kept in memory while Deepwater runs, never written to disk"));
      if (!m_phone) row->addStretch(1);
      bv->addLayout(row);
    }
    v->addWidget(m_bookBox);

    // the indicators
    auto* indHead = new QHBoxLayout;
    indHead->addWidget(section(QCoreApplication::translate("data", "Indicators"), inner));
    indHead->addStretch(1);
    auto* defaults = button(QCoreApplication::translate("data", "DEFAULT LIST"), false, inner);
    defaults->setToolTip(QCoreApplication::translate("data", "The 23 columns BTCUSDT records by default: averages 20/50/200, Bollinger, RSI 14, MACD, ATR, DMI, Stochastic, OBV, VWAP"));
    connect(defaults, &QPushButton::clicked, this, [this] { m_spec.indicators = defaultSpec().indicators; fillList(); sync(); });
    auto* none = button(QCoreApplication::translate("data", "NONE"), false, inner);
    connect(none, &QPushButton::clicked, this, [this] { m_spec.indicators.clear(); fillList(); sync(); });
    indHead->addWidget(defaults);
    indHead->addWidget(none);
    v->addLayout(indHead);
    m_list = new QWidget(inner);
    m_listLayout = new QVBoxLayout(m_list);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(0);
    v->addWidget(m_list);

    // adding one
    auto* addBox = new QWidget(inner);
    addBox->setStyleSheet(QString("QWidget#addBox { background: %1; border: 1px solid %2; border-radius: 4px; }").arg(c.panelAlt.name(), c.divider.name()));
    addBox->setObjectName("addBox");
    auto* av = new QVBoxLayout(addBox);
    av->setContentsMargins(12, 10, 12, 10);
    av->setSpacing(6);
    av->addWidget(ui::label(QCoreApplication::translate("data", "ADD AN INDICATOR"), theme::microLabel(), c.textMute, addBox));
    m_pick = new QComboBox(addBox);
    m_pick->setMaxVisibleItems(24);
    if (m_phone) {   // its longest name ("group · indicator") would hold the dialog wider than a phone: it elides instead
      m_pick->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
      m_pick->setMinimumContentsLength(12);
    }
    QVector<charts::ind::DefPtr> defs;
    for (const auto& d : charts::ind::all())
      if (storable(d.id))
        if (auto p = charts::ind::byId(d.id)) defs << p;
    std::stable_sort(defs.begin(), defs.end(), [](const charts::ind::DefPtr& a, const charts::ind::DefPtr& b) {
      return QCoreApplication::translate("charts", qPrintable(a->group)) < QCoreApplication::translate("charts", qPrintable(b->group));
    });
    for (const auto& d : defs)
      m_pick->addItem(QCoreApplication::translate("charts", qPrintable(d->group)) + "  ·  " + QCoreApplication::translate("charts", qPrintable(d->name)), d->id);
    av->addWidget(m_pick);
    m_form = new QWidget(addBox);
    m_formLayout = new QGridLayout(m_form);
    m_formLayout->setContentsMargins(0, 0, 0, 0);
    m_formLayout->setHorizontalSpacing(10);
    m_formLayout->setVerticalSpacing(4);
    av->addWidget(m_form);
    m_outs = new QWidget(addBox);
    m_outsLayout = new QGridLayout(m_outs);
    m_outsLayout->setContentsMargins(0, 2, 0, 2);
    m_outsLayout->setHorizontalSpacing(8);
    m_outsLayout->setVerticalSpacing(3);
    av->addWidget(m_outs);
    auto* checkRow = new QHBoxLayout;
    m_checkLbl = note(QString(), addBox, c.textDim);
    checkRow->addWidget(m_checkLbl, 1);
    m_add = button(QCoreApplication::translate("data", "ADD"), false, addBox);
    checkRow->addWidget(m_add, 0, Qt::AlignTop);
    av->addLayout(checkRow);
    v->addSpacing(4);
    v->addWidget(addBox);
    connect(m_pick, &QComboBox::currentIndexChanged, this, [this] { buildForm(); });
    connect(m_add, &QPushButton::clicked, this, [this] { addIndicator(); });
    m_checkTimer.setSingleShot(true);
    m_checkTimer.setInterval(250);
    connect(&m_checkTimer, &QTimer::timeout, this, [this] { runCheck(); });

    // size
    v->addWidget(section(QCoreApplication::translate("data", "Size"), inner));
    m_estimate = ui::label(QString(), theme::mono(12, QFont::Bold), c.text, inner);
    v->addWidget(m_estimate);
    m_estimateNote = note(QString(), inner);
    v->addWidget(m_estimateNote);
    v->addStretch(1);

    // buttons
    auto* foot = new QHBoxLayout;
    foot->setContentsMargins(mx, 10, mx, 16);
    // phones: the note on a row of its own, then REMOVE, then CANCEL and SAVE (one row is too narrow for the three)
    auto* phoneFoot = new QVBoxLayout;
    phoneFoot->setContentsMargins(mx, 8, mx, 12);
    phoneFoot->setSpacing(8);
    QPushButton* remove = nullptr;
    if (!isNew) {
      remove = button(QCoreApplication::translate("data", "REMOVE THE PAIR"), false, this, true);
      connect(remove, &QPushButton::clicked, this, [this] { removePair(); });
      if (!m_phone) foot->addWidget(remove);
    }
    m_footNote = ui::label(QString(), theme::sans(11), c.warn, this);
    m_footNote->setWordWrap(true);
    if (m_phone) phoneFoot->addWidget(m_footNote);
    else foot->addWidget(m_footNote, 1);
    auto* cancel = button(QCoreApplication::translate("data", "CANCEL"), false, this);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    m_save = button(isNew ? QCoreApplication::translate("data", "ADD THE PAIR") : QCoreApplication::translate("data", "SAVE"), true, this);
    connect(m_save, &QPushButton::clicked, this, [this] { save(); });
    if (m_phone) {
      for (auto* b : {remove, cancel, m_save}) if (b) b->setMinimumHeight(38);
      if (remove) { auto* r = new QHBoxLayout; r->addWidget(remove); r->addStretch(1); phoneFoot->addLayout(r); }
      foot->setContentsMargins(0, 0, 0, 0);
      foot->addStretch(1);
      foot->addWidget(cancel);
      foot->addWidget(m_save);
      phoneFoot->addLayout(foot);
      outer->addLayout(phoneFoot);
    } else {
      delete phoneFoot;
      foot->addWidget(cancel);
      foot->addWidget(m_save);
      outer->addLayout(foot);
    }

    // the stored first bar until Binance answers (edit), then Binance's
    if (!isNew)
      for (const auto& t : timeframes()) {
        const SyncState s = st.state(market, t.seconds);
        if (s.count && s.startReq == 0 && (!m_listing || s.first < m_listing)) m_listing = s.first;
      }
    fillList();
    // test hooks: the pair typed, the indicator picked
    if (isNew && qEnvironmentVariableIsSet("DW_TECHNICALDATA_PAIR_SYMBOL")) m_symbol->setText(qEnvironmentVariable("DW_TECHNICALDATA_PAIR_SYMBOL").toUpper());
    if (qEnvironmentVariableIsSet("DW_TECHNICALDATA_PAIR_PICK")) {
      const int i = m_pick->findData(qEnvironmentVariable("DW_TECHNICALDATA_PAIR_PICK"));
      if (i >= 0) m_pick->setCurrentIndex(i);
    }
    buildForm();
    // test hooks: timeframes and first day set, the indicator picked added once checked, saved once Binance answered
    if (qEnvironmentVariableIsSet("DW_TECHNICALDATA_PAIR_TFS")) {
      const QStringList want = qEnvironmentVariable("DW_TECHNICALDATA_PAIR_TFS").split(',');
      for (int i = 0; i < m_tfButtons.size(); ++i) m_tfButtons[i]->setChecked(want.contains(timeframes()[i].name));
    }
    if (qEnvironmentVariableIsSet("DW_TECHNICALDATA_PAIR_START")) {
      m_date->setDate(QDate::fromString(qEnvironmentVariable("DW_TECHNICALDATA_PAIR_START"), "yyyy-MM-dd"));
      m_fromDate->setChecked(true);
    }
    m_autoAdd = qEnvironmentVariableIsSet("DW_TECHNICALDATA_PAIR_ADD");
    m_autoSave = qEnvironmentVariableIsSet("DW_TECHNICALDATA_PAIR_SAVE");
    showKind();
    if (m_fx) showForex();
    else if (!m_symbol->text().isEmpty()) checkSymbol();
    if (m_fx && qEnvironmentVariableIsSet("DW_TECHNICALDATA_PAIR_SYMBOL")) {
      const int i = m_fxPick->findData(qEnvironmentVariable("DW_TECHNICALDATA_PAIR_SYMBOL").toUpper());
      if (i >= 0) m_fxPick->setCurrentIndex(i);
    }
    sync();
    if (qEnvironmentVariableIsSet("DW_TECHNICALDATA_PAIR_SCROLL"))   // test hook: the bottom of the window
      QTimer::singleShot(400, scroll, [scroll] { scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum()); });
    QWidget* w = parent ? parent->window() : nullptr;
    QSize cap = w ? w->size() - QSize(40, 40) : QSize(900, 900);
    if (QScreen* sc = screen()) cap = cap.boundedTo(sc->availableGeometry().size() - QSize(40, 60));
    if (m_phone) {   // phones: the window's width (less 12 px a side), the form scrolls between the title and the buttons
      const QSize pc = w ? w->size() - QSize(24, 24) : cap;
      resize(pc.width(), std::min(inner->sizeHint().height() + 120, pc.height()));
    } else {
      resize(QSize(720, inner->sizeHint().height() + 70).boundedTo(cap));
    }
  }

private:
  // phones: a radio without text and its text wrapped beside it (clicking the text picks the radio)
  QWidget* radioRow(QRadioButton* r, QLabel** text, QWidget* parent) {
    auto* row = new QWidget(parent);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(2);
    r->setParent(row);
    h->addWidget(r, 0, Qt::AlignTop);
    *text = new RadioText(r, row);
    h->addWidget(*text, 1);
    return row;
  }
  QString symbol() const { return m_symbol->text().trimmed().toUpper(); }
  QString marketKey() const {
    if (!m_market.isEmpty()) return m_market;
    return m_fx ? dukascopy::marketOf(m_fxPick->currentData().toString()) : "BINANCE:" + symbol();
  }
  // The earliest first day the date field takes: Binance opened in 2017, Dukascopy's forex goes back to 2003 (each major's
  // own start). A single 2017 minimum kept forex pairs from their first fourteen years.
  QDate firstAllowedDay() const {
    if (m_fx)
      if (const dukascopy::Instrument* i = dukascopy::instrument(m_market.isEmpty() ? m_fxPick->currentData().toString() : m_market))
        return i->first;
    return m_fx ? QDate(2003, 5, 4) : QDate(2017, 1, 1);
  }
  void updateDateRange() {
    QSignalBlocker b(m_date);   // a date clamped by the new range is not the user choosing "From"
    m_date->setMinimumDate(firstAllowedDay());
  }
  // crypto ↔ forex (a new pair): its own pair field, timeframes and texts
  void setKind(bool fx) {
    m_fx = fx;
    ++m_symGen;   // a Binance answer still on its way belongs to the other kind
    m_checkedSym.clear();
    updateDateRange();
    const PairSpec d = fx ? defaultForexSpec() : defaultSpec();
    for (int i = 0; i < m_tfButtons.size(); ++i) {
      QSignalBlocker b(m_tfButtons[i]);
      m_tfButtons[i]->setChecked(d.timeframes.contains(timeframes()[i].seconds));
    }
    m_listing = 0;
    m_symOk = false;
    showKind();
    if (fx) showForex();
    else if (!symbol().isEmpty()) checkSymbol();
    sync();
  }
  void showKind() {
    m_symbol->setVisible(!m_fx || !m_market.isEmpty());
    m_fxPick->setVisible(m_fx && m_market.isEmpty());
    m_symInfo->clear();
    m_intro->setText(m_fx ? QCoreApplication::translate("data", "Forex from Dukascopy's historical data feed (no key): bid prices, each timeframe chosen from "
                                                        "the first day chosen; the trading day runs from 17:00 to 17:00 New York. Dukascopy publishes a day "
                                                        "once it is over: the newest bars are yesterday's. Indicators are Charts' engine.")
                          : QCoreApplication::translate("data", "Binance spot: closed bars of each timeframe chosen, from the first day chosen, then kept up to date every "
                                                        "minute while Deepwater runs. Indicators are Charts' engine, on each series' whole history."));
    m_tfNote->setText(m_fx ? QCoreApplication::translate("data", "1h to 1W come from Dukascopy's monthly files (about 280 a pair from 2003: hours). 1m, 5m "
                                                         "and 15m need its minute files, one a day (about 6 000 a pair, some 15 s each: days). 4h, 1D and "
                                                         "1W are made from 1h, which comes with them.")
                           : QCoreApplication::translate("data", "1m is about 4.9 million bars from 2017 for BTCUSDT (≈ 2 GB with 23 indicator columns); 1h, 81 000."));
  }
  void showForex() {
    updateDateRange();
    const dukascopy::Instrument* i = dukascopy::instrument(m_market.isEmpty() ? m_fxPick->currentData().toString() : m_market);
    if (!i) {
      m_symOk = false;
      m_symInfo->setText(m_market.isEmpty() && m_fxPick->count() == 0 ? QCoreApplication::translate("data", "the seven majors are all recorded") : QString());
      return;
    }
    m_symOk = true;
    m_listing = QDateTime(i->first, QTime(0, 0), QTimeZone::UTC).toSecsSinceEpoch();
    m_symInfo->setText(i->base + " / " + i->quote + "  ·  " + QCoreApplication::translate("data", "Dukascopy, bid prices") + "  ·  " +
                       QCoreApplication::translate("data", "history from about %1").arg(i->first.toString("yyyy-MM")));
    m_symInfo->setStyleSheet(QString("color: %1;").arg(theme::pal().up.name()));
  }
  PairSpec current() const {
    PairSpec s;
    for (int i = 0; i < m_tfButtons.size(); ++i) if (m_tfButtons[i]->isChecked()) s.timeframes << timeframes()[i].seconds;
    s.start = m_fromDate->isChecked() ? QDateTime(m_date->date(), QTime(0, 0), QTimeZone::UTC).toSecsSinceEpoch() : 0;
    s.indicators = m_spec.indicators;
    if (!m_fx)
      for (QPushButton* b : m_fpButtons) if (b->isChecked()) s.footprint << b->property("venue").toString();
    if (!m_fx && m_depthBtn) {
      s.depth = m_depthBtn->isChecked();
      s.openInterest = m_oiBtn->isChecked();
      s.liveBook = m_liveBtn->isChecked();
    }
    if (!s.footprint.isEmpty() && m_fpFrom && m_fpFrom->isChecked()) s.footprintStart = QDateTime(m_fpDate->date(), QTime(0, 0), QTimeZone::UTC).toSecsSinceEpoch();
    return s;
  }

  // Binance: does the pair exist and trade, and its first day (the first daily bar)
  void checkSymbol() {
    const QString sym = symbol();
    if (sym.isEmpty() || m_fx) return;   // forex: the list of majors, nothing to ask
    const int gen = ++m_symGen;
    if (m_market.isEmpty() && Store::instance().pairs().contains("BINANCE:" + sym)) {
      m_symInfo->setText(QCoreApplication::translate("data", "already recorded: open its settings from the list"));
      m_symInfo->setStyleSheet(QString("color: %1;").arg(theme::pal().warn.name()));
      m_symOk = false;
      sync();
      return;
    }
    net::Request r;
    r.url = QUrl("https://api.binance.com/api/v3/exchangeInfo?symbol=" + sym);
    r.ttlSec = 3600;
    net::get(r, this, [this, gen, sym](const net::Reply& rep) {
      if (gen != m_symGen) return;
      const QJsonObject o = QJsonDocument::fromJson(rep.body).object();
      const QJsonObject s = o.value("symbols").toArray().first().toObject();
      if (s.isEmpty()) {
        m_symOk = false;
        const bool unknown = o.value("code").toInt() == -1121 || rep.status == 400;
        m_symInfo->setText(unknown ? QCoreApplication::translate("data", "Binance has no spot pair %1").arg(sym) : QCoreApplication::translate("data", "Binance did not answer: %1").arg(rep.error.isEmpty() ? QString("HTTP %1").arg(rep.status) : rep.error));
        m_symInfo->setStyleSheet(QString("color: %1;").arg(theme::pal().down.name()));
        sync();
        return;
      }
      const QString status = s.value("status").toString();
      m_pairText = s.value("baseAsset").toString() + " / " + s.value("quoteAsset").toString();
      m_trading = status == "TRADING";
      m_symOk = true;
      m_checkedSym = sym;
      showSymbol();
      // its first day
      net::Request k;
      QUrl u("https://api.binance.com/api/v3/klines");
      u.setQuery(QUrlQuery{{"symbol", sym}, {"interval", "1d"}, {"startTime", "0"}, {"limit", "1"}});
      k.url = u;
      k.ttlSec = 86400;
      net::get(k, this, [this, gen](const net::Reply& rep) {
        if (gen != m_symGen) return;
        const QJsonArray a = QJsonDocument::fromJson(rep.body).array();
        if (!a.isEmpty()) m_listing = qint64(a.first().toArray().first().toDouble()) / 1000;
        showSymbol();
        sync();
      });
      sync();
    });
  }
  void showSymbol() {
    const auto& c = theme::pal();
    QString t = m_pairText + "  ·  " + (m_trading ? QCoreApplication::translate("data", "trading") : QCoreApplication::translate("data", "not trading any more: its history only"));
    if (m_listing) t += "  ·  " + QCoreApplication::translate("data", "first bar %1").arg(day(m_listing));
    m_symInfo->setText(t);
    m_symInfo->setStyleSheet(QString("color: %1;").arg((m_trading ? c.up : c.warn).name()));
  }

  void fillList() {
    while (QLayoutItem* it = m_listLayout->takeAt(0)) { delete it->widget(); delete it; }
    const auto& c = theme::pal();
    if (m_spec.indicators.isEmpty()) {
      m_listLayout->addWidget(note(QCoreApplication::translate("data", "No indicator: only the bars are recorded."), m_list));
      return;
    }
    for (int i = 0; i < m_spec.indicators.size(); ++i) {
      const IndSpec& ind = m_spec.indicators[i];
      auto* row = new QWidget(m_list);
      row->setObjectName("indRow");
      row->setStyleSheet(QString("QWidget#indRow { border-bottom: 1px solid %1; }").arg(c.divider.name()));
      // phones: the name and × on one line, the columns (wrapped) under them
      auto* h = new QHBoxLayout;
      QVBoxLayout* rv = nullptr;
      if (m_phone) {
        rv = new QVBoxLayout(row);
        rv->setContentsMargins(0, 4, 0, 4);
        rv->setSpacing(2);
        rv->addLayout(h);
      } else row->setLayout(h);
      if (!m_phone) h->setContentsMargins(0, 3, 0, 3);
      auto* name = ui::label(describe(ind), theme::sans(12), c.text, row);
      name->setWordWrap(true);
      h->addWidget(name, 1);
      QStringList cols;
      for (const IndColumn& col : columnsOf(ind)) cols << col.name;
      QString where;
      if (!ind.tfs.isEmpty()) {
        QStringList n;
        for (int t : ind.tfs) n << tfName(t);
        where = "  " + QCoreApplication::translate("data", "(only %1)").arg(n.join(" "));
      }
      auto* colsLbl = ui::label(cols.join("  ") + where, theme::mono(10), c.textDim, row);
      colsLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
      if (m_phone) colsLbl->setWordWrap(true);
      else h->addWidget(colsLbl);
      auto* x = new QPushButton("×", row);
      x->setCursor(Qt::PointingHandCursor);
      x->setToolTip(QCoreApplication::translate("data", "Remove"));
      x->setStyleSheet(QString("QPushButton { background: transparent; border: none; color: %1; font-size: 15px; padding: 0 6px; }"
                               "QPushButton:hover { color: %2; }").arg(c.textMute.name(), c.down.name()));
      connect(x, &QPushButton::clicked, this, [this, i] {
        if (i < m_spec.indicators.size()) m_spec.indicators.remove(i);
        QTimer::singleShot(0, this, [this] { fillList(); sync(); });
      });
      h->addWidget(x);
      if (rv) rv->addWidget(colsLbl);
      m_listLayout->addWidget(row);
    }
  }

  // the settings of the indicator picked, as Charts' own
  void buildForm() {
    while (QLayoutItem* it = m_formLayout->takeAt(0)) { delete it->widget(); delete it; }
    m_getters.clear();
    const charts::ind::DefPtr d = charts::ind::byId(m_pick->currentData().toString());
    if (!d) return;
    const auto& c = theme::pal();
    int k = 0;
    for (const auto& in : d->inputs) {
      if (in.type == "time") continue;
      // three settings a row, label then field; phones: the label (wrapped) over its field, one setting at a time
      const int row = m_phone ? k * 2 : k / 3, col = m_phone ? 0 : (k % 3) * 2;
      ++k;
      auto* lbl = ui::label(QCoreApplication::translate("charts", qPrintable(in.label)), theme::sans(11), c.textDim, m_form);
      if (m_phone) lbl->setWordWrap(true);
      m_formLayout->addWidget(lbl, row, col);
      QWidget* w = nullptr;
      if (in.type == "int") {
        auto* s = new QSpinBox(m_form);
        s->setRange(std::isfinite(in.min) ? int(in.min) : -100000, std::isfinite(in.max) ? int(in.max) : 100000);
        s->setValue(in.def.toInt());
        connect(s, &QSpinBox::valueChanged, this, [this] { paramsEdited(); });
        m_getters.insert(in.key, [s] { return QJsonValue(s->value()); });
        w = s;
      } else if (in.type == "float") {
        auto* s = new QDoubleSpinBox(m_form);
        s->setDecimals(4);
        s->setRange(std::isfinite(in.min) ? in.min : -1e6, std::isfinite(in.max) ? in.max : 1e6);
        s->setSingleStep(std::isfinite(in.step) ? in.step : 0.1);
        s->setValue(in.def.toDouble());
        connect(s, &QDoubleSpinBox::valueChanged, this, [this] { paramsEdited(); });
        m_getters.insert(in.key, [s] { return QJsonValue(s->value()); });
        w = s;
      } else {
        auto* b = new QComboBox(m_form);
        const QStringList opts = in.type == "source" ? QStringList{"close", "open", "high", "low", "hl2", "hlc3", "ohlc4", "hlcc4", "volume"} : in.options;
        for (const QString& o : opts) b->addItem(QCoreApplication::translate("charts", qPrintable(o)), o);
        const int i = b->findData(in.def.toString());
        b->setCurrentIndex(std::max(0, i));
        connect(b, &QComboBox::currentIndexChanged, this, [this] { paramsEdited(); });
        m_getters.insert(in.key, [b] { return QJsonValue(b->currentData().toString()); });
        w = b;
      }
      w->setMinimumWidth(110);
      if (m_phone) m_formLayout->addWidget(w, row + 1, 0);
      else m_formLayout->addWidget(w, row, col + 1);
    }
    if (k == 0) m_formLayout->addWidget(note(QCoreApplication::translate("data", "No setting."), m_form), 0, 0);
    m_formLayout->setColumnStretch(m_phone ? 0 : 5, 1);
    runCheck();
  }
  // A setting changed: the last check is for the previous values. ADD is off until the new check answers (250 ms
  // later), or ADD pressed in between added the previous parameters.
  void paramsEdited() {
    ++m_checkGen;
    m_check = {};
    m_add->setEnabled(false);
    m_checkTimer.start();
  }
  QJsonObject formParams() const {
    QJsonObject p;
    for (auto it = m_getters.cbegin(); it != m_getters.cend(); ++it) p.insert(it.key(), it.value()());
    return p;
  }

  // computed on test bars before it can be added (TechnicalDataSpec.h): on a worker, the window stays usable
  void runCheck() {
    const QString id = m_pick->currentData().toString();
    const QJsonObject params = formParams();
    const int gen = ++m_checkGen;
    m_check = {};
    m_add->setEnabled(false);
    m_checkLbl->setText(QCoreApplication::translate("data", "checking it on test bars…"));
    m_checkLbl->setStyleSheet(QString("color: %1;").arg(theme::pal().textDim.name()));
    auto* w = new QFutureWatcher<IndCheck>(this);
    connect(w, &QFutureWatcher<IndCheck>::finished, this, [this, w, gen, id, params] {
      w->deleteLater();
      if (gen != m_checkGen) return;
      m_check = w->result();
      m_checkId = id;
      m_checkParams = params;
      showCheck();
    });
    w->setFuture(QtConcurrent::run([id, params] { return checkIndicator(id, params, allTfs()); }));
  }
  void showCheck() {
    while (QLayoutItem* it = m_outsLayout->takeAt(0)) { delete it->widget(); delete it; }
    m_outBoxes.clear();
    const auto& c = theme::pal();
    const charts::ind::DefPtr d = charts::ind::byId(m_checkId);
    if (d && d->outputs.size() > 1) {
      int k = 0;
      for (const auto& o : d->outputs) {
        IndSpec probe{m_checkId, m_checkParams};
        probe.outputs = {o.key};
        const QVector<IndColumn> col = columnsOf(probe);
        auto* b = new QCheckBox(QCoreApplication::translate("charts", qPrintable(o.label)), m_outs);
        auto* name = ui::label(col.isEmpty() ? QString() : col.first().name, theme::mono(10), c.textMute, m_outs);
        const int per = m_phone ? 1 : 2;   // outputs a row (phones: one)
        m_outsLayout->addWidget(b, k / per, (k % per) * 2);
        m_outsLayout->addWidget(name, k / per, (k % per) * 2 + 1);
        ++k;
        b->setProperty("key", o.key);
        const bool usable = m_check.outputs.contains(o.key);
        b->setEnabled(usable);
        b->setChecked(usable);
        if (m_check.ahead.contains(o.key)) b->setToolTip(QCoreApplication::translate("data", "Looks ahead: its values change once later bars come"));
        else if (!usable) b->setToolTip(QCoreApplication::translate("data", "No value with these settings"));
        connect(b, &QCheckBox::toggled, this, [this] { syncAdd(); });
        m_outBoxes << b;
      }
      m_outsLayout->setColumnStretch(1, 1);
      m_outsLayout->setColumnStretch(3, 1);
    }
    QString t;
    QColor col = c.up;
    if (!m_check.ok) { t = m_check.reason; col = c.down; }
    else {
      t = QCoreApplication::translate("data", "Can be stored: it never looks ahead, and computed again from %1 bars before new ones it equals the values computed over the whole history.").arg(OVERLAP);
      if (m_check.running) t += " " + QCoreApplication::translate("data", "A running sum from the first bar: carried over from one piece to the next.");
      if (!m_check.reason.isEmpty()) { t += " " + m_check.reason; col = c.warn; }
    }
    m_checkLbl->setText(t);
    m_checkLbl->setStyleSheet(QString("color: %1;").arg(col.name()));
    syncAdd();
    if (m_autoAdd && m_add->isEnabled()) { m_autoAdd = false; addIndicator(); }
  }
  IndSpec candidate() const {
    IndSpec i{m_checkId, m_checkParams};
    i.running = m_check.running;
    if (m_check.tfs.size() < allTfs().size()) i.tfs = m_check.tfs;
    if (!m_outBoxes.isEmpty())
      for (auto* b : m_outBoxes) if (b->isChecked()) i.outputs << b->property("key").toString();
    return i;
  }
  void syncAdd() {
    bool ok = m_check.ok && m_checkId == m_pick->currentData().toString();
    QString why;
    if (ok && !m_outBoxes.isEmpty() && candidate().outputs.isEmpty()) ok = false;
    if (ok) {
      const QStringList have = columnNames(PairSpec{{60}, 0, m_spec.indicators});
      QStringList dup;
      for (const IndColumn& c : columnsOf(candidate())) if (have.contains(c.name)) dup << c.name;
      if (!dup.isEmpty()) { ok = false; why = QCoreApplication::translate("data", "Already in the list: %1").arg(dup.join(", ")); }
    }
    m_add->setEnabled(ok);
    m_add->setToolTip(why);
    if (!why.isEmpty()) {
      m_checkLbl->setText(why);
      m_checkLbl->setStyleSheet(QString("color: %1;").arg(theme::pal().warn.name()));
    }
  }
  void addIndicator() {
    // only what was checked: the indicator and settings shown now (a click queued before the button went off)
    if (!m_check.ok || m_checkId != m_pick->currentData().toString() || m_checkParams != formParams()) return;
    m_spec.indicators << candidate();
    fillList();
    sync();
    syncAdd();
  }

  // the size, the buttons
  void sync() {
    // forex: 4h, 1D and 1W are made from the hours, so 1h stays with them
    if (m_fx) {
      bool derived = false;
      for (int i = 0; i < m_tfButtons.size(); ++i) derived |= m_tfButtons[i]->isChecked() && timeframes()[i].seconds > 3600;
      for (int i = 0; i < m_tfButtons.size(); ++i)
        if (timeframes()[i].seconds == 3600) {
          if (derived && !m_tfButtons[i]->isChecked()) { QSignalBlocker b(m_tfButtons[i]); m_tfButtons[i]->setChecked(true); }
          m_tfButtons[i]->setToolTip(derived ? QCoreApplication::translate("data", "Kept: 4h, 1D and 1W are made from it") : QString());
        }
    }
    const PairSpec s = current();
    const QString fromFirst = m_fx ? QCoreApplication::translate("data", "From the pair's first day on Dukascopy (about %1)").arg(day(m_listing))
                              : m_listing ? QCoreApplication::translate("data", "From the pair's first day on Binance (%1)").arg(day(m_listing))
                                          : QCoreApplication::translate("data", "From the pair's first day on Binance");
    if (m_fromFirstText) m_fromFirstText->setText(fromFirst);   // phones: the wrapped text beside the radio
    else m_fromFirst->setText(fromFirst);
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 from = s.start ? std::max(s.start, m_listing) : m_listing;
    const QVector<IndColumn> cols = columnsOf(s);
    qint64 nBars = 0, size = 0;
    QStringList per;
    for (int tf : s.timeframes) {
      // forex trades five days of seven
      const qint64 n = from ? (m_fx && tf < 604800 ? estimateBars(tf, from, now) * 5 / 7 : estimateBars(tf, from, now)) : 0;
      int k = 0;
      for (const IndColumn& c : cols) k += c.appliesTo(tf);
      nBars += n;
      size += estimateBytes(n, k);
      if (n) per << tfName(tf) + " " + bars(n);
    }
    // the footprint: days × about 130 KB a market (measured 2026-11-20 on BTCUSDT: 125-142 KB a day)
    qint64 fpDays = 0;
    for (const QString& id : s.footprint) {
      const auto v = fp::venueFromId(id);
      if (!v) continue;
      const QDate first = std::max(fp::firstDay(*v, binanceSymbol(marketKey())),
                                   s.footprintStart ? QDateTime::fromSecsSinceEpoch(s.footprintStart, QTimeZone::UTC).date() : QDate());
      fpDays += std::max<qint64>(0, first.daysTo(QDate::currentDate()));
    }
    size += fpDays * 135000;
    if (fpDays) per << QCoreApplication::translate("data", "footprint %n day(s)", "", int(fpDays));
    // depth ≈ 120 KB a day, open interest ≈ 17 KB a day (measured 2026-11-20 on BTCUSDT)
    if (s.depth) size += std::max<qint64>(0, technicaldata::perp::firstDepthDay().daysTo(QDate::currentDate())) * 120000;
    if (s.openInterest) size += std::max<qint64>(0, technicaldata::perp::firstMetricsDay().daysTo(QDate::currentDate())) * 17000;
    if (m_fpBox) m_fpBox->setVisible(!m_fx);
    if (m_bookBox) m_bookBox->setVisible(!m_fx);
    if (!from) {
      m_estimate->setText("—");
      m_estimateNote->setText(QCoreApplication::translate("data", "Known once Binance gives the pair's first day."));
    } else {
      m_estimate->setText(QCoreApplication::translate("data", "≈ %1  ·  ≈ %2 on disk").arg(bars(nBars), bytes(size)));
      m_estimateNote->setText(per.join("  ·  ") + "\n" + QCoreApplication::translate("data", "%n indicator column(s). From the database's own size: about 95 bytes a bar and 10 a value.", "", int(cols.size())));
    }
    QString problem;
    if (s.timeframes.isEmpty()) problem = QCoreApplication::translate("data", "Pick at least one timeframe.");
    else if (m_market.isEmpty() && !m_symOk && !m_fx) problem = symbol().isEmpty() ? QCoreApplication::translate("data", "Type a Binance spot pair, e.g. ETHUSDT.") : QString();
    if (s.start && m_listing && s.start <= m_listing && m_fromDate->isChecked()) problem = QCoreApplication::translate("data", "That day is before the pair's first bar: its whole history.");
    m_footNote->setText(problem);
    m_save->setEnabled(!s.timeframes.isEmpty() && (!m_market.isEmpty() || m_symOk));
    if (m_autoSave && !m_autoAdd && m_save->isEnabled() && m_listing) { m_autoSave = false; QTimer::singleShot(300, this, [this] { save(); }); }
  }

  // what saving would delete, asked first
  QStringList deletions(const PairSpec& now) const {
    QStringList out;
    if (m_market.isEmpty()) return out;
    auto& st = Store::instance();
    for (int tf : m_spec.timeframes) {
      if (now.timeframes.contains(tf)) continue;
      const SyncState s = st.state(m_market, tf);
      if (s.count) out << QCoreApplication::translate("data", "%1: all its %2").arg(tfName(tf), bars(s.count));
    }
    if (now.start > m_spec.start) {
      qint64 n = 0;
      for (int tf : now.timeframes) {
        const SyncState s = st.state(m_market, tf);
        if (s.count && now.start > s.first) n += std::min(s.count, (std::min(now.start, s.last) - s.first) / tf);
      }
      if (n) out << QCoreApplication::translate("data", "the bars before %1 (≈ %2)").arg(day(now.start), bars(n));
    }
    // the footprint: a market no longer recorded, or a later first day
    for (const QString& id : m_spec.footprint) {
      const QVector<FpDay> days = st.footprintDays(m_market, id);
      const qint64 cut = now.footprint.contains(id) ? now.footprintStart : LLONG_MAX;
      int n = 0;
      for (const FpDay& d : days) n += d.day < cut;
      if (n && !now.footprint.contains(id))
        out << QCoreApplication::translate("data", "footprint %1: its %n day(s)", "", n).arg(id == "spot" ? QCoreApplication::translate("data", "spot") : QCoreApplication::translate("data", "perpetual"));
      else if (n)
        out << QCoreApplication::translate("data", "footprint %1: %n day(s) before %2", "", n).arg(id == "spot" ? QCoreApplication::translate("data", "spot") : QCoreApplication::translate("data", "perpetual"), day(cut));
    }
    // the perpetual's depth or open interest unticked: their days go
    if (m_spec.depth && !now.depth)
      if (const int n = int(st.archiveDays(m_market, "depth").size()))
        out << QCoreApplication::translate("data", "the book depth: its %n day(s)", "", n);
    if (m_spec.openInterest && !now.openInterest)
      if (const int n = int(st.archiveDays(m_market, "oi").size()))
        out << QCoreApplication::translate("data", "the open interest: its %n day(s)", "", n);
    // forex, an earlier first day: Dukascopy's minute files are read again from it, in order
    if (dukascopy::isForex(m_market) && now.start < m_spec.start) {
      qint64 n = 0;
      for (int tf : {60, 300, 900}) if (now.timeframes.contains(tf)) n += st.state(m_market, tf).count;
      if (n) out << QCoreApplication::translate("data", "the 1m, 5m and 15m bars (≈ %1), downloaded again from the new first day").arg(bars(n));
    }
    return out;
  }
  bool ask(const QString& title, const QString& text, const QString& yes) {
    QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::NoButton, this);
    box.setTextFormat(Qt::PlainText);
    box.setStyleSheet(qss());
    auto* y = box.addButton(yes, QMessageBox::DestructiveRole);
    box.addButton(QCoreApplication::translate("data", "Cancel"), QMessageBox::RejectRole);
    box.exec();
    return box.clickedButton() == y;
  }
  void save() {
    const PairSpec s = current();
    const QString market = marketKey();
    // a new Binance pair: only the symbol Binance confirmed, as typed now (a stale answer must not add another one)
    if (m_market.isEmpty() && !m_fx && (symbol().isEmpty() || !m_symOk || m_checkedSym != symbol())) return;
    const QStringList del = deletions(s);
    if (!del.isEmpty() &&
        !ask(QCoreApplication::translate("data", "Delete stored bars?"), QCoreApplication::translate("data", "Saving deletes, from %1:\n\n• %2\n\nThey would have to be downloaded again to come back.")
                                            .arg(binanceSymbol(market), del.join("\n• ")), QCoreApplication::translate("data", "Delete and save")))
      return;
    auto done = m_done;
    Store::instance().setPairSpec(market, specToJson(s), parentWidget(), [done, market] { if (done) done(market); });
    accept();
  }
  void removePair() {
    auto& st = Store::instance();
    qint64 n = 0;
    for (int tf : m_spec.timeframes) n += st.state(m_market, tf).count;
    if (!ask(QCoreApplication::translate("data", "Remove %1?").arg(binanceSymbol(m_market)),
             QCoreApplication::translate("data", "Every bar and indicator stored for %1 is deleted (%2), and it is no longer collected.").arg(binanceSymbol(m_market), bars(n)),
             QCoreApplication::translate("data", "Remove")))
      return;
    auto done = m_done;
    const QString market = m_market;
    st.removePair(market, parentWidget(), [done, market] { if (done) done(market); });
    accept();
  }

  QString m_market;
  std::function<void(const QString&)> m_done;
  PairSpec m_spec;
  QLineEdit* m_symbol;
  QLabel *m_symInfo, *m_checkLbl, *m_estimate, *m_estimateNote, *m_footNote;
  QTimer m_symTimer, m_checkTimer;
  int m_symGen = 0, m_checkGen = 0;
  bool m_symOk = false, m_trading = true, m_autoAdd = false, m_autoSave = false, m_fx = false;
  QPushButton *m_kindCrypto = nullptr, *m_kindFx = nullptr;
  QComboBox* m_fxPick = nullptr;
  QLabel *m_intro = nullptr, *m_tfNote = nullptr;
  QString m_pairText;
  QString m_checkedSym;     // the Binance symbol the last answer confirmed
  qint64 m_listing = 0;
  QRadioButton *m_fromFirst, *m_fromDate;
  QLabel* m_fromFirstText = nullptr;   // phones: m_fromFirst's text, wrapped (radioRow)
  bool m_phone = false;
  QDateEdit* m_date;
  QVector<QPushButton*> m_tfButtons;
  QWidget* m_fpBox = nullptr;
  QWidget* m_bookBox = nullptr;
  QPushButton *m_depthBtn = nullptr, *m_oiBtn = nullptr, *m_liveBtn = nullptr;
  QVector<QPushButton*> m_fpButtons;
  QRadioButton *m_fpFirst = nullptr, *m_fpFrom = nullptr;
  QDateEdit* m_fpDate = nullptr;
  QWidget *m_list, *m_form, *m_outs;
  QVBoxLayout* m_listLayout;
  QGridLayout* m_formLayout;
  QGridLayout* m_outsLayout;
  QComboBox* m_pick;
  QHash<QString, std::function<QJsonValue()>> m_getters;
  IndCheck m_check;
  QString m_checkId;
  QJsonObject m_checkParams;
  QVector<QCheckBox*> m_outBoxes;
  QPushButton *m_add, *m_save;
};
}  // namespace

QDialog* openPairDialog(QWidget* parent, const QString& market, std::function<void(const QString&)> done) {
  auto* d = new PairDialog(parent, market, std::move(done));
  d->setAttribute(Qt::WA_DeleteOnClose);
  d->open();
  return d;
}

}  // namespace technicaldatapage
