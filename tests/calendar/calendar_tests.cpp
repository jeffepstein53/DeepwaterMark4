// Tests of the calendar's figures: see CMakeLists.txt. Titles and bodies are excerpts of real ones (November 2026).
#include "core/I18n.h"
#include "fundamental/Fundamental.h"
#include "core/Paths.h"
#include "core/Secret.h"
#include "macro/CentralBanks.h"
#include "macro/CrossCheck.h"
#include "macro/Official.h"
#include "macro/National.h"
#include "macro/RateTables.h"
#include "net/Http.h"
#include "net/Sources.h"
#include "news/Calendar.h"
#include "news/DataHeadline.h"
#include "news/History.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QTimer>
#include <cstdio>

// ---------------------------------------------------------------------------------------------- stubs
static QTemporaryDir* g_dir = nullptr;
static QHash<QString, QByteArray> g_bodies;   // url fragment → body
static QDateTime g_fetched;                   // when the stub says every body was downloaded
static int g_requests = 0;
namespace paths {
QString data(const QString& sub) { const QString d = g_dir->path() + "/" + sub; QDir().mkpath(d); return d; }
}
namespace i18n { QLocale locale() { return QLocale(QLocale::English); } }
namespace sources { void declare(const Source&) {} }
namespace net {
void get(const Request& r, QObject* ctx, Callback cb) {
  ++g_requests;
  Reply rep;
  rep.error = "stub: no body"; rep.status = 0;
  for (auto it = g_bodies.cbegin(); it != g_bodies.cend(); ++it)
    if (r.url.toString(QUrl::FullyEncoded).contains(it.key())) { rep.body = it.value(); rep.status = 200; rep.error.clear(); rep.fetched = g_fetched; }
  QTimer::singleShot(0, ctx, [cb, rep] { cb(rep); });
}
}

static int g_ok = 0, g_bad = 0;
static void expect(const char* what, bool ok, const QString& got = {}) {
  if (ok) ++g_ok; else ++g_bad;
  std::printf("%s  %s%s\n", ok ? "ok " : "BAD", what, ok || got.isEmpty() ? "" : qPrintable("   got: " + got));
}
static void spin(int ms) {
  QElapsedTimer t; t.start();
  while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}
static QDateTime utc(const char* s) { auto t = QDateTime::fromString(s, Qt::ISODate); t.setTimeZone(QTimeZone::UTC); return t; }

// ---------------------------------------------------------------------------------------------- statistics offices
static void offices() {
  using namespace official;
  auto p = parseOns(R"({"months":[{"date":"2026 SEP","value":"3.5"},{"date":"2026 OCT","value":"3.6"}],"quarters":[{"date":"2026 Q3","value":"9"}]})");
  expect("ONS: months before quarters", p.size() == 2 && p.last() == qMakePair(QDate(2026, 10, 1), 3.6));
  p = parseOns(R"({"months":[],"quarters":[{"date":"2026 Q2","value":"0.5"},{"date":"2026 Q3","value":"0.7"}]})");
  expect("ONS: quarters when there are no months", p.size() == 2 && p.last() == qMakePair(QDate(2026, 7, 1), 0.7));
  p = parseStatCan(R"([{"status":"SUCCESS","object":{"vectorId":2062812,"vectorDataPoint":[{"refPer":"2026-09-01","value":17300.2},{"refPer":"2026-10-01","value":17323.0}]}}])");
  expect("Statistics Canada", p.size() == 2 && p.last() == qMakePair(QDate(2026, 10, 1), 17323.0));
  expect("Statistics Canada: a failed vector gives nothing", parseStatCan(R"([{"status":"FAILED","object":"not found"}])").isEmpty());
  p = parseAbs("DATAFLOW,MEASURE,INDEX,TSEST,REGION,FREQ,TIME_PERIOD,OBS_VALUE,UNIT_MEASURE\r\nABS:CPI(2.0.0),3,10001,10,50,M,2026-09,4.3,PCT\r\nABS:CPI(2.0.0),3,10001,10,50,M,2026-10,4.9,PCT\r\n");
  expect("ABS", p.size() == 2 && p.last() == qMakePair(QDate(2026, 10, 1), 4.9));
  p = parseAbs("DATAFLOW,MEASURE,TIME_PERIOD,OBS_VALUE\nX,1,2026-Q2,0.4\nX,2,2026-Q2,0.5\n");
  expect("ABS: several series give nothing", p.isEmpty());
  p = parseAbs("DATAFLOW,MEASURE,TIME_PERIOD,OBS_VALUE\nX,1,2026-Q2,0.4\nX,1,2026-Q3,0.6\n");
  expect("ABS: quarters", p.size() == 2 && p.last().first == QDate(2026, 7, 1));
  p = parseCensus("RETAIL & FOOD SERVICES\n\n\nYEAR      JAN       FEB\n\n2025   159177    159189    158647    159921    160471    161205    162855    162412    164361    166063    166124    167691\n"
                  "2026   701860    707959    719700    723921    731415    732898    729915    740502    746637    748220   \n\n\nSEASONAL FACTORS\n\nYEAR      JAN\n\n2025     0.911     0.880\n2026     0.910     0.879\n");
  expect("Census: the first table only", p.size() == 22 && p.last() == qMakePair(QDate(2026, 10, 1), 748220.0), QString::number(p.size()));
  p = parseUmich("Month,YYYY,ICS_ALL\nOctober,2026,50.2\nNovember,2026,51.5\n", 0);
  expect("Michigan", p.size() == 2 && p.last() == qMakePair(QDate(2026, 11, 1), 51.5));
  p = parseBls(R"({"Results":{"series":[{"data":[{"year":"2026","period":"M10","value":"333.1"},{"year":"2026","period":"M09","value":"332.0"},{"year":"2025","period":"M13","value":"1"}]}]}})");
  expect("BLS: sorted, annual averages left out", p.size() == 2 && p.first().first == QDate(2026, 9, 1));
  p = parseSnb("\"CubeId\";\"x\"\n\"Date\";\"D0\";\"Value\"\n\"2026-09\";\"ALT\";\"3.1\"\n\"2026-10\";\"ALT\";\"3.2\"\n\"2026-10\";\"ALQ\";\"9\"\n", {"ALT"});
  expect("SNB portal: one series of the cube", p.size() == 2 && p.last() == qMakePair(QDate(2026, 10, 1), 3.2));
  Points lv; for (int i = 0; i < 14; ++i) lv << qMakePair(QDate(2025, 9, 1).addMonths(i), 100.0 + i);
  expect("y/y over twelve months", pctChange(lv, 12).size() == 2 && std::abs(pctChange(lv, 12).last().second - (113.0 / 101 - 1) * 100) < 1e-9);
  lv.remove(5);
  expect("a hole gives no figure over the wrong span", pctChange(lv, 1).size() == 11);
  expect("differences", diff({{QDate(2026, 9, 1), 14782.2}, {QDate(2026, 10, 1), 14738.6}}).size() == 1);
}

