#include "pages/market/watch/InstrumentSheet.h"
#include "pages/market/watch/CandleChart.h"
#include "pages/market/watch/SheetCards.h"
#include "pages/market/watch/WatchCalc.h"
#include "pages/market/watch/WatchIcons.h"
#include "pages/market/watch/WatchStyle.h"
#include "market/Instruments.h"
#include "analytics/Technical.h"
#include "macro/CentralBanks.h"
#include "macro/KeyFigures.h"
#include "macro/MacroStore.h"
#include "positioning/Cot.h"
#include "positioning/Retail.h"
#include "seasonality/Seasonality.h"
#include "ui/Format.h"
#include "core/I18n.h"
#include <QCoreApplication>
#include "ui/Theme.h"
#include "ui/Touch.h"
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QDateTime>
#include <QTimeZone>

namespace watch {
static constexpr int kHeader = 38, kM = 8, kRight = 384, kChartH = 644;

InstrumentSheet::InstrumentSheet(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  m_scroll = new QScrollArea(this);
  m_scroll->setFrameShape(QFrame::NoFrame);
  m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);   // the reference shows no scroll bar
  m_scroll->setWidgetResizable(false);
  m_content = new QWidget;
  m_content->setAutoFillBackground(false);
  m_scroll->setWidget(m_content);
  m_scroll->viewport()->setAutoFillBackground(false);
  m_scroll->viewport()->installEventFilter(this);
  m_scrollAnim = new QVariantAnimation(this);
  m_scrollAnim->setDuration(380); m_scrollAnim->setEasingCurve(QEasingCurve::OutCubic);
  connect(m_scrollAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { m_scroll->verticalScrollBar()->setValue(v.toInt()); });

  m_chart = new CandleChart(m_content);
  m_retail = new RetailCard(m_content);
  m_cot = new CotCard(m_content);
  m_data = new DataCompareCard(m_content);
  m_tech = new TechTilesCard(m_content);
  m_perf = new IndexPerfCard(m_content);
  m_meeting = new MeetingCard(m_content);
  m_month = new SeasonMonthCard(m_content);
  m_path = new YearPathCard(m_content);
  m_retail->setSource(QCoreApplication::translate("watch", "Myfxbook community outlook"));
  m_cot->setSource(QCoreApplication::translate("watch", "CFTC Commitments of Traders"));
  m_data->setSource(QCoreApplication::translate("watch", "Macro releases"));
  m_meeting->setSource(QCoreApplication::translate("watch", "Rate futures"));
  m_perf->setFramelessWhileComputing(true);
  // cards change height with their state (reference: compact while loading, full when loaded) → reflow
  for (SheetCard* c : std::initializer_list<SheetCard*>{m_retail, m_cot, m_data, m_tech, m_perf, m_meeting, m_month, m_path})
    connect(c, &SheetCard::stateChanged, this, [this] { relayout(); });
  connect(m_chart, &CandleChart::loaded, this, [this] { relayout(); });

  // services feeding the cards; each refills its card while a symbol is open
  connect(&positioning::Retail::instance(), &positioning::Retail::changed, this, [this] { if (!m_symbol.isEmpty()) feedRetail(); });
  connect(&macro::MacroStore::instance(), &macro::MacroStore::updated, this, [this] { if (!m_symbol.isEmpty()) feedMacro(); });
  macro::KeyFigures::instance().start();
  connect(&macro::KeyFigures::instance(), &macro::KeyFigures::changed, this, [this] { if (!m_symbol.isEmpty() && isVisible()) feedMacro(); });
  connect(&macro::CentralBanks::instance(), &macro::CentralBanks::updated, this, [this] { if (!m_symbol.isEmpty()) feedMeetings(); });
}

void InstrumentSheet::showEvent(QShowEvent*) { positioning::Retail::instance().setActive(this, true); }
void InstrumentSheet::hideEvent(QHideEvent*) { positioning::Retail::instance().setActive(this, false); }

