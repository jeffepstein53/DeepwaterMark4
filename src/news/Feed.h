#pragma once
#include <QObject>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QImage>
#include <functional>

// News aggregation from public RSS/Atom feeds (News › News Feed; usable by Squawk, Session Recap, Overview).
//
//   auto& feed = news::Feed::instance();
//   connect(&feed, &news::Feed::updated, this, [&] { show(feed.stories()); });
//   feed.setActive(this, isVisible());          // polling only while at least one owner is active
//
// Each story keeps what its feed gives: headline, summary, body HTML when the feed carries it (investinglive),
// image (enclosure / media:content), source, author, time and link. Topics and impact are computed by Deepwater
// with keyword rules (classify()). Stories are archived for 4 days under paths::data("news/feed").
namespace news {

enum Topic : quint32 {
  CentralBank = 1, Inflation = 2, Employment = 4, Gdp = 8, Pmi = 16, RetailSales = 32, Earnings = 64
};
QList<Topic> allTopics();
QString topicName(Topic t);              // "CENTRAL BANK"

enum class Impact3 { None = 0, Important = 1, High = 2 };

struct Story {
  QString id;            // link (or guid)
  QString title;
  QString summary;       // plain text
  QString contentHtml;   // sanitised body HTML from the feed (p, h2–h4, lists, b/i, links), may be empty
  QString image;         // image URL from the feed, may be empty
  QString sourceId;      // "investinglive"
  QString source;        // "INVESTINGLIVE" (display)
  QString author;
  QDateTime time;        // UTC
  QString link;
  quint32 topics = 0;
  Impact3 impact = Impact3::None;
  int score = 0;         // impact score behind `impact` (higher = more market-moving)
  int market = 0;        // market relevance (macro, central banks, rates, FX, commodities, indices words)
  bool lifestyle = false;// personal finance / lifestyle / stock-picking promotion: kept out of the feed's lists
  Topic primaryTopic() const;   // first topic set, or 0
  bool has(Topic t) const { return topics & t; }
};

struct FeedSource {
  QString id;            // "investinglive"
  QString name;          // "INVESTINGLIVE"
  QString site;          // "investinglive.com"
  QStringList urls;
  int pollSec;
};

// Topic and impact rules. Exposed so other pages tag their own headlines the same way.
void classify(Story& s);

class Feed : public QObject {
  Q_OBJECT
public:
  static Feed& instance();
  const QList<FeedSource>& sources() const;
  QList<Story> stories() const { return m_stories; }      // newest first
  const Story* find(const QString& id) const;
  QDateTime lastRefresh() const { return m_lastRefresh; }
  QString sourceError(const QString& sourceId) const {   // the first failing feed URL of the source; empty when none
    for (auto it = m_errors.cbegin(); it != m_errors.cend(); ++it) if (it.key().first == sourceId) return it.value();
    return {};
  }
  void setActive(QObject* owner, bool active);
  static QStringList defaultSourcesOff();   // settings key news.feed.sourcesOff
  void refresh();
  bool busy() const { return m_inflight > 0; }   // feed downloads on the wire (the tools Claude reads with wait for them all)
signals:
  void updated();
  void settled();    // every download started has come back (answer, error or timeout)
private:
  Feed();
  void poll(bool force);
  void fetch(const FeedSource& s, const QString& url);
  void settle();
  int m_inflight = 0;
  void merge(const QList<Story>& in);
  void save();
  QList<Story> m_stories;
  QHash<QString, int> m_index;
  QHash<QString, QDateTime> m_lastPoll;     // per url
  QHash<QPair<QString, QString>, QString> m_errors;   // (source id, feed URL) → last error
  QSet<QObject*> m_active;
  QDateTime m_lastRefresh;
  QTimer m_timer, m_saveTimer;
};

// Async image download (cached 7 days) and decode off the GUI thread; cb runs on the GUI thread with a null
// image on failure. Repeated requests for the same URL share one download.
void loadImage(const QString& url, QObject* ctx, std::function<void(const QImage&)> cb);
}
