#include "banks/Research.h"
#include "banks/Registry.h"
#include "net/Http.h"
#include "net/Sources.h"
#include "core/Paths.h"
#include <QXmlStreamReader>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QFile>
#include <QFileInfo>
#include <QTextDocumentFragment>
#include <QTimeZone>
#include <algorithm>

namespace banks {

static const sources::Registrar regIng({"ing-think", "ING Bank N.V. (ING THINK economic research)",
    {"think.ing.com"}, sources::Access::Keyless, 2000,
    "Research notes (RSS + article listings, PDF downloads) and the monthly forecast tables (FX, rates, bonds, commodities)",
    "think.ing.com/about/terms-of-use: \"ING grants you the license to use, distribute, reproduce, modify, adapt, publicly "
    "perform and publicly display such Content by mentioning ING Bank N.V. as copyright owner\". robots.txt allows / "
    "(only /sign-up disallowed). Query-string links in its pages are crawler traps and are never followed.",
    {"news/bank-research", "positioning/bank-positioning"}});

// The four US banks whose written research their terms keep to links (Registry.cpp): their public podcast and YouTube
// feeds instead (2026-11-21, user's choice). Syndication feeds published for podcast apps and readers: the app shows
// what such an app shows (title, date, a short summary, a link to the episode or video) and stores nothing else.
// Hosts checked 2026-11-21: none is on a bank's own domain.
static const sources::Registrar regPodcasts({"bank-podcasts", "Podcast hosts of J.P. Morgan, Bank of America, Goldman Sachs and Morgan Stanley",
    {"feed.podbean.com", "feeds.megaphone.fm", "rss.art19.com"}, sources::Access::Keyless, 2000,
    "Episode titles, dates, short summaries and links of the banks' market podcasts (At Any Rate, Global Data Pod, "
    "Global Research Unlocked, Exchanges, The Markets, Thoughts on the Market)",
    "Public RSS feeds published for podcast apps; only what a podcast app lists is shown, the audio plays on the host",
    {"news/bank-research"}});
static const sources::Registrar regBankVideos({"bank-videos", "YouTube channels of J.P. Morgan AM, Merrill (BofA), Goldman Sachs, Morgan Stanley",
    {"www.youtube.com"}, sources::Access::Keyless, 1000,
    "Video titles, dates and links (the channels' public Atom feeds, 15 newest; Shorts left out)",
    "YouTube's public channel feeds; the videos play on YouTube", {"news/bank-research"}});

static QString idOf(const QString& url);
static QString plain(const QString& html);

namespace {
struct Feed { const char* bank; const char* show; const char* url; bool video; int ttlH; };
const Feed kFeeds[] = {
  {"jpm", "At Any Rate", "https://feed.podbean.com/atanyrate/feed.xml", false, 6},
  {"jpm", "Global Data Pod", "https://feed.podbean.com/globaldatapod/feed.xml", false, 6},
  {"jpm", "J.P. Morgan Asset Management", "https://www.youtube.com/feeds/videos.xml?channel_id=UCzRipjQYhKGNFdTHNMzNfIQ", true, 3},
  {"bac", "Global Research Unlocked", "https://feed.podbean.com/bofaglobalresearch/feed.xml", false, 6},
  {"bac", "Merrill", "https://www.youtube.com/feeds/videos.xml?channel_id=UCrRaql-djQAGecox63ZECnA", true, 3},
  {"gs", "Exchanges", "https://feeds.megaphone.fm/GLD9218176758", false, 6},
  {"gs", "The Markets", "https://feeds.megaphone.fm/GLD9322922848", false, 6},
  {"gs", "Goldman Sachs", "https://www.youtube.com/feeds/videos.xml?channel_id=UCyz6-taovlaOkPsPtK4KNEg", true, 3},
  // ~10 MB even compressed (every episode's transcript): read twice a day
  {"ms", "Thoughts on the Market", "https://rss.art19.com/thoughts-on-the-market", false, 12},
  {"ms", "Morgan Stanley", "https://www.youtube.com/feeds/videos.xml?channel_id=UCz6RzD6KG_hH_oHb2kyW5jQ", true, 3},
};
constexpr int kPerFeed = 25;   // newest items read per feed (the archive keeps older ones already seen)
QString shortText(QString t) {
  const int cut = int(t.indexOf("----- Transcript"));   // Morgan Stanley's descriptions carry the whole transcript
  if (cut > 0) t.truncate(cut);
  t = QTextDocumentFragment::fromHtml(t.left(4000)).toPlainText().simplified();
  return t.size() > 320 ? t.left(t.lastIndexOf(' ', 317)) + QString::fromUtf8("…") : t;
}
}  // namespace

// podcast RSS (item: title, link or enclosure, pubDate, itunes:summary / description) or YouTube Atom (entry: title,
// link href, published, media:description)
void Research::readFeed(const QString& bank, const QString& show, bool video, const QByteArray& body) {
  QList<Note> out;
  QXmlStreamReader x(body);
  Note cur; bool in = false; QString enclosure, summary, description;
  while (!x.atEnd() && out.size() < kPerFeed) {
    x.readNext();
    const auto name = x.name();
    if (x.isStartElement()) {
      if (name == QLatin1String("item") || name == QLatin1String("entry")) { in = true; cur = Note(); enclosure.clear(); summary.clear(); description.clear(); continue; }
      if (!in) continue;
      if (name == QLatin1String("title") && cur.title.isEmpty()) cur.title = plain(x.readElementText());
      // RSS: the plain <link> (not a podcast's atom:link to its own feed); Atom (YouTube): <link rel="alternate" href>
      else if (name == QLatin1String("link") && (video ? x.attributes().value("rel") == QLatin1String("alternate") : x.namespaceUri().isEmpty())) {
        const QString href = x.attributes().value("href").toString();
        cur.url = !href.isEmpty() ? href : x.readElementText().trimmed();
      } else if (name == QLatin1String("enclosure")) enclosure = x.attributes().value("url").toString();
      else if (name == QLatin1String("pubDate")) cur.published = QDateTime::fromString(x.readElementText().trimmed(), Qt::RFC2822Date).toUTC();
      else if (name == QLatin1String("published")) cur.published = QDateTime::fromString(x.readElementText().trimmed(), Qt::ISODate).toUTC();
      else if (name == QLatin1String("summary") || name == QLatin1String("subtitle")) { if (summary.isEmpty()) summary = x.readElementText(); }
      else if (name == QLatin1String("description")) description = x.readElementText();
    } else if (x.isEndElement() && (name == QLatin1String("item") || name == QLatin1String("entry"))) {
      in = false;
      if (cur.url.isEmpty()) cur.url = enclosure;   // Megaphone and Art19 items have no page link: the episode's audio
      if (cur.title.isEmpty() || !cur.url.startsWith(QLatin1String("https://"))) continue;
      if (video && cur.url.contains(QLatin1String("/shorts/"))) continue;
      cur.bankId = bank; cur.type = video ? "VIDEO" : "PODCAST"; cur.topic = show;
      cur.summary = shortText(!summary.isEmpty() ? summary : description);
      cur.id = idOf(cur.url);
      out << cur;
    }
  }
  if (!out.isEmpty()) merge(out);
}

QString pdfCacheFile(const QString& url) {
  const QByteArray h = QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex();
  return paths::cache("banks/pdf") + "/" + QString::fromLatin1(h.left(16)) + ".pdf";
}
QString typeLabel(const QString& type, bool plural) {
  if (type == "ARTICLE") return plural ? QCoreApplication::translate("banks", "ARTICLES") : QCoreApplication::translate("banks", "ARTICLE");
  if (type == "SNAP") return plural ? QCoreApplication::translate("banks", "SNAPS") : QCoreApplication::translate("banks", "SNAP");
  if (type == "OPINION") return plural ? QCoreApplication::translate("banks", "OPINIONS") : QCoreApplication::translate("banks", "OPINION");
  if (type == "REPORT") return plural ? QCoreApplication::translate("banks", "REPORTS") : QCoreApplication::translate("banks", "REPORT");
  if (type == "BUNDLE") return plural ? QCoreApplication::translate("banks", "BUNDLES") : QCoreApplication::translate("banks", "BUNDLE");
  if (type == "FORECASTS") return QCoreApplication::translate("banks", "FORECASTS");
  if (type == "PODCAST") return plural ? QCoreApplication::translate("banks", "PODCASTS") : QCoreApplication::translate("banks", "PODCAST");
  if (type == "VIDEO") return plural ? QCoreApplication::translate("banks", "VIDEOS") : QCoreApplication::translate("banks", "VIDEO");
  return plural ? type + "S" : type;
}

QString Note::localFile() const {
  if (pdfUrl.isEmpty()) return {};
  const QString f = pdfCacheFile(pdfUrl);
  return QFileInfo::exists(f) ? f : QString();
}

static QString idOf(const QString& url) {
  return QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
}
static QString plain(const QString& html) {
  return QTextDocumentFragment::fromHtml(html).toPlainText().simplified();
}

// ING page URL → (type, pdf URL). Types are the site's sections: articles, snaps, opinions, reports, bundles.
static bool ingKind(const QString& url, QString* type, QString* pdf) {
  static const QRegularExpression re(R"(^https://think\.ing\.com/(articles|snaps|opinions|reports|bundles)/([a-z0-9\-]+)/?$)");
  const auto m = re.match(url);
  if (!m.hasMatch()) return false;
  QString sec = m.captured(1); sec.chop(1);          // articles → article
  *type = sec.toUpper();
  *pdf = sec == "bundle" ? QString() : QString("https://think.ing.com/downloads/pdf/%1/%2").arg(sec, m.captured(2));
  return true;
}

Research& Research::instance() { static Research r; return r; }
Research::Research() { load(); }

void Research::load() {
  QFile f(paths::data("banks") + "/notes.json");
  if (!f.open(QIODevice::ReadOnly)) return;
  const QJsonArray a = QJsonDocument::fromJson(f.readAll()).array();
  for (const auto& v : a) {
    const QJsonObject o = v.toObject();
    Note n;
    n.id = o.value("id").toString(); n.bankId = o.value("bank").toString(); n.type = o.value("type").toString();
    n.topic = o.value("topic").toString().replace("&amp;", "&"); n.title = o.value("title").toString(); n.summary = o.value("summary").toString();
    n.url = o.value("url").toString(); n.pdfUrl = o.value("pdf").toString();
    n.published = QDateTime::fromString(o.value("published").toString(), Qt::ISODate);
    if (!n.id.isEmpty() && !n.title.isEmpty()) m_notes.insert(n.id, n);
  }
}
void Research::save() const {
  QJsonArray a;
  for (const auto& n : notes()) {
    a.append(QJsonObject{{"id", n.id}, {"bank", n.bankId}, {"type", n.type}, {"topic", n.topic}, {"title", n.title},
                         {"summary", n.summary}, {"url", n.url}, {"pdf", n.pdfUrl},
                         {"published", n.published.toUTC().toString(Qt::ISODate)}});
  }
  QSaveFile f(paths::data("banks") + "/notes.json");
  if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(a).toJson(QJsonDocument::Compact)); f.commit(); }
}

