#pragma once
#include <QObject>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QList>
#include <cmath>

// Economic calendar service (News › Calendar; also for Squawk, Session Recap, Overview, Fundamental Heatmap).
//
//   auto& cal = news::Calendar::instance();
//   connect(&cal, &news::Calendar::weekLoaded, this, [&](QDate w) { show(cal.events(w)); });
//   connect(&cal, &news::Calendar::actualArrived, this, [](const news::Event& e) { ... });
//   cal.load(news::Calendar::weekStart(QDate::currentDate()));
//   cal.setActive(this, isVisible());          // live polling only while at least one owner is active
//
// Sources (declared in Calendar.cpp):
//   - ForexFactory weekly export (nfs.faireconomy.media): time, currency, impact, forecast (consensus), previous.
//     Only the current week (and the next one once published) exist there.
//   - Myfxbook economic calendar RSS: actual values of recent releases, recorded as they come.
// Weeks are recorded under paths::data("news/calendar"), so past weeks stay available and the recorded actuals
// build each indicator's "recorded calendar releases" history. Economists' min/max range and bank forecasts are
// kept in the model (Event::low/high/banks) but no free source fills them yet: they stay empty ("—").
namespace news {

enum class Impact { Holiday = 0, Low = 1, Medium = 2, High = 3 };
QString impactName(Impact i);                 // "Low", "Medium", "High", "Holiday"

// One published figure, kept as the source wrote it ("3.1%", "30.3K", "-4.10", "4.095%").
struct Value {
  QString text;          // empty when the source has none
  double v = NAN;        // number in the unit of `text` (3.1 for "3.1%", 30.3 for "30.3K")
  QString suffix;        // "%", "K", "M", "B", "T" or ""
  int decimals = 0;
  bool has() const { return !text.isEmpty(); }
  bool num() const { return std::isfinite(v); }
  double scaled() const; // v times the suffix multiplier (K = 1e3 …); % stays as is
};
Value parseValue(const QString& s);
QString formatLike(double v, const Value& like, bool sign = false);   // same unit/decimals as `like`

struct BankForecast {
  QString bank;          // "ING"
  Value value;
  QString source;        // "ING THINK — Key events in developed markets next week"
  QString url;           // document to open
  QDate published;
};

struct Event {
  QString key;           // unique id: "<ccy>|<title>|<utc time>"
  QString seriesKey;     // same indicator across weeks: "<ccy>|<title>"
  QDateTime time;        // UTC
  bool allDay = false;   // "All Day" / "Tentative": only the date is known
  QString currency;      // "CAD", "ALL" for global events
  QString title;         // as published ("CPI y/y")
  QString name;          // without the period suffix ("CPI")
  QString period;        // "ANNUAL" / "MONTHLY" / "QUARTERLY" / "" (parsed from the source's suffix)
  QString periodRaw;     // the suffix it came from: "y/y", "m/m", "q/q"
  Impact impact = Impact::Low;
  Value actual, forecast, previous;
  Value low, high;       // economists' range (min/max); empty when no source gives it
  QList<BankForecast> banks;
  QString forecastSource = "ForexFactory";   // who published forecast/previous
  QString actualSource;  // who gave the actual: "Myfxbook", "investingLive headline", "OECD official series" …
  QString actualDetail;  // the headline or the official observation it was read from (tooltips)
  QDateTime actualAt;    // when Deepwater recorded the actual (UTC)

  double surprise() const;        // actual − forecast in the actual's unit; NaN when one is missing
  QString surpriseText() const;   // "+10.9K", "-0.1%", "" when none
  int direction() const;          // +1 actual > previous, -1 below, 0 equal / unknown
  bool released() const { return actual.has(); }
  bool isSpeech() const;
  bool hasRange() const { return low.num() && high.num(); }
};

// Provenance of an actual in the interface language (stored in English on disk): "BLS official series" →
// "série officielle BLS", detail lines with their series name and period translated.
QString actualSourceText(const Event& e);
QString actualDetailText(const Event& e);

class Calendar : public QObject {
  Q_OBJECT
public:
  static Calendar& instance();
  static QDate weekStart(const QDate& anyDay);        // the Sunday starting that day's week
  static QDate currentWeek();                          // this week (weeks are Sunday→Saturday in New York time,
                                                       // like the ForexFactory export)