// ---------------------------------------------------------------------------------------------- headlines and feed
static QJsonObject ev(const char* time, const char* ccy, const char* title, int impact, const char* f, const char* p) {
  const auto v = [](const char* t) { return *t ? QJsonObject{{"t", t}} : QJsonObject{}; };
  return {{"time", time}, {"allDay", false}, {"ccy", ccy}, {"title", title}, {"impact", impact}, {"actual", QJsonObject{}},
          {"forecast", v(f)}, {"previous", v(p)}, {"low", QJsonObject{}}, {"high", QJsonObject{}}, {"fsrc", "ForexFactory"}};
}
static void writeWeek(const char* day, const QJsonArray& a) {
  QFile f(paths::data("news/calendar") + "/week-" + day + ".json");
  if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(a).toJson());
}
static const news::Event* find(const QList<news::Event>& l, const char* ccy, const char* title) {
  for (const auto& e : l) if (e.currency == ccy && e.title == title) return &e;
  return nullptr;
}
static void headline(const char* text, const char* at) {
  if (const auto h = news::parseDataHeadline(text)) news::Calendar::instance().applyHeadline(*h, utc(at), "FinancialJuice", text);
}
static QString act(const QDate& w, const char* ccy, const char* title) {
  const auto l = news::Calendar::instance().events(w);
  const auto* e = find(l, ccy, title);
  return e ? e->actual.text : QString("no event");
}

