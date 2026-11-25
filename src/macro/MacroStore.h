#pragma once
#include <QObject>
#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QVector>
#include <QString>
#include <QCoreApplication>
#include <cmath>

// Macro indicators by economy, real recorded values from official agencies, history back to 2000 where it exists.
// Loading is progressive (one bulk request per data set, paced, cached on disk), never blocks the GUI:
//
//   auto& m = macro::MacroStore::instance();
//   m.ensureLoaded();                                         // idempotent, call from showEvent
//   connect(&m, &macro::MacroStore::updated, this, [this] { refresh(); });
//   macro::Series s = m.series("us", macro::Ind::Inflation);   // monthly CPI y/y since 2000 (BIS)
//   macro::Latest l = m.latest("us", macro::Ind::Unemployment);
//   macro::CurrencyMacro c = m.forCurrency("AUD");            // Fundamental Heatmap / Markets Watch sheet
//
// Data sets: BIS CBPOL (policy rates, monthly + daily) and WS_LONG_CPI (CPI y/y, monthly), OECD QNA (GDP q/q,
// y/y), OECD LFS (unemployment, monthly), OECD MEI/CLI (business and consumer confidence), OECD FINMARK
// (10-year and 3-month rates, monthly), World Bank WDI (annual long-run figures). No forecasts, no estimates.
// Over them, for the eight currencies: the statistics offices' own series (BLS, ONS, ABS, Statistics Canada, Statistics
// Bureau of Japan, BFS and SECO via the SNB, Eurostat; macro/National.h) and, until a series has its new period, the
// release calendar's figure.
namespace macro {

enum class Ind {
  PolicyRate,        // % — BIS CBPOL (central-bank feeds override the latest value, see CentralBanks)
  GdpQoq,            // % q/q, real, seasonally adjusted (OECD), not annualised
  GdpYoy,            // % y/y, real (OECD)
  Inflation,         // CPI % y/y, monthly (BIS long CPI series)
  Unemployment,      // % of labour force, monthly SA (OECD LFS)
  BusinessConf,      // OECD business confidence, amplitude adjusted (100 = long-run average)
  ConsumerConf,      // OECD consumer confidence, amplitude adjusted (100 = long-run average)
  Yield10y,          // % long-term government bond yield, monthly average (OECD FINMARK)
  Rate3m,            // % 3-month interbank rate, monthly average (OECD FINMARK)
  GdpUsd,            // current US$ (World Bank, annual)
  GdpPerCapita,      // current US$ (World Bank)
  Population,        // persons (World Bank)
  RealGrowth,        // % real GDP growth (World Bank)
  InflationAnnual,   // % CPI annual average (World Bank)
  UnemploymentAnnual,// % ILO modelled (World Bank)
  CurrentAccount,    // % of GDP (World Bank)
  GovDebt,           // central government debt % of GDP (World Bank)
  FxReserves,        // total reserves incl. gold, current US$ (World Bank)
  // appended 2026-11 (values above keep their numbers)
  CoreInflation,     // core CPI % y/y (CPI excluding food and energy): OECD; national/official series where fresher
                     // (euro area ECB/Eurostat HICP ex energy & food, Canada BoC CPI-XFET, Switzerland BFS core 1 via
                     // SNB, Japan e-Stat CPI less fresh food & energy when the "e-stat" key is saved)
  RetailSalesYoy,    // retail trade volume % y/y, computed by Deepwater from the OECD SA volume index (monthly, else quarterly)
  Count
};

struct Point { QDate date; double value = NAN; };   // date = first day of the period (month, quarter, year)

struct Series {
  Ind ind = Ind::PolicyRate;
  QString iso2;
  QVector<Point> pts;   // ascending
  char freq = 'M';      // 'D' 'M' 'Q' 'A'
  QString source;       // "BIS", "OECD", "World Bank", "Federal Reserve"…
  QString dataset;      // "WS_LONG_CPI", "QNA", "WDI NY.GDP.MKTP.CD"…
  bool ok() const { return !pts.isEmpty(); }
  Point last() const { return pts.isEmpty() ? Point{} : pts.last(); }
  Point prior() const { return pts.size() < 2 ? Point{} : pts[pts.size() - 2]; }
};

// A published figure of the release calendar, shown until the series has its period (macro/National.h).
struct CalendarFigure {
  QString iso2;
  Ind ind = Ind::Inflation;
  QDate period;          // first day of the month or quarter it measures
  char freq = 'M';
  double value = NAN;    // percent
  QString source;        // "Myfxbook", "FinancialJuice", "ONS" …
  QDateTime published;   // UTC
  bool valid() const { return std::isfinite(value) && period.isValid(); }
};

struct Latest {
  double value = NAN, prior = NAN;
  QDate period;          // reference period of the latest value
  char freq = 'M';
  QString source;
  bool valid() const { return std::isfinite(value); }
};

struct IndicatorInfo {
  Ind ind;
  QString name;          // "Inflation Rate (Annual)"
  QString shortName;     // "INFLATION"
  QString unit;          // "%", "USD", "", "index"
  int decimals;          // display decimals
  int goodWhenUp;        // +1 rising is good (cyan), -1 rising is bad (red), 0 neutral (grey)
  bool level;            // level series: change shown in % (GDP $, population); else in pp
  QString source;        // publisher
  QString note;          // tooltip: definition and caveats
};
const IndicatorInfo& info(Ind i);
QString formatValue(Ind i, double v);          // "3.7%", "$30.77T", "341.8M", "100.4"
QString formatPeriod(const QDate& d, char freq);   // "Sep 2026", "Q3 2026", "2025"

// Policy rate as shown in registries and summaries.
struct PolicyRate {
  double rate = NAN;
  QDate asOf;            // date of the observation
  QString badge;         // "FED", "ECB", … or "BIS" / "WB"
  QString sourceName;    // "Federal Reserve (NY Fed EFFR target range)", "BIS policy-rate series" …
  QDateTime fetched;     // when the feed answered (local display "PUBLIC FEED · 19:00:00")
  bool valid() const { return std::isfinite(rate); }
};

// Per-currency macro snapshot for the Fundamental Heatmap and the Markets Watch instrument sheet.
struct CurrencyMacro {
  QString currency, iso2;
  PolicyRate policy;
  Latest gdpQoq, inflation, unemployment, businessConfidence, consumerConfidence, yield10y;
  // PMI composite: S&P Global / ISM PMIs have no free source; always invalid here. Show "—" with
  // pmiNote as tooltip, or businessConfidence (OECD, 100 = average) labelled as such.
  Latest pmiComposite;
  Latest coreInflation, retailSales;   // appended: core CPI y/y, retail sales volume y/y
  QString pmiNote = QCoreApplication::translate("economy", "PMI composite (S&P Global) has no free source; OECD business confidence is the closest official series.");
};

class MacroStore : public QObject {
  Q_OBJECT
public:
  static MacroStore& instance();
  // Same data sets with their whole history (no start date), for Facts imports; the pages keep instance() (from 2000).
  static MacroStore& deep();
  bool isDeep() const { return m_deep; }
  void ensureLoaded();                         // starts every data set once; later calls refresh stale ones
  bool loading() const { return m_pending + m_parsing > 0; }
  bool nationalLoading(const QString& iso2) const { return m_nationalPending.value(iso2.toLower()) > 0; }
  int datasetsLoaded() const { return m_loaded.size(); }

