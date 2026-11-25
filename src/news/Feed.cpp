#include "core/Settings.h"
#include <QCoreApplication>
#include "news/Feed.h"
#include "net/Http.h"
#include "net/Sources.h"
#include "core/Paths.h"
#include <QXmlStreamReader>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QFile>
#include <QPointer>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QLocale>
#include <QTimeZone>
#include <QImageReader>
#include <QBuffer>

namespace news {

using sources::Access;
static const sources::Registrar r1({"investing-rss", "Investing.com", {"www.investing.com"}, Access::Keyless, 20000,
    "News RSS (all news, economy, economic indicators, forex, stock market)",
    "RSS feeds published by Investing.com for readers; headlines link back to the site; polled every 5 min", {"news/news-feed"}});
static const sources::Registrar r2({"investinglive", "investingLive (ex-ForexLive)", {"investinglive.com", "www.investinglive.com"},
    Access::Keyless, 10000, "Breaking FX news RSS with the article body",
    "Public RSS feed; robots.txt allows feeds for ordinary agents; shown with the source name and a link", {"news/news-feed"}});
static const sources::Registrar r3({"yahoo-rss", "Yahoo Finance", {"feeds.finance.yahoo.com"}, Access::Keyless, 20000,
    "Headline RSS for S&P 500, Dow, Nasdaq, EUR/USD, gold, crude", "Public RSS for personal use with attribution", {"news/news-feed"}});
static const sources::Registrar r4({"cnbc-rss", "CNBC", {"www.cnbc.com"}, Access::Keyless, 20000,
    "Top news, economy, finance and earnings RSS", "Public RSS feeds for personal, non-commercial use with attribution", {"news/news-feed"}});
static const sources::Registrar r5({"marketwatch-rss", "MarketWatch (Dow Jones)", {"feeds.content.dowjones.io"}, Access::Keyless, 20000,
    "Top stories, MarketPulse and real-time headline RSS", "Public RSS feeds for personal, non-commercial use with attribution", {"news/news-feed"}});
static const sources::Registrar r6({"financialjuice", "FinancialJuice", {"www.financialjuice.com"}, Access::Keyless, 60000,
    "Headline wire RSS", "Public RSS feed; the publisher asks for one request per minute at most", {"news/news-feed", "news/squawk"}});
static const sources::Registrar r7({"nasdaq-rss", "Nasdaq", {"www.nasdaq.com"}, Access::Keyless, 30000,
    "Markets RSS", "Public RSS; robots.txt asks a 30 s crawl delay", {"news/news-feed"}});
static const sources::Registrar r8({"fed-rss", "Federal Reserve", {"www.federalreserve.gov"}, Access::Keyless, 5000,
    "Press releases RSS", "US government work, public domain", {"news/news-feed"}});
static const sources::Registrar r9({"ecb-rss", "European Central Bank", {"www.ecb.europa.eu"}, Access::Keyless, 5000,
    "Press releases and speeches RSS", "ECB website content may be reused with the source acknowledged", {"news/news-feed"}});
static const sources::Registrar r10({"boe-rss", "Bank of England", {"www.bankofengland.co.uk"}, Access::Keyless, 5000,
    "News RSS", "Bank of England website terms: reuse with attribution (Open Government Licence for most content)", {"news/news-feed"}});
static const sources::Registrar r11({"news-images", "Feed images (publishers' CDNs)",
    {"content-media.investing.com", "i-invdn-com.investing.com", "images.mktw.net", "s.yimg.com", "media.zenfs.com",
     "image.cnbcfm.com", "investinglive.com"},
    Access::Keyless, 300, "Story images referenced by the RSS feeds (enclosure / media:content)",
    "Shown next to the story with its source, as an RSS reader does; cached 7 days", {"news/news-feed"}});

// ------------------------------------------------------------------------------------------------ topics
QList<Topic> allTopics() { return {CentralBank, Inflation, Employment, Gdp, Pmi, RetailSales, Earnings}; }
QString topicName(Topic t) {
  switch (t) {
    case CentralBank: return QCoreApplication::translate("news", "CENTRAL BANK");
    case Inflation: return QCoreApplication::translate("news", "INFLATION");
    case Employment: return QCoreApplication::translate("news", "EMPLOYMENT");
    case Gdp: return QCoreApplication::translate("news", "GDP");
    case Pmi: return QCoreApplication::translate("news", "PMI");
    case RetailSales: return QCoreApplication::translate("news", "RETAIL SALES");
    case Earnings: return QCoreApplication::translate("news", "EARNINGS");
  }
  return {};
}
Topic Story::primaryTopic() const {
  for (Topic t : allTopics()) if (topics & t) return t;
  return Topic(0);
}

void classify(Story& s) {
  using RE = QRegularExpression;
  const auto ci = RE::CaseInsensitiveOption;
  static const RE cb(R"(\b(fed(?! up)|feds|fomc|federal reserve|ecb|boe|boj|snb|rba|rbnz|boc|pboc|central banks?|bank of (england|japan|canada)|reserve bank|powell|lagarde|bailey|ueda|macklem|bullock|warsh|rate (hikes?|cuts?|decision)|interest rates?|monetary policy|policy rate|hawkish|dovish|fomc minutes|bank rate)\b)", ci);
  static const RE infl(R"(\b(inflation|cpi|pce|ppi|consumer prices?|price index|deflation|disinflation|hicp)\b)", ci);
  static const RE emp(R"(\b(jobs?|payrolls?|nonfarm|non-farm|unemployment|jobless|employment|labou?r market|wages?|hiring|layoffs?|claimant)\b)", ci);
  static const RE gdp(R"(\b(gdp|gross domestic product|economic growth|recession|economy (grew|shrank|contract\w*|expand\w*))\b)", ci);
  static const RE pmi(R"(\b(pmi|ism|purchasing managers)\b)", ci);
  static const RE retail(R"(\b(retail sales|consumer spending|retail spending)\b)", ci);
  static const RE earn(R"(\b(earnings|quarterly results|q[1-4] results|eps|quarterly profit|profit warning|guidance|beats estimates|misses estimates|revenue forecast)\b)", ci);
  static const RE decision(R"(\b(rate decision|raises rates|hikes rates|cuts rates|holds rates|leaves rates|keeps rates|rate hike|rate cut|fomc|policy meeting|minutes|press conference|basis points?|bps)\b)", ci);
  static const RE data(R"((\d[\d.,]*\s?%?\s*(vs|versus)\.?\s*[-+]?\d)|\b(expected|estimates?|forecast|consensus)\b)", ci);
  static const RE major(R"(\b(us|u\.s\.|united states|euro ?zone|euro area|uk|britain|british|japan|china|germany|canada|australia|fed|ecb|boj|boe)\b)", ci);
  const QString text = s.title + " " + s.summary.left(280);
  quint32 t = 0;
  if (cb.match(text).hasMatch()) t |= CentralBank;
  if (infl.match(text).hasMatch()) t |= Inflation;
  if (emp.match(text).hasMatch()) t |= Employment;
  if (gdp.match(text).hasMatch()) t |= Gdp;
  if (pmi.match(text).hasMatch()) t |= Pmi;
  if (retail.match(text).hasMatch()) t |= RetailSales;
  if (earn.match(s.title).hasMatch()) t |= Earnings;
  s.topics = t;
  int score = 0;
  if (t & CentralBank) score += 2;
  if (decision.match(s.title).hasMatch() && (t & CentralBank)) score += 2;
  if (t & (Inflation | Employment | Gdp)) score += 2;
  if (t & (Pmi | RetailSales)) score += 1;
  if (t & Earnings) score += 1;
  if (t && data.match(s.title).hasMatch()) score += 1;
  if (t && major.match(s.title).hasMatch()) score += 1;
  s.score = score;
  // market relevance and lifestyle / personal-finance detection (kept out of the lead and the lists)
  static const RE mkt(R"(\b(stocks?|equit(y|ies)|shares|yields?|treasur(y|ies)|bonds?|dollar|euro|yen|sterling|currenc(y|ies)|forex|fx|oil|crude|opec|gold|copper|s&p|nasdaq|dow|futures|markets?|tariffs?|recession|economy|economic|gdp|inflation|rates?|fed|ecb|boj|boe|central bank|bitcoin|earnings|guidance)\b)", ci);
  static const RE life(R"(\b(my (husband|wife|brother|sister|mother|mom|father|dad|parents|daughter|son|partner|boyfriend|girlfriend|ex)|i'?m \d\d|retire(e|es|ment)?|social security|401\(k\)|\bira\b|inheritance|estate plan|will\b.*\b(sister|brother)|moneyist|dear|should i\b|credit cards?|savings accounts?|cd rates|mortgage rates today|wedding|divorce|recipe|vacation|travel tips|millionaire|best .* to buy|stocks? to buy|buy this|should you (buy|invest)|here'?s why|what'?s the catch|dividend stocks?|growth stocks? to|passive income|motley fool|medicare|caregiv\w*|enroll\w*|tips (for|to)|ways to|your (money|finances|savings|paycheck|retirement)|how much (you|to)|nest egg|annuit\w*|financial advisers?|financial planners?|widow\w*|in-laws|stepchildren|stepkids|estranged)\b)", ci);
  static const RE life401(R"((\b401\(k|\bIRA\b|\bRoth\b))", ci);
  int m = 0; for (auto it = mkt.globalMatch(s.title + " " + s.summary.left(200)); it.hasNext(); it.next()) ++m;
  s.market = m;
  static const RE firstPersonQ(R"((^|[\s«"“])(i|i've|i'm|i’m|we|my)\b.*\?\s*$)", ci);
  QString tt = s.title; tt.replace(QChar(0x2019), '\'');
  s.lifestyle = life.match(tt).hasMatch() || life401.match(tt).hasMatch() || firstPersonQ.match(tt).hasMatch() || s.link.contains("fool.com") ||
                s.link.contains("/personal-finance/");
  s.impact = score >= 5 ? Impact3::High : score >= 2 ? Impact3::Important : Impact3::None;
}

