// Data › Technical data (user's design, 2026-11-17/18): what technical-data.sqlite holds — the pairs recorded (added, set
// and removed here: PairDialog.h), the collection of each timeframe of the pair picked (bars, period, holes, indicators,
// what runs now) and the last bars of a timeframe with every indicator, as stored.
// Reads src/technicaldata (Store, Collector, Indicators); nothing here computes or downloads.
#include "app/Pages.h"
#include "core/Settings.h"
#include "global/PageScroll.h"
#include "pages/data/FootprintPanel.h"
#include "pages/data/PairDialog.h"
#include "technicaldata/TechnicalDataSpec.h"
#include "technicaldata/Dukascopy.h"
#include "technicaldata/ForexCollector.h"
#include "technicaldata/ArchiveCollector.h"
#include "technicaldata/TechnicalDataBars.h"
#include "technicaldata/Collector.h"
#include "technicaldata/TechnicalDataIndicators.h"
#include "technicaldata/TechnicalDataStore.h"
#include "ui/Kit.h"
#include "ui/Theme.h"
#include "ui/Touch.h"
#include <QAbstractTableModel>
#include <QCoreApplication>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTableView>
#include <QTimeZone>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <cmath>

namespace technicaldatapage {
using namespace technicaldata;

QString num(double v, int dec) {
  if (!std::isfinite(v)) return QStringLiteral("—");
  return QLocale(QLocale::English).toString(v, 'f', dec);
}
QString when(qint64 t, int tf) {
  if (t <= 0) return QStringLiteral("—");
  const QDateTime d = QDateTime::fromSecsSinceEpoch(t, QTimeZone::UTC);
  // a crypto day starts at midnight UTC: its date; a forex day at 17:00 New York the evening before: the time too
  return tf >= 86400 && t % 86400 == 0 ? d.toString("yyyy-MM-dd") : d.toString("yyyy-MM-dd HH:mm");
}

// ------------------------------------------------------------------------------------------------ collection
// One row per timeframe: bars, period, indicators, holes, state, and how far the history has come.
// what a row's state column says, and its colour (the page paints it; the steps hook writes it down)
std::pair<QString, QColor> stateOf(const Collector::Progress& row, bool collecting) {
  const auto& c = theme::pal();
  const SyncState& s = row.state;
  if (row.cancelled) return {QCoreApplication::translate("data", "cancelled"), c.textMute};
  if (row.paused)
    return {s.count && s.historyDone ? QCoreApplication::translate("data", "paused (complete up to here)") : QCoreApplication::translate("data", "paused"), c.warn};
  if (!row.error.isEmpty() && row.market.startsWith("DUKASCOPY:"))   // a busy server, not a fault: tried again
    return {QCoreApplication::translate("data", "Dukascopy refused (%1): asked again in 10 min").arg(row.error), c.warn};
  if (!row.error.isEmpty()) return {QCoreApplication::translate("data", "error: %1").arg(row.error), c.down};
  if (row.running)
    return {s.historyDone ? QCoreApplication::translate("data", "updating") : QCoreApplication::translate("data", "downloading history"), c.accent};
  if (row.queued) return {QCoreApplication::translate("data", "waiting"), c.textDim};
  if (s.historyDone) {
    if (!collecting) return {QCoreApplication::translate("data", "complete"), c.textDim};
    return {row.market.startsWith("DUKASCOPY:") ? QCoreApplication::translate("data", "complete, kept up to date daily")
                                                : QCoreApplication::translate("data", "complete, kept up to date"), c.up};
  }
  if (s.count == 0)
    return {collecting ? QCoreApplication::translate("data", "not started") : QCoreApplication::translate("data", "nothing yet"), c.textDim};
  return {QCoreApplication::translate("data", "history incomplete"), c.textDim};
}

class CollectionView : public QWidget {
public:
  const QVector<Collector::Progress>& rows() const { return m_rows; }
  bool collecting() const { return m_collecting; }
  explicit CollectionView(QWidget* p) : QWidget(p) { setMinimumHeight(kHead + 7 * kRow + 4); }
  // holes: tf → (holes, missing bars, holes not asked for again yet), as technical-data.sqlite keeps them
  void setRows(const QVector<Collector::Progress>& rows, const QHash<int, IndState>& ind, const QHash<int, std::array<qint64, 3>>& holes,
               bool collecting) {
    m_rows = rows; m_ind = ind; m_holes = holes; m_collecting = collecting;
    setMinimumHeight(kHead + int(rows.size()) * kRow + 4);
    update();
  }
  QSize sizeHint() const override { return QSize(640, kHead + int(m_rows.size()) * kRow + 4); }
protected:
  static constexpr int kHead = 26, kRow = 46;
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto& c = theme::pal();
    const int w = width();
    // columns: TF | BARS | FROM → TO | INDICATORS | HOLES | STATE. A phone (this view under 520 px) drops INDICATORS
    // (the state says when they lag) and keeps the others narrower; the column of a dropped value is skipped below.
    const bool phone = w < 520;
    const double xd[] = {0, 44, 128, 278, 394, 478}, xp[] = {0, 38, 102, 216, 216, 268};
    const double* x = phone ? xp : xd;
    const QStringList heads = {QCoreApplication::translate("data", "TF"), QCoreApplication::translate("data", "BARS"), QCoreApplication::translate("data", "FROM → TO"), QCoreApplication::translate("data", "INDICATORS"), QCoreApplication::translate("data", "HOLES"), QCoreApplication::translate("data", "STATE")};
    p.setFont(theme::microLabel());
    p.setPen(c.textMute);
    for (int i = 0; i < heads.size(); ++i)
      if (!(phone && i == 3)) p.drawText(QRectF(x[i] + 6, 0, (i + 1 < 6 ? x[i + 1] - x[i] : w - x[i]) - 6, kHead), Qt::AlignVCenter | Qt::AlignLeft, heads[i]);
    p.setPen(c.divider);
    p.drawLine(QPointF(0, kHead - 0.5), QPointF(w, kHead - 0.5));
    const qint64 nowS = QDateTime::currentSecsSinceEpoch();
    for (int r = 0; r < m_rows.size(); ++r) {
      const auto& row = m_rows[r];
      const double y = kHead + r * kRow;
      const SyncState& s = row.state;
      const IndState is = m_ind.value(row.seconds);
      if (row.running) p.fillRect(QRectF(0, y, w, kRow), c.rowSelected);
      p.setFont(theme::mono(12, QFont::Bold));
      p.setPen(c.accentText);
      p.drawText(QRectF(x[0] + 6, y + 4, x[1] - x[0], 20), Qt::AlignVCenter, row.tf);
      p.setFont(theme::mono(11));
      p.setPen(c.text);
      p.drawText(QRectF(x[1] + 6, y + 4, x[2] - x[1] - 6, 20), Qt::AlignVCenter, QLocale(QLocale::English).toString(s.count));
      p.setPen(c.textDim);
      p.setFont(theme::mono(10));
      p.drawText(QRectF(x[2] + 6, y + 4, x[3] - x[2] - 6, 20), Qt::AlignVCenter, when(s.first, row.seconds));
      p.drawText(QRectF(x[2] + 6, y + 22, x[3] - x[2] - 6, 18), Qt::AlignVCenter, "→ " + when(s.last, row.seconds));
      p.setFont(theme::mono(11));
      p.setPen(is.count >= s.count && s.count > 0 ? c.up : c.text);
      if (!phone) p.drawText(QRectF(x[3] + 6, y + 4, x[4] - x[3] - 6, 20), Qt::AlignVCenter, QLocale(QLocale::English).toString(is.count));
      const auto h = m_holes.value(row.seconds, {0, 0, 0});
      p.setPen(h[2] ? c.warn : c.textDim);
      p.drawText(QRectF(x[4] + 6, y + 4, x[5] - x[4] - 6, 20), Qt::AlignVCenter, h[0] ? QString::number(h[0]) : QStringLiteral("—"));
      if (h[0] && !phone) {   // the missing bars: no room on a phone
        p.setFont(theme::mono(9));
        p.setPen(c.textMute);
        p.drawText(QRectF(x[4] + 6, y + 22, x[5] - x[4] - 6, 18), Qt::AlignVCenter,
                   QCoreApplication::translate("data", "%1 bars").arg(QLocale(QLocale::English).toString(h[1])));
      }
      // state
      const auto [state, sc] = stateOf(row, m_collecting);
      p.setPen(sc);
      p.setFont(theme::mono(10));
      p.drawText(QRectF(x[5] + 6, y + 4, w - x[5] - 6, 20), Qt::AlignVCenter | Qt::AlignLeft,
                 QFontMetrics(p.font()).elidedText(state, Qt::ElideRight, int(w - x[5] - 8)));
      // history bar: from the series' first bar to now
      const double span = double(std::max<qint64>(1, nowS - s.first));
      const double done = s.count == 0 ? 0 : s.historyDone ? 1 : std::clamp(double(s.last - s.first) / span, 0.0, 1.0);
      const QRectF bar(x[5] + 6, y + 27, w - x[5] - 14, 5);
      p.setPen(Qt::NoPen);
      p.setBrush(c.panelAlt);
      p.drawRoundedRect(bar, 2, 2);
      p.setBrush(s.historyDone ? c.up : c.accent);
      p.drawRoundedRect(QRectF(bar.left(), bar.top(), bar.width() * done, bar.height()), 2, 2);
      p.setPen(c.divider);
      p.drawLine(QPointF(0, y + kRow - 0.5), QPointF(w, y + kRow - 0.5));
    }
  }
private:
  QVector<Collector::Progress> m_rows;
  QHash<int, IndState> m_ind;
  QHash<int, std::array<qint64, 3>> m_holes;
  bool m_collecting = false;
};