  Series series(const QString& iso2, Ind i) const;
  // As published, without the central-bank feed's latest value that series() adds to PolicyRate (Facts: the BIS chain
  // must hold BIS figures only; the feed has its own chain).
  Series rawSeries(const QString& iso2, Ind i) const { return m_data.value(iso2.toLower()).value(int(i)); }
  // The other compilations of the same indicator, as published (BIS, OECD, ECB…), for the eight currencies' economies: the
  // second reading of the figure shown (macro/KeyFigures.h).
  QList<Series> otherReadings(const QString& iso2, Ind i) const { return m_readings.value(iso2.toLower()).value(int(i)).values(); }
  Point bisDaily(const QString& iso2) const { return m_bisDaily.value(iso2.toLower()); }
  Latest latest(const QString& iso2, Ind i) const;
  PolicyRate policyRate(const QString& iso2) const;   // central-bank feed (CentralBanks) else BIS else invalid
  CurrencyMacro forCurrency(const QString& ccy) const;   // USD EUR GBP JPY CHF CAD AUD NZD (+ others with data)

  struct Coverage { int economies = 0, monthly = 0, liveFeeds = 0; };
  Coverage coverage() const;                   // registry size, economies with monthly series, with a daily rate feed
  QString lastError() const { return m_error; }

signals:
  void updated();                              // a data set arrived (or a central-bank rate changed)

private:
  explicit MacroStore(bool deep = false);
  QUrl u(const QString& url) const;   // the request URL, start dates removed for the deep store
  bool m_deep = false;
  void loadBisPolicy();
  void loadBisDaily();
  void loadBisCpi();
  void loadOecd(const QString& key, const QString& url, int ttl);
  void loadWorldBank(Ind i, const QString& code);
  void loadCoreAndRetail();          // CoreInflation + RetailSalesYoy (OECD, ECB, Eurostat, BoC, SNB, e-Stat)
  void putIfFresher(const QString& iso2, const Series& s);   // replaces a series only if the new one ends later
  void loadNational(const QString& iso2 = {});   // the statistics offices' own series (macro/National.h); one country or all
  void applyNational();                          // lays them over m_data (again after each data set that replaces it)
  void readCalendar();                           // the release calendar's latest figures (last two weeks)
  void merge(const QHash<QString, QHash<int, Series>>& add, const QString& datasetKey);
  void failed(const QString& datasetKey);      // the request failed: retried by ensureLoaded() after 5 minutes

  QHash<QString, QHash<int, Series>> m_data;   // iso2 → indicator → series
  QHash<QString, QHash<int, Series>> m_national;   // same keys: the statistics offices' series
  void remember(const QString& iso2, const Series& s);
  QHash<QString, QHash<int, QHash<QString, Series>>> m_readings;   // iso2 → indicator → publisher → its series
  QHash<QString, QHash<int, CalendarFigure>> m_calendar;   // same keys: the release calendar's figures
  QHash<QString, int> m_nationalPending;       // iso2 → requests under way
  bool m_calendarWatched = false;
  QHash<QString, Point> m_bisDaily;            // iso2 → latest daily BIS policy rate
  QDateTime m_bisDailyFetched;
  QSet<QString> m_loaded;
  QHash<QString, qint64> m_started;            // dataset → msecs since epoch of the last request
  QHash<QString, qint64> m_retryAt;            // dataset whose last request failed → msecs when it may be asked again
  int m_pending = 0, m_parsing = 0;
  QString m_error;
};
}