// ------------------------------------------------------------------------------------------------ parsing
namespace {
QString decodeEntities(QString s) {
  static const QRegularExpression num(R"(&#(x?[0-9a-fA-F]+);)");
  QString out; qsizetype last = 0;
  for (auto it = num.globalMatch(s); it.hasNext();) {
    const auto m = it.next();
    QString n = m.captured(1); bool ok = false;
    const uint cp = n.startsWith('x') ? n.mid(1).toUInt(&ok, 16) : n.toUInt(&ok);
    out += s.mid(last, m.capturedStart() - last);
    if (ok) out += QString::fromUcs4(reinterpret_cast<const char32_t*>(&cp), 1);
    last = m.capturedEnd();
  }
  out += s.mid(last);
  out.replace("&nbsp;", " ").replace("&quot;", "\"").replace("&apos;", "'").replace("&#39;", "'")
     .replace("&lt;", "<").replace("&gt;", ">").replace("&rsquo;", "’").replace("&lsquo;", "‘")
     .replace("&ldquo;", "“").replace("&rdquo;", "”").replace("&mdash;", "—").replace("&ndash;", "–")
     .replace("&hellip;", "…").replace("&amp;", "&");
  return out;
}
QString plain(const QString& html) {
  static const QRegularExpression br(R"(<\s*(br|/p|/li|/h\d)\s*/?>)", QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression tags(R"(<[^>]*>)");
  static const QRegularExpression ws(R"(\s+)");
  QString s = html; s.replace(br, " "); s.remove(tags);
  return decodeEntities(s).replace(ws, " ").trimmed();
}
// Keep a small safe subset of tags for the reader.
QString sanitize(const QString& html, QString* firstImage) {
  using RE = QRegularExpression;
  static const RE drop(R"(<(script|style|iframe|noscript|form|svg|video|object)[^>]*>.*?</\1>)",
                       RE::CaseInsensitiveOption | RE::DotMatchesEverythingOption);
  static const RE img(R"(<img[^>]*\bsrc\s*=\s*["']([^"']+)["'])", RE::CaseInsensitiveOption);
  static const RE tag(R"(<(/?)([a-zA-Z0-9]+)([^>]*)>)");
  static const RE href(R"(\bhref\s*=\s*["']([^"']+)["'])", RE::CaseInsensitiveOption);
  static const QSet<QString> keep{"p", "h1", "h2", "h3", "h4", "ul", "ol", "li", "b", "strong", "i", "em", "br",
                                  "blockquote", "a"};
  QString s = html; s.remove(drop);
  if (firstImage) { const auto m = img.match(s); if (m.hasMatch()) *firstImage = decodeEntities(m.captured(1)); }
  QString out; qsizetype last = 0;
  for (auto it = tag.globalMatch(s); it.hasNext();) {
    const auto m = it.next();
    out += s.mid(last, m.capturedStart() - last);
    last = m.capturedEnd();
    QString name = m.captured(2).toLower();
    if (name == "h1") name = "h2";
    if (!keep.contains(name)) continue;
    if (name == "a" && m.captured(1).isEmpty()) {
      const auto h = href.match(m.captured(3));
      out += h.hasMatch() ? "<a href=\"" + h.captured(1).toHtmlEscaped() + "\">" : "<a>";
    } else out += "<" + m.captured(1) + name + ">";
  }
  out += s.mid(last);
  return out.trimmed();
}
QDateTime parseDate(QString s) {
  s = s.trimmed();
  QDateTime t = QDateTime::fromString(s, Qt::ISODate);
  if (t.isValid()) return t.toUTC();
  static const QRegularExpression plainIso(R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$)");
  if (plainIso.match(s).hasMatch()) { t = QDateTime::fromString(s, "yyyy-MM-dd HH:mm:ss"); t.setTimeZone(QTimeZone::UTC); return t; }
  static const QList<QPair<QString, QString>> zones{{" GMT", " +0000"}, {" UTC", " +0000"}, {" UT", " +0000"},
      {" Z", " +0000"}, {" EST", " -0500"}, {" EDT", " -0400"}, {" CET", " +0100"}, {" CEST", " +0200"}};
  for (const auto& [a, b] : zones) if (s.endsWith(a)) { s = s.left(s.size() - a.size()) + b; break; }
  t = QDateTime::fromString(s, Qt::RFC2822Date);
  if (t.isValid()) return t.toUTC();
  // "Sat, 14 Nov 2026 00:00 +0000" (no seconds)
  static const QRegularExpression noSec(R"(^(?:\w+, )?(\d{1,2}) (\w{3}) (\d{4}) (\d{2}):(\d{2}) ([+-]\d{4})$)");
  const auto m = noSec.match(s);
  if (m.hasMatch()) {
    t = QLocale(QLocale::English).toDateTime(QString("%1 %2 %3 %4:%5").arg(m.captured(1), m.captured(2), m.captured(3),
                                                                             m.captured(4), m.captured(5)), "d MMM yyyy HH:mm");
    const int off = m.captured(6).mid(1, 2).toInt() * 3600 + m.captured(6).mid(3, 2).toInt() * 60;
    t.setTimeZone(QTimeZone::fromSecondsAheadOfUtc(m.captured(6)[0] == '-' ? -off : off));
    return t.toUTC();
  }
  return {};
}

