#include "macro/MacroStore.h"
#include "macro/Registry.h"
#include "macro/CentralBanks.h"
#include "macro/Csv.h"
#include "macro/National.h"
#include "news/Calendar.h"
#include "core/Settings.h"
#include "net/Http.h"
#include "net/Sources.h"
#include "ui/Format.h"
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QMutex>
#include "core/I18n.h"
#include <QDebug>

namespace macro {
namespace {
const sources::Registrar regBis({"bis", "Bank for International Settlements (BIS Data Portal)", {"stats.bis.org"},
    sources::Access::Keyless, 1500, "Central bank policy rates (WS_CBPOL, daily and monthly), consumer prices y/y (WS_LONG_CPI)",
    "BIS terms of use: free reuse with attribution (\"Source: BIS\"); no key; one bulk request per data set",
    {"economy/macro", "economy/rates", "global/overview", "market/fundamental-heatmap", "market/markets-watch"}});
const sources::Registrar regOecd({"oecd", "OECD Data Explorer (SDMX API)", {"sdmx.oecd.org"},
    sources::Access::Keyless, 3500, "Quarterly GDP growth, monthly unemployment, business/consumer confidence, 10Y and 3M rates",
    "OECD terms: free reuse with attribution (CC BY 4.0); the API allows about 20 requests per minute, so requests "
    "are spaced 3.5 s and bulk (all countries at once)", {"economy/macro", "market/fundamental-heatmap", "market/markets-watch"}});
const sources::Registrar regWb({"worldbank", "World Bank (World Development Indicators API)", {"api.worldbank.org"},
    sources::Access::Keyless, 1000, "Annual GDP, GDP per capita, population, growth, unemployment, current account, "
    "government debt, FX reserves", "World Bank open data, CC BY 4.0; country/all requests only (lists are WAF-blocked)",
    {"economy/macro", "market/fundamental-heatmap", "market/markets-watch"}});

const sources::Registrar regEurostat({"eurostat", "Eurostat (dissemination API)", {"ec.europa.eu"},
    sources::Access::Keyless, 1500, "HICP excluding energy and food, annual rate, euro area",
    "Eurostat copyright notice: free reuse with the source acknowledged", {"economy/macro", "market/fundamental-heatmap", "market/markets-watch"}});
const sources::Registrar regEstat({"estat", "Statistics Bureau of Japan (e-Stat API)", {"api.e-stat.go.jp"},
    sources::Access::Key, 3000, "CPI less fresh food and energy, y/y (table 0004052037)",
    "e-Stat terms: free use with credit; needs the user's application id (settings key keys.e-stat)", {"economy/macro", "market/fundamental-heatmap", "market/markets-watch"}, "e-stat"});

struct Data { QHash<QString, QHash<int, Series>> map; };

QDate periodDate(const QString& p, char* freq) {
  // 2026-09 | 2026-Q3 | 2026 | 2026-11-10
  if (p.size() == 10) { if (freq) *freq = 'D'; return QDate::fromString(p, "yyyy-MM-dd"); }
  if (p.size() == 7 && p[4] == '-' && p[5] == 'Q') {
    if (freq) *freq = 'Q';
    return QDate(p.left(4).toInt(), (p.mid(6).toInt() - 1) * 3 + 1, 1);
  }
  if (p.size() == 7) { if (freq) *freq = 'M'; return QDate(p.left(4).toInt(), p.mid(5).toInt(), 1); }
  if (p.size() == 4) { if (freq) *freq = 'A'; return QDate(p.toInt(), 1, 1); }
  return {};
}


void sortSeries(Data& d) {
  for (auto& m : d.map) for (auto& s : m) {
    std::sort(s.pts.begin(), s.pts.end(), [](const Point& a, const Point& b) { return a.date < b.date; });
    // drop duplicate dates (keep the last value)
    QVector<Point> u; u.reserve(s.pts.size());
    for (const auto& p : s.pts) { if (!u.isEmpty() && u.last().date == p.date) u.last() = p; else u << p; }
    s.pts = u;
  }
}

// BIS CSV detail=dataonly: FREQ,REF_AREA,[UNIT_MEASURE,]TIME_PERIOD,OBS_VALUE
Data parseBis(const QByteArray& body, Ind ind, bool policy) {
  Data d;
  const auto rows = csv::parse(body);
  if (rows.size() < 2) return d;
  const int ia = rows[0].indexOf("REF_AREA"), it = rows[0].indexOf("TIME_PERIOD"), iv = rows[0].indexOf("OBS_VALUE");
  if (ia < 0 || it < 0 || iv < 0) return d;
  QHash<QString, QStringList> areaTo;   // BIS area → economies
  for (const auto& e : economies()) {
    if (policy) { if (!e.bis.isEmpty()) areaTo[e.bis] << e.iso2; }
    else areaTo[e.iso2.toUpper()] << e.iso2;
  }
  if (!policy) areaTo["XM"] = {"ez"};
  for (int r = 1; r < rows.size(); ++r) {
    const auto& row = rows[r];
    if (row.size() <= qMax(ia, qMax(it, iv))) continue;
    bool ok = false; const double v = row[iv].toDouble(&ok);
    if (!ok || !std::isfinite(v)) continue;
    char f = 'M'; const QDate dt = periodDate(row[it], &f);
    if (!dt.isValid()) continue;
    // BIS publishes the mid-point of the Fed's target range since Dec 2008; the app shows the upper bound
    const double val = (policy && row[ia] == "US" && dt >= QDate(2008, 12, 16)) ? v + 0.125 : v;
    for (const auto& iso : areaTo.value(row[ia])) {
      Series& s = d.map[iso][int(ind)];
      s.ind = ind; s.iso2 = iso; s.freq = f; s.source = "BIS"; s.dataset = policy ? "WS_CBPOL" : "WS_LONG_CPI";
      s.pts.append({dt, val});
    }
  }
  sortSeries(d);
  return d;
}

// OECD SDMX CSV (format=csv): column codes; `pick` maps a code of column `col` to an indicator.
Data parseOecd(const QByteArray& body, const QString& col, const QHash<QString, Ind>& pick, const QString& dataset) {
  Data d;
  const auto rows = csv::parse(body);
  if (rows.size() < 2) return d;
  const int ia = rows[0].indexOf("REF_AREA"), it = rows[0].indexOf("TIME_PERIOD"), iv = rows[0].indexOf("OBS_VALUE");
  const int ic = rows[0].indexOf(col);
  if (ia < 0 || it < 0 || iv < 0 || ic < 0) return d;
  QHash<QString, QString> areaTo;
  for (const auto& e : economies()) { if (!e.oecd.isEmpty()) areaTo[e.oecd] = e.iso2; areaTo.insert(e.iso3, e.iso2); }
  areaTo["EA20"] = "ez"; areaTo["EA"] = "ez";
  areaTo.remove("EMU");
  for (int r = 1; r < rows.size(); ++r) {
    const auto& row = rows[r];
    if (row.size() <= qMax(qMax(ia, it), qMax(iv, ic))) continue;
    const auto pi = pick.constFind(row[ic]);
    if (pi == pick.cend()) continue;
    const QString iso = areaTo.value(row[ia]);
    if (iso.isEmpty()) continue;
    bool ok = false; const double v = row[iv].toDouble(&ok);
    if (!ok || !std::isfinite(v)) continue;
    char f = 'M'; const QDate dt = periodDate(row[it], &f);
    if (!dt.isValid()) continue;
    Series& s = d.map[iso][int(*pi)];
    s.ind = *pi; s.iso2 = iso; s.freq = f; s.source = "OECD"; s.dataset = dataset;
    s.pts.append({dt, v});
  }
  sortSeries(d);
  return d;
}

Data parseWb(const QByteArray& body, Ind ind, const QString& code) {
  Data d;
  const auto doc = QJsonDocument::fromJson(body);
  if (!doc.isArray() || doc.array().size() < 2) return d;
  QHash<QString, QString> iso3To;
  for (const auto& e : economies()) iso3To.insert(e.iso3, e.iso2);
  for (const auto& v : doc.array()[1].toArray()) {
    const auto o = v.toObject();
    if (o.value("value").isNull()) continue;
    const QString iso = iso3To.value(o.value("countryiso3code").toString());
    if (iso.isEmpty()) continue;
    Series& s = d.map[iso][int(ind)];
    s.ind = ind; s.iso2 = iso; s.freq = 'A'; s.source = "World Bank"; s.dataset = "WDI " + code;
    s.pts.append({QDate(o.value("date").toString().toInt(), 1, 1), o.value("value").toDouble()});
  }
  sortSeries(d);
  return d;
}

template <class F>
void runParse(QObject* ctx, F f, std::function<void(Data)> done) {
  auto* w = new QFutureWatcher<Data>(ctx);
  QObject::connect(w, &QFutureWatcher<Data>::finished, ctx, [w, done] { done(w->result()); w->deleteLater(); });
  w->setFuture(QtConcurrent::run(std::move(f)));
}
}

// ------------------------------------------------------------------------------------------------ info
const IndicatorInfo& info(Ind i) {
  static const QList<IndicatorInfo> t{
    {Ind::PolicyRate, QT_TRANSLATE_NOOP("economy", "Interest Rate"), QT_TRANSLATE_NOOP("economy", "POLICY RATE"), "%", 2, 0, false, "BIS",
     QT_TRANSLATE_NOOP("economy", "Central bank policy rate. BIS policy-rate series (monthly history, daily latest); the central bank's own feed overrides it where the app follows the bank.")},
    {Ind::GdpQoq, QT_TRANSLATE_NOOP("economy", "GDP (Quarter q/q)"), QT_TRANSLATE_NOOP("economy", "GDP Q/Q"), "%", 1, 1, false, "OECD",
     QT_TRANSLATE_NOOP("economy", "Real GDP, quarter on quarter growth, seasonally adjusted, not annualised (OECD Quarterly National Accounts).")},
    {Ind::GdpYoy, QT_TRANSLATE_NOOP("economy", "GDP (Annual y/y)"), QT_TRANSLATE_NOOP("economy", "GDP Y/Y"), "%", 1, 1, false, "OECD",
     QT_TRANSLATE_NOOP("economy", "Real GDP, growth over the same quarter of the previous year (OECD Quarterly National Accounts).")},
    {Ind::Inflation, QT_TRANSLATE_NOOP("economy", "Inflation Rate (Annual)"), QT_TRANSLATE_NOOP("economy", "INFLATION"), "%", 1, -1, false, "BIS",
     QT_TRANSLATE_NOOP("economy", "Consumer prices, year-on-year change, as the statistics office publishes it (BLS, Eurostat, ONS, Statistics Bureau of Japan, BFS, Statistics Canada, ABS); elsewhere and for the older history, the BIS long series.")},
    {Ind::Unemployment, QT_TRANSLATE_NOOP("economy", "Unemployment Rate"), QT_TRANSLATE_NOOP("economy", "UNEMPLOYMENT"), "%", 1, -1, false, "OECD",
     QT_TRANSLATE_NOOP("economy", "Unemployment rate, % of labour force, seasonally adjusted, as the statistics office publishes it (BLS, ONS, SECO, Statistics Canada, ABS); elsewhere the OECD's series (quarterly for New Zealand).")},
    {Ind::BusinessConf, QT_TRANSLATE_NOOP("economy", "Business Confidence (OECD)"), QT_TRANSLATE_NOOP("economy", "BUSINESS CONF."), "", 1, 1, false, "OECD",
     QT_TRANSLATE_NOOP("economy", "OECD business confidence indicator, amplitude adjusted (100 = long-term average). Shown in place of a PMI: S&P Global / ISM PMIs have no free source.")},
    {Ind::ConsumerConf, QT_TRANSLATE_NOOP("economy", "Consumer Confidence (OECD)"), QT_TRANSLATE_NOOP("economy", "CONSUMER CONF."), "", 1, 1, false, "OECD",
     QT_TRANSLATE_NOOP("economy", "OECD consumer confidence indicator, amplitude adjusted (100 = long-term average).")},
    {Ind::Yield10y, QT_TRANSLATE_NOOP("economy", "10Y Government Yield"), QT_TRANSLATE_NOOP("economy", "10Y GOV. YIELD"), "%", 2, 0, false, "OECD",
     QT_TRANSLATE_NOOP("economy", "Long-term (10-year) government bond yield, monthly average (OECD Financial market statistics).")},
    {Ind::Rate3m, QT_TRANSLATE_NOOP("economy", "3M Interbank Rate"), QT_TRANSLATE_NOOP("economy", "3M RATE"), "%", 2, 0, false, "OECD",
     QT_TRANSLATE_NOOP("economy", "Short-term (3-month) interbank rate, monthly average (OECD Financial market statistics).")},
    {Ind::GdpUsd, QT_TRANSLATE_NOOP("economy", "GDP"), QT_TRANSLATE_NOOP("economy", "GDP"), "USD", 2, 1, true, "World Bank", QT_TRANSLATE_NOOP("economy", "GDP at current US$ (World Bank WDI NY.GDP.MKTP.CD).")},
    {Ind::GdpPerCapita, QT_TRANSLATE_NOOP("economy", "GDP per Capita"), QT_TRANSLATE_NOOP("economy", "GDP PER CAPITA"), "USD", 0, 1, true, "World Bank", QT_TRANSLATE_NOOP("economy", "GDP per capita, current US$ (World Bank WDI NY.GDP.PCAP.CD).")},
    {Ind::Population, QT_TRANSLATE_NOOP("economy", "Population"), QT_TRANSLATE_NOOP("economy", "POPULATION"), "", 1, 0, true, "World Bank", QT_TRANSLATE_NOOP("economy", "Total population (World Bank WDI SP.POP.TOTL).")},
    {Ind::RealGrowth, QT_TRANSLATE_NOOP("economy", "Real GDP Growth"), QT_TRANSLATE_NOOP("economy", "REAL GDP GROWTH"), "%", 1, 1, false, "World Bank", QT_TRANSLATE_NOOP("economy", "Real GDP growth, annual % (World Bank WDI NY.GDP.MKTP.KD.ZG).")},
    {Ind::InflationAnnual, QT_TRANSLATE_NOOP("economy", "Inflation (Annual Average)"), QT_TRANSLATE_NOOP("economy", "INFLATION"), "%", 1, -1, false, "World Bank", QT_TRANSLATE_NOOP("economy", "Consumer prices, annual average % (World Bank WDI FP.CPI.TOTL.ZG).")},
    {Ind::UnemploymentAnnual, QT_TRANSLATE_NOOP("economy", "Unemployment (ILO, Annual)"), QT_TRANSLATE_NOOP("economy", "UNEMPLOYMENT"), "%", 1, -1, false, "World Bank", QT_TRANSLATE_NOOP("economy", "Unemployment, % of labour force, ILO modelled estimate (World Bank WDI SL.UEM.TOTL.ZS).")},
    {Ind::CurrentAccount, QT_TRANSLATE_NOOP("economy", "Current Account (% of GDP)"), QT_TRANSLATE_NOOP("economy", "CURRENT ACCOUNT"), "%", 1, 1, false, "World Bank", QT_TRANSLATE_NOOP("economy", "Current account balance, % of GDP (World Bank WDI BN.CAB.XOKA.GD.ZS).")},
    {Ind::GovDebt, QT_TRANSLATE_NOOP("economy", "Gov. Debt / GDP"), QT_TRANSLATE_NOOP("economy", "GOV. DEBT / GDP"), "%", 0, -1, false, "World Bank", QT_TRANSLATE_NOOP("economy", "Central government debt, total, % of GDP (World Bank WDI GC.DOD.TOTL.GD.ZS).")},
    {Ind::FxReserves, QT_TRANSLATE_NOOP("economy", "FX Reserves"), QT_TRANSLATE_NOOP("economy", "FX RESERVES"), "USD", 2, 1, true, "World Bank", QT_TRANSLATE_NOOP("economy", "Total reserves including gold, current US$ (World Bank WDI FI.RES.TOTL.CD).")},
    {Ind::CoreInflation, QT_TRANSLATE_NOOP("economy", "Core Inflation Rate (Annual)"), QT_TRANSLATE_NOOP("economy", "CORE INFLATION"), "%", 1, -1, false, "OECD",
     QT_TRANSLATE_NOOP("economy", "Core consumer prices, year-on-year, in each country's own definition: BLS less food and energy, Eurostat less energy and food, ONS less energy, food, alcohol and tobacco, Japan less fresh food, BoC CPI-XFET, BFS core inflation 1 via the SNB, ABS trimmed mean; elsewhere the OECD's (less food and energy).")},
    {Ind::RetailSalesYoy, QT_TRANSLATE_NOOP("economy", "Retail Sales (Annual y/y)"), QT_TRANSLATE_NOOP("economy", "RETAIL SALES"), "%", 1, 1, false, "OECD",
     QT_TRANSLATE_NOOP("economy", "Retail trade volume, seasonally adjusted, change over 12 months, computed by Deepwater from the OECD index (quarterly where no monthly series exists). Australia: ABS household spending at current prices, which replaced retail trade in 2025.")},
  };
  // translated copy for the interface language (rebuilt when the language changes)
  static QMutex mx; static QString lang; static QList<IndicatorInfo> tt;
  QMutexLocker lock(&mx);
  if (tt.isEmpty() || lang != i18n::code()) {
    lang = i18n::code(); tt = t;
    for (auto& x : tt) {
      x.name = QCoreApplication::translate("economy", x.name.toUtf8().constData());
      x.shortName = QCoreApplication::translate("economy", x.shortName.toUtf8().constData());
      x.note = QCoreApplication::translate("economy", x.note.toUtf8().constData());
    }
  }
  return tt[qBound(0, int(i), tt.size() - 1)];
}

static QString money(double v) {
  const double a = std::fabs(v);
  const QString s = v < 0 ? "-$" : "$";
  if (a >= 1e12) return s + QString::number(a / 1e12, 'f', 2) + "T";
  if (a >= 1e9) return s + QString::number(a / 1e9, 'f', 2) + "B";
  if (a >= 1e6) return s + QString::number(a / 1e6, 'f', 2) + "M";
  return s + fmt::num(a, 0);
}

QString formatValue(Ind i, double v) {
  if (!std::isfinite(v)) return fmt::dash();
  const auto& in = info(i);
  if (i == Ind::GdpPerCapita) return (v < 0 ? "-$" : "$") + fmt::num(std::fabs(v), 0);
  if (in.unit == "USD") return money(v);
  if (i == Ind::Population) {
    if (v >= 1e9) return QString::number(v / 1e9, 'f', 2) + "B";
    if (v >= 1e6) return QString::number(v / 1e6, 'f', 1) + "M";
    return fmt::num(v / 1e3, 1) + "K";
  }
  if (std::fabs(v) * std::pow(10.0, in.decimals) < 0.5) v = 0;   // never "-0.0%"
  if (in.unit == "%") return QString::number(v, 'f', in.decimals) + "%";
  return QString::number(v, 'f', in.decimals);
}

QString formatPeriod(const QDate& d, char freq) {
  if (!d.isValid()) return fmt::dash();
  switch (freq) {
    case 'A': return QString::number(d.year());
    case 'Q': return QCoreApplication::translate("economy", "Q%1 %2").arg((d.month() - 1) / 3 + 1).arg(d.year());
    case 'D': return i18n::locale().toString(d, "d MMM yyyy");
    default: return i18n::locale().toString(d, "MMM yyyy");
  }
}

// ------------------------------------------------------------------------------------------------ store
MacroStore& MacroStore::instance() { static MacroStore* s = new MacroStore; return *s; }
MacroStore& MacroStore::deep() { static MacroStore* s = new MacroStore(true); return *s; }

QUrl MacroStore::u(const QString& url) const {
  if (!m_deep) return QUrl(url);
  QString x = url;
  static const QRegularExpression start("startPeriod=[^&]*&?"), other("[?&](start_date|fromDate)=[^&]*");
  x.remove(start); x.remove(other);
  x.replace("date=2000:", "date=1960:");
  if (x.endsWith('?') || x.endsWith('&')) x.chop(1);
  return QUrl(x);
}

MacroStore::MacroStore(bool deep) : m_deep(deep) {
  connect(&CentralBanks::instance(), &CentralBanks::updated, this, &MacroStore::updated);
}

void MacroStore::merge(const QHash<QString, QHash<int, Series>>& add, const QString& key) {
  for (auto it = add.cbegin(); it != add.cend(); ++it)
    for (auto jt = it->cbegin(); jt != it->cend(); ++jt) { m_data[it.key()][jt.key()] = *jt; remember(it.key(), *jt); }
  m_loaded.insert(key);
  if (m_parsing > 0) --m_parsing;
  applyNational();
  emit updated();
}

void MacroStore::remember(const QString& iso2, const Series& s) {
  static const QSet<QString> eight{"us", "ez", "gb", "jp", "ch", "ca", "au", "nz"};
  static const QSet<int> kept{int(Ind::Inflation), int(Ind::CoreInflation), int(Ind::Unemployment), int(Ind::GdpQoq)};
  if (m_deep || !s.ok() || !eight.contains(iso2) || !kept.contains(int(s.ind))) return;
  m_readings[iso2][int(s.ind)][s.source] = s;
}

void MacroStore::ensureLoaded() {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  auto due = [&](const QString& k, qint64 everyMs) {
    // a data set whose last request failed is asked again after a few minutes, not after its full refresh period
    if (const auto r = m_retryAt.constFind(k); r != m_retryAt.cend()) {
      if (now < *r) return false;
      m_retryAt.remove(k);
    } else if (const auto it = m_started.constFind(k); it != m_started.cend() && now - *it < everyMs) return false;
    m_started[k] = now; return true;
  };
  const qint64 H = 3600 * 1000;
  CentralBanks::instance().ensureLoaded();
  // the deep store (Data › Macro data importer) reads the raw monthly BIS chains only: without startPeriod the daily
  // request was the whole daily history of every bank at each pass, used for nothing
  if (!m_deep && due("bis.cbpol.d", 3 * H)) loadBisDaily();
  if (due("bis.cbpol.m", 12 * H)) loadBisPolicy();
  if (due("bis.cpi", 12 * H)) loadBisCpi();
  if (due("oecd.qna", 12 * H))
    loadOecd("oecd.qna", "https://sdmx.oecd.org/public/rest/data/OECD.SDD.NAD,DSD_NAMAIN1@DF_QNA_EXPENDITURE_GROWTH_OECD,/"
                         "Q.Y..S1.S1.B1GQ._Z._Z._Z.PC.L.G1+GY.?startPeriod=2000-Q1&format=csv", 24 * 3600);
  if (due("oecd.lfs", 12 * H))
    loadOecd("oecd.lfs", "https://sdmx.oecd.org/public/rest/data/OECD.SDD.TPS,DSD_LFS@DF_IALFS_UNE_M,1.0/"
                         "..._Z.Y._T.Y_GE15..M?startPeriod=2000-01&format=csv", 24 * 3600);
  if (due("oecd.cli", 12 * H))
    loadOecd("oecd.cli", "https://sdmx.oecd.org/public/rest/data/OECD.SDD.STES,DSD_STES@DF_CLI,/"
                         ".M.BCICP+CCICP...AA...H?startPeriod=2000-01&format=csv", 24 * 3600);
  if (due("oecd.fin", 12 * H))
    loadOecd("oecd.fin", "https://sdmx.oecd.org/public/rest/data/OECD.SDD.STES,DSD_STES@DF_FINMARK,/"
                         ".M.IRLT+IR3TIB.PA.....?startPeriod=2000-01&format=csv", 24 * 3600);
  static const QList<QPair<Ind, QString>> wb{
    {Ind::GdpUsd, "NY.GDP.MKTP.CD"}, {Ind::GdpPerCapita, "NY.GDP.PCAP.CD"}, {Ind::Population, "SP.POP.TOTL"},
    {Ind::RealGrowth, "NY.GDP.MKTP.KD.ZG"}, {Ind::InflationAnnual, "FP.CPI.TOTL.ZG"},
    {Ind::UnemploymentAnnual, "SL.UEM.TOTL.ZS"}, {Ind::CurrentAccount, "BN.CAB.XOKA.GD.ZS"},
    {Ind::GovDebt, "GC.DOD.TOTL.GD.ZS"}, {Ind::FxReserves, "FI.RES.TOTL.CD"}};
  for (const auto& [ind, code] : wb) if (due("wb." + code, 24 * H)) loadWorldBank(ind, code);
  if (due("core-retail", 12 * H)) loadCoreAndRetail();
  if (m_deep) return;   // the importer records the compilations as published, nothing laid over them
  if (due("oecd.lfs.q", 12 * H))   // New Zealand publishes by quarter only
    loadOecd("oecd.lfs.q", "https://sdmx.oecd.org/public/rest/data/OECD.SDD.TPS,DSD_LFS@DF_IALFS_UNE_M,1.0/"
                           "NZL..._Z.Y._T.Y_GE15..Q?startPeriod=2000-Q1&format=csv", 24 * 3600);
  if (due("national", 3 * H)) loadNational();
  if (!m_calendarWatched) {
    m_calendarWatched = true;
    auto& cal = news::Calendar::instance();
    connect(&cal, &news::Calendar::weekLoaded, this, [this] { readCalendar(); });
    connect(&cal, &news::Calendar::actualArrived, this, [this](const news::Event& e) {
      // a figure read at the office itself: its copy of the whole series is fresh in the cache
      if (e.quality == 9) if (const Economy* c = economyForCurrency(e.currency)) loadNational(c->iso2);
    });
    readCalendar();
  }
}

void MacroStore::failed(const QString& key) {
  m_retryAt[key] = QDateTime::currentMSecsSinceEpoch() + 5 * 60 * 1000;
}

void MacroStore::loadBisPolicy() {
  ++m_pending;
  net::Request r; r.ttlSec = 24 * 3600;
  r.url = u("https://stats.bis.org/api/v1/data/WS_CBPOL/M/all?startPeriod=2000-01&format=csv&detail=dataonly");
  net::get(r, this, [this](const net::Reply& rep) {
    --m_pending;
    if (!rep.ok()) { m_error = "BIS policy rates: " + rep.error; failed("bis.cbpol.m"); emit updated(); return; }
    ++m_parsing;
    runParse(this, [b = rep.body] { return parseBis(b, Ind::PolicyRate, true); },
             [this](Data d) { merge(d.map, "bis.cbpol.m"); });
  });
}

void MacroStore::loadBisDaily() {
  ++m_pending;
  net::Request r; r.ttlSec = 3 * 3600;
  const QString from = QDate::currentDate().addDays(-120).toString("yyyy-MM-dd");
  r.url = u("https://stats.bis.org/api/v1/data/WS_CBPOL/D/all?startPeriod=" + from + "&format=csv&detail=dataonly");
  net::get(r, this, [this](const net::Reply& rep) {
    --m_pending;
    if (!rep.ok()) { m_error = "BIS daily policy rates: " + rep.error; failed("bis.cbpol.d"); emit updated(); return; }
    const QDateTime fetched = rep.fetched;
    ++m_parsing;
    runParse(this, [b = rep.body] { return parseBis(b, Ind::PolicyRate, true); }, [this, fetched](Data d) {
      for (auto it = d.map.cbegin(); it != d.map.cend(); ++it) {
        const Series s = it->value(int(Ind::PolicyRate));
        if (s.ok()) m_bisDaily[it.key()] = s.last();
      }
      m_bisDailyFetched = fetched;
      m_loaded.insert("bis.cbpol.d");
      if (m_parsing > 0) --m_parsing;
      emit updated();
    });
  });
}

void MacroStore::loadBisCpi() {
  ++m_pending;
  net::Request r; r.ttlSec = 24 * 3600;
  r.url = u("https://stats.bis.org/api/v1/data/WS_LONG_CPI/M..771/all?startPeriod=2000-01&format=csv&detail=dataonly");
  net::get(r, this, [this](const net::Reply& rep) {
    --m_pending;
    if (!rep.ok()) { m_error = "BIS consumer prices: " + rep.error; failed("bis.cpi"); emit updated(); return; }
    ++m_parsing;
    runParse(this, [b = rep.body] { return parseBis(b, Ind::Inflation, false); },
             [this](Data d) { merge(d.map, "bis.cpi"); });
  });
}

void MacroStore::loadOecd(const QString& key, const QString& url, int ttl) {
  ++m_pending;
  net::Request r; r.ttlSec = ttl; r.url = u(url); r.timeoutMs = 90000;
  // the OECD API answers 500 to Qt's default "Accept-Language: en-US,*"
  r.headers = {{"Accept-Language", "en"}, {"Accept", "text/csv, */*"}};
  net::get(r, this, [this, key](const net::Reply& rep) {
    --m_pending;
    if (!rep.ok() || !rep.body.startsWith("DATAFLOW")) {
      m_error = "OECD " + key + ": " + (rep.ok() ? QString("unexpected answer") : rep.error);
      qWarning().noquote() << "[macro]" << m_error << rep.status << rep.body.left(120);
      failed(key);
      emit updated(); return;
    }
    QString col; QHash<QString, Ind> pick; QString ds;
    if (key == "oecd.qna") { col = "TRANSFORMATION"; pick = {{"G1", Ind::GdpQoq}, {"GY", Ind::GdpYoy}}; ds = "QNA"; }
    else if (key == "oecd.lfs" || key == "oecd.lfs.q") { col = "MEASURE"; pick = {{"UNE_LF_M", Ind::Unemployment}}; ds = "LFS"; }
    else if (key == "oecd.cli") { col = "MEASURE"; pick = {{"BCICP", Ind::BusinessConf}, {"CCICP", Ind::ConsumerConf}}; ds = "MEI/CLI"; }
    else { col = "MEASURE"; pick = {{"IRLT", Ind::Yield10y}, {"IR3TIB", Ind::Rate3m}}; ds = "FINMARK"; }
    ++m_parsing;
    runParse(this, [b = rep.body, col, pick, ds] { return parseOecd(b, col, pick, ds); },
             [this, key](Data d) { merge(d.map, key); });
  });
}

void MacroStore::loadWorldBank(Ind ind, const QString& code) {
  ++m_pending;
  net::Request r; r.ttlSec = 7 * 24 * 3600; r.timeoutMs = 60000;
  r.url = u("https://api.worldbank.org/v2/country/all/indicator/" + code + "?format=json&per_page=20000&date=2000:" + QString::number(QDate::currentDate().year()));
  net::get(r, this, [this, ind, code](const net::Reply& rep) {
    --m_pending;
    if (!rep.ok() || !rep.body.startsWith('[')) { m_error = "World Bank " + code + ": " + (rep.ok() ? QString("blocked") : rep.error); failed("wb." + code); emit updated(); return; }
    ++m_parsing;
    runParse(this, [b = rep.body, ind, code] { return parseWb(b, ind, code); },
             [this, code](Data d) { merge(d.map, "wb." + code); });
  });
}

Series MacroStore::series(const QString& iso2, Ind i) const {
  Series s = m_data.value(iso2.toLower()).value(int(i));
  if (const CalendarFigure f = m_calendar.value(iso2.toLower()).value(int(i)); f.valid())
    s = national::withFlash(s, f, QDateTime::currentDateTimeUtc());
  if (i == Ind::PolicyRate) {
    // extend the monthly BIS history with the latest feed value (central bank or BIS daily)
    const PolicyRate pr = policyRate(iso2);
    if (pr.valid() && pr.asOf.isValid()) {
      const QDate m(pr.asOf.year(), pr.asOf.month(), 1);
      if (!s.pts.isEmpty() && s.pts.last().date == m) s.pts.last().value = pr.rate;
      else if (s.pts.isEmpty() || s.pts.last().date < m) s.pts.append({m, pr.rate});
      s.ind = i; s.iso2 = iso2.toLower(); s.freq = 'M';
      if (s.source.isEmpty()) s.source = pr.badge;
    }
  }
  return s;
}

Latest MacroStore::latest(const QString& iso2, Ind i) const {
  Latest l;
  if (i == Ind::PolicyRate) {
    const PolicyRate pr = policyRate(iso2);
    l.value = pr.rate; l.period = pr.asOf; l.freq = 'D'; l.source = pr.badge;
    // prior = the rate before the last change
    const Series s = m_data.value(iso2.toLower()).value(int(i));
    for (int k = s.pts.size() - 1; k >= 0; --k)
      if (std::fabs(s.pts[k].value - pr.rate) > 1e-9) { l.prior = s.pts[k].value; break; }
    return l;
  }
  const Series s = series(iso2, i);
  if (!s.ok()) return l;
  l.value = s.last().value; l.prior = s.prior().value; l.period = s.last().date; l.freq = s.freq; l.source = s.source;
  return l;
}

PolicyRate MacroStore::policyRate(const QString& iso2) const {
  PolicyRate pr;
  const Economy* e = economy(iso2);
  if (!e) return pr;
  pr.badge = rateBadge(*e);
  if (!e->bank.isEmpty()) {
    if (const auto* b = CentralBanks::instance().bank(e->bank); b && std::isfinite(b->rate)) {
      pr.rate = b->rate; pr.asOf = b->rateAsOf; pr.sourceName = b->rateSource; pr.fetched = b->rateFetched;
      return pr;
    }
  }
  if (const auto it = m_bisDaily.constFind(e->iso2); it != m_bisDaily.cend()) {
    pr.rate = it->value; pr.asOf = it->date; pr.fetched = m_bisDailyFetched;
    pr.sourceName = QCoreApplication::translate("economy", "BIS central bank policy rates (daily)");
    if (pr.badge == "WB") pr.badge = "BIS";
    return pr;
  }
  const Series s = m_data.value(e->iso2).value(int(Ind::PolicyRate));
  if (s.ok()) { pr.rate = s.last().value; pr.asOf = s.last().date; pr.sourceName = QCoreApplication::translate("economy", "BIS central bank policy rates (monthly)"); }
  return pr;
}

CurrencyMacro MacroStore::forCurrency(const QString& ccy) const {
  CurrencyMacro c; c.currency = ccy.toUpper();
  const Economy* e = economyForCurrency(ccy);
  if (!e) return c;
  c.iso2 = e->iso2;
  c.policy = policyRate(e->iso2);
  c.gdpQoq = latest(e->iso2, Ind::GdpQoq);
  c.inflation = latest(e->iso2, Ind::Inflation);
  c.unemployment = latest(e->iso2, Ind::Unemployment);
  if (!c.unemployment.valid()) c.unemployment = latest(e->iso2, Ind::UnemploymentAnnual);
  c.businessConfidence = latest(e->iso2, Ind::BusinessConf);
  c.consumerConfidence = latest(e->iso2, Ind::ConsumerConf);
  c.yield10y = latest(e->iso2, Ind::Yield10y);
  c.coreInflation = latest(e->iso2, Ind::CoreInflation);
  c.retailSales = latest(e->iso2, Ind::RetailSalesYoy);
  return c;
}

MacroStore::Coverage MacroStore::coverage() const {
  Coverage c; c.economies = economies().size();
  for (const auto& e : economies()) {
    const auto& m = m_data.value(e.iso2);
    bool monthly = false;
    for (int k : {int(Ind::Inflation), int(Ind::Unemployment), int(Ind::PolicyRate), int(Ind::BusinessConf)})
      if (m.value(k).ok() && m.value(k).freq == 'M') monthly = true;
    if (monthly) ++c.monthly;
    if (isEuroMember(e)) continue;   // one feed for the euro area
    if (m_bisDaily.contains(e.iso2) || (!e.bank.isEmpty() && CentralBanks::instance().bank(e.bank))) ++c.liveFeeds;
  }
  return c;
}

// ------------------------------------------------------------------------------------------------ core CPI + retail
void MacroStore::putIfFresher(const QString& iso, const Series& s) {
  if (!s.ok()) return;
  remember(iso, s);
  Series& cur = m_data[iso][int(s.ind)];
  if (const Series n = m_national.value(iso).value(int(s.ind)); n.ok() && cur.source == n.source) cur = {};   // compare with compilations only
  if (!cur.ok() || s.last().date > cur.last().date) {
    // keep the older history of the replaced series before the new one starts
    Series merged = s;
    if (cur.ok() && cur.freq == s.freq) {
      QVector<Point> head;
      for (const auto& p : cur.pts) if (p.date < s.pts.first().date) head << p;
      merged.pts = head + s.pts;
      if (!head.isEmpty()) merged.dataset = s.dataset + " (history: " + cur.source + " " + cur.dataset + ")";
    }
    cur = merged;
  }
  applyNational();
}

// ------------------------------------------------------------------------------------------------ national series
void MacroStore::loadNational(const QString& iso2) {
  if (m_deep) return;
  for (const national::Def& d : national::defs()) {
    if (!iso2.isEmpty() && d.iso2 != iso2) continue;
    ++m_pending;
    ++m_nationalPending[d.iso2];
    official::fetch({d.agency, d.key}, national::ttlSec(d.agency), this, [this, d](const official::Answer& a) {
      --m_pending;
      if (m_nationalPending[d.iso2] > 0) --m_nationalPending[d.iso2];
      const Series s = national::build(d, a);
      if (a.ok() && s.ok()) {
        m_national[d.iso2][int(d.ind)] = s;
        applyNational();
      } else {
        qWarning().noquote() << "[macro]" << official::agencyName(d.agency) << d.key << (a.error.isEmpty() ? QStringLiteral("empty answer") : a.error);
        failed("national");   // asked again in a few minutes; the ones that answered come from the cache
      }
      emit updated();
    });
  }
}

void MacroStore::applyNational() {
  if (m_deep) return;
  static const QHash<QString, bool> instead = [] {
    QHash<QString, bool> h;
    for (const auto& d : national::defs()) h.insert(d.iso2 + QString::number(int(d.ind)), d.instead);
    return h;
  }();
  for (auto it = m_national.cbegin(); it != m_national.cend(); ++it)
    for (auto jt = it->cbegin(); jt != it->cend(); ++jt) {
      Series& cur = m_data[it.key()][jt.key()];
      cur = national::overlay(cur, *jt, instead.value(it.key() + QString::number(jt.key())));
    }
}

void MacroStore::readCalendar() {
  if (m_deep) return;
  auto& cal = news::Calendar::instance();
  const QDateTime now = QDateTime::currentDateTimeUtc();
  QHash<QString, QHash<int, CalendarFigure>> found;
  const auto keep = [&found](const CalendarFigure& f) {
    if (!f.valid()) return;
    CalendarFigure& cur = found[f.iso2][int(f.ind)];
    if (!cur.valid() || f.period > cur.period || (f.period == cur.period && f.published > cur.published)) cur = f;
  };
  for (int w = 0; w < 4; ++w)
    for (const news::Event& e : cal.events(news::Calendar::currentWeek().addDays(-7 * w))) {
      if (e.time > now) continue;
      QString src = e.actualSource;
      src.remove(" headline").remove(" official series");
      if (e.actual.num() && e.actual.suffix == "%") keep(national::flashOf(e.currency, e.title, e.actual.v, e.time, src));
      for (const news::Detail& d : e.details) {
        const news::Value v = news::parseValue(d.actual);
        if (v.num() && v.suffix == "%") keep(national::flashOfDetail(e.currency, d.name, v.v, e.time, d.source));
      }
    }
  m_calendar = found;
  emit updated();
}

void MacroStore::loadCoreAndRetail() {
  auto oecd = [this](const QString& key, const QString& url, std::function<Data(const QByteArray&)> parse) {
    ++m_pending;
    net::Request r; r.ttlSec = 24 * 3600; r.url = u(url); r.timeoutMs = 90000;
    r.headers = {{"Accept-Language", "en"}, {"Accept", "text/csv, */*"}};
    net::get(r, this, [this, key, parse](const net::Reply& rep) {
      --m_pending;
      if (!rep.ok() || !rep.body.startsWith("DATAFLOW")) { m_error = "OECD " + key + ": " + (rep.ok() ? QString("unexpected answer") : rep.error); failed("core-retail"); emit updated(); return; }
      ++m_parsing;
      runParse(this, [b = rep.body, parse] { return parse(b); }, [this, key](Data d) {
        for (auto it = d.map.cbegin(); it != d.map.cend(); ++it)
          for (auto jt = it->cbegin(); jt != it->cend(); ++jt) putIfFresher(it.key(), *jt);
        m_loaded.insert(key);
        if (m_parsing > 0) --m_parsing;
        emit updated();
      });
    });
  };
  // core CPI, monthly (all OECD areas) then quarterly (fills NZ)
  oecd("oecd.core.m", "https://sdmx.oecd.org/public/rest/data/OECD.SDD.TPS,DSD_PRICES@DF_PRICES_ALL,1.0/"
                      ".M.N.CPI.PA._TXCP01_NRG.N.GY?startPeriod=2000-01&format=csv",
       [](const QByteArray& b) { return parseOecd(b, "TRANSFORMATION", {{"GY", Ind::CoreInflation}}, "PRICES_ALL (CPI ex food & energy)"); });
  oecd("oecd.core.q", "https://sdmx.oecd.org/public/rest/data/OECD.SDD.TPS,DSD_PRICES@DF_PRICES_ALL,1.0/"
                      "NZL.Q.N.CPI.PA._TXCP01_NRG.N.GY?startPeriod=2000-Q1&format=csv",
       [](const QByteArray& b) { return parseOecd(b, "TRANSFORMATION", {{"GY", Ind::CoreInflation}}, "PRICES_ALL (CPI ex food & energy, quarterly)"); });
  // retail trade volume index (SA), monthly + quarterly → 12-month change
  oecd("oecd.retail", "https://sdmx.oecd.org/public/rest/data/OECD.SDD.STES,DSD_STES@DF_INDSERV,/"
                      ".M+Q.TOVM.IX.G47.Y......?startPeriod=1999-01&format=csv",
       [](const QByteArray& b) {
         Data idx = parseOecd(b, "FREQ", {{"M", Ind::RetailSalesYoy}}, "INDSERV TOVM G47");
         Data q = parseOecd(b, "FREQ", {{"Q", Ind::RetailSalesYoy}}, "INDSERV TOVM G47 (quarterly)");
         Data out;
         auto yoy = [](const Series& s, int lag) {
           Series y = s; y.pts.clear(); y.source = "OECD (y/y computed by Deepwater)";
           QHash<QDate, double> byDate; for (const auto& p : s.pts) byDate.insert(p.date, p.value);
           for (const auto& p : s.pts) {
             const QDate prev = p.date.addMonths(-lag);
             if (byDate.contains(prev) && byDate[prev] != 0) y.pts << Point{p.date, (p.value / byDate[prev] - 1) * 100};
           }
           return y;
         };
         for (auto it = idx.map.cbegin(); it != idx.map.cend(); ++it) {
           const Series m = it->value(int(Ind::RetailSalesYoy));
           if (m.ok() && m.last().date >= QDate::currentDate().addYears(-2)) out.map[it.key()][int(Ind::RetailSalesYoy)] = yoy(m, 12);
         }
         for (auto it = q.map.cbegin(); it != q.map.cend(); ++it) {
           if (out.map.value(it.key()).contains(int(Ind::RetailSalesYoy))) continue;
           out.map[it.key()][int(Ind::RetailSalesYoy)] = yoy(it->value(int(Ind::RetailSalesYoy)), 12);
         }
         return out;
       });
  // euro area: ECB HICP ex energy & food (history to Dec 2025, changing composition) + Eurostat (current EA)
  {
    net::Request r; r.ttlSec = 24 * 3600;
    r.url = u("https://data-api.ecb.europa.eu/service/data/ICP/M.U2.N.XEF000.4.ANR?startPeriod=2000-01&format=csvdata");
    net::get(r, this, [this](const net::Reply& rep) {
      const auto rows = csv::parse(rep.body);
      if (!rep.ok() || rows.size() < 2) return;
      const int it = rows[0].indexOf("TIME_PERIOD"), iv = rows[0].indexOf("OBS_VALUE");
      Series s; s.ind = Ind::CoreInflation; s.iso2 = "ez"; s.freq = 'M'; s.source = "ECB"; s.dataset = "ICP XEF000 (HICP ex energy & food)";
      for (int i = 1; i < rows.size(); ++i) {
        bool ok = false; const double v = rows[i].value(iv).toDouble(&ok);
        const QDate d = QDate::fromString(rows[i].value(it) + "-01", "yyyy-MM-dd");
        if (ok && d.isValid()) s.pts << Point{d, v};
      }
      std::sort(s.pts.begin(), s.pts.end(), [](const Point& a, const Point& b) { return a.date < b.date; });
      putIfFresher("ez", s); emit updated();
      net::Request e; e.ttlSec = 24 * 3600;
      e.url = u("https://ec.europa.eu/eurostat/api/dissemination/statistics/1.0/data/prc_hicp_minr?format=JSON&lang=EN"
                   "&geo=EA&coicop18=TOT_X_NRG_FOOD&unit=RCH_A&sinceTimePeriod=2024-01");
      net::get(e, this, [this](const net::Reply& er) {
        const auto o = QJsonDocument::fromJson(er.body).object();
        const auto tix = o.value("dimension").toObject().value("time").toObject().value("category").toObject().value("index").toObject();
        const auto vals = o.value("value").toObject();
        if (!er.ok() || tix.isEmpty()) return;
        Series s2; s2.ind = Ind::CoreInflation; s2.iso2 = "ez"; s2.freq = 'M'; s2.source = "Eurostat"; s2.dataset = "HICP ex energy & food (prc_hicp_minr)";
        for (auto it = tix.begin(); it != tix.end(); ++it) {
          const QString k = QString::number(it.value().toInt());
          if (!vals.contains(k)) continue;
          s2.pts << Point{QDate::fromString(it.key() + "-01", "yyyy-MM-dd"), vals.value(k).toDouble()};
        }
        std::sort(s2.pts.begin(), s2.pts.end(), [](const Point& a, const Point& b) { return a.date < b.date; });
        putIfFresher("ez", s2); emit updated();
      });
    });
  }
  // euro area: headline HICP from Eurostat (current EA, flash included), over the BIS series that comes weeks later
  if (!m_deep) {
    net::Request e; e.ttlSec = 3 * 3600;
    e.url = u("https://ec.europa.eu/eurostat/api/dissemination/statistics/1.0/data/prc_hicp_minr?format=JSON&lang=EN"
              "&geo=EA&coicop18=TOTAL&unit=RCH_A&sinceTimePeriod=2016-01");
    net::get(e, this, [this](const net::Reply& er) {
      const auto o = QJsonDocument::fromJson(er.body).object();
      const auto tix = o.value("dimension").toObject().value("time").toObject().value("category").toObject().value("index").toObject();
      const auto vals = o.value("value").toObject();
      if (!er.ok() || tix.isEmpty()) return;
      Series s; s.ind = Ind::Inflation; s.iso2 = "ez"; s.freq = 'M'; s.source = "Eurostat"; s.dataset = "HICP all items (prc_hicp_minr)";
      for (auto it = tix.begin(); it != tix.end(); ++it) {
        const QString k = QString::number(it.value().toInt());
        if (vals.contains(k)) s.pts << Point{QDate::fromString(it.key() + "-01", "yyyy-MM-dd"), vals.value(k).toDouble()};
      }
      std::sort(s.pts.begin(), s.pts.end(), [](const Point& a, const Point& b) { return a.date < b.date; });
      if (!s.ok()) return;
      m_national["ez"][int(Ind::Inflation)] = s;
      applyNational(); emit updated();
    });
  }
  // Canada: Bank of Canada CPI-XFET y/y
  {
    net::Request r; r.ttlSec = 24 * 3600;
    r.url = u("https://www.bankofcanada.ca/valet/observations/STATIC_CPIXFET/json?start_date=2000-01-01");
    net::get(r, this, [this](const net::Reply& rep) {
      const auto obs = QJsonDocument::fromJson(rep.body).object().value("observations").toArray();
      if (!rep.ok() || obs.isEmpty()) return;
      Series s; s.ind = Ind::CoreInflation; s.iso2 = "ca"; s.freq = 'M'; s.source = "Bank of Canada"; s.dataset = "CPI-XFET y/y";
      for (const auto& v : obs) {
        const auto o = v.toObject();
        const QString x = o.value("STATIC_CPIXFET").toObject().value("v").toString();
        if (!x.isEmpty()) s.pts << Point{QDate::fromString(o.value("d").toString(), "yyyy-MM-dd"), x.toDouble()};
      }
      putIfFresher("ca", s); emit updated();
    });
  }
  // Switzerland: BFS core inflation 1 (SNB data portal)
  {
    net::Request r; r.ttlSec = 24 * 3600;
    r.url = u("https://data.snb.ch/api/cube/plkoprinfla/data/csv/en?fromDate=2000-01");
    net::get(r, this, [this](const net::Reply& rep) {
      if (!rep.ok()) return;
      Series s; s.ind = Ind::CoreInflation; s.iso2 = "ch"; s.freq = 'M'; s.source = "BFS via SNB"; s.dataset = "core inflation 1";
      for (const auto& row : csv::parse(rep.body, ';')) {
        if (row.size() < 3 || row[1] != "K1") continue;
        bool ok = false; const double v = row[2].toDouble(&ok);
        const QDate d = QDate::fromString(row[0] + "-01", "yyyy-MM-dd");
        if (ok && d.isValid()) s.pts << Point{d, v};
      }
      putIfFresher("ch", s); emit updated();
    });
  }
  // Japan: e-Stat CPI less fresh food and energy (needs the user's key; the OECD series stops in 2021)
  QString key = settings::apiKey("e-stat");
  if (!key.isEmpty()) {
    net::Request r; r.ttlSec = 24 * 3600; r.timeoutMs = 60000;
    r.url = u("https://api.e-stat.go.jp/rest/3.0/app/json/getStatsData?appId=" + QUrl::toPercentEncoding(key) +
                 "&statsDataId=0004052037&cdCat01=0178&cdTab=3&cdArea=00000&lang=E");
    r.cacheKey = "e-stat 0004052037 0178 tab3";   // keeps the key out of the cache file name hash input
    net::get(r, this, [this](const net::Reply& rep) {
      const auto data = QJsonDocument::fromJson(rep.body).object().value("GET_STATS_DATA").toObject()
                          .value("STATISTICAL_DATA").toObject().value("DATA_INF").toObject().value("VALUE");
      const QJsonArray vals = data.isArray() ? data.toArray() : QJsonArray{data};
      if (!rep.ok() || vals.isEmpty()) return;
      Series s; s.ind = Ind::CoreInflation; s.iso2 = "jp"; s.freq = 'M'; s.source = "e-Stat (Statistics Bureau of Japan)";
      s.dataset = "CPI less fresh food and energy y/y";
      for (const auto& v : vals) {
        const auto o = v.toObject();
        const QString t = o.value("@time").toString();   // yyyy00MMMM
        bool ok = false; const double x = o.value("$").toString().toDouble(&ok);
        if (!ok || t.size() < 10) continue;
        const QDate d(t.left(4).toInt(), t.right(2).toInt(), 1);
        if (d.isValid()) s.pts << Point{d, x};
      }
      std::sort(s.pts.begin(), s.pts.end(), [](const Point& a, const Point& b) { return a.date < b.date; });
      putIfFresher("jp", s); emit updated();
    });
  }
}
}