void Research::merge(const QList<Note>& in) {
  bool changed = false;
  for (const auto& n : in) {
    auto it = m_notes.find(n.id);
    if (it == m_notes.end()) { m_notes.insert(n.id, n); changed = true; continue; }
    // keep the earliest publication time, fill missing fields
    Note& o = *it;
    if (o.topic.isEmpty() && !n.topic.isEmpty()) { o.topic = n.topic; changed = true; }
    if (o.summary.isEmpty() && !n.summary.isEmpty()) { o.summary = n.summary; changed = true; }
    if (n.published.isValid() && (!o.published.isValid() || n.published < o.published)) { o.published = n.published; changed = true; }
  }
  m_last = QDateTime::currentDateTimeUtc();
  if (changed) save();
  emit updated();
}

void Research::readIngRss(const QByteArray& body) {
  QList<Note> out;
  QXmlStreamReader x(body);
  Note cur; bool inItem = false;
  while (!x.atEnd()) {
    x.readNext();
    if (x.isStartElement()) {
      const auto name = x.name();
      if (name == QLatin1String("item")) { inItem = true; cur = Note(); continue; }
      if (!inItem) continue;
      if (name == QLatin1String("title")) cur.title = plain(x.readElementText());
      else if (name == QLatin1String("link")) cur.url = x.readElementText().trimmed();
      else if (name == QLatin1String("description")) cur.summary = plain(x.readElementText());
      else if (name == QLatin1String("subject")) cur.topic = plain(x.readElementText().section(',', -1));   // "Food &amp; Agri"
      else if (name == QLatin1String("date")) cur.published = QDateTime::fromString(x.readElementText().trimmed(), Qt::ISODate).toUTC();
    } else if (x.isEndElement() && x.name() == QLatin1String("item")) {
      inItem = false;
      QString type, pdf;
      if (!ingKind(cur.url, &type, &pdf) || cur.title.isEmpty()) continue;
      cur.bankId = "ing"; cur.type = type; cur.pdfUrl = pdf; cur.id = idOf(cur.url);
      out << cur;
    }
  }
  merge(out);
}