QList<Story> parseFeed(const QByteArray& body, const FeedSource& src) {
  QList<Story> out;
  QXmlStreamReader x(body);
  Story cur; bool in = false; QString desc, content;
  while (!x.atEnd()) {
    x.readNext();
    if (x.isStartElement()) {
      const QString n = x.name().toString(), q = x.qualifiedName().toString();
      if (n == "item" || n == "entry") { cur = Story(); in = true; desc.clear(); content.clear(); continue; }
      if (!in) continue;
      if (n == "title") cur.title = decodeEntities(x.readElementText(QXmlStreamReader::IncludeChildElements)).simplified();
      else if (n == "link") {
        const QString href = x.attributes().value("href").toString();
        if (!href.isEmpty()) { if (cur.link.isEmpty() || x.attributes().value("rel") == u"alternate") cur.link = href; }
        else cur.link = x.readElementText().trimmed();
      } else if (n == "guid" || n == "id") { const QString g = x.readElementText().trimmed(); if (cur.id.isEmpty()) cur.id = g; }
      else if (n == "pubDate" || n == "published" || (n == "updated" && !cur.time.isValid()) || (q == "dc:date" && !cur.time.isValid()))
        cur.time = parseDate(x.readElementText());
      else if (n == "description" || n == "summary") desc = x.readElementText(QXmlStreamReader::IncludeChildElements);
      else if (q == "content:encoded" || (n == "content" && x.namespaceUri().isEmpty()) || n == "content")
        { if (x.attributes().hasAttribute("url")) { if (cur.image.isEmpty()) cur.image = x.attributes().value("url").toString(); }
          else content = x.readElementText(QXmlStreamReader::IncludeChildElements); }
      else if (n == "creator" || n == "author") {
        const QString a = x.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
        if (cur.author.isEmpty() && !a.contains(".com") && !a.isEmpty()) cur.author = a;
      } else if (n == "enclosure" || n == "thumbnail") {
        const auto a = x.attributes();
        const QString type = a.value("type").toString();
        if (cur.image.isEmpty() && (type.isEmpty() || type.startsWith("image"))) cur.image = a.value("url").toString();
      }
    } else if (x.isEndElement() && (x.name() == u"item" || x.name() == u"entry")) {
      in = false;
      if (cur.title.isEmpty()) continue;
      if (src.id == "financialjuice") cur.title.remove(QRegularExpression("^FinancialJuice:\\s*"));
      const QString body = content.size() > desc.size() ? content : desc;
      QString img;
      if (body.contains('<')) {
        cur.contentHtml = sanitize(body, &img);
        if (plain(cur.contentHtml).size() < 400) cur.contentHtml.clear();   // a teaser, not an article
      }
      if (cur.image.isEmpty() && !img.isEmpty()) cur.image = img;
      cur.summary = plain(desc.isEmpty() ? content : desc);
      if (cur.summary == cur.title) cur.summary.clear();
      if (cur.summary.size() > 600) cur.summary = cur.summary.left(600).section(' ', 0, -2) + "…";
      if (!cur.link.isEmpty()) cur.id = cur.link;
      if (cur.id.isEmpty()) cur.id = src.id + ":" + cur.title;
      if (!cur.time.isValid()) cur.time = QDateTime::currentDateTimeUtc();
      cur.sourceId = src.id; cur.source = src.name;
      if (src.id == "centralbanks") {
        const QString h = QUrl(cur.link).host();
        cur.author = h.contains("federalreserve") ? "Federal Reserve" : h.contains("ecb") ? "ECB" : h.contains("bankofengland") ? "Bank of England" : "";
      }
      classify(cur);
      if (src.id == "centralbanks") { cur.topics |= CentralBank; if (cur.score < 2) { cur.score = 2; cur.impact = Impact3::Important; } }
      out << cur;
    }
  }
  return out;
}