QStringList InstrumentSheet::legsOf(const QString& symbol) {
  const auto* i = market::find(symbol);
  if (i && i->category == market::Category::Forex) return {i->base, i->quote};
  return {currencyOf(symbol)};
}
QString InstrumentSheet::bankOf(const QString& c) {
  static const QHash<QString, const char*> m{
    {"USD", QT_TRANSLATE_NOOP("watch", "Federal Reserve")}, {"EUR", QT_TRANSLATE_NOOP("watch", "ECB")}, {"GBP", QT_TRANSLATE_NOOP("watch", "Bank of England")}, {"JPY", QT_TRANSLATE_NOOP("watch", "Bank of Japan")},
    {"CHF", QT_TRANSLATE_NOOP("watch", "Swiss National Bank")}, {"CAD", QT_TRANSLATE_NOOP("watch", "Bank of Canada")}, {"AUD", QT_TRANSLATE_NOOP("watch", "Reserve Bank of Australia")},
    {"NZD", QT_TRANSLATE_NOOP("watch", "Reserve Bank of New Zealand")}, {"CNH", QT_TRANSLATE_NOOP("watch", "People's Bank of China")}, {"CNY", QT_TRANSLATE_NOOP("watch", "People's Bank of China")},
    {"MXN", QT_TRANSLATE_NOOP("watch", "Banxico")}, {"ZAR", QT_TRANSLATE_NOOP("watch", "South African Reserve Bank")}, {"TRY", QT_TRANSLATE_NOOP("watch", "CBRT")}, {"SEK", QT_TRANSLATE_NOOP("watch", "Riksbank")},
    {"NOK", QT_TRANSLATE_NOOP("watch", "Norges Bank")}, {"SGD", QT_TRANSLATE_NOOP("watch", "MAS")}, {"HKD", QT_TRANSLATE_NOOP("watch", "HKMA")}, {"PLN", QT_TRANSLATE_NOOP("watch", "NBP")}, {"INR", QT_TRANSLATE_NOOP("watch", "RBI")}, {"BRL", QT_TRANSLATE_NOOP("watch", "BCB")},
    {"KRW", QT_TRANSLATE_NOOP("watch", "Bank of Korea")}};
  const char* n = m.value(c, nullptr);
  return n ? QCoreApplication::translate("watch", n) : c;
}

void InstrumentSheet::open(const QString& symbol) {
  m_symbol = symbol; ++m_gen;
  m_scrollAnim->stop(); m_scroll->verticalScrollBar()->setValue(0);
  const QStringList legs = legsOf(symbol);
  m_chart->setSymbol(symbol);
  m_retail->setState(SheetCard::Connecting);
  m_cot->setLabels(legs.size() == 2 ? legs : QStringList{marketLabel(symbol).toUpper()});
  m_cot->setState(SheetCard::Connecting);
  m_data->setColumns(legs);
  m_data->setState(SheetCard::Loading);
  QStringList banks; for (const auto& l : legs) banks << bankOf(l);
  m_meeting->setBanks(banks);
  m_tech->setState(SheetCard::Loading);
  m_perf->setState(SheetCard::Computing, QCoreApplication::translate("watch", "Computing relative performance..."));
  m_month->setSuffix({}); m_month->setState(SheetCard::Loading);
  m_path->setSuffix({}); m_path->setState(SheetCard::Loading);
  relayout();
  update();
  if (qEnvironmentVariableIsSet("DW_WATCH_LOADING")) {   // test hook: freeze the sheet in its loading state (reference t1.5)
    m_chart->showLoading(symbol);
    m_retail->setState(SheetCard::NotConnected);
    emit opened(symbol, legs);
    return;
  }
  loadTechnicals();
  loadSeasonality();
  loadPerformance();
  feedRetail();
  loadCot();
  macro::MacroStore::instance().ensureLoaded();
  feedMacro();
  macro::CentralBanks::instance().ensureLoaded();
  feedMeetings();
  emit opened(symbol, legs);
}