static void headlines() {
  expect("headline parser samples", news::selfTestDataHeadlines() == 0);
  const QDate w(2026, 11, 22);
  writeWeek("2026-11-22", {
    ev("2026-11-22T22:00:00Z", "AUD", "Flash Services PMI", 1, "", "47.8"),
    ev("2026-11-23T08:15:00Z", "EUR", "French Flash Manufacturing PMI", 2, "49.8", "49.3"),
    ev("2026-11-23T08:15:00Z", "EUR", "French Flash Services PMI", 2, "47.4", "48.4"),
    ev("2026-11-23T08:30:00Z", "EUR", "German Flash Services PMI", 2, "52.4", "51.7"),
    ev("2026-11-23T09:00:00Z", "EUR", "Flash Services PMI", 1, "51.8", "51.7"),
    ev("2026-11-23T14:45:00Z", "USD", "Flash Manufacturing PMI", 2, "54.5", "56.0"),
    ev("2026-11-23T14:45:00Z", "USD", "Flash Services PMI", 2, "55.6", "57.9"),
    ev("2026-11-24T00:30:00Z", "JPY", "Flash Manufacturing PMI", 1, "53.5", "53.3"),
    ev("2026-11-24T11:00:00Z", "GBP", "CBI Realized Sales", 1, "-27", "-10"),
    ev("2026-11-24T23:50:00Z", "JPY", "SPPI y/y", 1, "3.6%", "3.7%"),
    ev("2026-11-25T13:30:00Z", "USD", "Core PCE Price Index m/m", 3, "0.3%", "0.3%"),
    ev("2026-11-25T13:30:00Z", "USD", "Prelim GDP Price Index q/q", 2, "2.3%", "2.3%"),
    ev("2026-11-25T13:30:00Z", "USD", "Prelim GDP q/q", 3, "3.5%", "3.4%"),
    ev("2026-11-25T13:30:00Z", "USD", "Unemployment Claims", 1, "201K", "203K"),
    ev("2026-11-25T14:00:00Z", "USD", "Richmond Manufacturing Index", 2, "-5", "-7"),
    ev("2026-11-25T14:00:00Z", "USD", "HPI m/m", 1, "0.1%", "0.2%"),
    ev("2026-11-25T15:00:00Z", "USD", "New Home Sales", 1, "650K", "676K")});
  auto& c = news::Calendar::instance();
  headline("French Comp Flash PMI Actual 46.2 (Forecast 47.9, Previous 46.6)", "2026-11-23T08:15:00Z");
  headline("French Services PMI Flash Actual 46.6 (Forecast 47.5, Previous 46.2)", "2026-11-23T08:15:00Z");
  headline("French Manufacturing PMI Flash Actual 47.9 (Forecast 49.8, Previous 49.5)", "2026-11-23T08:15:00Z");
  expect("French services PMI (other survey of economists)", act(w, "EUR", "French Flash Services PMI") == "46.6", act(w, "EUR", "French Flash Services PMI"));
  expect("French manufacturing PMI", act(w, "EUR", "French Flash Manufacturing PMI") == "47.9");
  headline("German Service PMI Flash Actual 54 (Forecast 52.3, Previous 52.5)", "2026-11-23T08:30:00Z");
  expect("German services PMI (\"Service\")", act(w, "EUR", "German Flash Services PMI") == "54.0", act(w, "EUR", "German Flash Services PMI"));
  expect("a German figure does not fill the euro area's", act(w, "EUR", "Flash Services PMI").isEmpty());
  headline("Eurozone Services PMI Flash Actual 51.2 (Forecast 51.8, Previous 51.4)", "2026-11-23T09:00:00Z");
  expect("euro area services PMI", act(w, "EUR", "Flash Services PMI") == "51.2");
  headline("US S&P Services PMI Flash Actual 54.7 (Forecast 55.5, Previous 56.1)", "2026-11-23T14:45:00Z");
  headline("US S&P Manufacturing PMI Flash Actual 54.5 (Forecast 54.5, Previous 54.9)", "2026-11-23T14:45:00Z");
  headline("US S&P Composite PMI Flash Actual 54.6 (Forecast 55.6, Previous 55.9)", "2026-11-23T14:45:00Z");
  expect("US services PMI", act(w, "USD", "Flash Services PMI") == "54.7", act(w, "USD", "Flash Services PMI"));
  expect("US manufacturing PMI", act(w, "USD", "Flash Manufacturing PMI") == "54.5");
  const auto l0 = c.events(w);
  const auto* us = find(l0, "USD", "Flash Services PMI");
  expect("the previous as revised (56.1, first 57.9)", us && us->revised.text == "56.1", us ? us->revised.text : QString());
  expect("the composite is a detail", us && find(l0, "USD", "Flash Manufacturing PMI")->details.size() + us->details.size() == 1);
  headline("Australian Services PMI Flash Actual 46.2 (Forecast -, Previous 48.5)", "2026-11-22T22:00:00Z");
  expect("Australian services PMI without a forecast", act(w, "AUD", "Flash Services PMI") == "46.2");
  headline("Japanese Mfg PMI Flash Actual 53.7 (Forecast -, Previous 53.3)", "2026-11-24T00:30:00Z");
  expect("Japanese manufacturing PMI (\"Mfg\")", act(w, "JPY", "Flash Manufacturing PMI") == "53.7");
  headline("UK CBI Distributive Trades Actual 9 (Forecast -28, Previous -10)", "2026-11-24T11:00:00Z");
  expect("CBI distributive trades = realized sales", act(w, "GBP", "CBI Realized Sales") == "9", act(w, "GBP", "CBI Realized Sales"));
  headline("Japanese Service PPI Actual 4.2% (Forecast 3.6%, Previous 3.7%)", "2026-11-24T23:50:00Z");
  expect("Japanese services PPI", act(w, "JPY", "SPPI y/y") == "4.2%", act(w, "JPY", "SPPI y/y"));
  // 25 November, 13:30: many figures at once
  headline("US PCE Price Index YoY Actual 3.8% (Forecast 3.8%, Previous 3.6% ,Revision 3.7%)", "2026-11-25T13:30:01Z");
  headline("US Core PCE Price Index MoM Actual 0.3% (Forecast 0.3%, Previous 0.3%)", "2026-11-25T13:30:01Z");
  headline("US GDP QoQ 2nd Estimate Actual 4.3% (Forecast 3.5%, Previous 3.4%)", "2026-11-25T13:30:02Z");
  headline("US GDP Price Index Actual 2.3% (Forecast 2.3%, Previous 2.3%)", "2026-11-25T13:30:02Z");
  headline("US Core PCE Price Index YoY Actual 3.1% (Forecast 3.2%, Previous 3.1%)", "2026-11-25T13:30:02Z");
  headline("US Core PCE Price Index YoY Actual 3.3% (Forecast 3.2%, Previous 3.1%)", "2026-11-25T13:30:40Z");
  headline("US Initial Jobless Claims Actual 189k (Forecast 200k, Previous 203k ,Revision 202k)", "2026-11-25T13:30:02Z");
  headline("US Continued Jobless Claims Actual 1.697M (Forecast 1.74M, Previous 1.763M)", "2026-11-25T13:30:02Z");
  expect("core PCE m/m", act(w, "USD", "Core PCE Price Index m/m") == "0.3%");
  expect("GDP second estimate", act(w, "USD", "Prelim GDP q/q") == "4.3%", act(w, "USD", "Prelim GDP q/q"));
  expect("GDP price index", act(w, "USD", "Prelim GDP Price Index q/q") == "2.3%");
  const auto l = c.events(w);
  const auto* pce = find(l, "USD", "Core PCE Price Index m/m");
  QString y; for (const auto& d : pce->details) if (d.name.contains("Core PCE Price Index YoY")) y = d.actual;
  expect("core PCE y/y is a detail, as corrected by the wire", y == "3.3%", y);
  const auto* cl = find(l, "USD", "Unemployment Claims");
  expect("jobless claims, and the previous as revised", cl->actual.text == "189K" && cl->revised.text == "202K", cl->actual.text + " / " + cl->revised.text);
  expect("the arrow compares with the revised previous", cl->direction() == -1);
  expect("the details read as one line", news::detailsText(*pce).contains("3.3%") && news::detailsText(*pce).contains("forecast 3.2%"));
  headline("US New Home Sales - Units Actual 0.701M (Forecast 0.65M, Previous 0.676M)", "2026-11-25T15:00:01Z");
  expect("0.701M is 701K", act(w, "USD", "New Home Sales") == "701K", act(w, "USD", "New Home Sales"));
  // the actuals feed a minute later
  g_fetched = utc("2026-11-25T13:33:00Z");
  const auto item = [](const char* title, const char* date, const char* p, const char* cons, const char* a) {
    return QString("<item><title>%1</title><pubDate>%2</pubDate><description>&lt;table&gt;&lt;td&gt;x&lt;/td&gt;&lt;td&gt;y&lt;/td&gt;&lt;td&gt;%3&lt;/td&gt;"
                   "&lt;td&gt;%4&lt;/td&gt;&lt;td&gt;%5&lt;/td&gt;&lt;/table&gt;</description></item>").arg(title, date, p, cons, a);
  };
  g_bodies["rss/forex-economic-calendar-events"] = ("<rss>" +
    item("United States GDP Price Index QoQ", "Wed, 25 Nov 2026 13:30 GMT", "6.1%", "2.3%", "1.8%") +
    item("United States Initial Jobless Claims", "Wed, 25 Nov 2026 13:30 GMT", "202.0K", "200.0K", "189.0K") +
    item("United States New Home Sales", "Wed, 25 Nov 2026 15:00 GMT", "676.0M", "0.65M", "701.0M") +
    item("Germany Services PMI", "Mon, 23 Nov 2026 08:30 GMT", "52.5", "52.3", "54.0") + "</rss>").toUtf8();
  c.refreshActuals();
  spin(60);
  const auto l2 = c.events(w);
  const auto* gp = find(l2, "USD", "Prelim GDP Price Index q/q");
  expect("the feed's own figure is taken and the disagreement is shown", gp->actual.text == "1.8%" && gp->other == "FinancialJuice 2.3%", gp->actual.text + " / " + gp->other);
  expect("its sentence", news::otherText(*gp).contains("2.3%") && news::otherText(*gp).contains("Myfxbook"));
  expect("the same figure in other words changes nothing", act(w, "USD", "Unemployment Claims") == "189K" && find(l2, "USD", "Unemployment Claims")->other.isEmpty());
  expect("two sources with the same figure: the second is kept", find(l2, "USD", "Unemployment Claims")->also == QStringList{"Myfxbook"} &&
        news::alsoText(*find(l2, "USD", "Unemployment Claims")).contains("Myfxbook"), find(l2, "USD", "Unemployment Claims")->also.join(","));
  expect("a source that disagrees is not a confirmation", !gp->also.contains("Myfxbook") && !gp->also.contains("FinancialJuice"), gp->also.join(","));
  const auto late = c.missingResults(900, utc("2026-11-25T16:00:00Z"));
  expect("a result still missing after a quarter of an hour", late.size() == 1 && late[0].title == "Richmond Manufacturing Index", late.isEmpty() ? QString("none") : late[0].title);
  expect("not in the first minutes, nor the day after", c.missingResults(900, utc("2026-11-25T14:10:00Z")).isEmpty() && c.missingResults(900, utc("2026-11-26T15:00:00Z")).isEmpty());
  expect("a unit error of the feed is not taken (701.0M)", act(w, "USD", "New Home Sales") == "701K", act(w, "USD", "New Home Sales"));
  expect("German PMI unchanged", act(w, "EUR", "German Flash Services PMI") == "54.0" && act(w, "EUR", "Flash Services PMI") == "51.2");
  spin(250);
  // everything is written with the week
  QFile f(paths::data("news/calendar") + "/week-2026-11-22.json");
  expect("recorded on disk", f.open(QIODevice::ReadOnly) && f.readAll().contains("FinancialJuice 2.3%"));
}