QJsonObject toJson(const Story& s) {
  return {{"id", s.id}, {"t", s.title}, {"s", s.summary}, {"c", s.contentHtml}, {"img", s.image}, {"src", s.sourceId},
          {"a", s.author}, {"time", s.time.toString(Qt::ISODate)}, {"link", s.link}};
}
}

// ------------------------------------------------------------------------------------------------ service
const QList<FeedSource>& Feed::sources() const {
  static const QList<FeedSource> s{
    {"investing", "INVESTING.COM", "investing.com",
     {"https://www.investing.com/rss/news.rss", "https://www.investing.com/rss/news_14.rss",
      "https://www.investing.com/rss/news_95.rss", "https://www.investing.com/rss/news_1.rss",
      "https://www.investing.com/rss/news_25.rss"}, 300},
    {"investinglive", "INVESTINGLIVE", "investinglive.com", {"https://investinglive.com/feed/"}, 120},
    {"yahoo", "YAHOO FINANCE", "finance.yahoo.com",
     {"https://feeds.finance.yahoo.com/rss/2.0/headline?s=%5EGSPC,%5EDJI,%5EIXIC,EURUSD=X,GC=F,CL=F&region=US&lang=en-US"}, 300},
    {"cnbc", "CNBC", "cnbc.com",
     {"https://www.cnbc.com/id/100003114/device/rss/rss.html", "https://www.cnbc.com/id/20910258/device/rss/rss.html",
      "https://www.cnbc.com/id/10000664/device/rss/rss.html", "https://www.cnbc.com/id/15839135/device/rss/rss.html"}, 120},
    {"marketwatch", "MARKETWATCH", "marketwatch.com",
     {"https://feeds.content.dowjones.io/public/rss/mw_topstories", "https://feeds.content.dowjones.io/public/rss/mw_marketpulse",
      "https://feeds.content.dowjones.io/public/rss/mw_realtimeheadlines"}, 120},
    {"centralbanks", QT_TRANSLATE_NOOP("news", "CENTRAL BANKS"), "federalreserve.gov · ecb.europa.eu · bankofengland.co.uk",
     {"https://www.federalreserve.gov/feeds/press_all.xml", "https://www.ecb.europa.eu/rss/press.html",
      "https://www.bankofengland.co.uk/rss/news"}, 300},
    {"financialjuice", "FINANCIALJUICE", "financialjuice.com", {"https://www.financialjuice.com/feed.ashx?xy=rss"}, 120},
    {"nasdaq", "NASDAQ", "nasdaq.com", {"https://www.nasdaq.com/feed/rssoutbound?category=Markets"}, 600},
  };
  return s;
}

