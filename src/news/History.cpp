#include <QDateTime>
#include "news/History.h"
#include "macro/Official.h"
#include <QCoreApplication>
#include "net/Http.h"
#include "net/Sources.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPointer>
#include <QTimer>
#include <QLocale>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <map>
#include <memory>
#include <cmath>

namespace news {

static const sources::Registrar regOecd({"oecd", "OECD", {"sdmx.oecd.org"}, sources::Access::Keyless, 3500,
    "Consumer prices, unemployment rates and GDP growth by country (release histories)",
    "OECD data are free to reuse with attribution (CC BY 4.0); the API allows 20 requests per minute", {"news/calendar"}});
static const sources::Registrar regValet({"boc-valet", "Bank of Canada", {"www.bankofcanada.ca"}, sources::Access::Keyless,
    1000, "Core CPI measures (CPI-trim, CPI-median, CPI-common)",
    "Bank of Canada terms: data may be reproduced with attribution", {"news/calendar"}});
static const sources::Registrar regBls({"bls", "US Bureau of Labor Statistics", {"api.bls.gov"}, sources::Access::Keyless,
    2000, "Nonfarm payrolls (CES0000000001)", "US government work, public domain; keyless API v1 (25 queries a day)",
    {"news/calendar"}});

namespace {
enum class Prov { Oecd, Valet, Bls };
enum class Tf { None, PctChange, Annualize, DiffK, Yoy };   // Yoy: official actuals only
struct Def {
  QString id; QString ccy; QStringList titles; Prov prov; QString query; Tf tf; QString unit; QString source; bool q = false;
};

const QString kPrices = "OECD.SDD.TPS,DSD_PRICES@DF_PRICES_ALL,1.0/";
const QString kLfs = "OECD.SDD.TPS,DSD_LFS@DF_IALFS_UNE_M,1.0/";
const QString kQna = "OECD.SDD.NAD,DSD_NAMAIN1@DF_QNA_EXPENDITURE_GROWTH_OECD,1.1/";

QList<Def> defs() {
  QList<Def> d;
  struct C { QString ccy, iso; };
  const QList<C> cs{{"USD", "USA"}, {"EUR", "EA20"}, {"GBP", "GBR"}, {"JPY", "JPN"}, {"CHF", "CHE"},
                    {"CAD", "CAN"}, {"AUD", "AUS"}, {"NZD", "NZL"}};
  for (const auto& c : cs) {
    QStringList yy{"cpi y/y"}, core{"core cpi y/y"};
    if (c.ccy == "EUR") { yy << "cpi flash estimate y/y" << "final cpi y/y"; core << "core cpi flash estimate y/y" << "final core cpi y/y"; }
    if (c.ccy == "AUD") yy << "monthly cpi indicator y/y";
    d << Def{"cpi-yy-" + c.iso, c.ccy, yy, Prov::Oecd, kPrices + c.iso + ".M.N.CPI.PA._T.N.GY", Tf::None, "%",
             QT_TRANSLATE_NOOP("news", "OECD · consumer prices, all items, y/y")};
    d << Def{"core-yy-" + c.iso, c.ccy, core, Prov::Oecd, kPrices + c.iso + ".M.N.CPI.PA._TXCP01_NRG.N.GY", Tf::None, "%",
             QT_TRANSLATE_NOOP("news", "OECD · consumer prices less food and energy, y/y")};
    if (c.ccy == "USD")
      d << Def{"cpi-mm-USA", "USD", {"cpi m/m"}, Prov::Oecd, kPrices + "USA.M.N.CPI.IX._T.S._Z", Tf::PctChange, "%",
               QT_TRANSLATE_NOOP("news", "OECD · consumer prices, seasonally adjusted index, m/m (computed)")};
    else if (c.ccy != "NZD" && c.ccy != "AUD")
      d << Def{"cpi-mm-" + c.iso, c.ccy, {"cpi m/m"}, Prov::Oecd, kPrices + c.iso + ".M.N.CPI.PC._T.N.G1", Tf::None, "%",
               QT_TRANSLATE_NOOP("news", "OECD · consumer prices, all items, m/m")};
    if (c.ccy == "NZD")
      d << Def{"cpi-qq-NZL", "NZD", {"cpi q/q"}, Prov::Oecd, kPrices + "NZL.Q.N.CPI.PC._T.N.G1", Tf::None, "%",
               QT_TRANSLATE_NOOP("news", "OECD · consumer prices, all items, q/q"), true};
    if (c.ccy != "CHF" && c.ccy != "NZD")
      d << Def{"une-" + c.iso, c.ccy, {"unemployment rate"}, Prov::Oecd,
               kLfs + c.iso + ".UNE_LF_M.PT_LF_SUB._Z.Y._T.Y_GE15._Z.M", Tf::None, "%",
               QT_TRANSLATE_NOOP("news", "OECD · harmonised unemployment rate, % of labour force")};
    QStringList gdp{"gdp q/q", "prelim gdp q/q", "flash gdp q/q", "prelim flash gdp q/q", "revised gdp q/q",
                    "final gdp q/q", "second estimate gdp q/q"};
    if (c.ccy == "USD") gdp = {"advance gdp q/q", "prelim gdp q/q", "final gdp q/q"};
    d << Def{"gdp-" + c.iso, c.ccy, gdp, Prov::Oecd, kQna + "Q.Y." + c.iso + ".S1.S1.B1GQ._Z._Z._Z.PC.L.G1.T0102",
             c.ccy == "USD" ? Tf::Annualize : Tf::None, "%",
             c.ccy == "USD" ? QT_TRANSLATE_NOOP("news", "OECD · real GDP, q/q annualised (computed)") : QT_TRANSLATE_NOOP("news", "OECD · real GDP, q/q"), true};
  }
  d << Def{"cpi-trim", "CAD", {"trimmed cpi y/y"}, Prov::Valet, "CPI_TRIM", Tf::None, "%", QT_TRANSLATE_NOOP("news", "Bank of Canada · CPI-trim, y/y")};
  d << Def{"cpi-median", "CAD", {"median cpi y/y"}, Prov::Valet, "CPI_MEDIAN", Tf::None, "%", QT_TRANSLATE_NOOP("news", "Bank of Canada · CPI-median, y/y")};
  d << Def{"cpi-common", "CAD", {"common cpi y/y"}, Prov::Valet, "CPI_COMMON", Tf::None, "%", QT_TRANSLATE_NOOP("news", "Bank of Canada · CPI-common, y/y")};
  d << Def{"nfp", "USD", {"non-farm employment change"}, Prov::Bls, "CES0000000001", Tf::DiffK, "K",
           QT_TRANSLATE_NOOP("news", "BLS · total nonfarm payrolls, monthly change")};
  return d;
}

const Def* defFor(const Event& e) {
  static const QList<Def> all = defs();
  const QString t = e.title.toLower();
  for (const auto& d : all) if (d.ccy == e.currency && d.titles.contains(t)) return &d;
  return nullptr;
}

QDate periodDate(const QString& p) {
  if (p.size() == 7 && p[4] == '-' && p[5] == 'Q') return QDate(p.left(4).toInt(), (p.mid(6).toInt() - 1) * 3 + 1, 1);
  if (p.size() == 7 && p[4] == '-') return QDate(p.left(4).toInt(), p.mid(5).toInt(), 1);
  return QDate::fromString(p.left(10), Qt::ISODate);
}

// OECD SDMX csv: keep the series with the most observations.
QVector<QPair<QDate, double>> parseOecd(const QByteArray& body) {
  const QList<QByteArray> lines = body.split('\n');
  if (lines.isEmpty()) return {};
  const QList<QByteArray> head = lines[0].trimmed().split(',');
  const int ti = head.indexOf("TIME_PERIOD"), vi = head.indexOf("OBS_VALUE");
  if (ti < 0 || vi < 0) return {};
  std::map<QByteArray, std::map<QDate, double>> series;
  for (int i = 1; i < lines.size(); ++i) {
    const QList<QByteArray> f = lines[i].trimmed().split(',');
    if (f.size() <= std::max(ti, vi)) continue;
    bool ok = false; const double v = f[vi].toDouble(&ok);
    const QDate d = periodDate(QString::fromLatin1(f[ti]));
    if (!ok || !d.isValid()) continue;
    QByteArray key; for (int k = 0; k < ti; ++k) key += f[k] + ',';
    series[key][d] = v;
  }
  const std::map<QDate, double>* best = nullptr;
  for (const auto& [k, s] : series) if (!best || s.size() > best->size()) best = &s;
  QVector<QPair<QDate, double>> out;
  if (best) for (const auto& [d, v] : *best) out << qMakePair(d, v);
  return out;
}

QVector<QPair<QDate, double>> parseValet(const QByteArray& body, const QString& id) {
  QVector<QPair<QDate, double>> out;
  for (const auto& o : QJsonDocument::fromJson(body).object()["observations"].toArray()) {
    const auto obj = o.toObject();
    bool ok = false; const double v = obj[id].toObject()["v"].toString().toDouble(&ok);
    const QDate d = QDate::fromString(obj["d"].toString(), Qt::ISODate);
    if (ok && d.isValid()) out << qMakePair(d, v);
  }
  return out;
}

QVector<QPair<QDate, double>> parseBls(const QByteArray& body) {
  QVector<QPair<QDate, double>> out;
  const auto series = QJsonDocument::fromJson(body).object()["Results"].toObject()["series"].toArray();
  for (const auto& s : series)
    for (const auto& o : s.toObject()["data"].toArray()) {
      const auto obj = o.toObject();
      const QString per = obj["period"].toString();
      if (!per.startsWith('M') || per == "M13") continue;
      bool ok = false; const double v = obj["value"].toString().toDouble(&ok);
      if (ok) out << qMakePair(QDate(obj["year"].toString().toInt(), per.mid(1).toInt(), 1), v);
    }
  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  return out;
}

void transform(QVector<QPair<QDate, double>>& s, Tf tf) {
  if (tf == Tf::None) return;
  QVector<QPair<QDate, double>> out;
  for (int i = 0; i < s.size(); ++i) {
    if (tf == Tf::Annualize) { out << qMakePair(s[i].first, (std::pow(1 + s[i].second / 100, 4) - 1) * 100); continue; }
    if (i == 0) continue;
    if (tf == Tf::PctChange) out << qMakePair(s[i].first, (s[i].second / s[i - 1].second - 1) * 100);
    else out << qMakePair(s[i].first, s[i].second - s[i - 1].second);   // payrolls are in thousands already
  }
  s = out;
}
}

History& History::instance() { static History* h = new History; return *h; }
bool History::hasOfficial(const Event& e) const { return defFor(e) != nullptr; }

void History::fetch(const Event& e, QObject* ctx, std::function<void(const Series&)> cb) {
  QPointer<QObject> guard(ctx);
  Series base; base.title = e.name; base.country = Calendar::countryOf(e.currency);
  const Def* d = defFor(e);
  auto recorded = [e, base]() {
    Series s = base; s.recorded = true; s.unit = e.actual.has() ? e.actual.suffix : e.previous.suffix;
    s.source = QT_TRANSLATE_NOOP("news", "recorded calendar releases");   // translated when shown
    for (const auto& [t, v] : Calendar::instance().recordedReleases(e.seriesKey)) { s.dates << t.date(); s.values << v.v; }
    if (s.values.isEmpty())
      s.error = QCoreApplication::translate("news", "No release history yet: no official series is mapped to this indicator, and Deepwater records "
                "each actual from now on.");
    return s;
  };
  if (!d) { QTimer::singleShot(0, ctx, [cb, s = recorded()] { cb(s); }); return; }
  // the memory copy follows the request's 12 h TTL: kept for good, a session left open for days never showed the
  // releases published meanwhile
  if (m_cachedAt.value(d->id) && QDateTime::currentMSecsSinceEpoch() - m_cachedAt.value(d->id) > 12LL * 3600 * 1000) {
    m_cache.remove(d->id); m_cachedAt.remove(d->id);
  }
  if (auto it = m_cache.constFind(d->id); it != m_cache.cend()) {
    Series s = *it; s.title = base.title; s.country = base.country;
    QTimer::singleShot(0, ctx, [cb, s] { cb(s); });
    return;
  }
  const Def def = *d;
  auto done = [this, def, base, cb, guard, recorded](QVector<QPair<QDate, double>> pts, const QString& err) {
    transform(pts, def.tf);
    Series s = base; s.unit = def.unit; s.source = def.source; s.quarterly = def.q;
    for (const auto& [dt, v] : pts) if (dt.year() >= 2000) { s.dates << dt; s.values << v; }
    if (s.values.isEmpty()) { s = recorded(); if (!err.isEmpty() && s.values.isEmpty()) s.error = QCoreApplication::translate("news", "Official series unavailable (%1).").arg(err); }
    else { m_cache.insert(def.id, s); m_cachedAt.insert(def.id, QDateTime::currentMSecsSinceEpoch()); }
    if (guard) cb(s);
  };
  net::Request r; r.ttlSec = 12 * 3600;
  if (def.prov == Prov::Oecd) {
    r.url = QUrl("https://sdmx.oecd.org/public/rest/data/" + def.query +
                 "?startPeriod=2000-01&dimensionAtObservation=AllDimensions&format=csv");
    r.headers << qMakePair(QByteArray("Accept-Language"), QByteArray("en"));   // Qt's default "en-US,*" → HTTP 500
    // the OECD API answers 500/429 now and then under load: one retry after a few seconds
    net::get(r, this, [this, done, r](const net::Reply& rep) {
      if (rep.body.isEmpty() && (rep.status >= 500 || rep.status == 429 || rep.status == 0)) {
        QTimer::singleShot(5000, this, [this, done, r] {
          net::get(r, this, [done](const net::Reply& rep2) {
            done(parseOecd(rep2.body), rep2.ok() || rep2.stale ? QString() : QString("OECD HTTP %1").arg(rep2.status));
          });
        });
        return;
      }
      done(parseOecd(rep.body), rep.ok() || rep.stale ? QString() : QString("OECD HTTP %1").arg(rep.status));
    });
  } else if (def.prov == Prov::Valet) {
    r.url = QUrl("https://www.bankofcanada.ca/valet/observations/" + def.query + "/json?start_date=2000-01-01");
    const QString id = def.query;
    net::get(r, this, [done, id](const net::Reply& rep) { done(parseValet(rep.body, id), rep.ok() ? QString() : QString("Bank of Canada HTTP %1").arg(rep.status)); });
  } else {
    // BLS v1 answers at most 10 years per request: two requests cover 20 years
    const int y = QDate::currentDate().year();
    auto parts = std::make_shared<QVector<QPair<QDate, double>>>();
    auto left = std::make_shared<int>(2);
    for (int k = 0; k < 2; ++k) {
      net::Request rk = r;
      rk.url = QUrl(QString("https://api.bls.gov/publicAPI/v1/timeseries/data/%1?startyear=%2&endyear=%3")
                        .arg(def.query).arg(y - 19 + 10 * k).arg(y - 10 + 10 * k));
      net::get(rk, this, [done, parts, left](const net::Reply& rep) {
        *parts += parseBls(rep.body);
        if (--*left == 0) {
          std::sort(parts->begin(), parts->end(), [](const auto& a, const auto& b) { return a.first < b.first; });
          QVector<QPair<QDate, double>> u;
          for (const auto& p : *parts) if (u.isEmpty() || u.last().first != p.first) u << p;
          done(u, {});
        }
      });
    }
  }
}

// ------------------------------------------------------------------------------------------------ official actuals
namespace {
struct ActualDef {
  Def def; QString provLabel;
  bool agency = false; official::Id id{official::Agency::Ons, {}};   // agency: read through macro/Official (def.prov unused)
  int lag = 1;          // months between the release's month and the month it measures
  double tol = 0;       // how far the previous observation may have been revised from the calendar's "previous"
  bool sameMonth = false;   // "Revised" survey: its previous is the first estimate of the same month
};

// Series whose observations are exactly the published release figure (no estimate in between).
QList<ActualDef> actualDefs() {
  QList<ActualDef> out;
  using A = official::Agency;
  const auto add = [&out](const char* id, const char* ccy, const QStringList& titles, A agency, const char* key, Tf tf,
                          const char* unit, const char* source, int lag = 1, double tol = 0.2, bool q = false) {
    ActualDef d;
    d.def = Def{id, ccy, titles, Prov::Oecd, QString(), tf, unit, source, q};
    d.provLabel = official::agencyName(agency);
    d.agency = true; d.id = {agency, key}; d.lag = lag; d.tol = tol;
    out << d;
  };
  // United Kingdom (ONS). The unemployment rate and earnings are three-month averages, labelled by their middle month
  // and last month.
  add("ons-cpi-yy", "GBP", {"cpi y/y"}, A::Ons, "economy/inflationandpriceindices/timeseries/d7g7/mm23", Tf::None, "%", QT_TRANSLATE_NOOP("news", "ONS · consumer prices, y/y"), 1, 0.1);
  add("ons-core-yy", "GBP", {"core cpi y/y"}, A::Ons, "economy/inflationandpriceindices/timeseries/dko8/mm23", Tf::None, "%", QT_TRANSLATE_NOOP("news", "ONS · core consumer prices, y/y"), 1, 0.1);
  add("ons-cpi-mm", "GBP", {"cpi m/m"}, A::Ons, "economy/inflationandpriceindices/timeseries/d7oe/mm23", Tf::None, "%", QT_TRANSLATE_NOOP("news", "ONS · consumer prices, m/m"), 1, 0.1);
  add("ons-une", "GBP", {"unemployment rate"}, A::Ons, "employmentandlabourmarket/peoplenotinwork/unemployment/timeseries/mgsx/lms", Tf::None, "%", QT_TRANSLATE_NOOP("news", "ONS · unemployment rate, three months"), 3, 0.1);
  add("ons-awe", "GBP", {"average earnings index 3m/y"}, A::Ons, "employmentandlabourmarket/peopleinwork/earningsandworkinghours/timeseries/kac3/lms", Tf::None, "%", QT_TRANSLATE_NOOP("news", "ONS · average weekly earnings, total pay, three months, y/y"), 2, 0.3);
  add("ons-gdp-mm", "GBP", {"gdp m/m"}, A::Ons, "economy/grossdomesticproductgdp/timeseries/ecyx/mgdp", Tf::None, "%", QT_TRANSLATE_NOOP("news", "ONS · monthly GDP, m/m"), 2, 0.3);
  add("ons-gdp-qq", "GBP", {"prelim gdp q/q", "final gdp q/q"}, A::Ons, "economy/grossdomesticproductgdp/timeseries/ihyq/pn2", Tf::None, "%", QT_TRANSLATE_NOOP("news", "ONS · GDP, q/q"), 1, 0.3, true);
  // Canada (Statistics Canada)
  add("can-emp", "CAD", {"employment change"}, A::StatCan, "2062811", Tf::DiffK, "K", QT_TRANSLATE_NOOP("news", "Statistics Canada · employment, monthly change"), 1, 5);
  add("can-une", "CAD", {"unemployment rate"}, A::StatCan, "2062815", Tf::None, "%", QT_TRANSLATE_NOOP("news", "Statistics Canada · unemployment rate"), 1, 0.1);
  add("can-cpi-mm", "CAD", {"cpi m/m"}, A::StatCan, "41690973", Tf::PctChange, "%", QT_TRANSLATE_NOOP("news", "Statistics Canada · consumer prices, m/m (computed from the index)"), 1, 0);
  add("can-cpi-yy", "CAD", {"cpi y/y"}, A::StatCan, "41690973", Tf::Yoy, "%", QT_TRANSLATE_NOOP("news", "Statistics Canada · consumer prices, y/y (computed from the index)"), 1, 0);
  add("can-gdp-mm", "CAD", {"gdp m/m"}, A::StatCan, "65201210", Tf::PctChange, "%", QT_TRANSLATE_NOOP("news", "Statistics Canada · GDP by industry, m/m (computed)"), 2, 0.2);
  // Australia (ABS)
  add("abs-cpi-yy", "AUD", {"cpi y/y", "monthly cpi indicator y/y"}, A::Abs, "CPI/3.10001.10.50.M", Tf::None, "%", QT_TRANSLATE_NOOP("news", "ABS · consumer prices, y/y"), 1, 0.1);
  add("abs-cpi-mm", "AUD", {"cpi m/m"}, A::Abs, "CPI/2.10001.10.50.M", Tf::None, "%", QT_TRANSLATE_NOOP("news", "ABS · consumer prices, m/m"), 1, 0.1);
  add("abs-tm-mm", "AUD", {"trimmed mean cpi m/m"}, A::Abs, "CPI/2.999902.20.50.M", Tf::None, "%", QT_TRANSLATE_NOOP("news", "ABS · trimmed mean consumer prices, m/m"), 1, 0.2);
  add("abs-tm-yy", "AUD", {"trimmed mean cpi y/y"}, A::Abs, "CPI/3.999902.20.50.M", Tf::None, "%", QT_TRANSLATE_NOOP("news", "ABS · trimmed mean consumer prices, y/y"), 1, 0.2);
  add("abs-emp", "AUD", {"employment change"}, A::Abs, "LF/M3.3.1599.20.AUS.M", Tf::DiffK, "K", QT_TRANSLATE_NOOP("news", "ABS · employment, monthly change"), 1, 5);
  add("abs-une", "AUD", {"unemployment rate"}, A::Abs, "LF/M13.3.1599.20.AUS.M", Tf::None, "%", QT_TRANSLATE_NOOP("news", "ABS · unemployment rate"), 1, 0.1);
  add("abs-wpi", "AUD", {"wage price index q/q"}, A::Abs, "WPI/2.THRPEB.7.TOT.20.AUS.Q", Tf::None, "%", QT_TRANSLATE_NOOP("news", "ABS · wage price index, q/q"), 1, 0.2, true);
  add("abs-gdp", "AUD", {"gdp q/q"}, A::Abs, "ANA_AGG/M2.GPM.20.AUS.Q", Tf::None, "%", QT_TRANSLATE_NOOP("news", "ABS · GDP, q/q"), 1, 0.3, true);
  // United States: retail sales (Census), consumer survey (Michigan), labour market and prices (BLS)
  add("us-retail", "USD", {"retail sales m/m"}, A::Census, "adv44X72.txt", Tf::PctChange, "%", QT_TRANSLATE_NOOP("news", "US Census Bureau · advance retail sales, m/m (computed)"), 1, 0.4);
  add("us-core-retail", "USD", {"core retail sales m/m"}, A::Census, "adv44Y72.txt", Tf::PctChange, "%", QT_TRANSLATE_NOOP("news", "US Census Bureau · advance retail sales excluding motor vehicles, m/m (computed)"), 1, 0.4);
  add("uom-sent", "USD", {"prelim uom consumer sentiment", "revised uom consumer sentiment"}, A::Umich, "tbmics.csv#0", Tf::None, "", QT_TRANSLATE_NOOP("news", "University of Michigan · consumer sentiment"), 0, 0.5);
  add("uom-infl", "USD", {"prelim uom inflation expectations", "revised uom inflation expectations"}, A::Umich, "tbmpx1px5.csv#0", Tf::None, "%", QT_TRANSLATE_NOOP("news", "University of Michigan · inflation expectations, one year"), 0, 0.3);
  add("bls-nfp", "USD", {"non-farm employment change"}, A::Bls, "CES0000000001", Tf::DiffK, "K", QT_TRANSLATE_NOOP("news", "BLS · total nonfarm payrolls, monthly change"), 1, 150);
  add("bls-une", "USD", {"unemployment rate"}, A::Bls, "LNS14000000", Tf::None, "%", QT_TRANSLATE_NOOP("news", "BLS · unemployment rate"), 1, 0.1);
  add("bls-ahe", "USD", {"average hourly earnings m/m"}, A::Bls, "CES0500000003", Tf::PctChange, "%", QT_TRANSLATE_NOOP("news", "BLS · average hourly earnings, m/m (computed)"), 1, 0.2);
  // BLS computes CPI percent changes from indexes rounded to three decimals: the same computation here gives exactly the
  // published figure.
  add("bls-cpi-sa", "USD", {"cpi m/m"}, A::Bls, "CUSR0000SA0", Tf::PctChange, "%", QT_TRANSLATE_NOOP("news", "BLS · CPI-U, SA, m/m"), 1, 0.1);
  add("bls-core-sa", "USD", {"core cpi m/m"}, A::Bls, "CUSR0000SA0L1E", Tf::PctChange, "%", QT_TRANSLATE_NOOP("news", "BLS · core CPI-U, SA, m/m"), 1, 0.1);
  add("bls-cpi-yy", "USD", {"cpi y/y"}, A::Bls, "CUUR0000SA0", Tf::Yoy, "%", QT_TRANSLATE_NOOP("news", "BLS · CPI-U, y/y"), 1, 0);
  add("bls-core-yy", "USD", {"core cpi y/y"}, A::Bls, "CUUR0000SA0L1E", Tf::Yoy, "%", QT_TRANSLATE_NOOP("news", "BLS · core CPI-U, y/y"), 1, 0);
  add("bls-ppi-sa", "USD", {"ppi m/m"}, A::Bls, "WPSFD4", Tf::PctChange, "%", QT_TRANSLATE_NOOP("news", "BLS · PPI final demand, SA, m/m"), 1, 0.3);
  add("bls-coreppi-sa", "USD", {"core ppi m/m"}, A::Bls, "WPSFD49104", Tf::PctChange, "%", QT_TRANSLATE_NOOP("news", "BLS · PPI final demand less foods and energy, SA, m/m"), 1, 0.3);
  for (auto& d : out) d.sameMonth = d.id.agency == A::Umich;

  for (const auto& d : defs()) {
    if (d.tf != Tf::None) continue;                          // computed transforms are not the published figure
    if (d.ccy == "GBP" && d.id.startsWith("une-")) continue; // UK rate is a 3-month average labelled differently
    ActualDef a;
    a.def = d; a.provLabel = d.prov == Prov::Valet ? "Bank of Canada Valet" : "OECD";
    if (d.prov == Prov::Valet) a.tol = 0.2;                  // the core measures are estimated anew every month
    out << a;
  }
  const auto german = [&out](const char* id, const QStringList& titles, const QString& query, const char* source) {
    ActualDef a;
    a.def = Def{id, "EUR", titles, Prov::Oecd, query, Tf::None, "%", source};
    a.provLabel = "OECD";
    out << a;
  };
  german("deu-cpi-mm", {"german prelim cpi m/m", "german final cpi m/m"}, kPrices + "DEU.M.N.CPI.PC._T.N.G1", QT_TRANSLATE_NOOP("news", "OECD · Germany consumer prices, m/m"));
  german("deu-cpi-yy", {"german prelim cpi y/y", "german final cpi y/y"}, kPrices + "DEU.M.N.CPI.PA._T.N.GY", QT_TRANSLATE_NOOP("news", "OECD · Germany consumer prices, y/y"));
  return out;
}

const QList<ActualDef>& allActualDefs() { static const QList<ActualDef> all = actualDefs(); return all; }

QList<const ActualDef*> actualDefsFor(const Event& e) {
  QList<const ActualDef*> out;
  const QString t = e.title.toLower();
  for (const auto& d : allActualDefs()) if (d.def.ccy == e.currency && d.def.titles.contains(t)) out << &d;
  return out;
}

// A copy downloaded before the release is useless, one from just after it is asked again soon (the office may have
// published since), an old one seldom.
int officialTtl(const Event& e, bool bls) {
  const qint64 since = e.time.secsTo(QDateTime::currentDateTimeUtc());
  return int(std::clamp<qint64>(since / 3, bls ? 600 : 60, 3 * 3600));
}

// The figure a release gave, read in its series. Empty value: the series does not prove it.
OfficialActual decide(const Event& e, const ActualDef& ad, official::Points pts, const QDateTime& fetched) {
  OfficialActual none;
  const Def& def = ad.def;
  if (ad.agency) {
    if (fetched.isValid() && fetched < e.time) return none;      // a copy from before the release
    if (ad.id.agency == official::Agency::Bls && (def.tf == Tf::PctChange || def.tf == Tf::Yoy))
      for (auto& p : pts) p.second = std::round(p.second * 1000) / 1000;
    if (def.tf == Tf::PctChange) pts = official::pctChange(pts, 1);
    else if (def.tf == Tf::Yoy) pts = official::pctChange(pts, 12);
    else if (def.tf == Tf::DiffK) pts = official::diff(pts);
  }
  const QDate rel = e.time.toUTC().date();
  QDate target;
  if (def.q) {
    const QDate qs(rel.year(), ((rel.month() - 1) / 3) * 3 + 1, 1);
    target = qs.addMonths(-3);
    if (target.addMonths(3).daysTo(rel) > 100) return none;
  } else {
    target = QDate(rel.year(), rel.month(), 1).addMonths(-ad.lag);
    // flash / preliminary monthly figures come out at the end of the month they measure (German prelim CPI on
    // the 30th, euro-area flash HICP on the last working day) or in the first days of the next one
    const bool early = !ad.agency && (e.title.contains("flash", Qt::CaseInsensitive) || e.title.contains("prelim", Qt::CaseInsensitive));
    if (early && rel.day() >= 15) target = QDate(rel.year(), rel.month(), 1);
  }
  int i = -1;
  for (int k = 0; k < pts.size(); ++k) if (pts[k].first == target) i = k;
  if (i < 0) return none;
  const Value& like = e.previous.num() ? e.previous : e.forecast;
  const int dec = std::max(e.previous.num() ? e.previous.decimals : 0, e.forecast.num() ? e.forecast.decimals : 0);
  if (like.suffix != def.unit) return none;
  // the observation before must be the calendar's previous ("final" and "revised" releases: their previous is the first
  // estimate of the same period, so the period's own observation is checked against it)
  const bool final = e.title.startsWith("Final", Qt::CaseInsensitive) || e.title.contains(" Final ", Qt::CaseInsensitive) ||
                     e.title.startsWith("Revised", Qt::CaseInsensitive);
  OfficialActual o;
  if (e.previous.num()) {
    const int j = final ? i : i - 1;
    if (j < 0) return none;
    const double f = std::pow(10.0, e.previous.decimals);
    const double seen = std::round(pts[j].second * f) / f, gap = std::abs(seen - e.previous.v);
    const double tol = final && ad.sameMonth ? (def.unit == "%" ? 1.0 : 6.0) : ad.tol;
    if (gap > std::max(tol, 0.5 / f) + 1e-9) return none;
    if (final && ad.sameMonth && gap < 0.5 / f && fetched.isValid() && e.time.secsTo(fetched) < 2 * 3600) return none;   // may still be the first estimate
    if (!final && gap >= 0.5 / f) { o.revised = parseValue(formatLike(seen, e.previous)); }
  } else if (final) return none;
  // too close to a rounding boundary to know the published digit: leave it empty
  if (def.tf != Tf::None || !ad.agency) {
    const double f = std::pow(10.0, dec), x = std::abs(pts[i].second) * f;
    if (std::abs(x - std::floor(x) - 0.5) < 0.02) return none;
  }
  Value v = like; v.decimals = dec;
  v.text = formatLike(pts[i].second, v);
  o.value = parseValue(v.text);
  const QString per = def.q ? QString("Q%1 %2").arg((target.month() - 1) / 3 + 1).arg(target.year())
                            : QLocale(QLocale::English).toString(target, "MMM yyyy");
  o.note = QString("%1 official series\n%2, %3 = %4 (previous checked against the calendar)")
               .arg(ad.provLabel, def.source, per, QString::number(pts[i].second, 'f', 3));
  return o;
}

struct SplitDef { const char* ccy; const char* title; official::Agency agency; const char* full; const char* part; };
const SplitDef kSplits[] = {
  {"AUD", "employment change", official::Agency::Abs, "LF/M1.3.1599.20.AUS.M", "LF/M2.3.1599.20.AUS.M"},
  {"CAD", "employment change", official::Agency::StatCan, "2062812", "2062813"},
  {"USD", "non-farm employment change", official::Agency::Bls, "LNS12500000", "LNS12600000"}};   // household survey
const SplitDef* splitFor(const Event& e) {
  const QString t = e.title.toLower();
  for (const auto& d : kSplits) if (e.currency == d.ccy && t == d.title) return &d;
  return nullptr;
}
}

// The office itself first, the international compilation when it does not have it.
namespace {
void tryOfficial(QObject* self, const Event& e, const QList<const ActualDef*>& list, int k, const QPointer<QObject>& guard,
                 const std::function<void(const OfficialActual&)>& cb) {
  if (!guard) return;
  if (k >= list.size()) { cb({}); return; }
  const ActualDef& ad = *list[k];   // static storage
  const auto done = [self, e, list, k, guard, cb](const official::Points& pts, const QDateTime& fetched) {
    if (!guard) return;
    const OfficialActual o = decide(e, *list[k], pts, fetched);
    if (o.value.has()) cb(o);
    else tryOfficial(self, e, list, k + 1, guard, cb);
  };
  if (ad.agency) {
    official::fetch(ad.id, officialTtl(e, ad.id.agency == official::Agency::Bls), self,
                    [done](const official::Answer& a) { done(a.pts, a.fetched); });
    return;
  }
  net::Request r;
  r.ttlSec = ad.def.prov == Prov::Valet ? officialTtl(e, false) : 3 * 3600;
  if (ad.def.prov == Prov::Oecd) {
    r.url = QUrl("https://sdmx.oecd.org/public/rest/data/" + ad.def.query +
                 "?startPeriod=2000-01&dimensionAtObservation=AllDimensions&format=csv");
    r.headers << qMakePair(QByteArray("Accept-Language"), QByteArray("en"));
    net::get(r, self, [done](const net::Reply& rep) { done(parseOecd(rep.body), {}); });
  } else {
    r.url = QUrl("https://www.bankofcanada.ca/valet/observations/" + ad.def.query + "/json?start_date=2000-01-01");
    const QString id = ad.def.query;
    net::get(r, self, [done, id](const net::Reply& rep) { done(parseValet(rep.body, id), {}); });
  }
}
}

bool History::canOfficial(const Event& e) const { return !actualDefsFor(e).isEmpty(); }

void History::officialActual(const Event& e, QObject* ctx, std::function<void(const OfficialActual&)> cb) {
  const QList<const ActualDef*> list = actualDefsFor(e);
  QPointer<QObject> guard(ctx);
  if (list.isEmpty() || e.actual.has() || e.time > QDateTime::currentDateTimeUtc() || (!e.previous.num() && !e.forecast.num())) {
    QTimer::singleShot(0, ctx, [cb] { cb({}); });
    return;
  }
  QTimer::singleShot(0, ctx, [this, e, list, guard, cb] { tryOfficial(this, e, list, 0, guard, cb); });
}

bool History::hasOfficialDetails(const Event& e) const { return splitFor(e) != nullptr; }
QString History::officialDetailsSource(const Event& e) const {
  const SplitDef* d = splitFor(e);
  return d ? official::agencyName(d->agency) : QString();
}

void History::officialDetails(const Event& e, QObject* ctx, std::function<void(const QList<Detail>&)> cb) {
  const SplitDef* d = splitFor(e);
  const QDateTime now = QDateTime::currentDateTimeUtc();
  if (!d || e.time > now) { QTimer::singleShot(0, ctx, [cb] { cb({}); }); return; }
  const QDate rel = e.time.toUTC().date();
  const QDate target = QDate(rel.year(), rel.month(), 1).addMonths(-1);
  const int ttl = officialTtl(e, d->agency == official::Agency::Bls);
  const SplitDef def = *d;
  // what the two series must add up to is the total the calendar shows: checked when it is known
  official::fetch({def.agency, def.full}, ttl, ctx, [def, ttl, ctx, e, target, cb](const official::Answer& full) {
    official::fetch({def.agency, def.part}, ttl, ctx, [def, e, target, cb, full](const official::Answer& part) {
      const auto read = [&](const official::Answer& a, const char* name) -> Detail {
        Detail x;
        if (!a.ok() || (a.fetched.isValid() && a.fetched < e.time)) return x;
        const official::Points ch = official::diff(a.pts);
        for (int i = 1; i < ch.size(); ++i) {
          if (ch[i].first != target) continue;
          Value like; like.suffix = "K"; like.decimals = 1;
          x.name = name;
          x.actual = formatLike(ch[i].second, like, true);
          x.previous = formatLike(ch[i - 1].second, like, true);
          x.source = official::agencyName(def.agency);
          x.at = a.fetched;
        }
        return x;
      };
      const Detail f = read(full, QT_TRANSLATE_NOOP("news", "Full-time jobs")), p = read(part, QT_TRANSLATE_NOOP("news", "Part-time jobs"));
      QList<Detail> out;
      if (!f.name.isEmpty() && !p.name.isEmpty()) out << f << p;
      cb(out);
    });
  });
}
}