// ------------------------------------------------------------------------------------------------ last bars
class BarsModel : public QAbstractTableModel {
public:
  void set(int tf, const QVector<Kline>& bars, const QHash<qint64, IndRow>& ind, const QStringList& cols, int warmup, bool forex) {
    beginResetModel();
    m_tf = tf; m_bars = bars; m_ind = ind; m_cols = cols; m_warmup = warmup; m_fx = forex;
    endResetModel();
  }
  int rowCount(const QModelIndex& = {}) const override { return int(m_bars.size()); }
  int columnCount(const QModelIndex& = {}) const override { return kFixed + int(m_cols.size()); }
  QVariant headerData(int s, Qt::Orientation o, int role) const override {
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
    static const char* fixed[] = {QT_TRANSLATE_NOOP("data", "time (UTC)"), QT_TRANSLATE_NOOP("data", "open"), QT_TRANSLATE_NOOP("data", "high"),
                                  QT_TRANSLATE_NOOP("data", "low"), QT_TRANSLATE_NOOP("data", "close"), QT_TRANSLATE_NOOP("data", "volume"),
                                  QT_TRANSLATE_NOOP("data", "trades"), QT_TRANSLATE_NOOP("data", "taker buy %")};
    return s < kFixed ? QCoreApplication::translate("data", fixed[s]) : m_cols.value(s - kFixed);
  }
  QVariant data(const QModelIndex& i, int role) const override {
    const Kline& k = m_bars[m_bars.size() - 1 - i.row()];   // newest first
    const auto ir = m_ind.constFind(k.t);
    if (role == Qt::ForegroundRole) {
      const auto& c = theme::pal();
      if (i.column() == 0) return c.textDim;
      if (i.column() >= kFixed && ir != m_ind.cend() && ir->warm) return c.textMute;   // warm-up: not settled yet
      if (i.column() == 4) return k.c >= k.o ? c.up : c.down;
      return c.text;
    }
    if (role == Qt::TextAlignmentRole) return int(i.column() == 0 ? Qt::AlignLeft | Qt::AlignVCenter : Qt::AlignRight | Qt::AlignVCenter);
    if (role == Qt::ToolTipRole && i.column() >= kFixed && ir != m_ind.cend() && ir->warm)
      return QCoreApplication::translate("data", "Warm-up: among the first %1 bars, long averages are not settled yet").arg(m_warmup);
    if (role != Qt::DisplayRole) return {};
    switch (i.column()) {
      case 0: return when(k.t, m_tf);
      case 1: return num(k.o, m_fx ? 5 : 2);
      case 2: return num(k.h, m_fx ? 5 : 2);
      case 3: return num(k.l, m_fx ? 5 : 2);
      case 4: return num(k.c, m_fx ? 5 : 2);
      case 5: return num(k.v, m_fx ? 2 : 3);
      case 6: return m_fx ? QStringLiteral("—") : QLocale(QLocale::English).toString(k.n);   // forex: no trade count
      case 7: return !m_fx && k.v > 0 ? num(k.tbv / k.v * 100, 1) : QStringLiteral("—");      // nor taker buys
      default: break;
    }
    if (ir == m_ind.cend()) return QStringLiteral("…");
    const double v = ir->v.value(i.column() - kFixed, NAN);
    const QString col = m_cols.value(i.column() - kFixed);
    const bool level = std::isfinite(v) && std::fabs(v) < 50;   // forex prices and their averages: five decimals
    const int dec = col == "obv" ? 1 : (col.startsWith("rsi") || col.startsWith("stoch") || col.startsWith("adx") || col.startsWith("di_") || col == "bb_width") ? 2
                  : col == "bb_pb" ? 4 : m_fx && level ? 5 : 2;
    return num(v, dec);
  }
private:
  static constexpr int kFixed = 8;
  int m_tf = 60, m_warmup = WARMUP;
  bool m_fx = false;
  QVector<Kline> m_bars;
  QHash<qint64, IndRow> m_ind;
  QStringList m_cols;
};