  void load(const QDate& weekStart);                  // async; emits weekLoaded when known (may be empty)
  QList<Event> events(const QDate& weekStart) const;  // sorted by time
  bool hasData(const QDate& weekStart) const;
  QString weekNote(const QDate& weekStart) const;     // why a week is empty ("not published yet" …)
  QList<QDate> recordedWeeks() const;
  QDateTime lastRefresh() const { return m_lastRefresh; }   // last successful download (UTC)
  void refresh();                                      // re-download now (paced by net::)
  // One poll of the actuals feed now; `actualsPolled` follows. A copy younger than 5 min is served without a request
  // (the source asks for one per 5 min at most; news/ActualsPace.h).
  void refreshActuals() { fetchMyfxbook(); }
  QDateTime lastActuals() const { return m_lastActuals; }   // when the actuals feed was last downloaded (UTC)

  // Live polling (actuals) while at least one owner is active.
  void setActive(QObject* owner, bool active);
  // Runs for the whole app session (started at launch by news/WireBridge.cpp): records the week, polls
  // actuals, reads the wire's data headlines and the official series.
  void startBackground();

  // Actual from a data headline (wire). Returns true when it matched one event and was recorded. The event must
  // share the currency and the indicator words, be released at most 5 min after / 20 min before the headline,
  // use the same unit and agree on the stated forecast or prior; ambiguous matches are dropped.
  bool applyHeadline(const struct DataHeadline& h, const QDateTime& time, const QString& source, const QString& text);
  // Actuals from official series for the past events of a week that still have none (async).
  void fillOfficial(const QDate& weekStart);

  // Description of an indicator, written by Deepwater from the indicator type (two paragraphs).
  static QStringList describe(const Event& e);
  // Country name for a currency ("CAD" → "Canada", "EUR" → "Euro area").
  static QString countryOf(const QString& ccy);
  // Every recorded release of one indicator (seriesKey), oldest first: date (UTC) and actual.
  QList<QPair<QDateTime, Value>> recordedReleases(const QString& seriesKey) const;

signals:
  void weekLoaded(const QDate& weekStart);
  void actualArrived(const news::Event& e);
  void refreshed();
  void actualsPolled();                                // an actuals poll came back (answer, cached copy or error)

private:
  Calendar();
  void fetchForexFactory(const QString& which);       // "thisweek" / "nextweek"
  void fetchMyfxbook();
  void planPoll();                                     // sets the timer: 5 min after the last request, or just after a release (news/ActualsPace.h)
  QList<QDateTime> awaitedReleases() const;            // medium / high impact figures of this week still to come, or out for less than ten minutes
  void mergeWeek(const QDate& week, const QList<Event>& fresh);
  void readWeek(const QDate& week);
  void saveWeek(const QDate& week);
  struct Match { QDate week; int index = -1; QString key; double score = 0; };   // a Myfxbook item → one event (key)
  Match matchActual(const QString& ccy, const QDateTime& t, const QString& name, const Value& previous,
                    const Value& consensus);
  void applyActual(const QString& name, const Match& m, const Value& actual);
  void schedule();

  QHash<QDate, QList<Event>> m_weeks;
  QSet<QDate> m_read;
  struct Note { const char* text = nullptr; QString arg; };   // English template (QT_TRANSLATE_NOOP) + argument
  QHash<QDate, Note> m_notes;
  // Releases (actual known) of the recorded week files, by week then series key, read once on a worker: expanding a
  // calendar row re-parsed every week file on the GUI thread. Weeks loaded in m_weeks take precedence (they may have
  // changed since); until the index is ready recordedReleases reads the files as before.
  using Releases = QHash<QString, QList<QPair<QDateTime, Value>>>;
  void buildReleaseIndex() const;
  mutable QHash<QDate, Releases> m_releaseIndex;
  mutable bool m_indexStarted = false, m_indexReady = false;
  QDateTime m_lastRefresh, m_lastActuals;
  QDateTime m_lastActualsTry;                          // last request that left the machine, answered or not
  QSet<QObject*> m_active;
  QTimer m_poll;                                       // single shot, set by planPoll
};
}