static void units() {
  using namespace news;
  Event e; e.previous = parseValue("676K"); e.forecast = parseValue("650K");
  Value a = parseValue("701.0M"), p = parseValue("676.0M");
  expect("wrong unit, same digits: the calendar's unit", adaptToEvent(a, p, e) && a.text == "701K", a.text);
  e.previous = parseValue("24.5B"); e.forecast = parseValue("18.0B");
  a = parseValue("-17.0B"); p = parseValue("-24.5B");
  expect("opposite sign convention", adaptToEvent(a, p, e) && a.text == "17.0B", a.text);
  e.previous = parseValue("203K"); e.forecast = parseValue("201K");
  a = parseValue("189.0K"); p = parseValue("202.0K");
  expect("189.0K reads 189K", adaptToEvent(a, p, e) && a.text == "189K", a.text);
  a = parseValue("189.4K");
  expect("189.4K keeps its decimal", adaptToEvent(a, p, e) && a.text == "189.4K", a.text);
  a = parseValue("189.0M"); p = parseValue("150.0M");
  expect("thousands against millions: refused", !adaptToEvent(a, p, e));
  e.previous = parseValue("-24"); e.forecast = parseValue("-22");
  a = parseValue("-22.0"); p = parseValue("-24.0");
  expect("-22.0 reads -22", adaptToEvent(a, p, e) && a.text == "-22", a.text);
}

// ---------------------------------------------------------------------------------------------- official actuals
static news::OfficialActual ask(const char* time, const char* ccy, const char* title, const char* f, const char* p) {
  news::Event e;
  e.time = utc(time); e.currency = ccy; e.title = title; e.forecast = news::parseValue(f); e.previous = news::parseValue(p);
  news::OfficialActual out;
  news::History::instance().officialActual(e, qApp, [&out](const news::OfficialActual& o) { out = o; });
  spin(60);
  return out;
}
static void official_() {
  g_fetched = QDateTime::currentDateTimeUtc();
  const auto ons = [](const char* pts) {
    QJsonArray a;
    for (const QString& x : QString(pts).split(',')) a << QJsonObject{{"date", x.section('=', 0, 0)}, {"value", x.section('=', 1)}};
    return QJsonDocument(QJsonObject{{"months", a}, {"quarters", QJsonArray()}}).toJson();
  };
  g_bodies["timeseries/d7g7/mm23"] = ons("2026 AUG=3.4,2026 SEP=3.5,2026 OCT=3.6");
  auto o = ask("2026-11-18T07:00:00Z", "GBP", "CPI y/y", "3.6%", "3.5%");
  expect("UK CPI y/y (ONS)", o.value.text == "3.6%" && o.note.startsWith("ONS official series"), o.value.text);
  g_bodies["timeseries/kac3/lms"] = ons("2026 JUL=3.9,2026 AUG=3.8,2026 SEP=3.6");
  o = ask("2026-11-17T07:00:00Z", "GBP", "Average Earnings Index 3m/y", "3.5%", "3.7%");
  expect("UK earnings, previous revised 3.7 → 3.8", o.value.text == "3.6%" && o.revised.text == "3.8%", o.value.text + " / " + o.revised.text);
  g_bodies["timeseries/mgsx/lms"] = ons("2026 JUN=4.9,2026 JUL=4.7,2026 AUG=4.6");
  o = ask("2026-11-17T07:00:00Z", "GBP", "Unemployment Rate", "4.7%", "4.7%");
  expect("UK unemployment rate", o.value.text == "4.6%", o.value.text);
  g_bodies["observations/CPI_MEDIAN"] = R"({"observations":[{"d":"2026-08-01","CPI_MEDIAN":{"v":"1.9"}},{"d":"2026-09-01","CPI_MEDIAN":{"v":"2.0"}},{"d":"2026-10-01","CPI_MEDIAN":{"v":"2.1"}}]})";
  o = ask("2026-11-16T13:30:00Z", "CAD", "Median CPI y/y", "2.0%", "1.9%");
  expect("Canada median CPI, previous revised 1.9 → 2.0", o.value.text == "2.1%" && o.revised.text == "2.0%", o.value.text + " / " + o.revised.text);
  g_bodies["adv44X72.txt"] = "X\n\nYEAR JAN\n\n2026   701860    707959    719700    723921    731415    732898    729915    740502    746637    748220   \n";
  o = ask("2026-11-17T13:30:00Z", "USD", "Retail Sales m/m", "0.1%", "0.8%");
  expect("US retail sales (Census)", o.value.text == "0.2%", o.value.text);
  g_bodies["tbmics.csv"] = "Month,YYYY,ICS_ALL\nOctober,2026,50.2\nNovember,2026,51.5\n";
  o = ask("2026-11-20T15:00:00Z", "USD", "Revised UoM Consumer Sentiment", "51.7", "51.0");
  expect("Michigan sentiment, revised", o.value.text == "51.5", o.value.text);
  g_bodies["tbmpx1px5.csv"] = "Month,YYYY,PX_MD,PX5_MD\nOctober,2026,4.7,3.5\nNovember,2026,3.7,3.3\n";
  o = ask("2026-11-20T15:00:00Z", "USD", "Revised UoM Inflation Expectations", "", "4.3%");
  expect("Michigan inflation expectations, revised", o.value.text == "3.7%", o.value.text);
  g_bodies["WPI/2.THRPEB.7.TOT.20.AUS.Q"] = "D,MEASURE,TIME_PERIOD,OBS_VALUE\nX,2,2026-Q1,0.8\nX,2,2026-Q2,0.8\nX,2,2026-Q3,1.1\n";
  o = ask("2026-11-18T00:30:00Z", "AUD", "Wage Price Index q/q", "1.0%", "0.8%");
  expect("Australian wage price index", o.value.text == "1.1%", o.value.text);
  g_bodies["LF/M3.3.1599.20.AUS.M"] = "D,MEASURE,TIME_PERIOD,OBS_VALUE\nX,M3,2026-08,14821.5\nX,M3,2026-09,14782.17\nX,M3,2026-10,14738.64\n";
  o = ask("2026-11-19T00:30:00Z", "AUD", "Employment Change", "25.5K", "-39.3K");
  expect("Australian employment change", o.value.text == "-43.5K", o.value.text);
  o = ask("2026-11-19T00:30:00Z", "AUD", "Employment Change", "25.5K", "12.0K");
  expect("a previous that is not the series': no figure", !o.value.has());
  // a body downloaded before the release is no use
  g_fetched = utc("2026-11-18T06:00:00Z");
  o = ask("2026-11-18T07:00:00Z", "GBP", "CPI y/y", "3.6%", "3.5%");
  expect("a copy from before the release is refused", !o.value.has(), o.value.text);
  g_fetched = QDateTime::currentDateTimeUtc();
  // full-time / part-time
  g_bodies["LF/M1.3.1599.20.AUS.M"] = "D,MEASURE,TIME_PERIOD,OBS_VALUE\nX,M1,2026-08,10100\nX,M1,2026-09,10090\nX,M1,2026-10,10050.5\n";
  g_bodies["LF/M2.3.1599.20.AUS.M"] = "D,MEASURE,TIME_PERIOD,OBS_VALUE\nX,M2,2026-08,4700\nX,M2,2026-09,4692\nX,M2,2026-10,4688\n";
  news::Event e; e.time = utc("2026-11-19T00:30:00Z"); e.currency = "AUD"; e.title = "Employment Change";
  QList<news::Detail> d;
  news::History::instance().officialDetails(e, qApp, [&d](const QList<news::Detail>& x) { d = x; });
  spin(60);
  expect("full-time and part-time jobs", d.size() == 2 && d[0].actual == "-39.5K" && d[1].actual == "-4.0K", d.size() == 2 ? d[0].actual + " " + d[1].actual : QString("none"));
}

