#include "news/Calendar.h"
#include "news/ActualsPace.h"
#include <QCoreApplication>
#include "core/I18n.h"
#include "news/DataHeadline.h"
#include "news/History.h"
#include "net/Http.h"
#include "net/Sources.h"
#include "core/Paths.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QFile>
#include <QDir>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QLocale>
#include <QTimeZone>
#include <algorithm>
#include <utility>

namespace news {

static const sources::Registrar regFF({"forexfactory", "Forex Factory (Fair Economy)", {"nfs.faireconomy.media"},
    sources::Access::Keyless, 300000, "Weekly economic calendar: time, currency, impact, forecast, previous",
    "Public weekly calendar export offered by Forex Factory for third-party use; asks for low request rates "
    "(one download per 5 minutes at most; cached 15 min)", {"news/calendar", "news/squawk", "global/session-recap", "global/overview"}});
static const sources::Registrar regMfb({"myfxbook-calendar", "Myfxbook (economic calendar RSS)", {"www.myfxbook.com"},
    sources::Access::Keyless, 2000, "Economic calendar RSS: actual values of recent releases",   // pace shared with the Retail login → outlook flow; the 5-min poll is the calendar's timer
    "Public RSS feed published for readers; polled every 5 minutes at most. The calendar pages themselves are "
    "behind a Cloudflare challenge and are not used", {"news/calendar", "news/squawk", "global/session-recap", "global/overview"}});

// ------------------------------------------------------------------------------------------------ values
QString impactName(Impact i) {
  switch (i) { case Impact::High: return QCoreApplication::translate("news", "High"); case Impact::Medium: return QCoreApplication::translate("news", "Medium");
               case Impact::Holiday: return QCoreApplication::translate("news", "Holiday"); default: return QCoreApplication::translate("news", "Low"); }
}

static double mult(const QString& s) {
  if (s == "K") return 1e3; if (s == "M") return 1e6; if (s == "B") return 1e9; if (s == "T") return 1e12;
  return 1;
}
double Value::scaled() const { return v * mult(suffix); }

Value parseValue(const QString& in) {
  Value out; out.text = in.trimmed();
  if (out.text == "&nbsp;" || out.text == "-" || out.text == "—") out.text.clear();
  if (out.text.isEmpty()) return out;
  static const QRegularExpression re(R"(^([<>]?)\s*([+-]?)\s*([0-9][0-9,]*\.?[0-9]*|\.[0-9]+)\s*(%|[KMBT])?)",
                                     QRegularExpression::CaseInsensitiveOption);
  const auto m = re.match(out.text);
  if (!m.hasMatch()) return out;
  QString n = m.captured(3); n.remove(',');
  bool ok = false; double v = n.toDouble(&ok);
  if (!ok) return out;
  if (m.captured(2) == "-") v = -v;
  out.v = v; out.suffix = m.captured(4).toUpper();
  const int dot = n.indexOf('.');
  out.decimals = dot < 0 ? 0 : int(n.size() - dot - 1);
  return out;
}

QString formatLike(double v, const Value& like, bool sign) {
  if (!std::isfinite(v)) return {};
  static const QLocale en(QLocale::English, QLocale::UnitedStates);
  QString s = en.toString(std::abs(v), 'f', like.decimals);
  bool zero = true; for (QChar c : s) if (c.isDigit() && c != '0') zero = false;
  if (!zero && v < 0) s.prepend('-'); else if (!zero && sign) s.prepend('+');
  return s + like.suffix;
}

// "54" next to a forecast of 52.4 reads 54.0
static Value sameDecimals(const Value& v, const Value& like) {
  if (!v.num() || !like.num() || v.suffix != like.suffix || v.decimals >= like.decimals) return v;
  return parseValue(formatLike(v.v, like));
}

double Event::surprise() const {
  if (!actual.num() || !forecast.num()) return NAN;
  if (actual.suffix == forecast.suffix) return actual.v - forecast.v;
  if (actual.suffix == "%" || forecast.suffix == "%") return NAN;
  return (actual.scaled() - forecast.scaled()) / mult(actual.suffix);
}
QString Event::surpriseText() const {
  const double s = surprise();
  if (!std::isfinite(s)) return {};
  Value like = actual; like.decimals = std::max(actual.decimals, forecast.decimals);
  return formatLike(s, like, true);
}
int Event::direction() const {
  if (!actual.num() || !previous.num()) return 0;
  const double a = actual.scaled(), p = (revised.num() ? revised : previous).scaled();
  return a > p ? 1 : a < p ? -1 : 0;
}
bool Event::isSpeech() const {
  return title.contains("Speaks", Qt::CaseInsensitive) || title.contains("Speech", Qt::CaseInsensitive) ||
         title.contains("Testifies", Qt::CaseInsensitive) || title.contains("Press Conference", Qt::CaseInsensitive);
}

static void splitPeriod(Event& e) {
  static const QRegularExpression re(R"(\s+(y/y|m/m|q/q|3m/3m|q/q annualized)$)", QRegularExpression::CaseInsensitiveOption);
  e.name = e.title; e.period.clear(); e.periodRaw.clear();
  const auto m = re.match(e.title);
  if (!m.hasMatch()) return;
  e.periodRaw = m.captured(1).toLower();
  e.name = e.title.left(m.capturedStart()).trimmed();
  if (e.periodRaw == "y/y") e.period = "ANNUAL";
  else if (e.periodRaw == "m/m") e.period = "MONTHLY";
  else if (e.periodRaw.startsWith("q/q")) e.period = "QUARTERLY";
  else e.period = "3-MONTH";
}

// ------------------------------------------------------------------------------------------------ json
static QJsonObject valueJson(const Value& v) { return v.has() ? QJsonObject{{"t", v.text}} : QJsonObject{}; }
static Value valueFrom(const QJsonValue& j) { return parseValue(j.toObject().value("t").toString()); }

static QJsonObject toJson(const Event& e) {
  QJsonObject o{{"time", e.time.toString(Qt::ISODate)}, {"allDay", e.allDay}, {"ccy", e.currency},
                {"title", e.title}, {"impact", int(e.impact)}, {"actual", valueJson(e.actual)},
                {"forecast", valueJson(e.forecast)}, {"previous", valueJson(e.previous)},
                {"low", valueJson(e.low)}, {"high", valueJson(e.high)}, {"fsrc", e.forecastSource}};
  if (!e.actualSource.isEmpty()) { o["asrc"] = e.actualSource; o["aat"] = e.actualAt.toString(Qt::ISODate); }
  if (!e.actualDetail.isEmpty()) o["adet"] = e.actualDetail;
  if (e.actualPub.isValid()) o["apub"] = e.actualPub.toString(Qt::ISODate);
  if (e.quality >= 0) o["aq"] = e.quality;
  if (!e.other.isEmpty()) o["oth"] = e.other;
  if (!e.also.isEmpty()) o["also"] = QJsonArray::fromStringList(e.also);
  if (e.revised.has()) { o["rev"] = valueJson(e.revised); o["rsrc"] = e.revisedSource; }
  QJsonArray det;
  for (const Detail& d : e.details)
    det.append(QJsonObject{{"n", d.name}, {"a", d.actual}, {"f", d.forecast}, {"p", d.previous}, {"r", d.revised},
                           {"s", d.source}, {"t", d.at.toString(Qt::ISODate)}});
  if (!det.isEmpty()) o["det"] = det;
  QJsonArray banks;
  for (const auto& b : e.banks)
    banks.append(QJsonObject{{"bank", b.bank}, {"v", valueJson(b.value)}, {"src", b.source}, {"url", b.url},
                             {"date", b.published.toString(Qt::ISODate)}});
  if (!banks.isEmpty()) o["banks"] = banks;
  return o;
}
static void finish(Event& e) {
  splitPeriod(e);
  e.seriesKey = e.currency + "|" + e.title;
  e.key = e.seriesKey + "|" + e.time.toString(Qt::ISODate);
}
static Event fromJson(const QJsonObject& o) {
  Event e;
  e.time = QDateTime::fromString(o["time"].toString(), Qt::ISODate).toUTC();
  e.allDay = o["allDay"].toBool(); e.currency = o["ccy"].toString(); e.title = o["title"].toString();
  e.impact = Impact(o["impact"].toInt(1));
  e.actual = valueFrom(o["actual"]); e.forecast = valueFrom(o["forecast"]); e.previous = valueFrom(o["previous"]);
  e.low = valueFrom(o["low"]); e.high = valueFrom(o["high"]);
  e.forecastSource = o["fsrc"].toString("ForexFactory");
  e.actualSource = o["asrc"].toString();
  e.actualDetail = o["adet"].toString();
  e.actualAt = QDateTime::fromString(o["aat"].toString(), Qt::ISODate);
  e.actualPub = QDateTime::fromString(o["apub"].toString(), Qt::ISODate);
  e.quality = o["aq"].toInt(-1);
  e.other = o["oth"].toString();
  for (const auto& v : o["also"].toArray()) e.also << v.toString();
  e.revised = valueFrom(o["rev"]); e.revisedSource = o["rsrc"].toString();
  e.actual = sameDecimals(e.actual, e.forecast.num() ? e.forecast : e.previous);
  e.revised = sameDecimals(e.revised, e.previous);
  for (const auto& v : o["det"].toArray()) {
    const auto d = v.toObject();
    e.details << Detail{d["n"].toString(), d["a"].toString(), d["f"].toString(), d["p"].toString(), d["r"].toString(),
                        d["s"].toString(), QDateTime::fromString(d["t"].toString(), Qt::ISODate)};
  }
  for (const auto& b : o["banks"].toArray()) {
    const auto bo = b.toObject();
    e.banks << BankForecast{bo["bank"].toString(), valueFrom(bo["v"]), bo["src"].toString(), bo["url"].toString(),
                            QDate::fromString(bo["date"].toString(), Qt::ISODate)};
  }
  finish(e);
  return e;
}

static QString weekFile(const QDate& w) { return paths::data("news/calendar") + "/week-" + w.toString("yyyy-MM-dd") + ".json"; }

// ------------------------------------------------------------------------------------------------ service
Calendar& Calendar::instance() { static Calendar* c = new Calendar; return *c; }

Calendar::Calendar() {
  m_poll.setSingleShot(true);
  m_poll.setTimerType(Qt::PreciseTimer);   // a coarse timer may be 5 % off: 15 s on 5 min, and it may fire early
  connect(&m_poll, &QTimer::timeout, this, [this] { fetchMyfxbook(); planPoll(); });
  m_touch.setSingleShot(true); m_touch.setInterval(150);
  connect(&m_touch, &QTimer::timeout, this, [this] {
    const QSet<QDate> weeks = std::exchange(m_touched, {});
    for (const QDate& w : weeks) { saveWeek(w); emit weekLoaded(w); }
  });
  connect(this, &Calendar::weekLoaded, this, [this](const QDate& w) { if (w == currentWeek()) planPoll(); });   // release times known or moved
  // ForexFactory itself every 30 minutes while active (forecast revisions, time changes)
  auto* ff = new QTimer(this); ff->setInterval(30 * 60 * 1000);
  connect(ff, &QTimer::timeout, this, [this] { if (!m_active.isEmpty()) fetchForexFactory("thisweek"); });
  ff->start();
}

QDate Calendar::weekStart(const QDate& d) { return d.addDays(-(d.dayOfWeek() % 7)); }   // Sunday
QDate Calendar::currentWeek() {
  return weekStart(QDateTime::currentDateTimeUtc().toTimeZone(QTimeZone("America/New_York")).date());
}

void Calendar::setActive(QObject* owner, bool active) {
  const bool was = !m_active.isEmpty();
  if (active) {
    if (!m_active.contains(owner)) connect(owner, &QObject::destroyed, this, [this, owner] { setActive(owner, false); });
    m_active.insert(owner);
  } else m_active.remove(owner);
  const bool now = !m_active.isEmpty();
  if (now && !was) { fetchMyfxbook(); planPoll(); }
  if (!now && was) m_poll.stop();
}

QList<QDateTime> Calendar::awaitedReleases() const {
  QList<QDateTime> out;
  const QDateTime from = QDateTime::currentDateTimeUtc().addSecs(-2 * actualspace::kEverySec - actualspace::kLagSec);
  for (const Event& e : m_weeks.value(currentWeek())) {
    if (e.allDay || e.impact < Impact::Medium || e.actual.has() || e.isSpeech()) continue;
    if (!e.forecast.has() && !e.previous.has()) continue;   // minutes, votes, press conferences: no figure will come
    if (e.time >= from) out << e.time;
  }
  return out;
}

void Calendar::planPoll() {
  if (m_active.isEmpty()) return;
  const QDateTime now = QDateTime::currentDateTimeUtc();
  QDateTime last = m_lastActuals;
  if (m_lastActualsTry.isValid() && (!last.isValid() || m_lastActualsTry > last)) last = m_lastActualsTry;
  if (!last.isValid()) last = now;   // the first request has just left: planned again when it comes back
  const qint64 ms = now.msecsTo(actualspace::nextPoll(last, now, awaitedReleases()));
  m_poll.start(int(std::clamp<qint64>(ms, 1000, 2 * actualspace::kEverySec * 1000LL)));
}

QList<Event> Calendar::events(const QDate& w) const { return m_weeks.value(w); }
bool Calendar::hasData(const QDate& w) const { return !m_weeks.value(w).isEmpty(); }
QString Calendar::weekNote(const QDate& w) const {   // translated when shown, never stored translated
  const auto it = m_notes.constFind(w);
  if (it == m_notes.cend() || !it->text) return {};
  const QString t = QCoreApplication::translate("news", it->text);
  return it->arg.isEmpty() ? t : t.arg(it->arg);
}

QList<QDate> Calendar::recordedWeeks() const {
  QList<QDate> out;
  const auto files = QDir(paths::data("news/calendar")).entryList({"week-*.json"}, QDir::Files, QDir::Name);
  for (const auto& f : files) out << QDate::fromString(f.mid(5, 10), "yyyy-MM-dd");
  return out;
}

// "m/m", "y/y", "q/q" from an event title or a Myfxbook item name ("Industrial Production MoM"); empty when none
static QString periodTag(const QString& s) {
  static const QRegularExpression re("(?<![A-Za-z])(m/m|y/y|q/q|MoM|YoY|QoQ)(?![A-Za-z])", QRegularExpression::CaseInsensitiveOption);
  const QString p = re.match(s).captured(1).toLower();
  return p == "mom" ? QString("m/m") : p == "yoy" ? QString("y/y") : p == "qoq" ? QString("q/q") : p;
}

void Calendar::readWeek(const QDate& w) {
  if (m_read.contains(w)) return;
  m_read.insert(w);
  QFile f(weekFile(w));
  if (!f.open(QIODevice::ReadOnly)) return;
  QList<Event> list;
  for (const auto& v : QJsonDocument::fromJson(f.readAll()).array()) list << fromJson(v.toObject());
  // actuals recorded before 2026-11-16 from a Myfxbook item of another period (YoY given to an m/m event): dropped,
  // the right one comes back with the next poll while the feed still lists it
  bool fixed = false;
  for (Event& e : list) {
    if (e.actualSource != "Myfxbook") continue;
    const QString want = periodTag(e.title), got = periodTag(e.actualDetail.section(": ", 1));
    if (want.isEmpty() || got.isEmpty() || want == got) continue;
    e.actual = Value{}; e.actualSource.clear(); e.actualDetail.clear(); e.actualAt = {};
    fixed = true;
  }
  if (!list.isEmpty()) m_weeks[w] = list;
  if (fixed) saveWeek(w);
}

void Calendar::saveWeek(const QDate& w) {
  QJsonArray a;
  for (const auto& e : m_weeks.value(w)) a.append(toJson(e));
  QSaveFile f(weekFile(w));
  if (!f.open(QIODevice::WriteOnly)) return;
  f.write(QJsonDocument(a).toJson(QJsonDocument::Compact));
  f.commit();
}

void Calendar::touch(const QDate& w) { m_touched.insert(w); m_touch.start(); }

// When ForexFactory last answered that next week's file does not exist. Its free export serves only the current week
// (ff_calendar_nextweek.json answered 404 on Tuesday 2026-11-24, and no week was ever recorded ahead of time): asked at
// each visit to next week, these misses counted against the export's low rate limit and could earn the whole calendar
// a "wait 4 minutes" (HTTP 429). Asked again after an hour at most.
static QDateTime g_nextWeekMissedAt;

void Calendar::load(const QDate& w) {
  readWeek(w);
  const QDate cur = currentWeek();
  const bool nextRecentlyMissing = g_nextWeekMissedAt.isValid() && g_nextWeekMissedAt.secsTo(QDateTime::currentDateTimeUtc()) < 3600;
  if (w == cur) fetchForexFactory("thisweek");
  else if (w == cur.addDays(7) && !nextRecentlyMissing) fetchForexFactory("nextweek");
  else {
    if (!hasData(w))
      m_notes[w] = {w < cur ? QT_TRANSLATE_NOOP("news", "No recording for this week. ForexFactory's free calendar file covers only the current week; "
                                                    "Deepwater keeps every week it has seen from now on.")
                            : QT_TRANSLATE_NOOP("news", "Not published yet: ForexFactory's free calendar file covers only the current week. This week shows here once it begins (on Sunday, New York time)."),
                    QString()};
    QTimer::singleShot(0, this, [this, w] { emit weekLoaded(w); });
  }
  if (!m_active.isEmpty()) fetchMyfxbook();
}

void Calendar::refresh() {
  fetchForexFactory("thisweek");
  fetchMyfxbook();
}

void Calendar::fetchForexFactory(const QString& which) {
  net::Request r;
  r.url = QUrl("https://nfs.faireconomy.media/ff_calendar_" + which + ".json");
  r.ttlSec = 15 * 60;
  const QDate cur = currentWeek();
  const QDate target = which == "nextweek" ? cur.addDays(7) : cur;
  net::get(r, this, [this, target, which](const net::Reply& rep) {
    // an error page with a body (HTTP 429 / 403 HTML) is an error too: the week gets a note instead of staying blank
    auto failed = [this, target, which](const QString& error, bool notPublished) {
      readWeek(target);
      if (!hasData(target))
        m_notes[target] = which == "nextweek" && notPublished
            ? Note{QT_TRANSLATE_NOOP("news", "Not published yet: ForexFactory's free calendar file covers only the current week. This week shows here once it begins (on Sunday, New York time)."), QString()}
            : Note{QT_TRANSLATE_NOOP("news", "ForexFactory calendar unavailable (%1)."), error};
      emit weekLoaded(target);
    };
    if (!rep.ok()) {
      // only ForexFactory's own "no such file" pauses next week's asking; a dropped connection does not
      if (which == "nextweek" && rep.status == 404) g_nextWeekMissedAt = QDateTime::currentDateTimeUtc();
      failed(rep.error, rep.status == 404 || rep.body.isEmpty());
      return;
    }
    const auto doc = QJsonDocument::fromJson(rep.body);
    QList<Event> fresh;
    QDate weekOf;
    for (const auto& v : doc.array()) {
      const auto o = v.toObject();
      Event e;
      e.title = o["title"].toString().trimmed();
      e.currency = o["country"].toString().toUpper();
      const QString d = o["date"].toString();
      e.time = QDateTime::fromString(d, Qt::ISODate).toUTC();
      if (!e.time.isValid()) continue;
      const QString imp = o["impact"].toString();
      e.impact = imp == "High" ? Impact::High : imp == "Medium" ? Impact::Medium : imp == "Holiday" ? Impact::Holiday : Impact::Low;
      e.allDay = e.impact == Impact::Holiday;   // the JSON export has no other all-day marker
      e.forecast = parseValue(o["forecast"].toString());
      e.previous = parseValue(o["previous"].toString());
      finish(e);
      fresh << e;
      // the export's week runs Sunday→Saturday in New York time
      const QDate nyDay = QDateTime::fromString(d, Qt::ISODate).date();
      const QDate ws = weekStart(nyDay);
      if (!weekOf.isValid() || ws < weekOf) weekOf = ws;
    }
    if (fresh.isEmpty()) { failed(QStringLiteral("empty answer"), true); return; }
    if (!rep.fromCache || !m_lastRefresh.isValid()) m_lastRefresh = rep.fetched;
    readWeek(weekOf);
    mergeWeek(weekOf, fresh);
    m_notes.remove(weekOf);
    emit weekLoaded(weekOf);
    if (weekOf != target) emit weekLoaded(target);
    emit refreshed();
  });
}

static bool sameFigureText(const Value& a, const Value& b) { return a.suffix == b.suffix && std::abs(a.v - b.v) < 1e-9; }

void Calendar::mergeWeek(const QDate& w, const QList<Event>& fresh) {
  QList<Event>& cur = m_weeks[w];
  QHash<QString, int> byKey, bySeriesDay;
  for (int i = 0; i < cur.size(); ++i) {
    byKey.insert(cur[i].key, i);
    bySeriesDay.insert(cur[i].seriesKey + "|" + cur[i].time.date().toString(Qt::ISODate), i);
  }
  QList<Event> out;
  for (Event e : fresh) {
    int i = byKey.value(e.key, -1);
    if (i < 0) i = bySeriesDay.value(e.seriesKey + "|" + e.time.date().toString(Qt::ISODate), -1);
    if (i >= 0) {   // keep what Deepwater recorded (actual, range, banks)
      const Event& old = cur[i];
      e.actual = old.actual; e.actualSource = old.actualSource; e.actualAt = old.actualAt; e.actualDetail = old.actualDetail;
      e.actualPub = old.actualPub; e.quality = old.quality; e.other = old.other; e.also = old.also; e.details = old.details;
      e.revised = old.revised; e.revisedSource = old.revisedSource;
      if (e.revised.num() && e.previous.num() && sameFigureText(e.revised, e.previous)) { e.revised = {}; e.revisedSource.clear(); }   // the calendar caught up
      e.low = old.low; e.high = old.high; e.banks = old.banks;
    }
    out << e;
  }
  std::stable_sort(out.begin(), out.end(), [](const Event& a, const Event& b) { return a.time < b.time; });
  cur = out;
  saveWeek(w);
}

// ------------------------------------------------------------------------------------------------ Myfxbook actuals
static QString ccyOfCountry(const QString& c) {
  static const QHash<QString, QString> m{
    {"united states", "USD"}, {"euro area", "EUR"}, {"european union", "EUR"}, {"germany", "EUR"}, {"france", "EUR"},
    {"italy", "EUR"}, {"spain", "EUR"}, {"united kingdom", "GBP"}, {"japan", "JPY"}, {"switzerland", "CHF"},
    {"canada", "CAD"}, {"australia", "AUD"}, {"new zealand", "NZD"}, {"china", "CNY"}};
  return m.value(c.toLower());
}
static const QStringList& countries() {
  static const QStringList c{"United States", "Euro Area", "European Union", "Germany", "France", "Italy", "Spain",
                             "United Kingdom", "Japan", "Switzerland", "Canada", "Australia", "New Zealand", "China"};
  return c;
}

static QString isoOfCountry(const QString& c) {
  static const QHash<QString, QString> m{
    {"united states", "US"}, {"euro area", "EZ"}, {"european union", "EZ"}, {"germany", "DE"}, {"france", "FR"},
    {"italy", "IT"}, {"spain", "ES"}, {"united kingdom", "GB"}, {"japan", "JP"}, {"switzerland", "CH"},
    {"canada", "CA"}, {"australia", "AU"}, {"new zealand", "NZ"}, {"china", "CN"}};
  return m.value(c.toLower());
}

// Words of an event name after aliasing the two vocabularies (Myfxbook ↔ ForexFactory).
static QSet<QString> nameTokens(QString s) {
  s = s.toLower();
  const QList<QPair<QString, QString>> alias{
    {"inflation rate", "cpi"}, {"consumer price index", "cpi"}, {"yoy", "y/y"}, {"mom", "m/m"}, {"qoq", "q/q"},
    {"non farm payrolls", "non-farm employment change"}, {"nonfarm payrolls", "non-farm employment change"},
    {"gdp growth rate", "gdp"}, {"interest rate decision", "rate"}, {"cash rate", "rate"},
    {"producer price index", "ppi"}, {"ppi input", "ppi input"}, {"unemployment change", "unemployment"},
    {"initial jobless claims", "unemployment claims"}, {"harmonised", "hicp"}, {"speech", "speaks"},
    {"core inflation rate", "core cpi"}, {"balance of trade", "trade balance"}, {"psnb", "public sector net borrowing"},
    {"caseshiller", "cs composite hpi"}};
  for (const auto& [a, b] : alias) s.replace(a, b);
  static const QRegularExpression split(R"([^a-z0-9/]+)");
  // whole words the wire shortens, and indicators both name their own way
  static const QHash<QString, QString> word{{"service", "services"}, {"mfg", "manufacturing"}, {"comp", "composite"},
    {"wkly", "weekly"}, {"prices", "price"}, {"sppi", "services ppi"}, {"ippi", "ppi"}, {"hpi", "house price index"},
    {"preliminary", "prelim"}, {"nationwide", "national"}};
  QSet<QString> out;
  static const QSet<QString> stop{"german", "french", "italian", "spanish", "the", "of", "and", "s", "&"};
  for (const auto& t : s.split(split, Qt::SkipEmptyParts)) {
    if (stop.contains(t)) continue;
    const auto w = word.constFind(t);
    if (w == word.cend()) out.insert(t);
    else for (const auto& x : w->split(' ')) out.insert(x);
  }
  return out;
}

void Calendar::fetchMyfxbook() {
  net::Request r;
  r.url = QUrl("https://www.myfxbook.com/rss/forex-economic-calendar-events");
  const QDateTime asked = QDateTime::currentDateTimeUtc();
  r.ttlSec = actualspace::ttlSec(m_lastActuals, asked, awaitedReleases());
  net::get(r, this, [this, asked](const net::Reply& rep) {
    if (!rep.fromCache || rep.stale) m_lastActualsTry = asked;
    if (rep.fetched.isValid() && !rep.body.isEmpty()) m_lastActuals = rep.fetched;
    planPoll();
    if (rep.body.isEmpty()) { emit actualsPolled(); return; }
    const QString xml = QString::fromUtf8(rep.body);
    static const QRegularExpression item(R"(<item>(.*?)</item>)", QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression title(R"(<title>(.*?)</title>)", QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression date(R"(<pubDate>(.*?)</pubDate>)");
    static const QRegularExpression desc(R"(<description>(.*?)</description>)", QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression td(R"(<td>(.*?)</td>)", QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression tags(R"(<[^>]*>)");
    auto unescape = [](QString s) {
      static const QRegularExpression num(R"(&#(\d+);)");
      QString out; qsizetype last = 0;
      for (auto it = num.globalMatch(s); it.hasNext();) {
        const auto m = it.next();
        out += s.mid(last, m.capturedStart() - last) + QChar(m.captured(1).toInt());
        last = m.capturedEnd();
      }
      out += s.mid(last);
      return out.replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"");
    };
    QHash<QString, std::pair<QString, Match>> best;   // event key → item name and match
    struct Figures { Value actual, previous, consensus; };
    QHash<QString, Figures> values;
    const auto figure = [](QString s) {   // "£-17.0B", "€27.5B": the currency sign is not part of the number
      static const QRegularExpression sign(QStringLiteral("[\\x{00A3}\\x{20AC}\\x{00A5}$]"));
      return parseValue(s.remove(sign));
    };
    for (auto it = item.globalMatch(xml); it.hasNext();) {
      const QString body = it.next().captured(1);
      const QString t = unescape(title.match(body).captured(1)).trimmed();
      QDateTime when = QLocale(QLocale::English).toDateTime(date.match(body).captured(1).trimmed(), "ddd, dd MMM yyyy HH:mm 'GMT'");
      when.setTimeZone(QTimeZone::UTC);
      const QString d = unescape(desc.match(body).captured(1));
      QStringList cells;
      for (auto ct = td.globalMatch(d); ct.hasNext();) cells << QString(ct.next().captured(1)).remove(tags).trimmed();
      if (cells.size() < 5 || !when.isValid()) continue;
      QString country, name;
      for (const auto& c : countries()) if (t.startsWith(c + " ")) { country = c; name = t.mid(c.size() + 1); break; }
      if (country.isEmpty()) continue;
      const Value actual = figure(cells[4]), previous = figure(cells[2]), consensus = figure(cells[3]);
      if (!actual.has()) continue;
      const Match m = matchActual(ccyOfCountry(country), isoOfCountry(country), when, name, previous, consensus);
      if (m.index < 0) continue;
      // one item per event and per poll, the best match (2026-11-16: "CAD Median CPI y/y" also took "Inflation
      // Rate YoY" and its actual flipped 2.0 % ↔ 3.1 % at every poll)
      const QString& k = m.key;
      if (const auto b = best.constFind(k); b != best.cend() && b->second.score >= m.score) continue;
      best[k] = {name, m}; values[k] = Figures{actual, previous, consensus};
    }
    for (auto it = best.cbegin(); it != best.cend(); ++it) {
      const Figures f = values.value(it.key());
      applyActual(it->first, it->second, f.actual, f.previous, f.consensus);
    }
    emit actualsPolled();
  });
}

// Share of the indicator words (period tags "y/y", "m/m"… left out) common to a Myfxbook item name and an event title.
static double nameOverlap(const QSet<QString>& a, const QSet<QString>& b) {
  static const QSet<QString> periods{"y/y", "m/m", "q/q"};
  const QSet<QString> x = a - periods, y = b - periods;
  return double((x & y).size()) / std::max<qsizetype>(1, (x | y).size());
}

static QString eventCountry(const Event& e);   // below, with the headlines

Calendar::Match Calendar::matchActual(const QString& ccy, const QString& country, const QDateTime& t, const QString& name,
                                      const Value& previous, const Value& consensus) {
  Match out;
  if (ccy.isEmpty()) return out;
  const QDate w = weekStart(t.toTimeZone(QTimeZone("America/New_York")).date());   // weeks are New York weeks
  readWeek(w);
  auto it = m_weeks.find(w);
  if (it == m_weeks.end()) return out;
  const QSet<QString> want = nameTokens(name);
  // Myfxbook lists the same release once per period ("Industrial Production MoM" and "… YoY"): only an event of that
  // period may take the figure (2026-11-16: both went to the m/m event, swapping at every poll)
  const QString itemPeriod = periodTag(name);
  int best = -1; double bestScore = 0;
  for (int i = 0; i < it->size(); ++i) {
    const Event& e = (*it)[i];
    if (e.currency != ccy || std::abs(e.time.secsTo(t)) > 5 * 60) continue;
    if (!country.isEmpty() && eventCountry(e) != country) continue;   // the euro's releases of several countries
    const QString eventPeriod = periodTag(e.title);
    if (!itemPeriod.isEmpty() && !eventPeriod.isEmpty() && itemPeriod != eventPeriod) continue;
    const QSet<QString> have = nameTokens(e.title);
    // the names must share an indicator word: equal previous/consensus figures alone are a coincidence
    if (nameOverlap(want, have) <= 0) continue;
    double score = double((want & have).size()) / std::max<qsizetype>(1, (want | have).size());
    if (previous.num() && e.previous.num() && std::abs(previous.scaled() - e.previous.scaled()) < 1e-9) score += 0.5;
    if (consensus.num() && e.forecast.num() && std::abs(consensus.scaled() - e.forecast.scaled()) < 1e-9) score += 0.25;
    if (score > bestScore) { bestScore = score; best = i; }
  }
  if (best < 0 || bestScore < 0.34) return out;
  out.week = w; out.index = best; out.key = (*it)[best].key; out.score = bestScore;
  return out;
}

namespace { bool sameFigure(const Value& a, const Value& b); bool hasStage(const Event& e); bool plausibleRevision(const Value& v, const Value& previous);
            bool setRevised(Event& e, Value v, const QString& source); QString sourceLabel(const Event& e);   // below, with the headlines
bool addAlso(Event& e, const QString& source) {   // a second source gave the figure shown
  if (source.isEmpty() || source == sourceLabel(e) || e.also.contains(source)) return false;
  e.also << source;
  return true;
}
}

void Calendar::applyActual(const QString& name, const Match& m, Value actual, Value previous, const Value& consensus) {
  auto it = m_weeks.find(m.week);
  if (it == m_weeks.end()) return;
  // by key: a listener of an earlier actual may have reloaded the week (indices move)
  const auto found = std::find_if(it->begin(), it->end(), [&](const Event& x) { return x.key == m.key; });
  if (found == it->end()) return;
  Event& e = *found;
  if (!adaptToEvent(actual, previous, e)) return;   // thousands against millions: not this figure
  const int quality = int(sameFigure(previous, e.previous)) + int(sameFigure(consensus, e.forecast));
  bool changed = false;
  // its previous is the figure as revised since (the calendar keeps the first one), when it is the same series
  if ((quality > 0 || e.actualSource == "Myfxbook") && !hasStage(e) && plausibleRevision(previous, e.previous) && !e.revised.has())
    changed |= setRevised(e, previous, QStringLiteral("Myfxbook"));
  const auto keep = [&](const QString& other) {
    if (e.other != other) { e.other = other; changed = true; }
    if (changed) touch(m.week);
  };
  if (e.actual.text == actual.text) {
    if (e.actualSource == "Myfxbook" && e.quality != quality) { e.quality = quality; changed = true; }
    if (e.actualSource != "Myfxbook") changed |= addAlso(e, QStringLiteral("Myfxbook"));
    keep(e.other);
    return;
  }
  if (e.actual.has() && e.actualSource != "Myfxbook") {
    if (sameFigure(actual, e.actual)) {
      changed |= addAlso(e, QStringLiteral("Myfxbook"));
      keep(e.other.startsWith("Myfxbook ") ? QString() : e.other);
      return;
    }
    // an official series stands; a headline that repeats the calendar's own figures stands against an item that
    // repeats none (2026-11-19: the euro area's unadjusted current account)
    if (e.quality == 9 || (quality == 0 && e.quality > 0)) { keep("Myfxbook " + actual.text); return; }
    e.other = sourceLabel(e) + " " + e.actual.text;
  }
  static const QString kDetail = QStringLiteral("Myfxbook economic calendar RSS: ");
  // an actual taken from a Myfxbook item whose name matches the event better is kept (the better item may have left
  // the feed while a looser one is still listed)
  if (e.actualSource == "Myfxbook" && e.actualDetail.startsWith(kDetail)) {
    const QSet<QString> have = nameTokens(e.title);
    if (nameOverlap(nameTokens(e.actualDetail.mid(kDetail.size())), have) > nameOverlap(nameTokens(name), have)) { keep(e.other); return; }
  }
  e.actual = actual; e.actualSource = "Myfxbook"; e.actualAt = QDateTime::currentDateTimeUtc(); e.also.clear();
  e.actualPub = {}; e.quality = quality;
  e.actualDetail = kDetail + name;
  saveWeek(m.week);
  emit actualArrived(e);
  emit weekLoaded(m.week);
}

void Calendar::buildReleaseIndex() const {
  if (m_indexStarted) return;
  m_indexStarted = true;
  const QString dir = paths::data("news/calendar");
  const QList<QDate> weeks = recordedWeeks();
  auto* w = new QFutureWatcher<QHash<QDate, Releases>>(const_cast<Calendar*>(this));
  connect(w, &QFutureWatcher<QHash<QDate, Releases>>::finished, this, [this, w] {
    w->deleteLater();
    m_releaseIndex = w->result();
    m_indexReady = true;
  });
  w->setFuture(QtConcurrent::run([dir, weeks] {
    QHash<QDate, Releases> idx;
    for (const QDate& wk : weeks) {
      QFile f(dir + "/week-" + wk.toString("yyyy-MM-dd") + ".json");
      if (!f.open(QIODevice::ReadOnly)) continue;
      Releases& r = idx[wk];
      for (const auto& v : QJsonDocument::fromJson(f.readAll()).array()) {
        const Event e = fromJson(v.toObject());
        if (e.actual.num()) r[e.seriesKey] << qMakePair(e.time, e.actual);
      }
    }
    return idx;
  }));
}

QList<QPair<QDateTime, Value>> Calendar::recordedReleases(const QString& seriesKey) const {
  buildReleaseIndex();
  QList<QPair<QDateTime, Value>> out;
  for (const QDate& w : recordedWeeks()) {
    QList<Event> list = m_weeks.value(w);
    if (list.isEmpty() && m_indexReady) {
      // a week file created after the index was built is in m_weeks (this session wrote it): absent here = no file
      if (const auto it = m_releaseIndex.constFind(w); it != m_releaseIndex.cend()) out << it->value(seriesKey);
      continue;
    }
    if (list.isEmpty()) {
      QFile f(weekFile(w));
      if (!f.open(QIODevice::ReadOnly)) continue;
      for (const auto& v : QJsonDocument::fromJson(f.readAll()).array()) list << fromJson(v.toObject());
    }
    for (const auto& e : list) if (e.seriesKey == seriesKey && e.actual.num()) out << qMakePair(e.time, e.actual);
  }
  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  return out;
}

// ------------------------------------------------------------------------------------------------ descriptions
QString Calendar::countryOf(const QString& ccy) {
  static const QHash<QString, const char*> m{{"USD", QT_TRANSLATE_NOOP("news", "United States")},
    {"EUR", QT_TRANSLATE_NOOP("news", "Euro area")}, {"GBP", QT_TRANSLATE_NOOP("news", "United Kingdom")},
    {"JPY", QT_TRANSLATE_NOOP("news", "Japan")}, {"CHF", QT_TRANSLATE_NOOP("news", "Switzerland")},
    {"CAD", QT_TRANSLATE_NOOP("news", "Canada")}, {"AUD", QT_TRANSLATE_NOOP("news", "Australia")},
    {"NZD", QT_TRANSLATE_NOOP("news", "New Zealand")}, {"CNY", QT_TRANSLATE_NOOP("news", "China")},
    {"ALL", QT_TRANSLATE_NOOP("news", "Global")}};
  const auto it = m.constFind(ccy.toUpper());
  return it == m.cend() ? ccy : QCoreApplication::translate("news", *it);   // translated at call time
}

QStringList Calendar::describe(const Event& e) {
  const QString t = e.title.toLower(), c = e.currency;
  QString what; bool inverse = false, neutral = false;
  auto has = [&](const char* k) { return t.contains(QLatin1String(k)); };
  if (e.impact == Impact::Holiday || has("bank holiday")) {
    return {QCoreApplication::translate("news", "Market holiday in %1: banks and most exchanges are closed, liquidity is thin.").arg(countryOf(c))};
  } else if (e.isSpeech()) {
    what = QCoreApplication::translate("news", "A public appearance by a policymaker. Markets listen for hints about the path of interest rates, the "
           "inflation outlook and the balance sheet."); neutral = true;
  } else if (has("auction")) {
    what = QCoreApplication::translate("news", "Government debt auction. The yield is the average rate the Treasury pays; strong demand (lower yield, "
           "higher bid-to-cover) points to appetite for the country's debt."); neutral = true;
  } else if (has("core") && (has("cpi") || has("pce") || has("hicp"))) {
    what = QCoreApplication::translate("news", "Core consumer prices: the change in the price of goods and services bought by households, excluding the "
           "most volatile items (food and energy). Central banks watch it to judge underlying inflation.");
  } else if (has("cpi") || has("hicp") || has("inflation")) {
    what = QCoreApplication::translate("news", "The Consumer Price Index tracks the change in the price of a basket of goods and services bought by "
           "households. It is the main gauge of inflation and drives central bank decisions.");
  } else if (has("ppi") || has("producer")) {
    what = QCoreApplication::translate("news", "Producer prices: the change in the price of goods sold by manufacturers. Higher costs upstream tend to "
           "reach consumer prices later.");
  } else if (has("pce")) {
    what = QCoreApplication::translate("news", "Personal consumption expenditure prices, the inflation measure the Federal Reserve targets.");
  } else if (has("gdp")) {
    what = QCoreApplication::translate("news", "Gross Domestic Product: the value of all goods and services produced, the broadest measure of economic "
           "activity and of the economy's health.");
  } else if (has("unemployment claims") || has("claimant")) {
    what = QCoreApplication::translate("news", "The number of people filing for unemployment benefits for the first time; a timely signal of the labour "
           "market."); inverse = true;
  } else if (has("unemployment")) {
    what = QCoreApplication::translate("news", "The share of the labour force without a job and actively looking for one."); inverse = true;
  } else if (has("employment") || has("payrolls") || has("jobs")) {
    what = QCoreApplication::translate("news", "The change in the number of people employed. Job creation feeds consumer spending, the largest part of "
           "economic activity.");
  } else if (has("earnings") || has("wage") || has("labor cost") || has("labour cost")) {
    what = QCoreApplication::translate("news", "The change in what employers pay their workers. Faster wage growth supports spending and can push "
           "inflation up.");
  } else if (has("retail sales")) {
    what = QCoreApplication::translate("news", "The change in the value of sales at the retail level, the main gauge of consumer spending.");
  } else if (has("pmi") || has("ism")) {
    what = QCoreApplication::translate("news", "A survey of purchasing managers. Above 50 means activity is expanding, below 50 contracting; it is one "
           "of the earliest reads on the economy.");
  } else if (has("rate") && (has("statement") || has("decision") || has("cash rate") || has("main refinancing") ||
             has("bank rate") || has("policy rate") || has("funds rate") || has("overnight rate") || has("official"))) {
    what = QCoreApplication::translate("news", "The central bank's policy rate decision. Rates are the main driver of a currency's value: investors seek "
           "the higher yield.");
  } else if (has("trade balance") || has("current account")) {
    what = QCoreApplication::translate("news", "The difference between the value of exports and imports. A larger surplus means more foreign demand for "
           "the currency.");
  } else if (has("housing") || has("home") || has("building") || has("nahb") || has("construction")) {
    what = QCoreApplication::translate("news", "A gauge of the housing market, which reacts early to interest rates and drives construction and spending.");
  } else if (has("confidence") || has("sentiment") || has("zew") || has("ifo") || has("tankan")) {
    what = QCoreApplication::translate("news", "A survey of how households or businesses see current conditions and the months ahead; confidence tends "
           "to lead spending and investment.");
  } else if (has("industrial production") || has("manufacturing production") || has("factory") || has("orders")) {
    what = QCoreApplication::translate("news", "Output or orders of factories, mines and utilities: a measure of the industrial cycle.");
  } else if (has("crude") || has("natural gas") || has("oil")) {
    what = QCoreApplication::translate("news", "The weekly change in commercial inventories; a surprise moves energy prices and commodity currencies."); neutral = true;
  } else {
    what = QCoreApplication::translate("news", "Economic release for %1.").arg(countryOf(c));
  }
  if (neutral || c == "ALL") return {what};
  const QString pos = QCoreApplication::translate("news", "positive (bullish)"), neg = QCoreApplication::translate("news", "negative (bearish)");
  const QString up = inverse ? neg : pos, down = inverse ? pos : neg;
  return {what, QCoreApplication::translate("news", "A reading above the forecast is usually taken as %1 for the %2, a reading below it as %3.")
                    .arg(up, c, down)};
}

// ------------------------------------------------------------------------------------------------ actuals: headlines
namespace {
// Words that say which estimate of a figure it is, not which figure.
const QSet<QString>& stageWords() {
  static const QSet<QString> w{"flash", "prelim", "preliminary", "final", "revised", "advance", "second", "2nd", "3rd", "estimate"};
  return w;
}
// Words too common to tie a headline to an event.
const QSet<QString>& genericWords() {
  static const QSet<QString> w{"index", "price", "sales", "change", "rate", "nsa", "sa", "yr", "total", "new", "net", "y/y", "m/m",
                               "q/q", "3m/3m", "weekly", "ex", "fed", "us", "overall", "business", "consumer", "flash", "prelim",
                               "preliminary", "final", "revised", "advance", "second", "2nd", "3rd", "estimate", "core"};
  return w;
}
bool hasStage(const Event& e) {
  static const QRegularExpression re(R"(\b(prelim|final|revised|advance|second estimate)\b)", QRegularExpression::CaseInsensitiveOption);
  return re.match(e.title).hasMatch();
}
// The two figures are the same number at the coarser of their precisions.
bool sameFigure(const Value& a, const Value& b) {
  if (!a.num() || !b.num() || (a.suffix == "%") != (b.suffix == "%")) return false;
  const double unit = std::max(mult(a.suffix) / std::pow(10.0, a.decimals), mult(b.suffix) / std::pow(10.0, b.decimals));
  return std::abs(a.scaled() - b.scaled()) <= 0.5 * unit + 1e-9;
}
// v in the unit of `like` (0.676M → 676K), with the digits it had.
Value convertLike(const Value& v, const Value& like) {
  if (!v.num() || v.suffix == like.suffix || like.suffix.isEmpty() || like.suffix == "%" || v.suffix == "%") return v;
  Value conv = like;
  const int shift = int(std::round(std::log10(mult(like.suffix) / mult(v.suffix))));
  conv.decimals = std::max(0, v.decimals + shift);
  return parseValue(formatLike(v.scaled() / mult(like.suffix), conv));
}
QString sourceLabel(const Event& e) {
  if (e.actualSource.endsWith(" headline")) return e.actualSource.chopped(9);
  if (e.actualSource.endsWith(" official series")) return e.actualSource.chopped(16);
  return e.actualSource;
}
// A previous figure close enough to the calendar's to be its revision, not another series'.
bool plausibleRevision(const Value& v, const Value& previous) {
  if (!v.num() || !previous.num() || (v.suffix == "%") != (previous.suffix == "%")) return false;
  const double d = std::abs(v.scaled() - previous.scaled());
  if (previous.suffix == "%") return d <= 0.5 + 1e-9;
  return d <= 0.10 * std::abs(previous.scaled()) + 1e-9;
}
bool setRevised(Event& e, Value v, const QString& source) {
  v = sameDecimals(convertLike(v, e.previous), e.previous);
  if (!v.num() || sameFigure(v, e.previous) || (e.revised.has() && e.revised.text == v.text)) return false;
  e.revised = v; e.revisedSource = source;
  return true;
}
}

bool adaptToEvent(Value& actual, Value& previous, const Event& e) {
  const Value& like = e.previous.num() ? e.previous : e.forecast;
  if (!actual.num() || !like.num()) return true;
  // the feed's unit is wrong when its previous has the calendar's digits under another suffix ("676.0M" for 676K)
  if (previous.num() && e.previous.num() && previous.suffix != e.previous.suffix && previous.suffix == actual.suffix &&
      previous.suffix != "%" && e.previous.suffix != "%" && std::abs(previous.v - e.previous.v) <= 0.5 * std::pow(10.0, -e.previous.decimals)) {
    const auto relabel = [&](Value& v) { Value u = v; u.suffix = e.previous.suffix; v = parseValue(formatLike(v.v, u)); };
    relabel(actual); relabel(previous);
  }
  // opposite sign convention (net borrowing as a negative balance)
  if (previous.num() && e.previous.num() && previous.v != 0 && previous.suffix == e.previous.suffix &&
      sameFigure(Value{{}, -previous.v, previous.suffix, previous.decimals}, e.previous) && !sameFigure(previous, e.previous)) {
    actual = parseValue(formatLike(-actual.v, actual)); previous = parseValue(formatLike(-previous.v, previous));
  }
  // thousands against millions and no explanation: not the same figure
  if (actual.suffix != like.suffix && actual.suffix != "%" && like.suffix != "%" && actual.v != 0 && like.v != 0) {
    const double ratio = std::abs(actual.scaled() / like.scaled());
    if (ratio > 100 || ratio < 0.01) return false;
  }
  actual = sameDecimals(actual, e.forecast.num() ? e.forecast : e.previous);
  // "203.0K" for a series published in whole thousands
  if (actual.decimals > like.decimals && actual.suffix == like.suffix) {
    const double f = std::pow(10.0, like.decimals);
    if (std::abs(actual.v * f - std::round(actual.v * f)) < 1e-9) {
      Value u = actual; u.decimals = like.decimals;
      actual = parseValue(formatLike(actual.v, u));
    }
  }
  return true;
}

// "German Flash Services PMI" → DE; a euro-area title → EZ; other currencies have one country.
static QString eventCountry(const Event& e) {
  if (e.currency != "EUR") return e.currency.left(2);
  static const QList<QPair<QString, QString>> c{{"German", "DE"}, {"French", "FR"}, {"Italian", "IT"}, {"Spanish", "ES"}, {"Belgian", "BE"}};
  for (const auto& [word, iso] : c) if (e.title.startsWith(word, Qt::CaseInsensitive)) return iso;
  return QStringLiteral("EZ");
}

bool Calendar::applyHeadline(const DataHeadline& h, const QDateTime& t, const QString& source, const QString& text) {
  if (h.currency.isEmpty() || !h.actual.num()) return false;
  const QDate w = weekStart(t.toTimeZone(QTimeZone("America/New_York")).date());
  readWeek(w);
  auto it = m_weeks.find(w);
  if (it == m_weeks.end()) return false;
  static const QSet<QString> periods{"y/y", "m/m", "q/q", "3m/3m"};
  static const QSet<QString> qualifiers{"core", "trimmed", "median", "common", "services", "manufacturing", "composite",
                                        "construction", "import", "export", "flash", "final"};
  static const QSet<QString> benign{"index", "global", "hcob", "p", "sa", "fed"};   // names of the survey's publisher
  QSet<QString> want = nameTokens(h.indicator);
  for (const auto& p : periods) want.remove(p);
  int best = -1, bestAgree = 0; double bestScore = 0; bool tie = false;
  int loose = -1; bool severalLoose = false;
  int related = -1; double relatedScore = 0;
  for (int i = 0; i < it->size(); ++i) {
    const Event& e = (*it)[i];
    if (e.currency != h.currency || (!h.country.isEmpty() && eventCountry(e) != h.country)) continue;
    const qint64 dt = e.time.secsTo(t);                          // headline after the release time
    if (dt < -5 * 60 || dt > 20 * 60) continue;
    QSet<QString> have = nameTokens(e.title);
    for (const auto& p : periods) have.remove(p);
    const double score = double((want & have).size()) / std::max<qsizetype>(1, (want | have).size());
    if (!((want - genericWords()) & (have - genericWords())).isEmpty() && !e.isSpeech()) {
      const double s = score + int(e.impact) * 0.001;            // same overlap: the event that matters more
      if (s > relatedScore) { relatedScore = s; related = i; }
    }
    if (!h.period.isEmpty() && periods.contains(h.period) && !e.periodRaw.isEmpty() && h.period != e.periodRaw) continue;
    bool qualOk = true;
    for (const auto& q : qualifiers) if (want.contains(q) != have.contains(q) && (q != "flash" && q != "final")) qualOk = false;
    if (!qualOk || score < 0.2) continue;
    // unit and scale must agree with the calendar's figures
    const Value& like = e.forecast.num() ? e.forecast : e.previous;
    if (!like.num()) continue;
    const bool pct = like.suffix == "%";
    if (pct != (h.actual.suffix == "%")) continue;
    if (!pct && like.suffix.isEmpty() != h.actual.suffix.isEmpty()) continue;
    auto same = [&](const Value& a, const Value& b) {   // a (headline) equals b (calendar) at b's precision
      if (!a.num() || !b.num()) return false;
      const double av = pct ? a.v : a.scaled() / mult(b.suffix);
      const double f = std::pow(10.0, std::min(b.decimals, std::max(a.decimals, 0) + (pct ? 0 : 3)));
      return std::abs(std::round(av * f) / f - b.v) <= 0.5 / f + 1e-9;
    };
    const int agree = int(same(h.forecast, e.forecast)) + int(same(h.previous, e.previous));
    // the calendar and the wire name a few surveys differently (CBI realized sales / distributive trades): the
    // calendar's previous and no other release of that country at that moment make up for it
    bool alone = true;
    for (const Event& o : *it) if (&o != &e && o.currency == e.currency && eventCountry(o) == eventCountry(e) && std::abs(o.time.secsTo(e.time)) < 60) alone = false;
    const bool named = score >= 0.5 || agree == 2 || (alone && same(h.previous, e.previous) && h.previous.v != 0);
    if (agree > 0 && named) {
      const double s = score + 0.1 * (agree - 1);
      if (std::abs(s - bestScore) < 1e-9 && best >= 0) tie = true;
      if (s > bestScore + 1e-9) { bestScore = s; best = i; bestAgree = agree; tie = false; }
      continue;
    }
    // same name in full, stage words apart, and a previous of the same size: the two surveys of economists differ
    const QSet<QString> a = have - stageWords() - benign, b = want - stageWords() - benign;
    const double scale = std::max(std::abs(e.previous.v), 1e-9);
    const bool near = h.previous.num() && e.previous.num() && h.previous.suffix == e.previous.suffix &&
                      std::abs(h.previous.v - e.previous.v) <= (pct ? 1.0 : 0.10 * scale);
    const bool samePeriod = e.periodRaw.isEmpty() ? true : h.period == e.periodRaw;
    if (h.structured && agree == 0 && !a.isEmpty() && a == b && near && samePeriod) {
      if (loose >= 0) severalLoose = true;
      loose = i;
    }
  }
  const bool ambiguous = best >= 0 ? tie : severalLoose;
  const int idx = best >= 0 ? best : loose;
  if (ambiguous) return false;
  if (idx < 0) {
    // no event of its own: a figure of the same release
    static const QRegularExpression auction(R"(\b(yield|bid-to-cover|awarded|auction)\b)", QRegularExpression::CaseInsensitiveOption);
    if (!h.structured || related < 0 || auction.match(h.name).hasMatch()) return false;
    Event& e = (*it)[related];
    Detail d{h.name, h.actual.text, h.forecast.text, h.previous.text, h.revised.text, source, t};
    for (Detail& o : e.details) {
      if (o.name != d.name) continue;
      if (o.at.secsTo(t) < 0 || (o.at.secsTo(t) == 0 && o.actual == d.actual)) return false;
      o = d;                                   // the wire corrected itself (Core PCE y/y, 2026-11-25: 3.1% then 3.3%)
      touch(w);
      return false;
    }
    if (e.details.size() >= 12) return false;
    e.details << d;
    touch(w);
    return false;
  }
  Event& e = (*it)[idx];
  const Value& like = e.forecast.num() ? e.forecast : e.previous;
  const Value a = sameDecimals(convertLike(h.actual, like), like);
  bool changed = false, arrived = false;
  const QString label = source + " headline";
  const auto take = [&] {
    e.actual = a; e.actualSource = label; e.actualDetail = text; e.actualPub = t; e.also.clear();
    e.quality = best >= 0 ? bestAgree : 0;
    e.actualAt = QDateTime::currentDateTimeUtc();
    changed = arrived = true;
  };
  if (!e.actual.has()) take();
  else if (!e.actual.num() || (like.num() && e.actual.num() && e.actual.v != 0 && like.v != 0 &&
           (std::abs(e.actual.scaled() / like.scaled()) > 100 && std::abs(a.scaled() / like.scaled()) <= 100))) take();   // unreadable, or a unit off
  else if (e.actualSource == label) {
    if (e.actualPub.isValid() && t > e.actualPub && a.text != e.actual.text && best >= 0) take();   // the wire corrected itself
  } else if (sameFigure(a, e.actual)) {
    if (!e.other.isEmpty() && e.other.startsWith(source + " ")) { e.other.clear(); changed = true; }
    changed |= addAlso(e, source);
  } else if (best >= 0) {
    const QString other = source + " " + a.text;
    if (e.quality == 0 && bestAgree > 0 && e.actualSource == "Myfxbook") { e.other = "Myfxbook " + e.actual.text; take(); }
    else if (e.other != other) { e.other = other; changed = true; }
  }
  // the previous as revised: stated, or the wire's own previous when the calendar still shows the first one
  if (h.revised.num()) changed |= setRevised(e, h.revised, source);
  else if (h.structured && h.previous.num() && !hasStage(e) && e.actual.has() && sameFigure(a, e.actual) &&
           plausibleRevision(h.previous, e.previous))
    changed |= setRevised(e, h.previous, source);
  if (!changed) return false;
  if (!arrived) { touch(w); return false; }
  saveWeek(w);
  emit actualArrived(e);
  emit weekLoaded(w);
  return true;
}

// ------------------------------------------------------------------------------------------------ actuals: official
void Calendar::fillOfficial(const QDate& w) {
  readWeek(w);
  const QDateTime now = QDateTime::currentDateTimeUtc();
  auto& history = History::instance();
  for (const Event& ev : m_weeks.value(w)) {
    if (ev.time > now || ev.time < now.addDays(-21)) continue;
    const QString key = ev.key;
    const auto found = [this, w, key]() -> Event* {
      auto it = m_weeks.find(w);
      if (it != m_weeks.end()) for (Event& e : *it) if (e.key == key) return &e;
      return nullptr;
    };
    if (!ev.actual.has() && history.canOfficial(ev))
      history.officialActual(ev, this, [this, w, found](const OfficialActual& o) {
        Event* e = o.value.has() ? found() : nullptr;
        if (!e || e->actual.has()) return;
        e->actual = o.value; e->actualSource = o.note.section('\n', 0, 0);
        e->actualDetail = o.note.section('\n', 1); e->actualAt = QDateTime::currentDateTimeUtc();
        e->quality = 9; e->also.clear();
        if (o.revised.has()) setRevised(*e, o.revised, sourceLabel(*e));
        saveWeek(w);
        emit actualArrived(*e);
        emit weekLoaded(w);
      });
    // a figure a wire or a feed gave: the statistics office's own series is its second source
    static QSet<QString> confirmed;   // compared once (the series kept its figure): not asked again this session
    if (ev.actual.num() && ev.quality != 9 && int(ev.impact) >= int(Impact::Medium) && ev.time > now.addDays(-8) &&
        !confirmed.contains(key) && history.canOfficial(ev)) {
      Event bare = ev;
      bare.actual = {};
      history.officialActual(bare, this, [this, w, found](const OfficialActual& o) {
        Event* e = o.value.has() ? found() : nullptr;
        if (!e || !e->actual.num() || e->quality == 9) return;
        confirmed.insert(e->key);
        QString office = o.note.section('\n', 0, 0);
        office.remove(QStringLiteral(" official series"));
        bool changed = false;
        if (sameFigure(o.value, e->actual)) {
          changed = addAlso(*e, office);
          if (e->other.startsWith(office + " ")) { e->other.clear(); changed = true; }
        } else if (const QString other = office + " " + o.value.text; e->other != other) { e->other = other; changed = true; }
        if (changed) touch(w);
      });
    }
    if (history.hasOfficialDetails(ev)) {
      bool have = false;
      for (const Detail& d : ev.details) have |= d.source == history.officialDetailsSource(ev);
      if (!have)
        history.officialDetails(ev, this, [this, w, found](const QList<Detail>& list) {
          Event* e = list.isEmpty() ? nullptr : found();
          if (!e) return;
          for (const Detail& d : list) {
            bool known = false;
            for (const Detail& o : e->details) known |= o.name == d.name;
            if (!known) e->details << d;
          }
          touch(w);
        });
    }
  }
}

QList<Event> Calendar::missingResults(int lateSec, const QDateTime& nowUtc) const {
  QList<Event> out;
  const QDateTime now = nowUtc.isValid() ? nowUtc : QDateTime::currentDateTimeUtc();
  for (const QList<Event>& week : m_weeks)
    for (const Event& e : week) {
      if (e.actual.has() || e.allDay || e.impact == Impact::Holiday || int(e.impact) < int(Impact::Medium) || e.isSpeech()) continue;
      if (!e.forecast.num() && !e.previous.num()) continue;   // a text, not a figure
      const qint64 age = e.time.secsTo(now);
      if (age >= lateSec && age <= 24 * 3600) out << e;
    }
  return out;
}

// The statistics offices answer within minutes of a release: asked 2, 5, 10, 20 and 40 minutes after each one that no
// headline and no feed has filled by then.
void Calendar::planOfficial() {
  const QDateTime now = QDateTime::currentDateTimeUtc();
  QDateTime next;
  for (const Event& e : m_weeks.value(currentWeek())) {
    if (e.actual.has() || e.allDay || !History::instance().canOfficial(e)) continue;
    for (const int s : {120, 300, 600, 1200, 2400}) {
      const QDateTime at = e.time.addSecs(s);
      if (at > now.addSecs(1) && (!next.isValid() || at < next)) { next = at; break; }
    }
  }
  if (next.isValid()) m_official.start(int(std::min<qint64>(now.msecsTo(next), 24LL * 3600 * 1000)));
  else m_official.stop();
}

void Calendar::startBackground() {
  static QObject* owner = nullptr;
  if (owner) return;
  owner = new QObject(this);
  setActive(owner, true);                 // Myfxbook for the whole session, at most every 5 min
  buildReleaseIndex();                    // ready by the time a row is expanded
  const QDate cur = currentWeek();
  load(cur);
  load(cur.addDays(-7));                  // last week's figures published while Deepwater was closed
  connect(this, &Calendar::weekLoaded, this, [this](const QDate& w) {
    static QSet<QDate> done;              // official series once per week and session (cached 3 h by net::)
    if (w >= currentWeek().addDays(-7) && w <= currentWeek() && !done.contains(w) && hasData(w)) { done.insert(w); fillOfficial(w); }
    if (w == currentWeek()) planOfficial();
  });
  m_official.setSingleShot(true);
  connect(&m_official, &QTimer::timeout, this, [this] { fillOfficial(currentWeek()); planOfficial(); });
  auto* hourly = new QTimer(this);
  hourly->setInterval(60 * 60 * 1000);
  connect(hourly, &QTimer::timeout, this, [this] { fillOfficial(currentWeek()); });
  hourly->start();
}

// ------------------------------------------------------------------------------------------------ provenance text
QString actualSourceText(const Event& e) {
  const QString s = e.actualSource;
  if (s.endsWith(" official series")) return QCoreApplication::translate("news", "%1 official series").arg(s.chopped(16));
  if (s.endsWith(" headline")) return QCoreApplication::translate("news", "%1 headline").arg(s.chopped(9));
  return s;
}
QString revisedText(const Event& e) {
  if (!e.revised.has()) return {};
  return QCoreApplication::translate("news", "Previous revised to %1, first published %2 (%3).").arg(e.revised.text, e.previous.text, e.revisedSource);
}
QString sourceOf(const Event& e) { return sourceLabel(e); }
QString alsoText(const Event& e) {
  if (e.also.isEmpty() || !e.actual.has()) return {};
  return QCoreApplication::translate("news", "Same figure from a second source: %1.").arg(e.also.join(", "));
}
QString otherText(const Event& e) {
  if (e.other.isEmpty()) return {};
  return QCoreApplication::translate("news", "Sources disagree: %1 gives %2 here, against %3.").arg(sourceLabel(e), e.actual.text, e.other);
}
QString detailsText(const Event& e) {
  QStringList parts;
  for (const Detail& d : e.details) {
    QStringList in;
    if (!d.forecast.isEmpty()) in << QCoreApplication::translate("news", "forecast %1").arg(d.forecast);
    if (!d.previous.isEmpty()) in << QCoreApplication::translate("news", "previous %1").arg(d.previous);
    if (!d.revised.isEmpty()) in << QCoreApplication::translate("news", "revised %1").arg(d.revised);
    parts << QCoreApplication::translate("news", d.name.toUtf8().constData()) + " " + d.actual + (in.isEmpty() ? QString() : " (" + in.join(", ") + ")");
  }
  if (parts.isEmpty()) return {};
  return QCoreApplication::translate("news", "Published with it: %1.").arg(parts.join(" · "));
}

QString actualDetailText(const Event& e) {
  QString d = e.actualDetail;
  if (d.isEmpty()) return d;
  if (d.startsWith("Myfxbook economic calendar RSS: "))
    return QCoreApplication::translate("news", "Myfxbook economic calendar RSS: %1").arg(d.mid(32));
  const QString checked = " (previous checked against the calendar)";
  if (!d.endsWith(checked)) return d;   // a headline: the source's own text
  d.chop(checked.size());
  // "<series>, <period> = <value>"
  const int eq = d.lastIndexOf(" = "), comma = d.lastIndexOf(", ", eq);
  if (eq < 0 || comma < 0) return d;
  const QString series = d.left(comma), period = d.mid(comma + 2, eq - comma - 2), value = d.mid(eq + 3);
  QString per = period;
  const QDate pd = QLocale(QLocale::English).toDate("1 " + period, "d MMM yyyy");
  if (pd.isValid()) per = i18n::locale().toString(pd, "MMM yyyy");
  else if (period.startsWith('Q')) per = QCoreApplication::translate("news", "Q%1 %2").arg(period.mid(1, 1), period.mid(3));
  return QCoreApplication::translate("news", "%1, %2 = %3 (previous checked against the calendar)")
      .arg(QCoreApplication::translate("news", series.toUtf8().constData()), per, value);
}
}