static QString archiveFile() { return paths::data("news/feed") + "/stories.json"; }

Feed& Feed::instance() { static Feed* f = new Feed; return *f; }

Feed::Feed() {
  QFile f(archiveFile());
  if (f.open(QIODevice::ReadOnly)) {
    QList<Story> list;
    const QDateTime cut = QDateTime::currentDateTimeUtc().addDays(-4);
    for (const auto& v : QJsonDocument::fromJson(f.readAll()).array()) {
      const auto o = v.toObject();
      Story s;
      s.id = o["id"].toString(); s.title = o["t"].toString(); s.summary = o["s"].toString(); s.contentHtml = o["c"].toString();
      s.image = o["img"].toString(); s.sourceId = o["src"].toString(); s.author = o["a"].toString();
      s.time = QDateTime::fromString(o["time"].toString(), Qt::ISODate).toUTC(); s.link = o["link"].toString();
      if (s.time < cut) continue;
      for (const auto& src : sources()) if (src.id == s.sourceId) s.source = src.name;
      if (s.source.isEmpty()) continue;
      classify(s);
      if (s.sourceId == "centralbanks") { s.topics |= CentralBank; if (s.score < 2) { s.score = 2; s.impact = Impact3::Important; } }
      list << s;
    }
    merge(list);
  }
  m_timer.setInterval(30 * 1000);
  connect(&m_timer, &QTimer::timeout, this, [this] { poll(false); });
  m_saveTimer.setSingleShot(true); m_saveTimer.setInterval(4000);
  connect(&m_saveTimer, &QTimer::timeout, this, &Feed::save);
}