// ---------------------------------------------------------------------------------------------- macro indicators
static void national_() {
  using namespace macro;
  using namespace macro::national;
  // US prices, October 2025 never published: the twelve-month change is still there for the months after it
  official::Points lv;
  for (int i = 0; i < 27; ++i) if (i != 13) lv << qMakePair(QDate(2024, 9, 1).addMonths(i), 300.0 + i);
  lv[lv.size() - 1].second = 337.511;                       // Nov 2026
  official::Answer a; a.pts = lv; a.fetched = QDateTime::currentDateTimeUtc();
  const Def us{"us", Ind::Inflation, official::Agency::Bls, "CUUR0000SA0", true, "CPI-U"};
  const Series y = build(us, a);
  bool oct = false, sep = false;
  for (const auto& q : y.pts) { if (q.date == QDate(2026, 10, 1)) oct = true; if (q.date == QDate(2026, 9, 1)) sep = true; }
  expect("US y/y: September 2026 computed, October 2026 absent (no October 2025)", sep && !oct && y.freq == 'M' && y.source == "BLS");
  // Japan's dashboard: two bases
  const QByteArray jp = R"({"GET_STATS":{"STATISTICAL_DATA":{"DATA_INF":{"DATA_OBJ":[
    {"VALUE":{"@indicator":"A","@time":"20260900","$":"2.1"}},{"VALUE":{"@indicator":"A","@time":"20261000","$":"2.2"}},
    {"VALUE":{"@indicator":"B","@time":"20260800","$":"1.8"}},{"VALUE":{"@indicator":"B","@time":"20260900","$":"2.2"}}]}}}})";
  const auto jpPts = official::parseEstatDashboard(jp, {"A", "B"});
  expect("Japan: current base first, older base before it", jpPts.size() == 3 && jpPts[0].second == 1.8 && jpPts[1].second == 2.1 && jpPts.last().first == QDate(2026, 10, 1));
  // SNB cubes as answered on 2026-11-25
  const auto sw = official::parseSnb("\xEF\xBB\xBF\"CubeId\";\"amarbma\"\n\"Date\";\"D0\";\"Value\"\n\"2026-09\";\"S1\";\"3.12215173\"\n\"2026-10\";\"T1\";\"2.98939387\"\n\"2026-10\";\"S1\";\"3.09398862\"\n", {"S1"});
  expect("Swiss unemployment, seasonally adjusted", sw.size() == 2 && std::abs(sw.last().second - 3.094) < 1e-3);
  // overlay
  Series bis; bis.ind = Ind::Inflation; bis.iso2 = "ca"; bis.source = "BIS"; bis.dataset = "WS_LONG_CPI";
  for (int i = 0; i < 10; ++i) bis.pts << Point{QDate(2025, 12, 1).addMonths(i), 2.0 + i / 10.0};      // … Sep 2026
  Series nat; nat.ind = Ind::Inflation; nat.iso2 = "ca"; nat.source = "Statistics Canada"; nat.dataset = "CPI";
  nat.pts = {Point{QDate(2026, 8, 1), 3.0}, Point{QDate(2026, 9, 1), 3.1}, Point{QDate(2026, 10, 1), 3.3}};
  Series o = overlay(bis, nat, false);
  expect("national series with the older history before it", o.last().value == 3.3 && o.pts.size() == 11 && o.source == "Statistics Canada" && o.dataset.contains("history: BIS"));
  o = overlay(o, nat, false);
  expect("laid again: the same", o.pts.size() == 11 && o.dataset.contains("history: BIS"));
  expect("a compilation that ends later stays", overlay(nat, [&] { Series b = bis; b.source = "X"; return b; }(), false).source == "Statistics Canada");
  expect("another definition is not spliced", overlay(bis, nat, true).pts.size() == 3);
  // the calendar's figure
  CalendarFigure f = flashOf("USD", "Prelim GDP q/q", 4.3, utc("2026-11-25T13:30:00Z"), "FinancialJuice");
  expect("US GDP: third quarter, annualised figure brought to the quarter", f.valid() && f.period == QDate(2026, 7, 1) && std::abs(f.value - 1.058) < 1e-3);
  expect("UK unemployment named by the middle month", flashOf("GBP", "Unemployment Rate", 4.6, utc("2026-11-17T07:00:00Z"), "ONS").period == QDate(2026, 8, 1));
  expect("euro-area flash of the same month", flashOf("EUR", "CPI Flash Estimate y/y", 4.0, utc("2026-10-30T10:00:00Z"), "x").period == QDate(2026, 10, 1));
  expect("euro-area flash in the first days of the next", flashOf("EUR", "CPI Flash Estimate y/y", 4.0, utc("2026-12-02T10:00:00Z"), "x").period == QDate(2026, 11, 1));
  expect("New Zealand unemployment by quarter", flashOf("NZD", "Unemployment Rate", 5.7, utc("2026-11-03T21:45:00Z"), "x").period == QDate(2026, 7, 1));
  expect("Japanese release late in the UTC day", flashOf("JPY", "National Core CPI y/y", 2.2, utc("2026-11-30T23:30:00Z"), "x").period == QDate(2026, 11, 1));
  expect("a German figure is not the euro area's", !flashOf("EUR", "German Prelim GDP q/q", 0.3, utc("2026-11-25T07:00:00Z"), "x").valid());
  expect("other events are not indicators", !flashOf("USD", "Prelim GDP Price Index q/q", 1.8, utc("2026-11-25T13:30:00Z"), "x").valid());
  f = flashOfDetail("NZD", "New Zealand Retail Sales YoY", 3.5, utc("2026-11-22T21:45:00Z"), "FinancialJuice");
  expect("New Zealand retail sales y/y from the release's details", f.valid() && f.period == QDate(2026, 7, 1) && f.freq == 'Q');
  Series ret; ret.ind = Ind::RetailSalesYoy; ret.iso2 = "nz"; ret.freq = 'Q'; ret.source = "OECD"; ret.pts = {Point{QDate(2026, 1, 1), 2.0}, Point{QDate(2026, 4, 1), 3.2}};
  const Series rw = withFlash(ret, f, utc("2026-11-25T18:00:00Z"));
  expect("added after the last quarter, its source said", rw.pts.size() == 3 && rw.last().value == 3.5 && rw.source.contains("FinancialJuice"));
  Series gdp; gdp.ind = Ind::GdpQoq; gdp.iso2 = "us"; gdp.freq = 'Q'; gdp.source = "OECD"; gdp.pts = {Point{QDate(2026, 4, 1), 0.5}, Point{QDate(2026, 7, 1), 0.8}};
  const CalendarFigure g = flashOf("USD", "Prelim GDP q/q", 4.3, utc("2026-11-25T13:30:00Z"), "FinancialJuice");
  expect("a newer estimate of the same quarter replaces the advance one", std::abs(withFlash(gdp, g, utc("2026-11-26T00:00:00Z")).last().value - 1.058) < 1e-3);
  expect("for twenty-five days only", withFlash(gdp, g, utc("2026-12-30T00:00:00Z")).last().value == 0.8);
  Series old; old.ind = Ind::CoreInflation; old.freq = 'M'; old.source = "OECD"; old.pts = {Point{QDate(2021, 6, 1), -1.0}};
  const Series fr = withFlash(old, flashOf("JPY", "National Core CPI y/y", 2.2, utc("2026-11-19T23:30:00Z"), "Myfxbook"), utc("2026-11-25T00:00:00Z"));
  expect("a series that stopped years ago is not continued across the gap", fr.pts.size() == 1 && fr.last().value == 2.2);
  CalendarFigure none;
  expect("nothing to add: unchanged", withFlash(gdp, none, utc("2026-11-26T00:00:00Z")).last().value == 0.8);
}

