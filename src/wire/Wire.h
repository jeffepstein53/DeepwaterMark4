#pragma once
#include <QVector>
#include <QObject>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QTimer>
#include <cmath>

// wire::Wire — the app's real-time wire (News › Squawk, Global › Overview's Squawk panel, Session Recap).
//
// Three kinds of items, newest first, timestamped to the second:
//   Headline  ("DATA" tab)     RSS headlines: FinancialJuice (60 s), investingLive, Fed, ECB, BoE press.
//   Mover     ("MOVERS" tab)   price moves computed from market::QuoteHub: an item each time an instrument's
//                              change vs previous close crosses a multiple of its class threshold
//                              (indices 1 %, stocks 2 %, commodities 2 %, crypto 3 %, FX majors 0.5 %).
//   Release   ("RELEASES" tab) economic releases from the calendar (wire::ReleaseSource) once an actual is
//                              published: "US CPI m/m Actual 0.3% (Forecast 0.2%, Previous 0.4%)".
// Every item is archived in paths::data("wire")/YYYY-MM-DD.jsonl (UTC day), kept 70 days, so past sessions
// can be rebuilt with between().
//
// Use:
//   auto& w = wire::Wire::instance();
//   w.setActive(this, true);                 // in showEvent; false in hideEvent (polls fast while a consumer shows)
//   connect(&w, &wire::Wire::changed, this, [&] { auto list = w.items(wire::Filter::All, 50); … });
//   connect(&w, &wire::Wire::moversChanged, …); w.movers();   // live price-move snapshot (ticker)
namespace wire {

enum class Kind { Headline, Mover, Release };
enum class Filter { All, Data, Movers, Releases };   // DATA = headlines (the reference's naming)

struct Item {
  QString id;            // unique: "<source>:<guid>", "mv:<symbol>:<yyyyMMdd>:<level>", "rel:<release id>"
  QDateTime time;        // UTC, to the second
  Kind kind = Kind::Headline;
  QString text;          // headline as displayed (source prefix removed)
  QString source;        // "FinancialJuice", "investingLive", "Federal Reserve", "ECB", "Bank of England",
                         // "Deepwater" (movers, computed), calendar attribution (releases)
  QString url;           // link to the article when the feed gives one
  // Mover
  QString symbol;        // app symbol (market::instruments)
  double changePct = NAN;  // change vs previous close when detected
  double price = NAN;      // price when detected
  // Release
  QString currency;
  int impact = 0;        // 1 low, 2 medium, 3 high
  QString actual, forecast, previous;
};

// Live price-move snapshot (one per instrument currently beyond its threshold), for tickers.
struct Move {
  QString symbol, name;
  double changePct = NAN, price = NAN;
  int decimals = 2;
  QDateTime since;       // when the current level was first crossed (the mover item's time)
};

class Wire : public QObject {
  Q_OBJECT
public:
  static Wire& instance();

  // Consumers register while visible: feeds are polled at their fast pace and QuoteHub watches movers.
  // With no visible consumer the headline feeds still refresh every 10 min so the archive stays complete.
  void setActive(QObject* owner, bool active);

  // Polls now every feed whose last request is older than its active pace (never faster: the pace is what each
  // publisher asks for). For the tools Claude reads with: with no page shown the feeds only follow their idle pace
  // (10 to 30 min), so a tool would otherwise answer with items of a quarter of an hour ago.
  void refreshNow();
  bool busy() const { return m_inflight > 0; }

  // Live items (last 24 h, at least the newest 150), newest first. max < 0: all.
  QList<Item> items(Filter f = Filter::All, int max = -1) const;
  int count(Filter f) const;
  QVector<int> counts() const;      // count(All), count(Data), count(Movers), count(Releases) in one go (ALL's list built once)
  // Archived items with from <= time < to (reads the day files it needs), newest first.
  QList<Item> between(const QDateTime& from, const QDateTime& to) const;
  // Instruments beyond their threshold right now, most recent level first.
  QList<Move> movers() const;
  double threshold(const QString& symbol) const;   // % threshold of the symbol's class, NAN if not watched

  QDateTime lastReceived() const { return m_lastReceived; }   // newest item time received this run
  QDateTime lastFetch() const { return m_lastFetch; }         // last successful feed fetch
  bool releasesConnected() const;                              // a calendar feeds the RELEASES part
  QStringList feedErrors() const;                              // "investingLive: HTTP 503" …

signals:
  void changed();                         // items added (any kind)
  void itemsAdded(const QList<wire::Item>& fresh);
  void moversChanged();                   // live move snapshot changed (prices or set)
  void settled();                         // every feed request started has come back

private:
  Wire();
  void settle();
  int m_inflight = 0;
  struct Feed { QString id, name, url; int activeSec, idleSec; QDateTime last; QDateTime due; QString error; bool busy = false; };
  void schedule();
  void fetch(Feed& f);
  void ingest(const QList<Item>& fresh);
  void onQuotes(const QStringList& symbols);
  void onReleases();
  void loadDay(const QDate& d) const;
  void append(const QList<Item>& items);
  void prune();
  bool anyActive() const;

  QList<Feed> m_feeds;
  QTimer m_timer;
  QHash<QObject*, bool> m_owners;
  QList<Item> m_live;                     // newest first, last 3 days
  mutable QHash<QDate, QList<Item>> m_days;   // archive cache by UTC day (newest first in each)
  mutable QSet<QString> m_ids;
  QHash<QString, int> m_level;            // symbol → signed level already emitted (removed when back at 0)
  QHash<QString, QDateTime> m_moveSince;  // symbol → time of its latest mover item
  QStringList m_universe;
  QDateTime m_lastReceived, m_lastFetch;
};

QString kindName(Kind k);                 // "DATA" / "MOVERS" / "RELEASES" in the interface language
QString releaseText(const QString& currency, const QString& title, const QString& actual,
                    const QString& forecast, const QString& previous);
}