// The bars' table with its time column frozen on the left: the thirty-odd columns of prices and indicators scroll
// sideways under it and each row's time stays in view (audit 2026-11-24: 1,146 px to scroll at 1600x1000, and once
// scrolled nothing told which bar a value belonged to). Qt's "frozen column" pattern: a second view of the same model,
// showing only column 0, laid over the first one and scrolled with it.
class FrozenTimeTable : public QTableView {
public:
  explicit FrozenTimeTable(QWidget* parent) : QTableView(parent), m_frozen(new QTableView(this)) {
    m_frozen->setFocusPolicy(Qt::NoFocus);
    m_frozen->verticalHeader()->hide();
    m_frozen->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_frozen->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_frozen->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_frozen->setFrameShape(QFrame::NoFrame);
    m_frozen->setShowGrid(false);
    m_frozen->setSelectionMode(QAbstractItemView::NoSelection);
    viewport()->stackUnder(m_frozen);
    connect(horizontalHeader(), &QHeaderView::sectionResized, this, [this](int i, int, int w) {
      if (i == 0) { m_frozen->setColumnWidth(0, w); place(); }
    });
    connect(verticalHeader(), &QHeaderView::sectionResized, this, [this](int i, int, int h) { m_frozen->setRowHeight(i, h); });
    connect(m_frozen->verticalScrollBar(), &QAbstractSlider::valueChanged, verticalScrollBar(), &QAbstractSlider::setValue);
    connect(verticalScrollBar(), &QAbstractSlider::valueChanged, m_frozen->verticalScrollBar(), &QAbstractSlider::setValue);
  }
  void setModel(QAbstractItemModel* m) override {
    QTableView::setModel(m);
    m_frozen->setModel(m);
    // after the views' own handling of a reset: only the time column in the frozen view, as wide as the table's
    connect(m, &QAbstractItemModel::modelReset, this, [this] { sync(); }, Qt::QueuedConnection);
    connect(m, &QAbstractItemModel::columnsInserted, this, [this] { sync(); }, Qt::QueuedConnection);
    sync();
  }
  // the same look as the table (called once the table is styled)
  void copyLook() {
    m_frozen->setFont(font());
    m_frozen->horizontalHeader()->setFont(horizontalHeader()->font());
    m_frozen->horizontalHeader()->setDefaultAlignment(horizontalHeader()->defaultAlignment());
    m_frozen->verticalHeader()->setDefaultSectionSize(verticalHeader()->defaultSectionSize());
    m_frozen->setStyleSheet(styleSheet());
    sync();
  }

protected:
  void resizeEvent(QResizeEvent* e) override { QTableView::resizeEvent(e); sync(); }
  void scrollTo(const QModelIndex& index, ScrollHint hint = EnsureVisible) override {
    if (index.column() > 0) QTableView::scrollTo(index, hint);
  }

private:
  void sync() {
    if (!model()) return;
    for (int c = 1; c < model()->columnCount(); ++c) m_frozen->setColumnHidden(c, true);
    m_frozen->setColumnHidden(0, false);
    m_frozen->setColumnWidth(0, columnWidth(0));
    m_frozen->horizontalHeader()->setFixedHeight(horizontalHeader()->height());
    m_frozen->verticalScrollBar()->setValue(verticalScrollBar()->value());
    place();
  }
  void place() {
    // over the table's header and rows, never over its sideways scroll bar
    const int head = horizontalHeader()->height();
    m_frozen->setGeometry(viewport()->x(), viewport()->y() - head, columnWidth(0), viewport()->height() + head);
    m_frozen->setVisible(model() && model()->columnCount() > 1);
  }
  QTableView* m_frozen;
};