// ---------------------------------------------------------------------------------------------- tables of rate odds
static QList<macro::ocr::Line> picture(const QString& title, const QString& effective, const QList<QStringList>& rows) {
  QList<macro::ocr::Line> l{{title, QRectF(0.48, 0.04, 0.05, 0.06)}, {"Effective Rate: " + effective + "% (" + effective + "%)", QRectF(0.40, 0.14, 0.2, 0.06)},
                            {"Meeting", QRectF(0.005, 0.24, 0.063, 0.06)}, {"Implied Rate", QRectF(0.641, 0.24, 0.097, 0.07)},
                            {"Implied Move (bps) Total (bps)", QRectF(0.753, 0.24, 0.242, 0.07)}};
  const double x[] = {0.004, 0.118, 0.270, 0.392, 0.661, 0.813, 0.94}, w[] = {0.052, 0.081, 0.046, 0.041, 0.055, 0.03, 0.033};
  for (int r = 0; r < rows.size(); ++r)
    for (int c = 0; c < rows[r].size() && c < 7; ++c)
      if (!rows[r][c].isEmpty()) l << macro::ocr::Line{rows[r][c], QRectF(x[c], 0.34 + 0.098 * r + (c == 3 ? 0.004 : 0), w[c], 0.048)};
  return l;
}
static void tables_() {
  using namespace macro::ratetables;
  // RBA, 2026-11-23: an implied rate not read, percent signs read as "96"
  const QList<QStringList> rba{{"Dec-26", "No Change", "100%", "", "4.843%", "-0.7", "-0.7"}, {"Feb-27", "No Change", "93.4%", "6.696", "4.826%", "-1.7", "-2.4"},
                               {"Mar-27", "No Change", "81.6%", "18.496", "4.780%", "-4.6", "-7.0"}, {"May-27", "No Change", "74.7%", "25.396", "4.7170/0", "-6.3", "-13.3"},
                               {"Jun-27", "No Change", "81.3%", "18.796", "4.670%", "-4.7", "-18.0"}, {"Aug-27", "No Change", "72.4%", "27.696", "", "-6.9", "-24.9"},
                               {"Sep-27", "No Change", "79.6%", "20.496", "4.550%", "-5.1", "-30.0"}};
  Table t = readPicture(picture("RBA", "4.85", rba), "RBA", 4.85);
  expect("RBA picture: seven meetings", t.ok() && t.rows.size() == 7 && std::abs(t.effective - 4.85) < 1e-9 && t.rows[0].month == QDate(2026, 12, 1) &&
        std::abs(t.rows[6].total + 30.0) < 1e-9);
  expect("a missing implied rate comes from the total", t.rows.size() == 7 && std::abs(t.rows[5].implied - 4.601) < 1e-6);
  expect("a misread percent sign", t.rows.size() == 7 && std::abs(t.rows[3].implied - 4.717) < 1e-6);
  expect("another bank's picture is refused", readPicture(picture("RBNZ", "2.75", rba), "RBA", 4.85).fault == Fault::OtherBank);
  expect("another rate is refused", !readPicture(picture("RBA", "4.35", rba), "RBA", NAN).ok());
  expect("a picture that is no table", readPicture({{"Sister", QRectF(0.2, 0.3, 0.35, 0.2)}}, "RBA", 4.85).fault == Fault::NotATable);
  QList<QStringList> bad = rba;
  bad[2][5] = "46"; bad[2][6] = "-70";                 // the decimal points lost on one row
  t = readPicture(picture("RBA", "4.85", bad), "RBA", NAN);
  expect("a row that does not hold together stops the table", t.rows.size() == 2);
  bad = rba; bad[2][6] = "-1.0";                       // one wrong reading, the two others agree
  t = readPicture(picture("RBA", "4.85", bad), "RBA", NAN);
  expect("two readings of three are enough", t.rows.size() == 7 && std::abs(t.rows[2].total + 7.0) < 0.31);
  bad = rba; bad[3][0] = "Mar-27";
  expect("months that go back stop the table", readPicture(picture("RBA", "4.85", bad), "RBA", NAN).rows.size() == 3);
  // Trading Economics
  const QByteArray te = R"~(<a href="/new-zealand/implied-rates">New Zealand</a><table><tr><th><span title="Target Rate (%)">Target Rate</span></th><th>Last</th></tr>
    <tr class="x"><td class="a">4.60</td><td>2.2%</td><td>4.3%</td></tr><tr><td>4.85</td><td>97.8%</td><td>95.7%</td></tr><tr><td>5.10</td><td>0.0%</td><td>0.0%</td></tr></table>
    <table><tr><td><a href="/united-states/implied-rates"><span>USA</span></a></td><td>3.75–4.00%</td><td>4.12%</td><td>+24.3bp</td><td>9 Dec 26</td></tr></table>)~";
  const Page pg = readTradingEconomics(te);
  expect("Trading Economics: the RBA's outcomes", pg.next.size() == 3 && std::abs(expectedBp(pg.next, 4.85) + 0.55) < 1e-9);
  expect("outcomes about another rate", !std::isfinite(expectedBp(pg.next, 4.35)));
  expect("Trading Economics: the other banks", pg.banks.size() == 1 && pg.banks["FED"].current == 4.0 && pg.banks["FED"].bp == 24.3 && pg.banks["FED"].meeting == QDate(2026, 12, 9));
  // investingLive
  const QByteArray il = R"(<p>Rate hikes by year-end</p><p><strong>Rate hikes by year-end</strong></p><ul><li><strong>BoE: </strong>20 bps (79% probability of <em>rate hike</em> at the next meeting)</li></ul>
    <p><u><strong>2027: 82 bps</strong></u></p><ul><li><strong>RBNZ:</strong> 21 bps (83% probability of <em>rate hike</em> at the next meeting)</li></ul><p>2027: 78 bps</p>
    <ul><li><strong>SNB:</strong> 4 bps (82% probability of no change at the next meeting)</li></ul><p>2027: 51 bps</p><ul><li>RBA: 0 bps (100% probability of no change at the next meeting)</li></ul><p>2027: 47 bps of <em>rate cuts</em></p>)";
  const auto w = readWeekly(il);
  expect("weekly article: four banks", w.size() == 4 && w["RBNZ"].what == "hike" && std::abs(w["RBNZ"].nextBp - 20.75) < 1e-9);
  expect("weekly article: no change leaves the rest to the section's side", std::abs(w["SNB"].nextBp - 4.5) < 1e-9 && w["RBA"].nextBp == 0 && w["RBA"].nextYearBp == -47);
  // decisions of the calendar
  expect("decisions: the banks the BIS alone gives", decisionOf("AUD", "Cash Rate") == "RBA" && decisionOf("NZD", "Official Cash Rate") == "RBNZ" &&
        decisionOf("JPY", "BOJ Policy Rate") == "BOJ" && decisionOf("CHF", "SNB Policy Rate") == "SNB" && decisionOf("NZD", "RBNZ Interest Rate Decision") == "RBNZ");
  expect("other events are not decisions", decisionOf("AUD", "RBA Rate Statement").isEmpty() && decisionOf("USD", "Federal Funds Rate").isEmpty());
}