const Story* Feed::find(const QString& id) const {
  const int i = m_index.value(id, -1);
  return i < 0 ? nullptr : &m_stories[i];
}

void Feed::setActive(QObject* owner, bool active) {
  const bool was = !m_active.isEmpty();
  if (active) {
    if (!m_active.contains(owner)) connect(owner, &QObject::destroyed, this, [this, owner] { setActive(owner, false); });
    m_active.insert(owner);
  } else m_active.remove(owner);
  const bool now = !m_active.isEmpty();
  if (now && !was) { poll(false); m_timer.start(); }
  if (!now && was) m_timer.stop();
}

void Feed::refresh() { poll(true); }

// Sources the user switched off in the feed header are not downloaded at all.
// Off by default: FinancialJuice and Nasdaq (see the page), Investing.com (its robots.txt answers 403 to our
// user agent, so we cannot confirm robots allow us; the user may switch it on).
QStringList Feed::defaultSourcesOff() { return {"financialjuice", "nasdaq", "investing"}; }

void Feed::poll(bool force) {
  const QDateTime now = QDateTime::currentDateTimeUtc();
  const QStringList off = settings::value("news.feed.sourcesOff", defaultSourcesOff()).toStringList();
  for (const auto& s : sources())
    for (const auto& u : s.urls) {
      if (off.contains(s.id)) continue;
      const QDateTime last = m_lastPoll.value(u);
      if (!force && last.isValid() && last.secsTo(now) < s.pollSec) continue;
      m_lastPoll[u] = now;
      fetch(s, u);
    }
}