// ------------------------------------------------------------------ technical tiles (src/analytics)
static Tile tileOf(const QString& key, const analytics::TechRead& r, int dp) {
  using namespace analytics;
  Tile t{key, QCoreApplication::translate("watch", "Neutral"), 0, {}};
  if (key == "TREND") {
    t.value = r.trend == Lean::Bull ? QCoreApplication::translate("watch", "Bullish") : r.trend == Lean::Bear ? QCoreApplication::translate("watch", "Bearish") : QCoreApplication::translate("watch", "Neutral");
    t.bias = r.trend == Lean::Bull ? 1 : r.trend == Lean::Bear ? -1 : 0;
    t.tip = QCoreApplication::translate("watch", "EMA20 %1 vs EMA50 %2: gap %3 (bullish ≥ +0.10 %, bearish ≤ −0.10 %).")
              .arg(fmt::num(r.ema20, dp), fmt::num(r.ema50, dp), fmt::pct(r.trendPct, 2));
  } else if (key == "MOMENTUM") {
    t.value = r.momentum == Momentum::Overbought ? QCoreApplication::translate("watch", "Overbought") : r.momentum == Momentum::Oversold ? QCoreApplication::translate("watch", "Oversold") : QCoreApplication::translate("watch", "Neutral");
    t.tip = QCoreApplication::translate("watch", "RSI(14) %1 (overbought ≥ 70, oversold ≤ 30), ROC(10) %2.").arg(fmt::num(r.rsi, 1), fmt::pct(r.roc10, 2));
  } else if (key == "VOLATILITY") {
    t.value = r.vol == Level3::High ? QCoreApplication::translate("watch", "High") : r.vol == Level3::Low ? QCoreApplication::translate("watch", "Low") : QCoreApplication::translate("watch", "Normal");
    t.tip = QCoreApplication::translate("watch", "ATR14 %1 of price, percentile %2 of the last 252 days; 20-day realised volatility %3 annualised.")
              .arg(fmt::pct(r.atrPct, 2, false), fmt::num(r.atrPctile, 0), fmt::pct(r.volAnn, 1, false));
  } else if (key == "VOLUME") {
    t.value = r.volume == VolumeRead::Above ? QCoreApplication::translate("watch", "Above avg") : r.volume == VolumeRead::Below ? QCoreApplication::translate("watch", "Below avg")
            : r.volume == VolumeRead::Average ? QCoreApplication::translate("watch", "Average") : QCoreApplication::translate("watch", "N/A");
    t.tip = r.volume == VolumeRead::NA ? QCoreApplication::translate("watch", "No usable traded volume for this market.")
            : QCoreApplication::translate("watch", "Last complete day's volume = %1× the 20-day mean (%2).").arg(fmt::num(r.volRatio, 2), r.volTicker);
  } else if (key == "STRUCTURE") {
    t.value = r.structure == Lean::Bull ? QCoreApplication::translate("watch", "Bullish") : r.structure == Lean::Bear ? QCoreApplication::translate("watch", "Bearish") : QCoreApplication::translate("watch", "Ranging");
    t.bias = r.structure == Lean::Bull ? 1 : r.structure == Lean::Bear ? -1 : 0;
    t.tip = QCoreApplication::translate("watch", "Last two 5-bar fractal swing highs and lows on daily bars: both higher bullish, both lower bearish.");
  } else {
    const auto st = r.liq.state;
    t.value = st == LiqRead::PoolAbove ? QCoreApplication::translate("watch", "Pool above") : st == LiqRead::PoolBelow ? QCoreApplication::translate("watch", "Pool below")
            : st == LiqRead::NearPool ? QCoreApplication::translate("watch", "Near pool")
            : st == LiqRead::RecentlySwept ? QCoreApplication::translate("watch", "Recently swept") : QCoreApplication::translate("watch", "No pool");
    t.tip = QCoreApplication::translate("watch", "Liquidity pool engine on daily bars: %1 untested of %2 pools; nearest at %3 from the last close.")
              .arg(r.liq.untested).arg(r.liq.total).arg(fmt::pct(r.liq.distPct, 2));
  }
  t.tip += "\n" + QCoreApplication::translate("watch", "Same reads as Market › Technical Heatmap.");
  return t;
}

void InstrumentSheet::loadTechnicals() {
  const int gen = m_gen;
  const QString sym = m_symbol;
  market::candles(sym, "1d", "2y", this, [this, gen, sym](const market::Series& s) {
    if (gen != m_gen) return;
    if (!s.ok()) { m_tech->setState(SheetCard::Message, QCoreApplication::translate("watch", "Daily history unavailable (%1)").arg(s.error)); relayout(); return; }
    const QStringList src = analytics::volumeSources(sym);
    auto bars = std::make_shared<QVector<market::Bar>>(s.bars);
    auto vol = std::make_shared<QVector<analytics::VolumeInput>>();
    auto compute = [this, gen, sym, bars, vol] {
      const auto* ins = market::find(sym);
      const bool crypto = ins && ins->category == market::Category::Crypto;
      const qint64 today = QDateTime::currentSecsSinceEpoch() / 86400;
      const auto b = *bars; const auto v = *vol;
      auto* w = new QFutureWatcher<analytics::TechRead>(this);
      connect(w, &QFutureWatcher<analytics::TechRead>::finished, this, [this, w, gen] {
        w->deleteLater();
        if (gen != m_gen) return;
        const auto r = w->result();
        if (!r.ok) { m_tech->setState(SheetCard::Message, r.error.isEmpty() ? QCoreApplication::translate("watch", "Not enough daily history") : r.error); relayout(); return; }
        // fmt::priceDecimals guesses "6 letters = FX pair" (5 decimals for NASDAQ, NIKKEI…): the instrument table knows
        const auto* insD = market::find(m_symbol);
        const int dp = insD ? insD->decimals : fmt::priceDecimals(r.last, m_symbol);
        QVector<Tile> tiles;
        // keys stay English (logic); the card translates them where it paints
        for (const char* k : {QT_TRANSLATE_NOOP("watch", "TREND"), QT_TRANSLATE_NOOP("watch", "MOMENTUM"), QT_TRANSLATE_NOOP("watch", "VOLATILITY"),
                              QT_TRANSLATE_NOOP("watch", "VOLUME"), QT_TRANSLATE_NOOP("watch", "STRUCTURE"), QT_TRANSLATE_NOOP("watch", "LIQUIDITY")})
          tiles << tileOf(k, r, dp);
        m_tech->setTiles(tiles);
        relayout();
      });
      w->setFuture(QtConcurrent::run([b, v, crypto, today] { return analytics::technicalRead(b, v, crypto, today); }));
    };
    if (src.isEmpty()) { vol->push_back({market::find(sym) ? market::find(sym)->yahoo : sym, s.bars}); compute(); return; }
    if (src.first() == "-") { compute(); return; }
    auto left = std::make_shared<int>(src.size());
    for (const auto& t : src)
      market::candles(t, "1d", "3mo", this, [this, gen, t, vol, left, compute](const market::Series& ps) {
        if (gen != m_gen) return;
        vol->push_back({t, ps.ok() ? ps.bars : QVector<market::Bar>{}});
        if (--*left == 0) compute();
      });
  });
}