// ------------------------------------------------------------------------------------------------ page
class TechnicalDataPage : public QWidget {
public:
  explicit TechnicalDataPage(QWidget* parent) : QWidget(parent) {
    const auto& c = theme::pal();
    // the page's content: in the page itself, or (one column, or a page this short) in a vertical page scroller (see
    // setScroll). Its widgets are made in m_content, not moved there by the layouts: a widget moved under another parent
    // is polished again, which can change cached sizes and so the desktop layout.
    m_outer = new QVBoxLayout(this);
    m_outer->setContentsMargins(0, 0, 0, 0);
    m_outer->setSpacing(0);
    m_content = new QWidget(this);
    m_outer->addWidget(m_content);
    m_content->installEventFilter(this);
    auto* root = new QVBoxLayout(m_content);
    m_root = root;
    root->setContentsMargins(16, 12, 16, 10);
    root->setSpacing(8);
    auto* head = new QHBoxLayout;
    m_head = head;
    head->setSpacing(12);
    head->addWidget(ui::label(QCoreApplication::translate("data", "TECHNICAL DATA"), theme::title(), c.accentText, m_content));
    m_subtitle = ui::label(QCoreApplication::translate("data", "market bars of the pairs you record, with their indicators"), theme::mono(11), c.textDim, m_content);
    head->addWidget(m_subtitle);
    head->addStretch(1);
    m_status = ui::label(QString(), theme::mono(10), c.textDim, m_content);
    head->addWidget(m_status);
    // everything paused / resumed (kept across restarts)
    m_pauseAll = button(QString(), m_content);
    connect(m_pauseAll, &QPushButton::clicked, this, [] { Store::instance().setAllPaused(!Store::instance().allPaused()); });
    head->addWidget(m_pauseAll);
    root->addLayout(head);
    // one column: the subtitle (wrapped) and the file's size under the title row (hidden, and empty, otherwise)
    m_head2 = new QWidget(m_content);
    m_head2L = new QVBoxLayout(m_head2);
    m_head2L->setContentsMargins(0, 0, 0, 0);
    m_head2L->setSpacing(4);
    m_head2->hide();
    root->addWidget(m_head2);

    auto* body = new QHBoxLayout;
    m_body = body;
    body->setSpacing(12);
    root->addLayout(body, 1);

    // left: the collection
    auto* left = new ui::Panel(QCoreApplication::translate("data", "Collection"), m_content);
    m_left = left;
    left->setInfo(QCoreApplication::translate("data", "Closed bars of each pair you record: its timeframes, from its first day or a day you choose, "
                     "and its indicators (SETTINGS; + PAIR for another). Crypto: Binance's public klines, caught up at launch and "
                     "kept up every minute; a hole is asked for once more, what stays is the exchange's own outage. Forex: "
                     "Dukascopy's bid prices, the day cut at 17:00 New York; Dukascopy publishes a day once it is over, so the "
                     "newest bars are yesterday's; no bar on weekends. Indicators are Charts' engine, on each series' whole history."));
    left->setMinimumWidth(660);
    left->setMaximumWidth(700);
    // the pairs: one picked, its settings; another added
    auto* addPair = button(QCoreApplication::translate("data", "+ PAIR"), left);
    addPair->setToolTip(QCoreApplication::translate("data", "Record another pair (Binance spot crypto, or a forex major from Dukascopy): its timeframes, first day and indicators"));
    connect(addPair, &QPushButton::clicked, this, [this] { watch(openPairDialog(this, QString(), [this](const QString& m) { select(m); })); });
    m_settings = button(QCoreApplication::translate("data", "SETTINGS"), left);
    m_settings->setToolTip(QCoreApplication::translate("data", "Timeframes, first day and indicators of this pair; remove it"));
    connect(m_settings, &QPushButton::clicked, this, [this] {
      if (!market().isEmpty()) watch(openPairDialog(this, market(), [this](const QString&) { reloadPairs(); }));
    });
    m_pausePair = button(QString(), left);
    connect(m_pausePair, &QPushButton::clicked, this, [this] {
      if (!market().isEmpty()) Store::instance().setPaused(market(), !Store::instance().pairPaused(market()));
    });
    m_cancel = button(QCoreApplication::translate("data", "CANCEL"), left);
    m_cancel->setToolTip(QCoreApplication::translate("data", "Stop this pair's collection and delete what it downloaded; the pair stays, paused and empty"));
    connect(m_cancel, &QPushButton::clicked, this, [this] { cancelPair(); });
    left->headerRight()->addWidget(m_pausePair);
    left->headerRight()->addWidget(m_cancel);
    left->headerRight()->addWidget(m_settings);
    left->headerRight()->addWidget(addPair);
    m_pairButtons = {m_pausePair, m_cancel, m_settings, addPair};
    // phones: these four buttons on a row of their own at the top of the panel (its title row has no room for them)
    m_tools = new QWidget(left);
    m_toolsL = new QHBoxLayout(m_tools);
    m_toolsL->setContentsMargins(0, 0, 0, 0);
    m_toolsL->setSpacing(6);
    m_toolsL->addStretch(1);
    m_tools->hide();
    left->body()->addWidget(m_tools);
    m_pairBar = new ui::PillBar({}, left);
    connect(m_pairBar, &ui::PillBar::changed, this, [this](int i) {
      if (i >= 0 && i < m_pairs.size()) settings::setValue("technicaldata.pair", m_pairs[i]);
      syncPauseButtons();
      pairPicked();
    });
    left->body()->addWidget(m_pairBar);
    m_market = ui::label(QString(), theme::mono(11), c.textDim, left);
    m_market->setWordWrap(true);
    left->body()->addWidget(m_market);
    m_view = new CollectionView(left);
    left->body()->addWidget(m_view);
    m_fpView = new FootprintCollectionView(left);   // the pair's footprint markets, when it records them
    left->body()->addWidget(m_fpView);
    m_jobs = ui::label(QString(), theme::mono(10), c.textDim, left);
    m_jobs->setWordWrap(true);
    left->body()->addWidget(m_jobs);
    m_note = ui::label(QString(), theme::sans(11), c.textMute, left);
    m_note->setWordWrap(true);
    left->body()->addWidget(m_note);
    left->body()->addStretch(1);
    body->addWidget(left);

    // right: the last bars of a timeframe
    auto* right = new ui::Panel(QCoreApplication::translate("data", "Last bars"), m_content);
    m_rightPanel = right;
    right->setInfo(QCoreApplication::translate("data", "The newest 300 bars of the timeframe as stored, newest first, with every indicator. Taker buy % is the "
                      "share of the volume bought by market orders. Greyed values are in the warm-up."));
    QStringList tfs;
    for (const auto& t : timeframes()) tfs << t.name;
    m_tfBar = new ui::PillBar(tfs, right);
    m_tfBar->setCurrent(3);   // 1h
    right->headerRight()->addWidget(m_tfBar);
    m_table = new FrozenTimeTable(right);
    m_table->setModel(&m_model);
    m_table->verticalHeader()->hide();
    m_table->setShowGrid(false);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->setFont(theme::mono(11));
    m_table->horizontalHeader()->setFont(theme::microLabel());
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_table->verticalHeader()->setDefaultSectionSize(22);
    m_table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_table->setStyleSheet(QString("QTableView { background: %1; border: none; color: %2; }"
                                   "QHeaderView::section { background: %1; color: %3; border: none; border-bottom: 1px solid %4; padding: 4px 8px; }"
                                   "QTableView::item { border-bottom: 1px solid %5; padding: 0 8px; }")
                               .arg(c.panel.name(), c.text.name(), c.textMute.name(), c.divider.name(), theme::alpha(c.divider, 120).name(QColor::HexArgb)));
    m_table->copyLook();   // the time column, frozen on the left, looks like the rest
    right->body()->addWidget(m_table, 1);
    // the footprint's own table (user's choice 2026-11-20), under the bars
    auto* rightCol = new QVBoxLayout;
    rightCol->setSpacing(12);
    rightCol->addWidget(right, 1);
    m_fpPanel = new FootprintPanel(m_content);
    rightCol->addWidget(m_fpPanel, 1);
    body->addLayout(rightCol, 1);

    // another timeframe has other widths (a day's volume or ATR is longer than an hour's): sized again, else cut with …
    connect(m_tfBar, &ui::PillBar::changed, this, [this](int) { m_barsDirty = m_firstFill = true; refresh(); });
    connect(&Store::instance(), &Store::pairsChanged, this, [this](const QString&) { m_deleting.clear(); reloadPairs(); });
    connect(&Store::instance(), &Store::pausedChanged, this, [this](const QString&) { reloadPairs(); });
    auto dirty = [this] { m_dirty = true; };
    connect(&Collector::instance(), &Collector::progressChanged, this, dirty);
    connect(&ForexCollector::instance(), &ForexCollector::progressChanged, this, dirty);
    connect(&ArchiveCollector::instance(), &ArchiveCollector::progressChanged, this, dirty);
    connect(&Store::instance(), &Store::footprintChanged, this, dirty);
    connect(&Indicators::instance(), &Indicators::progressChanged, this, dirty);
    connect(&Store::instance(), &Store::changed, this, [this](const QString&, int tf, qint64) { m_dirty = true; if (tf == currentTf()) m_barsDirty = true; });
    connect(&Store::instance(), &Store::indicatorsChanged, this, [this](const QString&, int tf) { m_dirty = true; if (tf == currentTf()) m_barsDirty = true; });
    m_timer.setInterval(1500);   // at most every 1.5 s while visible: the collector writes several pages a second
    connect(&m_timer, &QTimer::timeout, this, [this] { if (m_dirty || m_barsDirty) refresh(); });
    reloadPairs();
    // test hooks: the pair picked (BINANCE:ETHUSDT), the timeframe shown (1m 5m 15m 1h 4h 1D 1W or its index), the
    // settings window opened ("new" or a pair)
    if (qEnvironmentVariableIsSet("DW_TECHNICALDATA_PAIR")) select(qEnvironmentVariable("DW_TECHNICALDATA_PAIR"));
    if (qEnvironmentVariableIsSet("DW_TECHNICALDATA_TF")) {
      const QString t = qEnvironmentVariable("DW_TECHNICALDATA_TF");
      const int i = m_tfNames.indexOf(t);
      m_tfBar->setCurrent(i >= 0 ? i : t.toInt(), true);   // as a click: the table sized again for that timeframe
    }
    // test hook: a sequence of steps, as a user would do them, with what the page shows written down at each step:
    //   DW_TECHNICALDATA_STEPS="pick:BINANCE:ETHUSDT;waitfor:complete:90000;dump:/tmp/a.txt;pause;dump:/tmp/a.txt;
    //   cancel;wait:3000;shot:/tmp/a.png;resume;pauseall;resumeall;tf:1D;quit"   (pause / resume / cancel / pauseall /
    //   resumeall click the buttons; cancel answers its question "Stop and delete")
    if (qEnvironmentVariableIsSet("DW_TECHNICALDATA_STEPS")) {
      m_steps = qEnvironmentVariable("DW_TECHNICALDATA_STEPS").split(';', Qt::SkipEmptyParts);
      QTimer::singleShot(1500, this, [this] { step(); });
    }
    if (qEnvironmentVariableIsSet("DW_TECHNICALDATA_DIALOG"))
      QTimer::singleShot(600, this, [this] {
        const QString d = qEnvironmentVariable("DW_TECHNICALDATA_DIALOG");
        watch(openPairDialog(this, d == "new" ? QString() : d, [this](const QString& m) { select(m); }));
      });
  }

protected:
  void showEvent(QShowEvent* e) override {
    QWidget::showEvent(e);
    m_dirty = m_barsDirty = true;
    refresh();
    m_timer.start();
  }
  void hideEvent(QHideEvent* e) override {
    m_timer.stop();
    QWidget::hideEvent(e);
  }
  // Under 960 px (a phone, upright or sideways) the panels go in one column and the header on two rows; on a phone
  // (ui::compact) the panels' title rows also give their buttons rows of their own. One column, or a page this short,
  // scrolls.
  // From 960 px wide and 420 px tall, the desktop layout as it was.
  void resizeEvent(QResizeEvent* e) override {
    QWidget::resizeEvent(e);
    const int W = width();
    setColumn(W < 960);
    setPhone(ui::compact(W));
    setScroll(m_oneCol || height() < 420);
    if (m_pageScroll) m_pageScroll->relayout();
  }
  // the content's layout changed (a text, a panel shown): its height in the page scroller follows, once per pass
  bool eventFilter(QObject* o, QEvent* e) override {
    if (o == m_content && e->type() == QEvent::LayoutRequest && m_pageScroll && !m_relayoutQueued) {
      m_relayoutQueued = true;
      QTimer::singleShot(0, this, [this] { m_relayoutQueued = false; if (m_pageScroll) m_pageScroll->relayout(); });
    }
    return QWidget::eventFilter(o, e);
  }

private:
  void setColumn(bool on) {
    if (on == m_oneCol) return;
    m_oneCol = on;
    m_body->setDirection(on ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    m_left->setMinimumWidth(on ? 0 : 660);
    m_left->setMaximumWidth(on ? QWIDGETSIZE_MAX : 700);
    // header: title and PAUSE ALL; under them the subtitle, wrapped, and the file's size (one row is ~1000 px wide)
    m_head->removeWidget(m_subtitle); m_head->removeWidget(m_status);
    m_head2L->removeWidget(m_subtitle); m_head2L->removeWidget(m_status);
    m_subtitle->setWordWrap(on);
    if (on) { m_head2L->addWidget(m_subtitle); m_head2L->addWidget(m_status); }
    else { m_head->insertWidget(1, m_subtitle); m_head->insertWidget(3, m_status); }   // title, subtitle, stretch, status, PAUSE ALL
    m_head2->setVisible(on);
    if (!on)
      for (QWidget* w : {static_cast<QWidget*>(m_rightPanel), static_cast<QWidget*>(m_fpPanel)}) { w->setMinimumHeight(0); w->setMaximumHeight(QWIDGETSIZE_MAX); }
  }
  void setPhone(bool on) {
    if (on == m_phone) return;
    m_phone = on;
    // the collection's buttons: the panel's title row, or their own row
    for (int i = 0; i < m_pairButtons.size(); ++i) {
      m_left->headerRight()->removeWidget(m_pairButtons[i]);
      m_toolsL->removeWidget(m_pairButtons[i]);
      if (on) m_toolsL->insertWidget(i, m_pairButtons[i]);
      else m_left->headerRight()->addWidget(m_pairButtons[i]);
      m_pairButtons[i]->setMinimumHeight(on ? 34 : 0);
    }
    m_tools->setVisible(on);
    m_pauseAll->setMinimumHeight(on ? 34 : 0);
    // the timeframes: the panel's title row, or over the table
    m_rightPanel->headerRight()->removeWidget(m_tfBar);
    m_rightPanel->body()->removeWidget(m_tfBar);
    if (on) m_rightPanel->body()->insertWidget(0, m_tfBar, 0, Qt::AlignLeft);
    else m_rightPanel->headerRight()->addWidget(m_tfBar);
    // pills a finger can hit (28 px tall)
    m_tfBar->setFontPx(on ? 14 : 11);
    m_pairBar->setFontPx(on ? 14 : 11);
    m_fpPanel->setPhone(on);
  }
  void setScroll(bool on) {
    if (on == (m_pageScroll != nullptr)) return;
    if (on) {
      m_outer->removeWidget(m_content);
      m_pageScroll = new global::PageScroll(m_content, this);
      m_pageScroll->layout = [this](int w, int vh) { return contentHeight(w, vh); };
      m_outer->addWidget(m_pageScroll);
      m_content->show();
    } else {
      m_outer->removeWidget(m_pageScroll);
      m_pageScroll->takeWidget();
      m_content->setParent(this);
      m_outer->addWidget(m_content);
      m_content->show();
      m_pageScroll->hide();
      m_pageScroll->deleteLater();
      m_pageScroll = nullptr;
    }
  }
  // height of the content in the page scroller: two columns (a short page), taller than the page; one column, each
  // panel's own height, the two tables about a screen each
  int contentHeight(int w, int vh) {
    if (!m_oneCol) return std::max(vh, 640);
    m_rightPanel->setFixedHeight(std::clamp(vh - 40, 360, 480));
    m_fpPanel->setFixedHeight(m_phone ? std::clamp(vh, 560, 760) : std::clamp(vh + 60, 420, 560));
    const int h = m_root->hasHeightForWidth() ? m_root->totalHeightForWidth(w) : m_root->totalSizeHint().height();
    return std::max(h, m_root->totalMinimumSize().height());
  }
  static QPushButton* button(const QString& text, QWidget* parent) {
    auto* b = new QPushButton(text, parent);
    const auto& c = theme::pal();
    b->setCursor(Qt::PointingHandCursor);
    b->setFont(theme::mono(9, QFont::Bold, 1.2));
    b->setStyleSheet(QString("QPushButton { background: transparent; color: %1; border: 1px solid %2; border-radius: 3px; padding: 3px 9px; }"
                             "QPushButton:hover { color: %3; border-color: %4; } QPushButton:disabled { color: %5; border-color: %2; }")
                         .arg(c.textDim.name(), c.border.name(), c.text.name(), c.borderHi.name(), c.textMute.name()));
    return b;
  }
  int currentTf() const {
    const QString n = m_tfNames.value(m_tfBar->current());
    for (const auto& t : timeframes()) if (t.name == n) return t.seconds;
    return 3600;
  }
  QString market() const { return m_pairs.value(m_pairBar->current()); }
  // saved: the store deletes what is no longer wanted (seconds for a whole 1m history) before pairsChanged
  void watch(QDialog* d) {
    connect(d, &QDialog::accepted, this, [this] {
      m_deleting = QCoreApplication::translate("data", "Writing the pair's changes (and deleting what is no longer recorded)…");
      m_dirty = true;
      refresh();
    });
  }
  void select(const QString& m) {
    reloadPairs();
    const int i = int(m_pairs.indexOf(m));
    if (i < 0) return;
    settings::setValue("technicaldata.pair", m);
    m_pairBar->setCurrent(i);
    pairPicked();
  }
  // the store's pairs as pills (the one picked kept, else the one saved, else the first)
  void reloadPairs() {
    const QString was = market().isEmpty() ? settings::value("technicaldata.pair").toString() : market();
    m_pairs = Store::instance().pairs();
    QStringList names;
    for (const QString& p : m_pairs)
      names << binanceSymbol(p) + (Store::instance().cancelled(p) ? QStringLiteral(" ✕") : Store::instance().pairPaused(p) ? QStringLiteral(" ⏸") : QString());
    m_pairBar->setItems(names);
    m_pillNames = names;
    m_pairBar->setVisible(!m_pairs.isEmpty());
    const int i = int(m_pairs.indexOf(was));
    m_pairBar->setCurrent(i >= 0 ? i : 0);
    m_settings->setEnabled(!m_pairs.isEmpty());
    syncPauseButtons();
    pairPicked();
  }
  // ---- test steps (DW_TECHNICALDATA_STEPS)
  void step() {
    if (m_steps.isEmpty()) return;
    const QString st = m_steps.takeFirst().trimmed();
    const QString cmd = st.section(':', 0, 0), arg = st.section(':', 1);
    int delay = 400;
    if (cmd == "wait") delay = arg.toInt();
    else if (cmd == "pick") select(arg);
    else if (cmd == "tf") { const int i = int(m_tfNames.indexOf(arg)); if (i >= 0) m_tfBar->setCurrent(i, true); }
    else if (cmd == "pause" || cmd == "resume") { if (m_pausePair->isEnabled()) m_pausePair->click(); }
    else if (cmd == "cancel") { if (m_cancel->isEnabled()) cancelPair(false); }
    else if (cmd == "pauseall" || cmd == "resumeall") m_pauseAll->click();
    else if (cmd == "shot") window()->grab().save(arg);
    else if (cmd == "fptab") m_fpPanel->setMode(arg);   // the order flow panel's tab: fp, depth, oi, live
    else if (cmd == "dump") dump(arg, QString::number(m_stepNo));
    else if (cmd == "quit") { qApp->quit(); return; }
    else if (cmd == "waitfor") {   // waitfor:<text in every row's state>:<ms>
      const QString want = arg.section(':', 0, 0);
      const int ms = arg.section(':', 1).toInt();
      if (!m_waitClock.isValid()) m_waitClock.start();
      refresh();
      bool all = !m_view->rows().isEmpty();
      for (const auto& r : m_view->rows()) all &= stateOf(r, m_view->collecting()).first.contains(want);
      if (!all && m_waitClock.elapsed() < ms) { m_steps.prepend(st); QTimer::singleShot(500, this, [this] { step(); }); return; }
      m_waitClock.invalidate();
    }
    ++m_stepNo;
    QTimer::singleShot(delay, this, [this] { m_dirty = m_barsDirty = true; refresh(); step(); });
  }
  void dump(const QString& file, const QString& label) {
    m_dirty = true;
    refresh();
    QFile f(file);
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    QTextStream o(&f);
    auto btn = [](QPushButton* b) { return "\"" + b->text() + "\"" + (b->isEnabled() ? "" : "(off)"); };
    o << "== step " << label << "  pair=" << market() << "\n";
    o << "pills: " << m_pillNames.join(" | ") << "\n";
    o << "buttons: " << btn(m_pausePair) << " " << btn(m_cancel) << " " << btn(m_settings) << " all=" << btn(m_pauseAll) << "\n";
    o << "summary: " << m_market->text() << "\n";
    for (const auto& r : m_view->rows())
      o << "row " << r.tf << " bars=" << r.state.count << " state=\"" << stateOf(r, m_view->collecting()).first << "\"\n";
    for (const auto& r : m_fpView->rows())
      o << "archive " << r.kind << " days=" << r.stored << "/" << r.wanted << " missing=" << r.missing << " bytes=" << r.bytes
        << " state=\"" << footprintState(r, ArchiveCollector::instance().started()).first << "\"\n";
    if (m_fpPanel->isVisible()) o << m_fpPanel->dumpText() << "\n";
    o << "note: " << m_note->text() << "\n";
    if (!m_jobs->text().isEmpty()) o << "jobs: " << m_jobs->text().replace('\n', " / ") << "\n";
  }
  void syncPauseButtons() {
    auto& st = Store::instance();
    const bool all = st.allPaused(), pair = st.pairPaused(market()), cancelled = st.cancelled(market());
    m_pauseAll->setText(all ? QCoreApplication::translate("data", "▶ RESUME ALL") : QCoreApplication::translate("data", "⏸ PAUSE ALL"));
    m_pauseAll->setToolTip(QCoreApplication::translate("data", "Every pair's collection, crypto and forex; kept when Deepwater opens again"));
    m_pausePair->setText(cancelled ? QCoreApplication::translate("data", "▶ START AGAIN")
                         : pair ? QCoreApplication::translate("data", "▶ RESUME") : QCoreApplication::translate("data", "⏸ PAUSE"));
    m_pausePair->setToolTip(cancelled ? QCoreApplication::translate("data", "Collect this pair again from its first day")
                            : pair ? QCoreApplication::translate("data", "Collect this pair again, from where it stopped")
                                   : QCoreApplication::translate("data", "Stop collecting this pair for now; what is stored stays (kept when Deepwater opens again)"));
    // a cancel, removal or settings save still being written: nothing to click on that pair meanwhile
    const bool busy = st.busy(market());
    m_pausePair->setEnabled(!market().isEmpty() && !busy);
    m_cancel->setEnabled(!market().isEmpty() && !cancelled && !busy);   // nothing left to cancel
    m_settings->setEnabled(!market().isEmpty() && !busy);
  }
  // cancel (user's choice): the collection stops and what it downloaded goes; the pair stays, paused and empty
  void cancelPair(bool ask = true) {
    const QString m = market();
    if (m.isEmpty()) return;
    auto& st = Store::instance();
    qint64 n = 0;
    for (const auto& t : timeframes()) n += st.state(m, t.seconds).count;
    if (ask) {
    QMessageBox box(QMessageBox::Warning, QCoreApplication::translate("data", "Cancel %1's collection?").arg(binanceSymbol(m)),
                    QCoreApplication::translate("data", "The collection of %1 stops and everything it downloaded is deleted (%2 bars and their "
                                                "indicators). The pair stays in the list, paused and empty: RESUME starts it again from its first day.")
                        .arg(binanceSymbol(m), QLocale(QLocale::English).toString(n)),
                    QMessageBox::NoButton, this);
    box.setTextFormat(Qt::PlainText);
    auto* yes = box.addButton(QCoreApplication::translate("data", "Stop and delete"), QMessageBox::DestructiveRole);
    box.addButton(QCoreApplication::translate("data", "Keep"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != yes) return;
    }
    m_deleting = QCoreApplication::translate("data", "Deleting what %1's collection downloaded…").arg(binanceSymbol(m));
    m_dirty = true;
    refresh();
    st.cancelPair(m, this, [this] { m_deleting.clear(); m_dirty = m_barsDirty = m_firstFill = true; refresh(); });
  }
  // the pair's timeframes on the right (the one shown kept when it has it)
  void pairPicked() {
    const PairSpec spec = specFromJson(Store::instance().pairSpec(market()));
    const QString was = m_tfNames.value(m_tfBar->current(), "1h");
    m_tfNames.clear();
    for (const auto& t : timeframes()) if (spec.timeframes.contains(t.seconds)) m_tfNames << t.name;
    m_tfBar->setItems(m_tfNames);
    const int i = int(m_tfNames.indexOf(was)), h = int(m_tfNames.indexOf("1h"));
    m_tfBar->setCurrent(i >= 0 ? i : h >= 0 ? h : 0);
    m_fpPanel->setMarket(market());
    m_dirty = m_barsDirty = m_firstFill = true;
    refresh();
  }
  void refresh() {
    auto& col = Collector::instance();
    auto& ind = Indicators::instance();
    auto& st = Store::instance();
    const QString mk = market();
    const PairSpec spec = specFromJson(st.pairSpec(mk));
    if (m_dirty) {
      m_dirty = false;
      QHash<int, IndState> is;
      QHash<int, std::array<qint64, 3>> holes;
      for (const auto& t : timeframes()) {
        if (!spec.timeframes.contains(t.seconds)) continue;
        is.insert(t.seconds, st.indState(mk, t.seconds));
        std::array<qint64, 3> h{0, 0, 0};
        for (const Gap& g : st.gaps(mk, t.seconds)) { ++h[0]; h[1] += g.missing; h[2] += !g.checked; }
        holes.insert(t.seconds, h);
      }
      auto& fxc = ForexCollector::instance();
      const bool fx = dukascopy::isForex(mk);
      const bool collecting = fx ? fxc.started() : col.started();
      QVector<Collector::Progress> rows;
      for (const auto& r : fx ? fxc.progress() : col.progress()) if (r.market == mk) rows << r;
      m_view->setRows(rows, is, holes, collecting);
      m_view->setVisible(!mk.isEmpty());
      QVector<ArchiveCollector::Progress> fpRows;
      for (const auto& r : ArchiveCollector::instance().progress()) if (r.market == mk) fpRows << r;
      m_fpView->setRows(fpRows, ArchiveCollector::instance().started());
      m_fpView->setVisible(!fpRows.isEmpty());
      m_fpPanel->setMarket(mk);   // its settings may have changed
      const int nCols = int(columnsOf(spec).size());
      m_market->setText(mk.isEmpty() ? QCoreApplication::translate("data", "No pair recorded: add one with + PAIR.")
                        : QStringList{mk + "  ·  " + (fx ? QCoreApplication::translate("data", "Dukascopy, bid prices") : QCoreApplication::translate("data", "Binance spot")),
                                      spec.start ? QCoreApplication::translate("data", "from %1").arg(when(spec.start, 86400))
                                                 : QCoreApplication::translate("data", "from its first day"),
                                      QCoreApplication::translate("data", "%n timeframe(s)", "", int(spec.timeframes.size())),
                                      QCoreApplication::translate("data", "%n indicator column(s)", "", nCols)}.join("  ·  "));
      QStringList jobs;
      if (!m_deleting.isEmpty()) jobs << m_deleting;
      if (!col.status().isEmpty()) jobs << QCoreApplication::translate("data", "Download: %1").arg(col.status());
      if (!fxc.status().isEmpty()) jobs << QCoreApplication::translate("data", "Download: %1").arg(fxc.status());
      if (!ind.status().isEmpty()) jobs << ind.status();
      m_jobs->setText(jobs.join("\n"));
      m_status->setText(QCoreApplication::translate("data", "technical-data.sqlite · %1 MB").arg(QLocale(QLocale::English).toString(double(st.fileBytes()) / 1e6, 'f', 0)));
      const bool paused = st.isPaused(mk);
      m_note->setText(paused ? (st.allPaused() ? QCoreApplication::translate("data", "The whole collection is paused (▶ RESUME ALL at the top). What is stored stays; nothing "
                                                                                  "is downloaded until it is resumed, even after Deepwater opens again.")
                                               : st.cancelled(mk) ? QCoreApplication::translate("data", "This pair's collection was cancelled: what it had downloaded is deleted. "
                                                                                                       "▶ START AGAIN collects it from its first day.")
                                               : QCoreApplication::translate("data", "This pair's collection is paused. What is stored stays; ▶ RESUME goes on from where it stopped."))
                      : !collecting ? (fx ? QCoreApplication::translate("data", "Not collecting: the collection starts 20 s after Deepwater opens (never in a test profile), "
                                                                 "unless Dukascopy is turned off in Sources.")
                                        : QCoreApplication::translate("data", "Not collecting: the collection starts 20 s after Deepwater opens (never in a test profile), "
                                                                 "unless Binance is turned off in Sources."))
                      : fx ? QCoreApplication::translate("data", "Dukascopy answers slowly (about 15 s a file, two at once, often refusing for a while): the hours come first, a "
                                                         "monthly file each, then the minute files that 5m and 15m are made of, a day each, over days for "
                                                         "a history from 2003. The pairs take turns. A holiday stays listed as a hole; weekends are not holes.")
                           : QCoreApplication::translate("data", "Bars are only stored once closed. A hole the exchange never filled stays listed."));
    }
    if (m_barsDirty) {
      m_barsDirty = false;
      const int tf = currentTf();
      const QVector<Kline> bars = st.last(mk, tf, 300);
      QHash<qint64, IndRow> rows;
      if (!bars.isEmpty())
        for (const IndRow& r : st.indicators(mk, tf, bars.first().t, bars.last().t)) rows.insert(r.t, r);
      const int scroll = m_table->horizontalScrollBar()->value();
      m_model.set(tf, bars, rows, st.indicatorColumns(mk), warmupOf(spec), dukascopy::isForex(mk));
      // sized to the values once there are some (indicators come a little after the bars)
      if (m_firstFill) {
        m_table->ensurePolished();   // the style sheet's padding counted, even when filled before the page is shown
        m_table->resizeColumnsToContents();
        m_firstFill = bars.isEmpty() || rows.isEmpty();
      }
      m_table->horizontalScrollBar()->setValue(scroll);
    }
  }

  QLabel *m_status, *m_market, *m_jobs, *m_note;
  CollectionView* m_view;
  FootprintCollectionView* m_fpView;
  FootprintPanel* m_fpPanel;
  ui::PillBar *m_tfBar, *m_pairBar;
  QPushButton *m_settings, *m_pausePair, *m_cancel, *m_pauseAll;
  QStringList m_pairs, m_tfNames, m_pillNames, m_steps;
  int m_stepNo = 0;
  QElapsedTimer m_waitClock;
  QString m_deleting;
  FrozenTimeTable* m_table;
  BarsModel m_model;
  QTimer m_timer;
  bool m_dirty = true, m_barsDirty = true, m_firstFill = true;
  // one column / phone layout (see setColumn / setPhone / setScroll)
  QVBoxLayout *m_outer = nullptr, *m_root = nullptr, *m_head2L = nullptr;
  QHBoxLayout *m_head = nullptr, *m_body = nullptr, *m_toolsL = nullptr;
  QWidget *m_content = nullptr, *m_head2 = nullptr, *m_tools = nullptr;
  QLabel* m_subtitle = nullptr;
  ui::Panel *m_left = nullptr, *m_rightPanel = nullptr;
  QList<QPushButton*> m_pairButtons;
  global::PageScroll* m_pageScroll = nullptr;
  bool m_oneCol = false, m_phone = false, m_relayoutQueued = false;
};
}  // namespace technicaldatapage

using TechnicalDataPageImpl = technicaldatapage::TechnicalDataPage;
DW_PAGE("data/technical-data", TechnicalDataPageImpl)