// ---------------------------------------------------------------------------------------------- real rates and carry
namespace fundamental { Source* macroSource(QObject*) { return nullptr; } }   // stub: the pages' source needs the whole app
static void carry_() {
  using namespace fundamental;
  Inputs a, b;
  a.ccy = "AUD"; a.policyRate = 4.85; a.cpi.value = 3.8; a.cpi.period = QDate(2026, 10, 1); a.coreCpi.value = 3.5; a.coreCpi.period = a.cpi.period;
  b.ccy = "CHF"; b.policyRate = 0.0; b.cpi.value = 1.5; b.cpi.period = QDate(2026, 10, 1); b.coreCpi.value = 1.2; b.coreCpi.period = b.cpi.period;
  expect("real rate: policy less CPI", std::abs(realRate(a) - 1.05) < 1e-9 && std::abs(realRate(b) + 1.5) < 1e-9 && std::abs(realRate(a, true) - 1.35) < 1e-9);
  Comparison c = compare(a, b);
  expect("comparison: seven rows counted, two real rates after them", c.rows.size() == kComparedRows + kRealRows && c.counted == 3 && c.leadsA == 3);
  expect("real rates: values and the better side", std::abs(c.rows[7].a - 1.05) < 1e-9 && c.rows[7].better == 1 && std::abs(c.rows[8].b + 1.2) < 1e-9 && c.rows[8].better == 1);
  expect("carry: the side that yields more", c.carry == "Carry: AUD yields 2.55 pt more than CHF after inflation", c.carry);
  expect("carry: said the same way when the sides are swapped", compare(b, a).carry == c.carry);
  expect("the note shows the sum", c.rows[7].note.contains("AUD: policy rate 4.85%") && c.rows[7].note.contains("3.8% (Oct 2026)") && c.rows[7].note.contains("1.05%"), c.rows[7].note);
  a.cpi.history = {{QDate(2026, 4, 1), 4.0}, {QDate(2026, 7, 1), 3.8}}; a.cpi.period = QDate(2026, 7, 1);
  expect("a quarterly CPI is named by its quarter", compare(a, b).rows[7].note.contains("Q3 2026"), compare(a, b).rows[7].note);
  b.cpi.value = NAN; b.cpi.note = "no series";
  c = compare(a, b);
  expect("a missing CPI gives no real rate and no carry", !std::isfinite(c.rows[7].b) && c.rows[7].better == 0 && c.carry.isEmpty() && c.rows[7].note.contains("no series"));
  b = a; b.ccy = "NZD";
  expect("equal real rates", compare(a, b).carry.contains("the same"));
  expect("a fall too small to show is not -0.0", num(-0.04, 1) == "0.0" && num(-0.06, 1) == "-0.1" && num(0.0, 2) == "0.00");
}

// ---------------------------------------------------------------------------------------------- a figure and its other sources
static void checks_() {
  using namespace macro::check;
  const QDate oct(2026, 10, 1), sep(2026, 9, 1);
  Item it; it.ccy = "AUD"; it.figure = "CPI y/y"; it.unit = "%"; it.value = 3.8; it.period = oct;
  const auto reading = [](const char* src, double v, double ours, QDate p) { Reading r; r.source = src; r.value = v; r.ours = ours; r.period = p; return r; };
  judge(it);
  expect("check: no other source", it.verdict == Verdict::Alone && it.on == oct && !it.earlier);
  it.others = {reading("BIS", 3.8, 3.8, oct), reading("calendar", 3.8, 3.8, oct)};
  judge(it);
  expect("check: two sources agree", it.verdict == Verdict::Agree && it.against.isEmpty());
  it.others[0].value = 3.91;
  judge(it);
  expect("check: a difference of rounding is none", it.verdict == Verdict::Agree);
  it.others[0].value = 4.0;
  judge(it);
  expect("check: another figure differs, and is named", it.verdict == Verdict::Differ && it.against == "BIS 4.0%", it.against);
  it.others[0].judged = false;
  judge(it);
  expect("check: a reading that is not judged raises nothing", it.verdict == Verdict::Agree && it.against.isEmpty());
  {
    Item od; od.ccy = "GBP"; od.figure = "Odds"; od.unit = "bp"; od.tolerance = 4; od.freq = 'D'; od.value = 21.1; od.period = QDate(2026, 12, 17); od.each = true;
    od.others << Reading{"FinancialJuice", 22.1, 21.1, od.period} << Reading{"Trading Economics", 27.7, 21.1, od.period};
    judge(od);
    expect("check: odds, one publisher that agrees settles it and the other is named apart", od.verdict == Verdict::Agree && od.against == "Trading Economics 27.7 bp", od.against);
    od.others.removeFirst();
    judge(od);
    expect("check: odds, the only other publisher differs", od.verdict == Verdict::Differ);
    od.ccy = "CAD"; od.value = 20.6; od.others = {Reading{"Trading Economics", 25.7, 20.6, od.period}};
    od.others[0].gap = 276.0 - (256.0 + 20.6);   // counted from 2.50% there, from 2.56% here
    judge(od);
    expect("check: odds, the same rate counted from another base", od.verdict == Verdict::Agree && od.against.isEmpty());
    od.others[0].gap = 5.0;
    judge(od);
    expect("check: odds, another rate on the same base", od.verdict == Verdict::Differ);
  }
  it.others = {reading("BIS", 3.5, 3.5, sep)};
  judge(it);
  expect("check: an earlier period confirms when the latest has no second source", it.verdict == Verdict::Agree && it.earlier && it.on == sep);
  it.others = {reading("BIS", 9.0, 3.5, sep), reading("calendar", 3.8, 3.8, oct)};
  judge(it);
  expect("check: the period shown comes first", it.verdict == Verdict::Agree && !it.earlier);
  it.others = {reading("BIS", 3.5, NAN, sep)};
  judge(it);
  expect("check: nothing of ours for that period, nothing to compare", it.verdict == Verdict::Alone);
  it.due = QDate(2026, 11, 1);
  judge(it);
  expect("check: a newer figure is out", it.verdict == Verdict::Late);
  it.due = {}; it.value = NAN;
  judge(it);
  expect("check: no figure", it.verdict == Verdict::Missing);
  it.value = 0.5; it.tolerance = 0.15; it.others = {reading("calendar", 0.64, 0.5, oct)};
  judge(it);
  expect("check: the item's own tolerance", it.verdict == Verdict::Agree);
  it.others[0].tol = 0.06;
  judge(it);
  expect("check: a reading's own tolerance", it.verdict == Verdict::Differ);
  Item d; d.figure = "Meeting"; d.value = double(QDate(2026, 12, 17).toJulianDay());
  expect("check: a date is shown as one", figureText(d, d.value) == "17 Dec 2026", figureText(d, d.value));
  Item b; b.figure = "Odds"; b.unit = "bp"; b.decimals = 1;
  expect("check: basis points, no -0.0", figureText(b, -0.02) == "0.0 bp" && figureText(b, NAN) == QString::fromUtf8("—"), figureText(b, -0.02));
  const Summary s = summary({it, d});
  expect("check: the summary counts", s.figures == 2 && s.differ == 1 && s.missing == 1);
}