// ------------------------------------------------------------------ seasonality (src/seasonality engine)
void InstrumentSheet::loadSeasonality() {
  const int gen = m_gen;
  const QString sym = m_symbol;
  seasonality::loadHistory(sym, 16, this, [this, gen, sym](const QVector<seasonality::DailyClose>& closes, const QString& err, bool) {
    if (gen != m_gen) return;
    const auto ds = seasonality::build(closes, 16, QDate::currentDate(), sym);
    if (!err.isEmpty() || !ds.ok()) {
      const QString m = err.isEmpty() ? QCoreApplication::translate("watch", "Not enough daily history") : QCoreApplication::translate("watch", "Daily history unavailable");
      m_month->setState(SheetCard::Message, m); m_path->setState(SheetCard::Message, m);
      return;
    }
    Seasonality se;
    const auto months = seasonality::monthlyAverages(ds);
    for (int m = 0; m < 12; ++m) { se.month[m] = m < months.size() ? months[m].mean : NAN; se.monthN[m] = m < months.size() ? months[m].n : 0; }
    se.firstYear = ds.years.first().year;
    se.lastYear = ds.years.last().year;
    for (const auto& y : ds.years) if (y.finished) { if (!se.pathFirstYear) se.pathFirstYear = y.year; se.pathLastYear = y.year; }   // years in the average
    se.currentYear = ds.today.year();
    se.avgPath = seasonality::smoothPath(seasonality::averagePath(ds));
    se.curPath = seasonality::yearPath(ds, se.currentYear);
    se.ok = true;
    m_month->setData(se); m_path->setData(se);
    if (!se.pathLastYear) m_path->setState(SheetCard::Message, QCoreApplication::translate("watch", "Not enough daily history"));   // no finished year: no average
  });
}

// ------------------------------------------------------------------ retail (positioning::Retail, Myfxbook)
void InstrumentSheet::feedRetail() {
  using S = positioning::Retail::State;
  const auto& r = positioning::Retail::instance();
  const bool connected = r.state() == S::Connected || r.isFixture();
  if (!connected) {
    if (r.state() == S::LoggingIn) { m_retail->setState(SheetCard::Connecting); return; }
    m_retail->setState(SheetCard::NotConnected, QCoreApplication::translate("watch", "Connect in Positioning › Retail Sentiment"));
    return;
  }
  const auto& snap = r.latest();
  if (snap.isEmpty()) { m_retail->setState(SheetCard::Connecting); return; }
  const auto* ins = market::find(m_symbol);
  const auto* s = snap.find(ins ? ins->symbol : m_symbol);
  if (!s || !std::isfinite(s->longPct)) {
    m_retail->setState(SheetCard::Message, QCoreApplication::translate("watch", "Myfxbook publishes no outlook for %1").arg(marketLabel(m_symbol)));
    return;
  }
  m_retail->setRetail({s->longPct, s->shortPct, QCoreApplication::translate("watch", "Myfxbook community outlook"), snap.time});
}

