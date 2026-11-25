#pragma once
#include <QDate>
#include <QDateTime>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

// Official statistics read at the agency that publishes them, with no key and no account. Used where the international
// compilations (OECD, BIS) come weeks later: the calendar's actuals (news/History) and the macro indicators
// (macro/MacroStore). One figure per period, as published; nothing is estimated.
//
//   official::fetch({official::Agency::Ons, "economy/inflationandpriceindices/timeseries/d7g7/mm23"}, 3600, this,
//                   [](const official::Answer& a) { if (a.ok()) use(a.pts.last()); });
namespace official {

using Points = QVector<QPair<QDate, double>>;   // first day of the period (month or quarter), ascending

enum class Agency {
  Ons,      // UK Office for National Statistics; key = "<topic>/timeseries/<cdid>/<data set>"
  StatCan,  // Statistics Canada web data service; key = the vector's number
  Abs,      // Australian Bureau of Statistics data API; key = "<dataflow>/<SDMX key>"
  Census,   // US Census Bureau, advance retail sales text files; key = file name
  Umich,    // University of Michigan surveys of consumers; key = "<file>#<column after month and year>"
  Bls,      // US Bureau of Labor Statistics, public API v1; key = series id
  SnbCube,  // Swiss National Bank data portal; key = "<cube>;<first dimension>[;<second dimension>]"
  EstatDashboard   // Statistics Dashboard of Japan; key = "<indicator>[;<older indicator, for the dates before>]"
};

struct Id { Agency agency; QString key; };

struct Answer {
  Points pts;
  QDateTime fetched;   // when the body was downloaded (UTC)
  QString error;
  bool ok() const { return error.isEmpty() && !pts.isEmpty(); }
};

QString agencyName(Agency a);   // "ONS", "Statistics Canada", "ABS", …

// Async, on the GUI thread; skipped when ctx is gone. A copy younger than ttlSec is served without a request.
void fetch(const Id& id, int ttlSec, QObject* ctx, std::function<void(const Answer&)> cb);

// The parsers, exposed for the tests.
Points parseOns(const QByteArray& body);
Points parseStatCan(const QByteArray& body);
Points parseAbs(const QByteArray& body);
Points parseCensus(const QByteArray& body);
Points parseUmich(const QByteArray& body, int column);
Points parseBls(const QByteArray& body);
Points parseSnb(const QByteArray& body, const QStringList& dims);
Points parseEstatDashboard(const QByteArray& body, const QStringList& codes);

// Percent change over `lag` points of a series (1: on the previous period; 12: over a year of months; 4: of quarters).
// A hole gives no figure rather than one over the wrong span.
Points pctChange(const Points& level, int lag);
// Difference with the previous period.
Points diff(const Points& level);
}