// ---------------------------------------------------------------------------------------------- sources that stop answering
static void silent_() {
  using sources::Health; using sources::isSilent;
  const QDateTime now = utc("2026-11-25T18:00:00");
  Health h; h.okCount = 40; h.failCount = 3; h.failStreak = 3; h.lastOk = now.addSecs(-3600); h.lastFail = now.addSecs(-60);
  expect("silent: it answered an hour ago and fails since", isSilent({h}, now));
  Health x = h; x.failStreak = 1;
  expect("silent: one failure is an incident", !isSilent({x}, now));
  x = h; x.lastOk = now.addSecs(-300);
  expect("silent: five minutes is too early to say", !isSilent({x}, now));
  x = h; x.lastOk = QDateTime();
  expect("silent: a source that never answered is not one that stopped", !isSilent({x}, now));
  x = h; x.lastOk = now.addDays(-20);
  expect("silent: nor one that stopped three weeks ago", !isSilent({x}, now));
  x = h; x.lastFail = now.addSecs(-4 * 3600);
  expect("silent: not asked for hours, nothing known", !isSilent({x}, now));
  Health ok; ok.okCount = 5; ok.lastOk = now.addSecs(-120);
  expect("silent: another of its hosts answers", !isSilent({h, ok}, now));
  Health old; old.okCount = 5; old.lastOk = now.addDays(-2);
  expect("silent: a host not asked lately says nothing", isSilent({h, old}, now));
}

// ---------------------------------------------------------------------------------------------- one contract, two meetings
static void shared_() {
  using namespace macro;
  // a three-month contract over two meetings: 25 bp in all, priced 20 + 5 by the published table
  const QVector<QDate> eff{QDate(2026, 12, 23), QDate(2027, 2, 10)};
  RateContract c; c.start = QDate(2026, 12, 16); c.end = QDate(2027, 3, 17);
  const double T = c.start.daysTo(c.end), w0 = c.start.daysTo(eff[0]), w1 = eff[0].daysTo(eff[1]), w2 = eff[1].daysTo(c.end);
  c.avg = (w0 * 2.0 + w1 * 2.20 + w2 * 2.25) / T;
  const QVector<double> alone = solveMeetingRates(2.0, eff, {c});
  expect("shared contract: without a table, equal steps", alone.size() == 2 && std::abs((alone[0] - 2.0) - (alone[1] - alone[0])) < 1e-9);
  const QVector<double> split = solveMeetingRates(2.0, eff, {c}, {0.20, 0.05});
  expect("shared contract: split as the table splits it", split.size() == 2 && std::abs(split[0] - 2.20) < 1e-9 && std::abs(split[1] - 2.25) < 1e-9, QString::number(split.value(0)));
  const QVector<double> moved = solveMeetingRates(2.0, eff, {c}, {0.15, 0.0});
  expect("shared contract: the market's move since the table is shared equally", std::abs((moved[0] - 2.15) - (moved[1] - moved[0])) < 1e-9);
  const double avg = (w0 * 2.0 + w1 * moved[0] + w2 * moved[1]) / T;
  expect("shared contract: the contract's price is always met", std::abs(avg - c.avg) < 1e-9);
  // one meeting per contract: the table changes nothing
  RateContract one = c; one.end = QDate(2027, 2, 1);
  one.avg = (one.start.daysTo(eff[0]) * 2.0 + eff[0].daysTo(one.end) * 2.18) / one.start.daysTo(one.end);
  const QVector<double> a = solveMeetingRates(2.0, {eff[0]}, {one}), b = solveMeetingRates(2.0, {eff[0]}, {one}, {0.05});
  expect("one meeting in a contract: the price alone decides", std::abs(a[0] - 2.18) < 1e-9 && std::abs(b[0] - 2.18) < 1e-9, QString::number(b[0]));
}

// ---------------------------------------------------------------------------------------------- encrypted settings
static void secrets_() {
  const QString plain = QString::fromUtf8("session-of-a-test é€");
  const QString s = secret::seal(plain);
  expect("secret: read back as it was", secret::open(s) == plain);
  expect("secret: nothing for nothing", secret::seal(QString()).isEmpty() && secret::open(QString()).isEmpty());
  expect("secret: an older value is read as it is", secret::open(plain) == plain && !secret::isSealed(plain));
#ifdef Q_OS_WIN
  expect("secret: not written in clear", secret::isSealed(s) && !s.contains("session"));
  QString broken = s; broken[broken.size() / 2] = broken[broken.size() / 2] == 'A' ? 'B' : 'A';
  expect("secret: a value that was altered gives nothing", secret::open(broken).isEmpty());
#endif
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir dir; g_dir = &dir;
  offices();
  units();
  headlines();
  official_();
  national_();
  tables_();
  carry_();
  checks_();
  silent_();
  shared_();
  secrets_();
  std::printf("%d ok, %d bad, %d stubbed requests\n", g_ok, g_bad, g_requests);
  std::fflush(stdout);
  return g_bad;
}