// ------------------------------------------------------------------ COT (positioning::Cot)
static QString cotIdFor(const QString& symbol) {
  static const QHash<QString, QString> m{{"SPX", "ES"}, {"NDX", "NQ"}, {"DOW", "YM"}, {"RUT", "RTY"}, {"NIKKEI", "NIKKEI"},
    {"VIX", "VIX"}, {"COPPER", "COPPER"}, {"NATGAS", "NATGAS"}, {"CORN", "CORN"}, {"WHEAT", "WHEAT"}, {"SOYBEAN", "SOY"},
    {"DXY", "USD"}, {"WTI", "WTI"}};
  if (m.contains(symbol)) return m[symbol];
  const auto* mk = positioning::Cot::marketForSymbol(symbol);
  return mk ? mk->id : QString();
}
void InstrumentSheet::loadCot() {
  const int gen = m_gen;
  const QStringList legs = legsOf(m_symbol);
  const auto* ins = market::find(m_symbol);
  QStringList ids, labels;
  if (ins && ins->category == market::Category::Forex) {
    for (const auto& l : legs) { ids << (positioning::Cot::market(l) ? l : QString()); labels << l; }
  } else { ids << cotIdFor(m_symbol); labels << marketLabel(m_symbol).toUpper(); }
  bool any = false; for (const auto& i : ids) any |= !i.isEmpty();
  if (!any) { m_cot->setState(SheetCard::Message, QCoreApplication::translate("watch", "No CFTC futures contract for %1").arg(marketLabel(m_symbol))); return; }
  auto sides = std::make_shared<QVector<CotSide>>(ids.size());
  auto left = std::make_shared<int>(0);
  for (int i = 0; i < ids.size(); ++i) { (*sides)[i].label = labels[i]; if (!ids[i].isEmpty()) ++*left; }
  for (int i = 0; i < ids.size(); ++i) {
    if (ids[i].isEmpty()) { (*sides)[i].tip = QCoreApplication::translate("watch", "No standalone CFTC contract"); continue; }
    positioning::Cot::instance().history(ids[i], this, [this, gen, i, sides, left](const positioning::CotHistory& h) {
      if (gen != m_gen) return;
      auto& sd = (*sides)[i];
      if (h.ok() && h.weeks.last().legacy.size() > 1) {
        const auto& w = h.weeks.last();
        const auto& nc = w.legacy[1];   // Large Speculators = non-commercial
        sd.longPct = nc.longPct(); sd.shortPct = 100.0 - sd.longPct; sd.reportDate = w.date;
        sd.tip = QCoreApplication::translate("watch", "%1 · non-commercial long %2 / short %3 contracts (week %4 / %5)\nCFTC legacy report")
                   .arg(h.market.contract, fmt::num(nc.lng.pos, 0), fmt::num(nc.sht.pos, 0), fmt::signedNum(nc.lng.chg, 0), fmt::signedNum(nc.sht.chg, 0));
      } else sd.tip = h.error;
      if (--*left > 0) return;
      bool ok = false; for (const auto& x : *sides) ok |= std::isfinite(x.longPct);
      if (ok) m_cot->setSides(*sides);
      else m_cot->setState(SheetCard::Message, QCoreApplication::translate("watch", "CFTC report unavailable"));
    });
  }
}

// ------------------------------------------------------------------ data compare (macro::MacroStore)
void InstrumentSheet::feedMacro() {
  const QStringList legs = legsOf(m_symbol);
  const auto& store = macro::MacroStore::instance();
  QVector<macro::CurrencyMacro> cm; for (const auto& l : legs) cm << store.forCurrency(l);
  auto latestCell = [](macro::Ind ind, const macro::Latest& l, QString& tip) {
    if (!l.valid()) return fmt::dash();
    tip = macro::formatPeriod(l.period, l.freq) + " · " + l.source;
    return macro::formatValue(ind, l.value);
  };
  QVector<CompareRow> rows(5);
  rows[0].label = QCoreApplication::translate("watch", "Interest Rate"); rows[1].label = QCoreApplication::translate("watch", "GDP (Quarter q/q)"); rows[2].label = QCoreApplication::translate("watch", "Inflation Rate (Annual)");
  rows[3].label = QCoreApplication::translate("watch", "Unemployment Rate"); rows[4].label = QCoreApplication::translate("watch", "Business Confidence (OECD)");
  bool any = false;
  for (const auto& c : cm) {
    QString tip;
    const auto shortPeriod = [](const QDate& d, char freq) -> QString {
      if (!d.isValid()) return {};
      if (freq == 'A') return QString::number(d.year());
      if (freq == 'Q') return QCoreApplication::translate("economy", "Q%1 %2").arg((d.month() - 1) / 3 + 1).arg(d.toString("yy"));
      return i18n::locale().toString(d, freq == 'D' ? "d MMM" : "MMM yy");
    };
    rows[0].dates << (c.policy.valid() ? shortPeriod(c.policy.asOf, 'D') : QString());
    const auto checked = [&c](const QString& t, const char* figure) {
      const QString k = t.isEmpty() ? QString() : macro::checkTip(c.currency, QLatin1String(figure));
      return k.isEmpty() ? t : t + "\n" + k;
    };
    static const char* const figures[] = {"GDP q/q", "CPI y/y", "Unemployment"};
    rows[0].differs << macro::checkDiffers(c.currency, "Policy rate");
    if (c.policy.valid()) { rows[0].values << fmt::num(c.policy.rate, 2) + "%"; rows[0].tips << checked(QString("%1 · %2").arg(i18n::locale().toString(c.policy.asOf, "dd MMM yyyy"), c.policy.sourceName), "Policy rate"); any = true; }
    else { rows[0].values << fmt::dash(); rows[0].tips << QString(); }
    const QList<QPair<macro::Ind, const macro::Latest*>> cells{{macro::Ind::GdpQoq, &c.gdpQoq}, {macro::Ind::Inflation, &c.inflation},
                                                               {macro::Ind::Unemployment, &c.unemployment}, {macro::Ind::BusinessConf, &c.businessConfidence}};
    for (int k = 0; k < cells.size(); ++k) {
      tip.clear();
      rows[k + 1].values << latestCell(cells[k].first, *cells[k].second, tip);
      if (k == 3) tip += (tip.isEmpty() ? "" : "\n") + QCoreApplication::translate("watch", "100 = long-run average. ") + c.pmiNote;
      else tip = checked(tip, figures[k]);
      rows[k + 1].differs << (k < 3 && macro::checkDiffers(c.currency, QLatin1String(figures[k])));
      rows[k + 1].tips << tip;
      rows[k + 1].dates << (cells[k].second->valid() ? shortPeriod(cells[k].second->period, cells[k].second->freq) : QString());
      any |= cells[k].second->valid();
    }
  }
  if (!any) {
    if (store.loading()) m_data->setState(SheetCard::Connecting);
    else m_data->setState(SheetCard::Message, QCoreApplication::translate("watch", "No macro data for %1").arg(legs.join(" / ")));
    return;
  }
  m_data->setRows(rows);
}