// Listing pages (think.ing.com/articles/, /snaps/): one <a href=… class="card …"> per note with the topic,
// the publication time (data-date-raw, Unix seconds) and the title.
void Research::readIngListing(const QByteArray& body) {
  const QString h = QString::fromUtf8(body);
  static const QRegularExpression card(R"re(<a href="(https://think\.ing\.com/(?:articles|snaps|opinions|reports)/[a-z0-9\-]+/)" class="card)re");
  static const QRegularExpression cat(R"re(class="entry-category">\s*([^<]+?)\s*<)re");
  static const QRegularExpression date(R"re(data-date-raw="(\d+)")re");
  static const QRegularExpression title(R"re(class="card-title[^"]*">\s*([^<]+?)\s*<)re");
  QList<Note> out;
  auto it = card.globalMatch(h);
  QList<QPair<qsizetype, QString>> starts;
  while (it.hasNext()) { const auto m = it.next(); starts << qMakePair(m.capturedStart(), m.captured(1)); }
  for (int i = 0; i < starts.size(); ++i) {
    const qsizetype a = starts[i].first, b = i + 1 < starts.size() ? starts[i + 1].first : qMin(h.size(), a + 6000);
    const QString chunk = h.mid(a, b - a);
    Note n; n.url = starts[i].second;
    QString type, pdf;
    if (!ingKind(n.url, &type, &pdf)) continue;
    const auto mt = title.match(chunk); if (!mt.hasMatch()) continue;
    n.title = plain(mt.captured(1));
    if (const auto mc = cat.match(chunk); mc.hasMatch()) n.topic = plain(mc.captured(1));
    if (const auto md = date.match(chunk); md.hasMatch()) n.published = QDateTime::fromSecsSinceEpoch(md.captured(1).toLongLong(), QTimeZone::UTC);
    if (!n.published.isValid()) continue;
    n.bankId = "ing"; n.type = type; n.pdfUrl = pdf; n.id = idOf(n.url);
    out << n;
  }
  merge(out);
}

void Research::refresh() {
  if (m_lastPoll.isValid() && m_lastPoll.secsTo(QDateTime::currentDateTimeUtc()) < 15 * 60) return;
  m_lastPoll = QDateTime::currentDateTimeUtc();
  net::get(QUrl("https://think.ing.com/rss/"), 30 * 60, this, [this](const net::Reply& r) { if (r.ok()) readIngRss(r.body); else emit updated(); });
  for (const char* sec : {"articles", "snaps"}) {
    net::get(QUrl(QString("https://think.ing.com/%1/").arg(sec)), 60 * 60, this,
             [this](const net::Reply& r) { if (r.ok()) readIngListing(r.body); });
  }
  for (const Feed& f : kFeeds)
    net::get(QUrl(QString::fromLatin1(f.url)), f.ttlH * 3600, this, [this, f](const net::Reply& r) {
      if (r.ok()) readFeed(QString::fromLatin1(f.bank), QString::fromUtf8(f.show), f.video, r.body);
    });
  // The monthly forecast book (PDF) is a note too; its URL is on the forecasts page (read by banks::Forecasts).
  net::get(QUrl("https://think.ing.com/forecasts/"), 6 * 3600, this, [this](const net::Reply& r) {
    if (!r.ok()) return;
    static const QRegularExpression re(R"re(https://think\.ing\.com/uploads/pdf-forecasts/([A-Za-z]+)_(\d{4})_INGF\.pdf)re");
    const auto m = re.match(QString::fromUtf8(r.body));
    if (!m.hasMatch()) return;
    Note n; n.bankId = "ing"; n.type = "FORECASTS"; n.topic = "Monthly forecasts";
    n.url = "https://think.ing.com/forecasts/"; n.pdfUrl = m.captured(0);
    n.title = QString("ING Monthly: economic and financial forecasts, %1 %2").arg(m.captured(1), m.captured(2));
    n.id = idOf(n.pdfUrl);
    const QDate d = QDate::fromString(m.captured(1) + " 1 " + m.captured(2), "MMMM d yyyy");
    n.published = d.isValid() ? QDateTime(d, QTime(8, 0), QTimeZone::UTC) : r.fetched;
    merge({n});
  });
}

QList<Note> Research::notes(const QString& bankId) const {
  QList<Note> out;
  for (const auto& n : m_notes) if (bankId.isEmpty() || n.bankId == bankId) out << n;
  std::sort(out.begin(), out.end(), [](const Note& a, const Note& b) { return a.published > b.published; });
  return out;
}
const Note* Research::note(const QString& id) const { auto it = m_notes.constFind(id); return it == m_notes.cend() ? nullptr : &*it; }
int Research::publishedToday() const {
  const QDateTime midnight(QDate::currentDate(), QTime(0, 0));
  int n = 0; for (const auto& x : m_notes) if (x.published.toLocalTime() >= midnight) ++n;
  return n;
}
QStringList Research::types() const {
  QStringList t; for (const auto& n : m_notes) if (!t.contains(n.type)) t << n.type;
  t.sort(); return t;
}
QStringList Research::sources() const {
  QStringList s; for (const auto& b : researchBanks(Use::Full)) s << b.researchName;
  return s;
}

void Research::fetchPdf(const QString& noteId, QObject* ctx, std::function<void(const QString&, const QString&)> cb) {
  const Note* n = note(noteId);
  if (!n || n->pdfUrl.isEmpty()) { cb({}, QCoreApplication::translate("banks", "This note has no PDF")); return; }
  const QString file = pdfCacheFile(n->pdfUrl);
  if (QFileInfo::exists(file)) { cb(file, {}); return; }
  net::Request rq; rq.url = QUrl(n->pdfUrl); rq.staleOnError = false; rq.priority = true; rq.timeoutMs = 60000;
  net::get(rq, ctx, [file, cb](const net::Reply& r) {
    if (!r.ok()) { cb({}, r.error.isEmpty() ? QCoreApplication::translate("banks", "Download failed") : r.error); return; }
    if (!r.body.startsWith("%PDF")) { cb({}, QCoreApplication::translate("banks", "The bank's server did not return a PDF")); return; }
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) { cb({}, QCoreApplication::translate("banks", "Cannot write the cache")); return; }
    f.write(r.body); f.commit();
    cb(file, {});
  });
}
}