void Feed::settle() {
  if (m_inflight > 0 && --m_inflight == 0) emit settled();
}

void Feed::fetch(const FeedSource& s, const QString& url) {
  net::Request r; r.url = QUrl(url); r.ttlSec = s.pollSec - 5;
  r.priority = true;   // ahead of story images queued for the same host (investinglive.com: one request a minute)
  ++m_inflight;
  net::get(r, this, [this, s, url](const net::Reply& rep) {
    const QPair<QString, QString> key(s.id, url);
    // an HTTP error page (403 / 429 HTML) is an error, not an empty feed; per URL: a source may have several feeds
    if (!rep.ok() || rep.body.isEmpty()) {
      m_errors[key] = !rep.error.isEmpty() ? rep.error : rep.ok() ? QStringLiteral("empty answer") : QString("HTTP %1").arg(rep.status);
      emit updated(); settle(); return;
    }
    // a cached copy served after a failure: the feed still failed, so the header must say so (it showed the old
    // stories as if the source were fine)
    if (rep.stale) m_errors[key] = !rep.error.isEmpty() ? rep.error : QStringLiteral("served from cache after a failure");
    else m_errors.remove(key);
    auto* w = new QFutureWatcher<QList<Story>>(this);
    connect(w, &QFutureWatcher<QList<Story>>::finished, this, [this, w, fetched = rep.fetched, cache = rep.fromCache] {
      w->deleteLater();
      const auto list = w->result();
      if (!cache || !m_lastRefresh.isValid()) m_lastRefresh = std::max(m_lastRefresh, fetched);
      merge(list);
      settle();
    });
    w->setFuture(QtConcurrent::run([body = rep.body, s] { return parseFeed(body, s); }));
  });
}

