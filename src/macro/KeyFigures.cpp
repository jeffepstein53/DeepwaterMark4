#include "macro/KeyFigures.h"
#include "macro/CentralBanks.h"
#include "macro/MacroStore.h"
#include "macro/National.h"
#include "macro/Registry.h"
#include "news/Calendar.h"
#include "core/I18n.h"
#include <QCoreApplication>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>

namespace macro {
using check::Item;
using check::Reading;

namespace {
const QStringList kCurrencies{"USD", "EUR", "GBP", "JPY", "CHF", "CAD", "AUD", "NZD"};

QString tx(const char* s) { return QCoreApplication::translate("checks", s); }

double valueAt(const Series& s, const QDate& d) {
  for (int k = s.pts.size() - 1; k >= 0; --k) {
    if (s.pts[k].date == d) return s.pts[k].value;
    if (s.pts[k].date < d) break;
  }
  return NAN;
}

// The other compilation measures the same thing. Core inflation: each office has its own definition.
bool comparable(const QString& iso, Ind i, const QString& source) {
  switch (i) {
    case Ind::Inflation: return source == "BIS";
    case Ind::Unemployment: return source == "OECD" && iso != "ch";   // registered (SECO) against the survey
    case Ind::CoreInflation:
      if (iso == "us" || iso == "ca") return source == "OECD";
      if (iso == "ez") return source == "ECB";
      return false;
    default: return false;
  }
}

// The same statistic one period earlier, for the "previous" of an event; not for a second estimate of the same period.
bool isSecondEstimate(const QString& title) {
  static const QRegularExpression rx("\\b(prelim|final|flash|advance|revised|second)\\b", QRegularExpression::CaseInsensitiveOption);
  return rx.match(title).hasMatch();
}

// The event that states each bank's policy rate, and what to add to it for the rate the app shows (the ECB's deposit
// facility is 15 bp under its main refinancing rate since September 2024).
struct RateEvent { const char* title; double add; };
const QHash<QString, RateEvent>& rateEvents() {
  static const QHash<QString, RateEvent> h{
      {"USD", {"Federal Funds Rate", 0}}, {"EUR", {"Main Refinancing Rate", -0.15}}, {"GBP", {"Official Bank Rate", 0}},
      {"JPY", {"BOJ Policy Rate", 0}},    {"CHF", {"SNB Policy Rate", 0}},           {"CAD", {"Overnight Rate", 0}},
      {"AUD", {"Cash Rate", 0}},          {"NZD", {"Official Cash Rate", 0}}};
  return h;
}
QDate eventDay(const QString& ccy, const QDateTime& utc) {
  const bool east = ccy == "JPY" || ccy == "AUD" || ccy == "NZD";
  return utc.toUTC().addSecs(east ? 9 * 3600 : 0).date();
}

// The calendar's figures of one indicator, newest first.
struct CalendarReading { QDate period; double value; QString source; QDateTime at; bool previous; QStringList also; QString other; char freq; bool official; };

void statistic(Item& it, const QString& iso, Ind ind, const QList<CalendarReading>& cal, const QDateTime& now) {
  const auto& m = MacroStore::instance();
  const Series s = m.series(iso, ind);
  if (!s.ok()) return;
  it.value = s.last().value; it.period = s.last().date; it.freq = s.freq; it.source = s.source;
  const bool fromCalendar = it.source.endsWith(QCoreApplication::translate("economy", "%1 (calendar)").arg(QString()).trimmed());
  const bool fromOffice = !fromCalendar && it.source != "BIS" && it.source != "OECD";
  const bool gdp = ind == Ind::GdpQoq;
  // the compilations
  for (const Series& o : m.otherReadings(iso, ind)) {
    if (o.source == s.source || o.freq != s.freq) continue;
    if (!gdp && !comparable(iso, ind, o.source)) continue;
    const QDate d = qMin(o.last().date, it.period);
    if (d.addMonths(6) < it.period) continue;   // it stopped long ago
    Reading r{o.source, valueAt(o, d), valueAt(s, d), d};
    if (gdp && fromCalendar) { r.judged = false; r.note = tx("earlier estimate"); }
    if (std::isfinite(r.value) && std::isfinite(r.ours)) it.others << r;
  }
  // the release calendar: the figure the day it came out, or the "previous" of the next release
  QSet<QString> seen;
  for (const CalendarReading& c : cal) {
    if (c.freq != s.freq) continue;
    if (c.period > it.period && !c.previous) it.due = qMax(it.due, c.period);
    if (c.period > it.period || c.period.addMonths(s.freq == 'Q' ? 3 : 1) < it.period) continue;
    const double ours = valueAt(s, c.period);
    if (!std::isfinite(ours)) continue;
    const bool same = (!c.source.isEmpty() && it.source.startsWith(c.source)) || (c.official && fromOffice);   // the office's own series
    const QString name = c.previous ? tx("calendar, next release") : c.source;
    const QString key = name + c.period.toString(Qt::ISODate);
    if (!same && !seen.contains(key)) {
      seen.insert(key);
      it.others << Reading{name, c.value, ours, c.period, true, {}, gdp ? 0.15 : 0.06};
    }
    if (c.period != it.period || c.previous) continue;
    for (const QString& a : c.also)
      if (!it.source.startsWith(a) && !seen.contains(a)) { seen.insert(a); it.others << Reading{a, c.value, ours, c.period, true, {}, gdp ? 0.15 : 0.06}; }
    if (!c.other.isEmpty()) {
      const qsizetype sp = c.other.lastIndexOf(' ');
      const news::Value v = news::parseValue(c.other.mid(sp + 1));
      const QString src = c.other.left(sp);
      if (v.num() && v.suffix == "%" && !it.source.startsWith(src)) it.others << Reading{src, v.v, ours, c.period, true, {}, gdp ? 0.15 : 0.06};
    }
  }
  // nothing newer for far longer than the statistic's own rhythm
  const int age = int(it.period.daysTo(now.date()));
  if (!it.due.isValid() && age > (s.freq == 'Q' ? 265 : 140)) it.due = it.period.addMonths(s.freq == 'Q' ? 3 : 1);
}
}  // namespace

KeyFigures& KeyFigures::instance() { static KeyFigures* k = new KeyFigures; return *k; }
KeyFigures::KeyFigures() {
  m_timer.setSingleShot(true);
  connect(&m_timer, &QTimer::timeout, this, &KeyFigures::recompute);
}

void KeyFigures::start() {
  if (m_started) return;
  m_started = true;
  m_from = QDateTime::currentDateTimeUtc();
  connect(&MacroStore::instance(), &MacroStore::updated, this, &KeyFigures::later);
  connect(&news::Calendar::instance(), &news::Calendar::weekLoaded, this, &KeyFigures::later);
  connect(&news::Calendar::instance(), &news::Calendar::actualArrived, this, &KeyFigures::later);
  connect(&i18n::Notifier::instance(), &i18n::Notifier::changed, this, &KeyFigures::later);
  later();
}

const Item* KeyFigures::item(const QString& ccy, const QString& figure) const {
  for (const Item& it : m_items) if (it.ccy == ccy && it.figure == figure) return &it;
  return nullptr;
}

void KeyFigures::recompute() {
  const QDateTime now = QDateTime::currentDateTimeUtc();
  auto& cal = news::Calendar::instance();
  auto& banks = CentralBanks::instance();
  const auto& store = MacroStore::instance();

  // the calendar, once: statistics by country and indicator, rate decisions by currency
  QHash<QString, QHash<int, QList<CalendarReading>>> stats;
  struct RateReading { QDateTime at; double rate; bool decided; QString source; };
  QHash<QString, QList<RateReading>> rates;
  QHash<QString, QDate> meetingDay;   // the next decision the calendar lists
  for (int w = 1; w >= -6; --w)
    for (const news::Event& e : cal.events(news::Calendar::currentWeek().addDays(7 * w))) {
      if (!rateEvents().contains(e.currency) || e.allDay && e.time > now.addDays(8)) continue;
      if (const RateEvent re = rateEvents().value(e.currency); e.title.compare(QLatin1String(re.title), Qt::CaseInsensitive) == 0) {
        if (e.time <= now && e.actual.num() && e.actual.suffix == "%") rates[e.currency] << RateReading{e.time, e.actual.v + re.add, true, news::sourceOf(e)};
        else if (e.time > now && e.previous.num() && e.previous.suffix == "%") rates[e.currency] << RateReading{now, e.previous.v + re.add, false, {}};
        if (const QDate d = eventDay(e.currency, e.time); e.time > now && (!meetingDay.contains(e.currency) || d < meetingDay[e.currency])) meetingDay[e.currency] = d;
        continue;
      }
      if (e.actual.num() && e.actual.suffix == "%" && e.time <= now) {
        const CalendarFigure f = national::flashOf(e.currency, e.title, e.actual.v, e.time, news::sourceOf(e));
        if (f.valid()) stats[f.iso2][int(f.ind)] << CalendarReading{f.period, f.value, f.source, e.time, false, e.also, e.other, f.freq, e.quality == 9};
      } else if (e.time > now && e.previous.num() && e.previous.suffix == "%" && !isSecondEstimate(e.title)) {
        const CalendarFigure f = national::flashOf(e.currency, e.title, e.previous.v, e.time, {});
        if (f.valid()) stats[f.iso2][int(f.ind)] << CalendarReading{f.period.addMonths(f.freq == 'Q' ? -3 : -1), f.value, {}, e.time, true, {}, {}, f.freq, false};
      }
    }

  QList<Item> out;
  for (const QString& ccy : kCurrencies) {
    const Economy* eco = economyForCurrency(ccy);
    const Bank* b = banks.bank(ccy);
    if (!eco || !b) continue;
    const QString iso = eco->iso2;
    QDate lastDecision;
    for (const auto& md : b->meetings) if (md.decision <= now.date()) lastDecision = md.decision;

    {  // ---- policy rate
      Item it; it.ccy = ccy; it.figure = "Policy rate"; it.freq = 'D'; it.unit = "%"; it.decimals = 2; it.tolerance = 0.01;
      it.value = b->rate; it.source = b->rateSource; it.period = b->rateAsOf;
      if (std::isfinite(b->rate)) {
        if (const Point d = store.bisDaily(iso); d.date.isValid() && !b->rateSourceEn.startsWith("BIS")) {
          Reading r{QStringLiteral("BIS"), d.value, b->rate, it.period};
          r.judged = !lastDecision.isValid() || d.date > lastDecision.addDays(1);   // before that it is the rate of before the decision
          r.note = i18n::locale().toString(d.date, "d MMM");
          it.others << r;
        }
        bool one = false;
        for (const RateReading& c : rates.value(ccy)) {   // newest week first
          if (one || (b->rateSourceEn.startsWith("Release calendar") && c.decided)) continue;
          Reading r{c.decided ? c.source : tx("calendar, next release"), c.rate, b->rate, it.period};
          r.judged = !c.decided || !lastDecision.isValid() || c.at.date() >= lastDecision.addDays(-1);
          if (!r.judged) continue;
          it.others << r; one = true;
        }
      }
      out << it;
    }
    const struct { const char* name; Ind ind; } figures[] = {
        {"CPI y/y", Ind::Inflation}, {"Core CPI y/y", Ind::CoreInflation}, {"Unemployment", Ind::Unemployment}, {"GDP q/q", Ind::GdpQoq}};
    for (const auto& f : figures) {
      Item it; it.ccy = ccy; it.figure = f.name; it.unit = "%"; it.tolerance = f.ind == Ind::GdpQoq ? 0.15 : 0.11;
      statistic(it, iso, f.ind, stats.value(iso).value(int(f.ind)), now);
      out << it;
    }
    const MeetingDate next = b->nextMeeting();
    const QList<OtherOdds> others = banks.otherOdds(b->id);
    {  // ---- what the market prices for the next meeting
      Item it; it.ccy = ccy; it.figure = "Odds"; it.unit = "bp"; it.decimals = 1; it.tolerance = 4; it.freq = 'D'; it.each = true;
      it.value = b->priced ? b->impliedBp : NAN; it.source = b->pricingSource; it.period = next.decision;
      // the wire's pictures sharpen the computed odds and are read one after the other: without this bank's, the first minutes say nothing
      bool settled = m_from.isValid() && m_from.secsTo(now) > 600;
      for (const OtherOdds& o : others) settled |= o.source == "FinancialJuice";
      for (const OtherOdds& o : others) {
        Reading r{o.source, o.bp, it.value, it.period};
        r.judged = o.fresh && !o.used && settled;
        r.note = i18n::locale().toString(o.at.toLocalTime(), o.at.daysTo(now) > 0 ? "d MMM" : "HH:mm");
        // it counts from the official rate, the market from the overnight rate, a few bp away: the same rate expected is the same answer
        if (const double apart = o.level * 100 - (b->overnight * 100 + it.value); std::isfinite(apart) && std::fabs(apart) < std::fabs(o.bp - it.value)) {
          r.gap = apart;
          if (std::fabs(apart) <= it.tolerance && std::fabs(o.bp - it.value) > it.tolerance)
            r.note += ", " + tx("same rate expected: %1%").arg(i18n::locale().toString(o.level, 'f', 2));
        }
        it.others << r;
      }
      out << it;
    }
    {  // ---- its date
      Item it; it.ccy = ccy; it.figure = "Meeting"; it.tolerance = 0; it.freq = 'D'; it.decimals = 0;
      if (next.decision.isValid()) {
        it.value = double(next.decision.toJulianDay()); it.period = next.decision;
        it.source = next.verified ? tx("bank's schedule") : tx("secondary source");
        if (meetingDay.contains(ccy)) it.others << Reading{tx("calendar"), double(meetingDay[ccy].toJulianDay()), it.value, it.period};
        for (const OtherOdds& o : others)
          if (o.meeting.isValid()) it.others << Reading{o.source, double(o.meeting.toJulianDay()), it.value, it.period, o.fresh};
      }
      out << it;
    }
  }
  for (Item& it : out) check::judge(it);
  m_items = out;
  m_at = now;
  emit changed();
}

// ------------------------------------------------------------------------------------------------ texts
QString checkValue(const Item& it, double v) {
  QString t = check::figureText(it, v);
  if (t.endsWith(" bp")) t = t.chopped(2) + tx("bp");
  return t;
}
QString checkSource(const QString& s) {
  if (s == "BIS") return tx("BIS");
  if (s == "OECD") return tx("OECD");
  if (s == "ECB") return tx("ECB");
  return s;
}
QString checkFigureName(const QString& f) {
  if (f == "Policy rate") return tx("Policy rate");
  if (f == "CPI y/y") return tx("Inflation (12 months)");
  if (f == "Core CPI y/y") return tx("Core inflation (12 months)");
  if (f == "Unemployment") return tx("Unemployment");
  if (f == "GDP q/q") return tx("GDP (quarter)");
  if (f == "Odds") return tx("Next meeting, priced");
  if (f == "Meeting") return tx("Next meeting, date");
  return f;
}
QString checkVerdictName(check::Verdict v) {
  switch (v) {
    case check::Verdict::Agree: return tx("CONFIRMED");
    case check::Verdict::Differ: return tx("DIFFERS");
    case check::Verdict::Alone: return tx("ONE SOURCE");
    case check::Verdict::Late: return tx("LATE");
    case check::Verdict::Missing: return tx("MISSING");
  }
  return {};
}
QString checkPeriod(const Item& it) {
  if (!it.period.isValid()) return QStringLiteral("—");
  return it.freq == 'D' ? i18n::locale().toString(it.period, "d MMM yyyy") : formatPeriod(it.period, it.freq);
}
QString checkOthers(const Item& it) {
  QStringList out;
  for (const Reading& r : it.others) {
    QString t = checkSource(r.source) + " " + checkValue(it, r.value);
    QStringList in;
    if (r.period != it.period && it.freq != 'D') in << formatPeriod(r.period, it.freq);
    if (!r.note.isEmpty()) in << r.note;
    if (!in.isEmpty()) t += " (" + in.join(", ") + ")";
    if (!out.contains(t)) out << t;
  }
  return out.join(" · ");
}
QString checkTip(const QString& ccy, const QString& figure) {
  const Item* it = KeyFigures::instance().item(ccy, figure);
  if (!it) return {};
  QStringList t;
  if (const QString l = checkLine(*it); !l.isEmpty()) t << l;
  if (const QString o = checkOthers(*it); !o.isEmpty() && it->verdict != check::Verdict::Agree) t << tx("Other sources: %1").arg(o);
  return t.join('\n');
}
bool checkDiffers(const QString& ccy, const QString& figure) {
  const Item* it = KeyFigures::instance().item(ccy, figure);
  return it && it->verdict == check::Verdict::Differ;
}
QString checkLine(const Item& it) {
  QString against = it.against;
  for (const Reading& r : it.others) against.replace(r.source + " ", checkSource(r.source) + " ");
  if (it.unit == "bp") against.replace(" bp", " " + tx("bp"));
  const QString p = it.freq == 'D' || !it.on.isValid() ? QString() : QString(" (" + formatPeriod(it.on, it.freq) + ")");
  switch (it.verdict) {
    case check::Verdict::Differ: {
      double ours = it.value;
      for (const Reading& r : it.others) if (r.period == it.on && std::isfinite(r.ours)) ours = r.ours;
      return tx("Two sources differ: %1 here, %2").arg(checkValue(it, ours), against) + p;
    }
    case check::Verdict::Agree: {
      QStringList who;
      for (const Reading& r : it.others) if (r.judged && r.period == it.on && !who.contains(checkSource(r.source)) && !it.against.contains(r.source + " ")) who << checkSource(r.source);
      return tx("Confirmed by a second source: %1").arg(who.join(", ")) + p + (against.isEmpty() ? QString() : " · " + tx("apart: %1").arg(against));
    }
    case check::Verdict::Late: return tx("A newer figure has been published: this one is %1").arg(checkPeriod(it));
    case check::Verdict::Missing: return tx("No figure");
    case check::Verdict::Alone: return {};
  }
  return {};
}
}