// ------------------------------------------------------------------ meeting compare (macro::CentralBanks)
void InstrumentSheet::feedMeetings() {
  const QStringList legs = legsOf(m_symbol);
  const auto& cb = macro::CentralBanks::instance();
  const QStringList labels{"-50+bps", "-25bps", QCoreApplication::translate("watch", "No change"), "+25bps", "+50+bps"};
  for (int i = 0; i < legs.size(); ++i) {
    const macro::Bank* b = cb.bank(legs[i]);
    if (!b) { m_meeting->setBankState(i, SheetCard::Message, QCoreApplication::translate("watch", "No central-bank data for %1").arg(legs[i])); continue; }
    const macro::Odds o = b->nextOdds();
    if (b->priced && o.valid()) {
      MeetingData d; d.bank = bankOf(legs[i]); d.meeting = b->nextMeeting().decision; d.source = b->pricingSource;
      for (int k = 0; k < 5; ++k) d.buckets << MeetingBucket{labels[k], o.at(k) * 100.0};
      m_meeting->setBank(i, d);
    } else {
      const bool reading = b->pricingNote == QCoreApplication::translate("economy", "Reading what the market prices…");
      m_meeting->setBankState(i, reading ? SheetCard::Message : SheetCard::NotConnected, b->pricingNote.isEmpty() ? QCoreApplication::translate("watch", "No market pricing for this bank yet.") : b->pricingNote);
    }
  }
}

void InstrumentSheet::loadPerformance() {
  const int gen = m_gen;
  m_perfBars.clear();
  const qint64 from = QDateTime(QDate(2017, 1, 1), QTime(0, 0), QTimeZone::UTC).toSecsSinceEpoch();
  const qint64 to = QDateTime::currentSecsSinceEpoch();
  const auto* ins = market::find(m_symbol);
  const QStringList majors = market::majorCurrencies();
  const bool fxIndex = ins && ins->category == market::Category::Forex && majors.contains(ins->base) && majors.contains(ins->quote);
  QStringList need;
  QString bench;
  if (fxIndex) {
    for (const auto& s : indexPairs(ins->base) + indexPairs(ins->quote)) if (!need.contains(s)) need << s;
  } else {
    if (!ins) bench = "SPX";
    else switch (ins->category) {
      case market::Category::Stock: bench = "SPX"; break;
      case market::Category::Index: bench = ins->symbol == "SPX" ? "NDX" : "SPX"; break;
      case market::Category::Crypto: bench = ins->symbol == "BTCUSD" ? "NDX" : "BTCUSD"; break;
      case market::Category::Commodity: bench = ins->symbol == "XAUUSD" ? "DXY" : "XAUUSD"; break;
      case market::Category::Forex: bench = "DXY"; break;
      case market::Category::Bond: bench = "SPX"; break;
    }
    need << m_symbol << bench;
  }
  m_perfPending = need.size();
  for (const auto& sym : need) {
    market::candlesBetween(sym, "1d", from, to, this, [this, gen, sym, fxIndex, bench, from, ins](const market::Series& s) {
      if (gen != m_gen) return;
      if (s.ok()) m_perfBars.insert(sym, s.bars);
      if (--m_perfPending > 0) return;
      PerfLine a, b;
      if (fxIndex) {
        // reference order: quote currency (USD, blue) first, base (EUR, gold) second
        QHash<QString, QVector<market::Bar>> qa, ba;
        for (const auto& p : indexPairs(ins->quote)) if (m_perfBars.contains(p)) qa.insert(p, m_perfBars[p]);
        for (const auto& p : indexPairs(ins->base)) if (m_perfBars.contains(p)) ba.insert(p, m_perfBars[p]);
        a = currencyIndex(ins->quote, qa, from); b = currencyIndex(ins->base, ba, from);
      } else {
        a = rebased(bench == "SPX" || bench == "NDX" || bench == "DXY" ? market::find(bench)->name : market::display(bench), m_perfBars.value(bench), from);
        b = rebased(ins && ins->category != market::Category::Forex && ins->category != market::Category::Crypto ? ins->name : market::display(m_symbol), m_perfBars.value(m_symbol), from);
      }
      alignPair(a, b);
      if (a.pct.size() < 20 || b.pct.size() < 20) m_perf->setState(SheetCard::Message, QCoreApplication::translate("watch", "Not enough daily history to compare"));
      else m_perf->setLines(a, b);
    });
  }
}

