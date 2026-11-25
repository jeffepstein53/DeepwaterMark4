#include "macro/Official.h"
#include "net/Http.h"
#include "net/Sources.h"
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>
#include <cmath>

namespace official {
namespace {
const sources::Registrar regOns({"ons", "UK Office for National Statistics", {"www.ons.gov.uk"}, sources::Access::Keyless, 3000,
    "UK consumer prices, unemployment, earnings, GDP and retail sales as published",
    "Open Government Licence v3.0: free reuse with attribution", {"news/calendar", "economy/macro"}});
const sources::Registrar regStatCan({"statcan", "Statistics Canada", {"www150.statcan.gc.ca"}, sources::Access::Keyless, 3000,
    "Canadian employment, unemployment, consumer prices and monthly GDP as published (web data service)",
    "Statistics Canada Open Licence: free reuse with attribution", {"news/calendar", "economy/macro"}});
const sources::Registrar regAbs({"abs", "Australian Bureau of Statistics", {"data.api.abs.gov.au"}, sources::Access::Keyless, 3000,
    "Australian consumer prices, employment, unemployment and wages as published (data API)",
    "Creative Commons Attribution 4.0", {"news/calendar", "economy/macro"}});
const sources::Registrar regCensus({"census", "US Census Bureau", {"www.census.gov"}, sources::Access::Keyless, 3000,
    "US advance retail sales, seasonally adjusted", "US government work, public domain", {"news/calendar"}});
const sources::Registrar regUmich({"umich", "University of Michigan, Surveys of Consumers", {"www.sca.isr.umich.edu"}, sources::Access::Keyless, 5000,
    "US consumer sentiment and inflation expectations", "Public data files; reuse with attribution", {"news/calendar"}});

const sources::Registrar regEstatDash({"estat-dashboard", "Statistics Bureau of Japan (Statistics Dashboard API)", {"dashboard.e-stat.go.jp"},
    sources::Access::Keyless, 3000, "Japanese consumer prices, all items and less fresh food, y/y, as published",
    "Government of Japan Standard Terms of Use: free reuse with the source stated", {"news/calendar", "economy/macro"}});

double number(const QString& s, bool* ok) {
  QString t = s.trimmed();
  t.remove(',');
  return t.toDouble(ok);
}

int monthOf(const QString& name) {
  static const QStringList m{"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
  return int(m.indexOf(name.trimmed().left(3).toLower())) + 1;
}

// "2026-10", "2026-Q3", "2026 Q3" or "2026-10-01" → the first day of the period
QDate periodStart(const QString& p) {
  if (p.size() >= 7 && p[5] == 'Q') return QDate(p.left(4).toInt(), (p.mid(6, 1).toInt() - 1) * 3 + 1, 1);
  if (p.size() >= 7 && p[4] == '-') return QDate(p.left(4).toInt(), p.mid(5, 2).toInt(), 1);
  return {};
}

void sortUnique(Points& v) {
  std::stable_sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  Points u;
  for (const auto& p : v) { if (!u.isEmpty() && u.last().first == p.first) u.last() = p; else u << p; }
  v = u;
}
}

QString agencyName(Agency a) {
  switch (a) {
    case Agency::Ons: return QStringLiteral("ONS");
    case Agency::StatCan: return QStringLiteral("Statistics Canada");
    case Agency::Abs: return QStringLiteral("ABS");
    case Agency::Census: return QStringLiteral("US Census Bureau");
    case Agency::Umich: return QStringLiteral("University of Michigan");
    case Agency::Bls: return QStringLiteral("BLS");
    case Agency::SnbCube: return QStringLiteral("SNB data portal");
    case Agency::EstatDashboard: return QStringLiteral("Statistics Bureau of Japan");
  }
  return {};
}

// {"months":[{"date":"2026 OCT","value":"3.6",…}],"quarters":[{"date":"2026 Q3","value":"0.1"}]}: the months, else the quarters
Points parseOns(const QByteArray& body) {
  Points out;
  const auto o = QJsonDocument::fromJson(body).object();
  const auto read = [&out](const QJsonArray& a, bool quarters) {
    for (const auto& v : a) {
      const auto m = v.toObject();
      const QStringList d = m.value("date").toString().split(' ', Qt::SkipEmptyParts);
      bool ok = false;
      const double x = number(m.value("value").toString(), &ok);
      if (d.size() != 2 || !ok) continue;
      const int month = quarters ? (d[1].startsWith('Q') ? (d[1].mid(1).toInt() - 1) * 3 + 1 : 0) : monthOf(d[1]);
      const QDate day(d[0].toInt(), month, 1);
      if (day.isValid()) out << qMakePair(day, x);
    }
  };
  read(o.value("months").toArray(), false);
  if (out.isEmpty()) read(o.value("quarters").toArray(), true);
  sortUnique(out);
  return out;
}

// [{"status":"SUCCESS","object":{"vectorDataPoint":[{"refPer":"2026-10-01","value":21100.1,…}]}}]
Points parseStatCan(const QByteArray& body) {
  Points out;
  for (const auto& a : QJsonDocument::fromJson(body).array()) {
    const auto ao = a.toObject();
    if (ao.value("status").toString() != "SUCCESS") continue;
    for (const auto& p : ao.value("object").toObject().value("vectorDataPoint").toArray()) {
      const auto o = p.toObject();
      const QDate d = QDate::fromString(o.value("refPer").toString().left(10), Qt::ISODate);
      if (d.isValid() && o.value("value").isDouble()) out << qMakePair(d, o.value("value").toDouble());
    }
  }
  sortUnique(out);
  return out;
}

// SDMX csv of ONE series, columns found by name
Points parseAbs(const QByteArray& body) {
  Points out;
  const QList<QByteArray> lines = body.split('\n');
  if (lines.isEmpty()) return out;
  const QList<QByteArray> head = lines[0].trimmed().split(',');
  const int ti = int(head.indexOf("TIME_PERIOD")), vi = int(head.indexOf("OBS_VALUE"));
  if (ti < 0 || vi < 0) return out;
  QByteArray firstKey;
  for (int i = 1; i < lines.size(); ++i) {
    const QList<QByteArray> f = lines[i].trimmed().split(',');
    if (f.size() <= std::max(ti, vi)) continue;
    QByteArray key;
    for (int k = 0; k < ti; ++k) key += f[k] + ',';
    if (firstKey.isEmpty()) firstKey = key;
    else if (key != firstKey) return {};   // several series: no figure rather than a mix
    bool ok = false;
    const double v = f[vi].toDouble(&ok);
    const QDate d = periodStart(QString::fromLatin1(f[ti]));
    if (ok && d.isValid()) out << qMakePair(d, v);
  }
  sortUnique(out);
  return out;
}

// "2026   729915   740502 …": the first table of the file (adjusted sales, millions of dollars); seasonal factors follow
Points parseCensus(const QByteArray& body) {
  Points out;
  static const QRegularExpression ws("\\s+");
  int lastYear = 0;
  for (const QByteArray& raw : body.split('\n')) {
    const QStringList f = QString::fromLatin1(raw).trimmed().split(ws, Qt::SkipEmptyParts);
    if (f.isEmpty()) continue;
    bool okYear = false;
    const int year = f[0].toInt(&okYear);
    if (!okYear || year < 1990 || year > 2100) { if (lastYear && !f[0].at(0).isDigit()) break; continue; }
    if (lastYear && std::abs(year - lastYear) != 1) break;   // a second table starts
    lastYear = year;
    for (int m = 1; m < f.size() && m <= 12; ++m) {
      bool ok = false;
      const double v = number(f[m], &ok);
      if (ok && v > 1000) out << qMakePair(QDate(year, m, 1), v);   // a factor is about 1
    }
  }
  sortUnique(out);
  return out;
}

// "November,2026,51.5" or "November,2026,3.7,3.3"
Points parseUmich(const QByteArray& body, int column) {
  Points out;
  for (const QByteArray& raw : body.split('\n')) {
    const QStringList f = QString::fromLatin1(raw).trimmed().split(',');
    if (f.size() < 3 + column) continue;
    const int m = monthOf(f[0]);
    bool okY = false, ok = false;
    const int y = f[1].trimmed().toInt(&okY);
    const double v = number(f[2 + column], &ok);
    if (!okY || !ok || m <= 0) continue;
    const QDate d(y, m, 1);
    if (d.isValid()) out << qMakePair(d, v);
  }
  sortUnique(out);
  return out;
}

Points parseBls(const QByteArray& body) {
  Points out;
  const auto series = QJsonDocument::fromJson(body).object().value("Results").toObject().value("series").toArray();
  for (const auto& s : series)
    for (const auto& v : s.toObject().value("data").toArray()) {
      const auto o = v.toObject();
      const QString per = o.value("period").toString();
      if (!per.startsWith('M') || per == "M13") continue;
      bool ok = false;
      const double x = number(o.value("value").toString(), &ok);
      if (ok) out << qMakePair(QDate(o.value("year").toString().toInt(), per.mid(1).toInt(), 1), x);
    }
  sortUnique(out);
  return out;
}

// "2026-10";"D0 value";"D1 value";"2.9" lines of a cube; `dims` = the values the columns after the date must have
Points parseSnb(const QByteArray& body, const QStringList& dims) {
  Points out;
  for (const QByteArray& raw : body.split('\n')) {
    QString line = QString::fromUtf8(raw).trimmed();
    line.remove('"');
    const QStringList f = line.split(';');
    if (f.size() < 2 + dims.size()) continue;
    bool match = true;
    for (int i = 0; i < dims.size(); ++i) if (f[1 + i] != dims[i]) match = false;
    if (!match) continue;
    bool ok = false;
    const double v = number(f[1 + dims.size()], &ok);
    const QDate d = periodStart(f[0]);
    if (ok && d.isValid()) out << qMakePair(d, v);
  }
  sortUnique(out);
  return out;
}

// {"GET_STATS":{"STATISTICAL_DATA":{"DATA_INF":{"DATA_OBJ":[{"VALUE":{"@indicator":"07030106…","@time":"20261000","$":"2.3"}}]}}}}
// codes[0] is the current series; the ones after it only give the dates before it starts
Points parseEstatDashboard(const QByteArray& body, const QStringList& codes) {
  const auto obj = QJsonDocument::fromJson(body).object().value("GET_STATS").toObject().value("STATISTICAL_DATA").toObject()
                       .value("DATA_INF").toObject().value("DATA_OBJ");
  const QJsonArray rows = obj.isArray() ? obj.toArray() : QJsonArray{obj};
  QHash<QString, Points> by;
  for (const auto& r : rows) {
    const auto v = r.toObject().value("VALUE").toObject();
    const QString t = v.value("@time").toString();   // yyyyMM00
    bool ok = false;
    const double x = number(v.value("$").toString(), &ok);
    if (!ok || t.size() < 6) continue;
    const QDate d(t.left(4).toInt(), t.mid(4, 2).toInt(), 1);
    if (d.isValid()) by[v.value("@indicator").toString()] << qMakePair(d, x);
  }
  Points out;
  for (const QString& c : codes) {
    Points p = by.value(c);
    sortUnique(p);
    if (out.isEmpty()) { out = p; continue; }
    Points head;
    for (const auto& q : p) if (q.first < out.first().first) head << q;
    out = head + out;
  }
  return out;
}

Points pctChange(const Points& level, int lag) {
  Points out;
  if (level.size() < 2 || lag < 1) return out;
  // the spacing of the series: the smallest one among its last points (a hole is not a quarter)
  int step = 12;
  QHash<QDate, double> by;
  for (int i = 0; i < level.size(); ++i) {
    by.insert(level[i].first, level[i].second);
    if (i == 0 || i + 8 < level.size()) continue;
    const int months = (level[i].first.year() - level[i - 1].first.year()) * 12 + level[i].first.month() - level[i - 1].first.month();
    if (months > 0) step = std::min(step, months);
  }
  // by date, not by rank: one month that was never published (US prices, October 2025) must not void the year after it
  for (const auto& [d, v] : level) {
    const auto it = by.constFind(d.addMonths(-step * lag));
    if (it == by.cend() || *it == 0) continue;
    out << qMakePair(d, (v / *it - 1) * 100);
  }
  return out;
}

Points diff(const Points& level) {
  Points out;
  for (int i = 1; i < level.size(); ++i) {
    const QDate a = level[i - 1].first, b = level[i].first;
    if (a.addMonths(1) == b || a.addMonths(3) == b) out << qMakePair(b, level[i].second - level[i - 1].second);
  }
  return out;
}

void fetch(const Id& id, int ttlSec, QObject* ctx, std::function<void(const Answer&)> cb) {
  net::Request r;
  r.ttlSec = ttlSec;
  r.timeoutMs = 60000;
  std::function<Points(const QByteArray&)> parse;
  const int year = QDate::currentDate().year();
  switch (id.agency) {
    case Agency::Ons:
      r.url = QUrl("https://www.ons.gov.uk/" + id.key + "/data");
      parse = parseOns;
      break;
    case Agency::StatCan:
      r.url = QUrl(QString("https://www150.statcan.gc.ca/t1/wds/rest/getDataFromVectorByReferencePeriodRange?vectorIds=%22")
                   + id.key + QString("%22&startRefPeriod=%1-01-01&endReferencePeriod=%2-12-31").arg(year - 10).arg(year));
      parse = parseStatCan;
      break;
    case Agency::Abs:
      r.url = QUrl("https://data.api.abs.gov.au/rest/data/" + id.key + QString("?startPeriod=%1&format=csv").arg(year - 10));
      r.headers = {{"Accept", "application/vnd.sdmx.data+csv"}};
      parse = parseAbs;
      break;
    case Agency::Census:
      r.url = QUrl("https://www.census.gov/retail/marts/www/" + id.key);
      parse = parseCensus;
      break;
    case Agency::Umich: {
      const int column = id.key.section('#', 1).toInt();
      r.url = QUrl("http://www.sca.isr.umich.edu/files/" + id.key.section('#', 0, 0));
      parse = [column](const QByteArray& b) { return parseUmich(b, column); };
      break;
    }
    case Agency::Bls:
      r.url = QUrl(QString("https://api.bls.gov/publicAPI/v1/timeseries/data/%1?startyear=%2&endyear=%3").arg(id.key).arg(year - 9).arg(year));
      parse = parseBls;
      break;
    case Agency::SnbCube: {
      QStringList dims = id.key.split(';');
      const QString cube = dims.takeFirst();
      r.url = QUrl("https://data.snb.ch/api/cube/" + cube + QString("/data/csv/en?fromDate=%1-01").arg(year - 10));
      parse = [dims](const QByteArray& b) { return parseSnb(b, dims); };
      break;
    }
    case Agency::EstatDashboard: {
      const QStringList codes = id.key.split(';');
      r.url = QUrl("https://dashboard.e-stat.go.jp/api/1.0/Json/getData?Lang=EN&IndicatorCode=" + codes.join(',') +
                   QString("&RegionalRank=2&Cycle=1&TimeFrom=%1").arg(year - 10) + "0101");
      parse = [codes](const QByteArray& b) { return parseEstatDashboard(b, codes); };
      break;
    }
  }
  net::get(r, ctx, [cb, parse](const net::Reply& rep) {
    Answer a;
    a.fetched = rep.fetched;
    if (!rep.body.isEmpty()) a.pts = parse(rep.body);
    if (a.pts.isEmpty()) a.error = rep.error.isEmpty() ? QStringLiteral("unexpected answer") : rep.error;
    cb(a);
  });
}
}