void Feed::merge(const QList<Story>& in) {
  bool changed = false;
  const QDateTime future = QDateTime::currentDateTimeUtc().addSecs(600);
  for (Story s : in) {
    if (s.time > future) s.time = QDateTime::currentDateTimeUtc();
    if (m_index.contains(s.id)) {
      Story& old = m_stories[m_index[s.id]];
      if (old.image.isEmpty() && !s.image.isEmpty()) { old.image = s.image; changed = true; }
      if (old.contentHtml.isEmpty() && !s.contentHtml.isEmpty()) { old.contentHtml = s.contentHtml; changed = true; }
      continue;
    }
    // the same headline from two feeds of one publisher
    bool dup = false;
    for (const auto& o : m_stories) if (o.sourceId == s.sourceId && o.title == s.title) { dup = true; break; }
    if (dup) continue;
    m_stories << s; changed = true;
    m_index.insert(s.id, int(m_stories.size() - 1));
  }
  if (!changed) return;
  std::stable_sort(m_stories.begin(), m_stories.end(), [](const Story& a, const Story& b) { return a.time > b.time; });
  const QDateTime cut = QDateTime::currentDateTimeUtc().addDays(-4);
  while (m_stories.size() > 900 || (!m_stories.isEmpty() && m_stories.last().time < cut)) m_stories.removeLast();
  m_index.clear();
  for (int i = 0; i < m_stories.size(); ++i) m_index.insert(m_stories[i].id, i);
  if (!m_saveTimer.isActive()) m_saveTimer.start();
  emit updated();
}

void Feed::save() {
  QJsonArray a;
  for (const auto& s : m_stories) a.append(toJson(s));
  QSaveFile f(archiveFile());
  if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(a).toJson(QJsonDocument::Compact)); f.commit(); }
}

// ------------------------------------------------------------------------------------------------ images
void loadImage(const QString& url, QObject* ctx, std::function<void(const QImage&)> cb) {
  struct Pending { QList<QPair<QPointer<QObject>, std::function<void(const QImage&)>>> waiters; };
  static QHash<QString, QImage> mem;
  static QStringList order;
  static QHash<QString, Pending> pending;
  QPointer<QObject> guard(ctx);
  if (url.isEmpty()) { QTimer::singleShot(0, ctx, [cb] { cb(QImage()); }); return; }
  if (auto it = mem.constFind(url); it != mem.cend()) { QImage img = *it; QTimer::singleShot(0, ctx, [cb, img] { cb(img); }); return; }
  const bool first = !pending.contains(url);
  pending[url].waiters << qMakePair(guard, cb);
  if (!first) return;
  net::Request r; r.url = QUrl(url); r.ttlSec = 7 * 86400;
  net::get(r, nullptr, [url](const net::Reply& rep) {
    auto* w = new QFutureWatcher<QImage>();
    QObject::connect(w, &QFutureWatcher<QImage>::finished, [w, url] {
      w->deleteLater();
      const QImage img = w->result();
      if (!img.isNull()) {
        // decoded images (≤ 1400 px wide, ~3–4 MB each) kept up to 128 MB, oldest out first (it was 160 of them: up to
        // ~0.6 GB); an image dropped here is read again from the disk cache when a new size of it is needed
        static qint64 bytes = 0;
        if (!mem.contains(url)) { mem.insert(url, img); order << url; bytes += img.sizeInBytes(); }
        while (bytes > 128LL * 1024 * 1024 && order.size() > 1) bytes -= mem.take(order.takeFirst()).sizeInBytes();
      }
      const auto waiters = pending.take(url).waiters;
      for (const auto& [g, f] : waiters) if (g) f(img);
    });
    w->setFuture(QtConcurrent::run([body = rep.body] {
      QBuffer buf; buf.setData(body); buf.open(QIODevice::ReadOnly);
      QImageReader rd(&buf);
      rd.setAutoTransform(true);
      const QSize sz = rd.size();
      if (sz.isValid() && sz.width() > 1400) rd.setScaledSize(sz.scaled(1400, 1400, Qt::KeepAspectRatio));
      return rd.read();
    }));
  });
}
}