// ------------------------------------------------------------------ layout
void InstrumentSheet::resizeEvent(QResizeEvent*) {
  m_scroll->setGeometry(0, kHeader, width(), height() - kHeader);
  relayout();
}
void InstrumentSheet::relayout() {
  const int W = m_scroll->viewport()->width() > 0 ? m_scroll->viewport()->width() : width();
  const int VH = m_scroll->viewport()->height() > 0 ? m_scroll->viewport()->height() : height() - kHeader;
  // size classes: narrow (< 1180) stacks the side cards and the bottom pairs; wide (> 1700) widens the side column
  const bool stacked = W < 1180, wide = W > 1700;
  // phone held upright: the three side cards one under the other (a third of 410 px cut their text) and a chart
  // that leaves room to see the next card
  const bool phone = ui::compact(W);
  m_tech->setColumns(phone ? 3 : 0);
  const int right = wide ? std::clamp(int(W * 0.24), kRight, 560) : kRight;
  // top block: 644 at the reference (viewport 928); shorter on short screens, taller on tall ones
  int topH = stacked ? std::clamp(VH - 2 * kM, 300, kChartH) : std::clamp(VH - 2 * kM, 590, kChartH);
  if (VH > 1000) topH = std::min(int(VH * 0.69), 900);
  if (phone) topH = std::clamp(int(VH * 0.75), 300, 640);   // room for the whole drawing-tool column
  const double sc = topH / double(kChartH);
  // heights measured on the reference: loading frame (t1.5) chart 586, retail 93, COT 160, data compare 68;
  // loaded (t2.0) chart 644, retail 270, COT 179, data compare 178
  const bool chartReady = m_chart->hasData();
  const int chartH = chartReady ? topH : int(topH * 586.0 / kChartH);
  // compact cards only during the first load (t1.5); once the chart is drawn the column keeps the loaded layout
  const bool full = chartReady;
  int y = kM;
  if (!stacked) {
    const int chartW = W - 2 * kM - kM - right;
    m_chart->setGeometry(kM, y, chartW, chartH);
    const int rx = kM + chartW + kM;
    const int ch = full || m_cot->state() == SheetCard::Ready ? std::max(160, int(std::round(179 * sc))) : 160;
    const int dh = full || m_data->state() == SheetCard::Ready ? std::max(178, int(std::round(178 * sc))) : 68;
    const int rh = full || m_retail->state() == SheetCard::Ready ? topH - ch - dh - 2 * kM : 93;
    int ry = y;
    m_retail->setGeometry(rx, ry, right, rh); ry += rh + kM;
    m_cot->setGeometry(rx, ry, right, ch); ry += ch + kM;
    m_data->setGeometry(rx, ry, right, dh); ry += dh;
    y += std::max(chartH, ry - y) + 10;
  } else if (phone) {
    m_chart->setGeometry(kM, y, W - 2 * kM, chartH);
    y += chartH + kM;
    const int rh = full ? 270 : 160, ch = full ? 210 : 160, dh = full || m_data->state() == SheetCard::Ready ? 200 : 100;
    m_retail->setGeometry(kM, y, W - 2 * kM, rh); y += rh + kM;
    m_cot->setGeometry(kM, y, W - 2 * kM, ch); y += ch + kM;
    m_data->setGeometry(kM, y, W - 2 * kM, dh); y += dh + 10;
  } else {
    m_chart->setGeometry(kM, y, W - 2 * kM, chartH);
    y += chartH + kM;
    const int cw = (W - 2 * kM - 2 * kM) / 3, cw3 = W - 2 * kM - 2 * (cw + kM);
    const int h = full ? 270 : 160;
    m_retail->setGeometry(kM, y, cw, h);
    m_cot->setGeometry(kM + cw + kM, y, cw, h);
    m_data->setGeometry(kM + 2 * (cw + kM), y, cw3, h);
    y += h + 10;
  }
  const int th = m_tech->state() == SheetCard::Ready ? (phone ? 93 + 53 : 93) : 67;   // phone: tiles 3 x 2
  m_tech->setGeometry(kM, y, W - 2 * kM, th);
  y += th + kM;
  auto pair = [&](QWidget* a, QWidget* b, int h) {
    if (stacked) {
      a->setGeometry(kM, y, W - 2 * kM, h); y += h + kM;
      b->setGeometry(kM, y, W - 2 * kM, h); y += h + kM;
      return;
    }
    const int half = (W - 2 * kM - kM) / 2, half2 = W - 2 * kM - kM - half;
    a->setGeometry(kM, y, half, h);
    b->setGeometry(kM + half + kM, y, half2, h);
    y += h + kM;
  };
  pair(m_perf, m_meeting, 277);
  pair(m_month, m_path, 293);
  m_content->resize(W, y);
}
void InstrumentSheet::scrollTo(int y) { m_scrollAnim->stop(); m_scroll->verticalScrollBar()->setValue(y); }

bool InstrumentSheet::eventFilter(QObject* o, QEvent* e) {
  if (o == m_scroll->viewport() && e->type() == QEvent::Wheel) {
    auto* we = static_cast<QWheelEvent*>(e);
    auto* sb = m_scroll->verticalScrollBar();
    if (!we->pixelDelta().isNull()) { m_scrollAnim->stop(); sb->setValue(sb->value() - we->pixelDelta().y()); return true; }
    const int base = m_scrollAnim->state() == QAbstractAnimation::Running ? m_scrollTarget : sb->value();
    m_scrollTarget = std::clamp(base - we->angleDelta().y() * 120 / 120, sb->minimum(), sb->maximum());
    m_scrollAnim->stop(); m_scrollAnim->setStartValue(sb->value()); m_scrollAnim->setEndValue(m_scrollTarget); m_scrollAnim->start();
    return true;
  }
  return QWidget::eventFilter(o, e);
}

// ------------------------------------------------------------------ header
QRectF InstrumentSheet::closeRect() const { return QRectF(width() - 36, 4, 30, 30); }
void InstrumentSheet::paintEvent(QPaintEvent*) {
  QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
  p.fillRect(rect(), theme::map(QColor(0x04, 0x03, 0x00)));
  p.fillRect(QRectF(0, 0, width(), kHeader - 1), col::sheetStrip());
  p.fillRect(QRectF(0, kHeader - 2, width(), 2), theme::map(QColor(0x24, 0x21, 0x14)));
  p.setFont(theme::sans(13, QFont::Bold)); p.setPen(theme::map(QColor(0xeb, 0xe4, 0xda)));
  const auto* ins = market::find(m_symbol);
  QString head = marketLabel(m_symbol);
  if (ins && ins->category == market::Category::Stock) head += "  ·  " + ins->name;
  p.drawText(QPointF(10, 23), head);
  const QRectF c = closeRect();
  p.setPen(QPen(m_closeHover ? theme::map(QColor(0xe8, 0xe4, 0xdc)) : theme::map(QColor(0x6e, 0x6b, 0x64)), 1.3));
  const QPointF m = c.center() + QPointF(0, -1);
  p.drawLine(m + QPointF(-4.5, -4.5), m + QPointF(4.5, 4.5)); p.drawLine(m + QPointF(-4.5, 4.5), m + QPointF(4.5, -4.5));
}
void InstrumentSheet::mousePressEvent(QMouseEvent* e) { if (closeRect().contains(e->position())) emit closed(); }
void InstrumentSheet::mouseMoveEvent(QMouseEvent* e) {
  const bool h = closeRect().contains(e->position());
  if (h != m_closeHover) { m_closeHover = h; setCursor(h ? Qt::PointingHandCursor : Qt::ArrowCursor); update(closeRect().toAlignedRect()); }
}
void InstrumentSheet::keyPressEvent(QKeyEvent* e) {
  if (e->key() == Qt::Key_Escape) { emit closed(); return; }
  QWidget::keyPressEvent(e);
}
}
