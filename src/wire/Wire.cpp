#include "wire/Wire.h"
#include "wire/ReleaseSource.h"
#include "core/Paths.h"
#include "market/Instruments.h"
#include "market/QuoteHub.h"
#include "net/Http.h"
#include "net/Sources.h"
#include "ui/Format.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QTimeZone>
#include <QXmlStreamReader>
#include <QtConcurrent>
#include <algorithm>

namespace wire {

// ------------------------------------------------------------------------------------------ sources
static const sources::Registrar kFinancialJuice({"financialjuice", "FinancialJuice",
  {"www.financialjuice.com"}, sources::Access::Keyless, 60000,
  "Real-time market headlines (public RSS feed, last 100 headlines)",
  "Public RSS feed offered for readers; headlines shown with their time and a link back; polled at most once a minute",
  {"news/squawk", "global/session-recap", "global/overview"}});
static const sources::Registrar kInvestingLive({"investinglive", "investingLive (ex-ForexLive)",
  {"investinglive.com"}, sources::Access::Keyless, 10000,   // the host's pace (shared with news-feed); the 2-min poll is the wire's own timer
  "FX and macro news headlines (RSS feed, last 25 stories)",
  "Public RSS feed (robots allow /feed for generic agents); headline + link only, polled every 2 min at most",
  {"news/squawk", "global/session-recap"}});
static const sources::Registrar kFedPress({"fed-press", "Federal Reserve Board",
  {"www.federalreserve.gov"}, sources::Access::Keyless, 10000,
  "Press releases (RSS press_all)", "US government work, public domain",
  {"news/squawk", "global/session-recap"}});
static const sources::Registrar kEcbPress({"ecb-press", "European Central Bank",
  {"www.ecb.europa.eu"}, sources::Access::Keyless, 10000,
  "Press releases, speeches, interviews (RSS)", "ECB copyright; reuse allowed provided the source is acknowledged",
  {"news/squawk", "global/session-recap"}});
static const sources::Registrar kBoePress({"boe-news", "Bank of England",
  {"www.bankofengland.co.uk"}, sources::Access::Keyless, 10000,
  "News and press releases (RSS)", "Bank of England content, reuse with attribution (BoE terms, Open Government Licence style)",
  {"news/squawk", "global/session-recap"}});

static constexpr int kLiveDays = 3;        // days kept in memory for the live list and dedup
static constexpr int kArchiveDays = 70;    // archive retention

QString kindName(Kind k) {
  return k == Kind::Mover ? QCoreApplication::translate("wire", "MOVERS")
       : k == Kind::Release ? QCoreApplication::translate("wire", "RELEASES") : QCoreApplication::translate("wire", "DATA");
}

QString releaseText(const QString& ccy, const QString& title, const QString& a, const QString& f, const QString& p) {
  auto v = [](const QString& s) { return s.isEmpty() ? QStringLiteral("-") : s; };
  return QString("%1 %2 Actual %3 (Forecast %4, Previous %5)").arg(currencyRegion(ccy), title, v(a), v(f), v(p));
}

// ------------------------------------------------------------------------------------------ parsing
namespace {
QDateTime parseRfc822(QString s) {
  s = s.simplified();
  if (s.isEmpty()) return {};
  // ISO 8601 (Atom, dc:date)
  if (s.size() >= 10 && s[4] == '-') {
    QDateTime t = QDateTime::fromString(s, Qt::ISODateWithMs);
    if (!t.isValid()) t = QDateTime::fromString(s, Qt::ISODate);
    return t.toUTC();
  }
  if (s.contains(',')) s = s.mid(s.indexOf(',') + 1).trimmed();
  const QStringList p = s.split(' ', Qt::SkipEmptyParts);   // 14 Nov 2026 15:09:00 GMT
  if (p.size() < 4) return {};
  const QString base = p[0].rightJustified(2, '0') + " " + p[1].left(3) + " " + p[2] + " " +
                       (p[3].count(':') == 1 ? p[3] + ":00" : p[3]);
  QDateTime t = QLocale::c().toDateTime(base, "dd MMM yyyy HH:mm:ss");
  if (!t.isValid()) return {};
  int off = 0;
  const QString z = p.size() > 4 ? p[4] : QString("GMT");
  if (z.size() == 5 && (z[0] == '+' || z[0] == '-')) {
    off = (z.mid(1, 2).toInt() * 60 + z.mid(3, 2).toInt()) * 60 * (z[0] == '-' ? -1 : 1);
  } else {
    static const QHash<QString, int> zones{{"EST", -5}, {"EDT", -4}, {"CST", -6}, {"CDT", -5}, {"PST", -8},
                                           {"PDT", -7}, {"BST", 1}, {"CET", 1}, {"CEST", 2}};
    off = zones.value(z.toUpper(), 0) * 3600;
  }
  t.setTimeZone(QTimeZone::fromSecondsAheadOfUtc(off));
  return t.toUTC();
}

QString cleanText(QString s) {
  s.replace(QRegularExpression("<[^>]*>"), " ");
  static const QList<QPair<QString, QString>> ent{{"&quot;", "\""}, {"&#39;", "'"}, {"&apos;", "'"}, {"&#8217;", "’"},
    {"&#8216;", "‘"}, {"&#8220;", "“"}, {"&#8221;", "”"}, {"&#8211;", "–"}, {"&#8212;", "—"}, {"&nbsp;", " "},
    {"&lt;", "<"}, {"&gt;", ">"}, {"&amp;", "&"}};
  for (int pass = 0; pass < 2; ++pass)
    for (const auto& [a, b] : ent) s.replace(a, b);
  return s.simplified();
}

struct ParsedFeed { QList<Item> items; QString error; };

ParsedFeed parseFeed(const QByteArray& body, const QString& feedId, const QString& feedName) {
  ParsedFeed out;
  QXmlStreamReader x(body);
  Item cur; bool in = false;
  QString guid;
  while (!x.atEnd()) {
    x.readNext();
    if (x.isStartElement()) {
      const auto n = x.name();
      if (n == u"item" || n == u"entry") { in = true; cur = Item{}; guid.clear(); continue; }
      if (!in) continue;
      if (n == u"title") cur.text = cleanText(x.readElementText(QXmlStreamReader::IncludeChildElements));
      else if (n == u"link") {
        const QString href = x.attributes().value("href").toString();
        const QString t = x.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
        if (cur.url.isEmpty()) cur.url = href.isEmpty() ? t : href;
      } else if (n == u"guid" || n == u"id") guid = x.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
      else if (n == u"pubDate" || n == u"updated" || n == u"published" || (n == u"date" && x.prefix() == u"dc")) {
        const QDateTime t = parseRfc822(x.readElementText(QXmlStreamReader::IncludeChildElements));
        if (t.isValid() && !cur.time.isValid()) cur.time = t;
      }
    } else if (x.isEndElement() && (x.name() == u"item" || x.name() == u"entry") && in) {
      in = false;
      if (cur.text.isEmpty() || !cur.time.isValid()) continue;
      static const QRegularExpression prefix("^FinancialJuice:\\s*", QRegularExpression::CaseInsensitiveOption);
      cur.text.remove(prefix);
      cur.kind = Kind::Headline;
      cur.source = feedName;
      cur.id = feedId + ":" + (guid.isEmpty() ? cur.url : guid);
      if (guid.isEmpty() && cur.url.isEmpty()) cur.id = feedId + ":" + QString::number(qHash(cur.text)) + ":" + QString::number(cur.time.toSecsSinceEpoch());
      out.items << cur;
    }
  }
  if (x.hasError() && out.items.isEmpty()) out.error = "unreadable feed: " + x.errorString();
  return out;
}

QJsonObject toJson(const Item& i) {
  QJsonObject o{{"id", i.id}, {"t", double(i.time.toMSecsSinceEpoch())}, {"k", int(i.kind)}, {"x", i.text}, {"s", i.source}};
  if (!i.url.isEmpty()) o["u"] = i.url;
  if (i.kind == Kind::Mover) { o["sym"] = i.symbol; o["chg"] = i.changePct; o["px"] = i.price; }
  if (i.kind == Kind::Release) {
    o["ccy"] = i.currency; o["imp"] = i.impact; o["a"] = i.actual; o["f"] = i.forecast; o["p"] = i.previous;
  }
  return o;
}
Item fromJson(const QJsonObject& o) {
  Item i;
  i.id = o["id"].toString();
  i.time = QDateTime::fromMSecsSinceEpoch(qint64(o["t"].toDouble()), QTimeZone::UTC);
  i.kind = Kind(o["k"].toInt());
  i.text = o["x"].toString(); i.source = o["s"].toString(); i.url = o["u"].toString();
  i.symbol = o["sym"].toString();
  i.changePct = o.contains("chg") ? o["chg"].toDouble() : NAN;
  i.price = o.contains("px") ? o["px"].toDouble() : NAN;
  i.currency = o["ccy"].toString(); i.impact = o["imp"].toInt();
  i.actual = o["a"].toString(); i.forecast = o["f"].toString(); i.previous = o["p"].toString();
  return i;
}
QString dayFile(const QDate& d) { return paths::data("wire") + "/" + d.toString("yyyy-MM-dd") + ".jsonl"; }

bool newerFirst(const Item& a, const Item& b) { return a.time != b.time ? a.time > b.time : a.id > b.id; }

double classThreshold(market::Category c, const QString& group) {
  switch (c) {
    case market::Category::Index: return 1.0;
    case market::Category::Stock: return 2.0;
    case market::Category::Commodity: return 2.0;
    case market::Category::Crypto: return 3.0;
    case market::Category::Forex: return group == "Majors" ? 0.5 : NAN;
    case market::Category::Bond: return NAN;
  }
  return NAN;
}
}

// ------------------------------------------------------------------------------------------ Wire
Wire& Wire::instance() { static Wire* w = new Wire; return *w; }

Wire::Wire() {
  m_feeds = {
    {"fj", "FinancialJuice", "https://www.financialjuice.com/feed.ashx?xy=rss", 60, 600, {}, {}, {}},
    {"il", "investingLive", "https://investinglive.com/feed/", 120, 900, {}, {}, {}},
    {"fed", "Federal Reserve", "https://www.federalreserve.gov/feeds/press_all.xml", 600, 1800, {}, {}, {}},
    {"ecb", "ECB", "https://www.ecb.europa.eu/rss/press.html", 600, 1800, {}, {}, {}},
    {"boe", "Bank of England", "https://www.bankofengland.co.uk/rss/news", 600, 1800, {}, {}, {}},
  };
  // live memory: the last few UTC days of the archive
  const QDate today = QDateTime::currentDateTimeUtc().date();
  for (int d = 0; d <= kLiveDays; ++d) { loadDay(today.addDays(-d)); m_live += m_days.value(today.addDays(-d)); }
  std::sort(m_live.begin(), m_live.end(), newerFirst);
  for (const auto& i : std::as_const(m_live)) {
    if (i.kind != Kind::Mover || !std::isfinite(i.changePct)) continue;
    const double thr = threshold(i.symbol);
    if (!std::isfinite(thr)) continue;
    // the level already emitted is the newest mover's (m_live is newest first): keyed by symbol, not by day, so UTC
    // midnight does not re-emit every instrument still beyond its threshold
    if (!m_level.contains(i.symbol)) m_level[i.symbol] = int(std::trunc(i.changePct / thr));
    if (!m_moveSince.contains(i.symbol) || m_moveSince[i.symbol] < i.time) m_moveSince[i.symbol] = i.time;
  }
  prune();

  // movers universe: indices (but VIX), commodities, stocks, crypto, FX majors
  for (const auto& in : market::instruments())
    if (std::isfinite(classThreshold(in.category, in.group)) && in.symbol != "VIX") m_universe << in.symbol;
  auto& hub = market::QuoteHub::instance();
  hub.subscribe(this, m_universe, false);
  connect(&hub, &market::QuoteHub::updated, this, &Wire::onQuotes);

  connect(&Hooks::instance(), &Hooks::releaseSourceChanged, this, [this] {
    if (auto* s = releaseSource()) connect(s, &ReleaseSource::changed, this, &Wire::onReleases, Qt::UniqueConnection);
    onReleases();
  });
  if (auto* s = releaseSource()) connect(s, &ReleaseSource::changed, this, &Wire::onReleases, Qt::UniqueConnection);
  QTimer::singleShot(0, this, &Wire::onReleases);

  m_timer.setSingleShot(true);
  connect(&m_timer, &QTimer::timeout, this, &Wire::schedule);
  QTimer::singleShot(0, this, &Wire::schedule);
}

bool Wire::anyActive() const {
  for (auto it = m_owners.cbegin(); it != m_owners.cend(); ++it) if (it.value()) return true;
  return false;
}

void Wire::setActive(QObject* owner, bool active) {
  if (!m_owners.contains(owner))
    connect(owner, &QObject::destroyed, this, [this, owner] {
      m_owners.remove(owner);
      market::QuoteHub::instance().setActive(this, anyActive());
    });
  const bool was = anyActive();
  m_owners[owner] = active;
  const bool now = anyActive();
  if (was != now) {
    market::QuoteHub::instance().setActive(this, now);
    if (now) { onQuotes(m_universe); schedule(); }   // fast feeds become due at once if stale
  }
}

bool Wire::releasesConnected() const { return releaseSource() != nullptr; }

QStringList Wire::feedErrors() const {
  QStringList out;
  for (const auto& f : m_feeds) if (!f.error.isEmpty()) out << f.name + ": " + f.error;
  return out;
}

void Wire::schedule() {
  const QDateTime now = QDateTime::currentDateTimeUtc();
  const bool act = anyActive();
  QDateTime next;
  for (auto& f : m_feeds) {
    const QDateTime due = f.last.isValid() ? f.last.addSecs(act ? f.activeSec : f.idleSec) : now;
    if (!f.busy && due <= now) fetch(f);
    const QDateTime d2 = f.last.isValid() ? f.last.addSecs(act ? f.activeSec : f.idleSec) : now.addSecs(5);
    if (!next.isValid() || d2 < next) next = d2;
  }
  const qint64 ms = qBound<qint64>(qint64(1000), now.msecsTo(next) + 50, qint64(30 * 60 * 1000));
  m_timer.start(int(ms));
}

void Wire::settle() {
  if (m_inflight > 0 && --m_inflight == 0) emit settled();
}

void Wire::refreshNow() {
  const QDateTime now = QDateTime::currentDateTimeUtc();
  for (auto& f : m_feeds)
    if (!f.busy && (!f.last.isValid() || f.last.secsTo(now) >= f.activeSec)) fetch(f);
}

void Wire::fetch(Feed& f) {
  f.busy = true;
  f.last = QDateTime::currentDateTimeUtc();
  net::Request r; r.url = QUrl(f.url); r.ttlSec = qMax(1, f.activeSec - 5); r.staleOnError = false;
  const QString id = f.id, name = f.name;
  ++m_inflight;
  net::get(r, this, [this, id, name](const net::Reply& rep) {
    Feed* feed = nullptr;
    for (auto& x : m_feeds) if (x.id == id) feed = &x;
    if (!feed) { settle(); return; }
    if (!rep.ok()) { feed->busy = false; feed->error = rep.error; emit changed(); settle(); return; }
    auto* w = new QFutureWatcher<ParsedFeed>(this);
    connect(w, &QFutureWatcher<ParsedFeed>::finished, this, [this, w, id] {
      const ParsedFeed pf = w->result();
      w->deleteLater();
      for (auto& x : m_feeds) if (x.id == id) { x.busy = false; x.error = pf.error; }
      if (pf.error.isEmpty()) m_lastFetch = QDateTime::currentDateTimeUtc();
      const QDateTime before = m_lastReceived;
      ingest(pf.items);
      if (m_lastReceived == before) emit changed();   // nothing new: still tell consumers (footer stamp, errors)
      settle();
    });
    w->setFuture(QtConcurrent::run(parseFeed, rep.body, id, name));
  });
}

void Wire::ingest(const QList<Item>& in) {
  const QDateTime now = QDateTime::currentDateTimeUtc();
  const QDateTime floor = now.addDays(-kLiveDays);
  QList<Item> fresh;
  for (Item i : in) {
    if (i.time < floor) continue;                         // old press releases: not wire news
    if (i.time > now.addSecs(120)) i.time = now;          // clock skew / scheduled items
    if (m_ids.contains(i.id)) continue;
    m_ids.insert(i.id);
    fresh << i;
  }
  if (fresh.isEmpty()) return;
  std::sort(fresh.begin(), fresh.end(), newerFirst);
  append(fresh);
  for (const auto& i : fresh) {
    if (!m_lastReceived.isValid() || i.time > m_lastReceived) m_lastReceived = i.time;
    m_days[i.time.date()].prepend(i);
  }
  m_live += fresh;
  std::sort(m_live.begin(), m_live.end(), newerFirst);
  while (!m_live.isEmpty() && m_live.last().time < floor) m_live.removeLast();
  emit itemsAdded(fresh);
  emit changed();
}

void Wire::append(const QList<Item>& items) {
  QHash<QDate, QByteArray> chunks;
  for (const auto& i : items) chunks[i.time.date()] += QJsonDocument(toJson(i)).toJson(QJsonDocument::Compact) + '\n';
  for (auto it = chunks.cbegin(); it != chunks.cend(); ++it) {
    QFile f(dayFile(it.key()));
    if (f.open(QIODevice::Append)) f.write(it.value());
  }
}

void Wire::loadDay(const QDate& d) const {
  if (m_days.contains(d)) return;
  QList<Item>& list = m_days[d];
  QFile f(dayFile(d));
  if (!f.open(QIODevice::ReadOnly)) return;
  while (!f.atEnd()) {
    const QByteArray line = f.readLine().trimmed();
    if (line.isEmpty()) continue;
    const auto o = QJsonDocument::fromJson(line).object();
    if (o.isEmpty()) continue;
    Item i = fromJson(o);
    if (i.id.isEmpty() || m_ids.contains(i.id)) continue;
    m_ids.insert(i.id);
    list << i;
  }
  std::sort(list.begin(), list.end(), newerFirst);
}

void Wire::prune() {
  const QDate limit = QDateTime::currentDateTimeUtc().date().addDays(-kArchiveDays);
  QDir dir(paths::data("wire"));
  for (const auto& fi : dir.entryInfoList({"*.jsonl"}, QDir::Files)) {
    const QDate d = QDate::fromString(fi.completeBaseName(), "yyyy-MM-dd");
    if (d.isValid() && d < limit) QFile::remove(fi.absoluteFilePath());
  }
}

QList<Item> Wire::items(Filter f, int max) const {
  const QDateTime floor = QDateTime::currentDateTimeUtc().addSecs(-24 * 3600);
  QList<Item> out;
  for (const auto& i : m_live) {
    if (i.time < floor && out.size() >= 150) break;
    const bool ok = f == Filter::All || (f == Filter::Data && i.kind == Kind::Headline) ||
                    (f == Filter::Movers && i.kind == Kind::Mover) || (f == Filter::Releases && i.kind == Kind::Release);
    if (ok) out << i;
    if (max >= 0 && out.size() >= max) break;
  }
  if (f != Filter::All && max < 0) {   // keep the same time window as ALL so counts add up
    const QList<Item> all = items(Filter::All);
    const QDateTime oldest = all.isEmpty() ? QDateTime() : all.last().time;
    while (!out.isEmpty() && oldest.isValid() && out.last().time < oldest) out.removeLast();
  }
  return out;
}

int Wire::count(Filter f) const { return int(items(f).size()); }

// The same numbers as count(f) for the four filters, with ALL's list built once (each filtered count built it again).
QVector<int> Wire::counts() const {
  const QList<Item> all = items(Filter::All);
  const QDateTime oldest = all.isEmpty() ? QDateTime() : all.last().time;
  const QDateTime floor = QDateTime::currentDateTimeUtc().addSecs(-24 * 3600);
  QVector<int> out{int(all.size())};
  for (Filter f : {Filter::Data, Filter::Movers, Filter::Releases}) {
    const Kind k = f == Filter::Data ? Kind::Headline : f == Filter::Movers ? Kind::Mover : Kind::Release;
    QVector<QDateTime> kept;   // what items(f) keeps, then trims to ALL's window
    for (const auto& i : m_live) {
      if (i.time < floor && kept.size() >= 150) break;
      if (i.kind == k) kept << i.time;
    }
    while (!kept.isEmpty() && oldest.isValid() && kept.last() < oldest) kept.removeLast();
    out << int(kept.size());
  }
  return out;
}

QList<Item> Wire::between(const QDateTime& from, const QDateTime& to) const {
  QList<Item> out;
  if (!from.isValid() || !to.isValid() || to <= from) return out;
  for (QDate d = from.toUTC().date(); d <= to.toUTC().date(); d = d.addDays(1)) {
    loadDay(d);
    for (const auto& i : m_days.value(d)) if (i.time >= from && i.time < to) out << i;
  }
  // Archive days read for a range stayed in memory for the session (every day ever looked at, up to the 70-day
  // archive). Keep the live days (ingest adds to them and dedups against their ids) and the days of this request (a
  // caller asking the same range again is served from memory); forget the rest. An id is in one day's list only
  // (loadDay skips ids already known), so dropping a day's ids never forgets a kept item.
  const QDate liveStart = QDateTime::currentDateTimeUtc().date().addDays(-kLiveDays);
  const QDate reqFrom = from.toUTC().date(), reqTo = to.toUTC().date();
  for (auto it = m_days.begin(); it != m_days.end();) {
    const QDate d = it.key();
    if (d >= liveStart || (d >= reqFrom && d <= reqTo)) { ++it; continue; }
    for (const auto& i : std::as_const(it.value())) m_ids.remove(i.id);
    it = m_days.erase(it);
  }
  std::sort(out.begin(), out.end(), newerFirst);
  return out;
}

double Wire::threshold(const QString& symbol) const {
  const auto* in = market::find(symbol);
  return in ? classThreshold(in->category, in->group) : NAN;
}

void Wire::onQuotes(const QStringList& symbols) {
  const auto& hub = market::QuoteHub::instance();
  const QDateTime now = QDateTime::currentDateTimeUtc();
  QList<Item> fresh;
  bool moved = false;
  for (const auto& s : symbols) {
    if (!m_universe.contains(s)) continue;
    moved = true;
    const auto q = hub.quote(s);
    if (!q.valid() || !std::isfinite(q.changePct) || !q.time.isValid()) continue;
    const double thr = threshold(s);
    const int lvl = int(std::trunc(q.changePct / thr));
    // back inside its threshold: the next crossing is a new move
    if (lvl == 0) { m_level.remove(s); continue; }
    // a quote older than 30 min means the market is closed: the move is stamped with its last trade
    const QDateTime t = q.time.secsTo(now) > 1800 ? q.time.toUTC() : now;
    const int had = m_level.value(s);
    if ((had > 0) == (lvl > 0) && std::abs(lvl) <= std::abs(had)) continue;
    m_level[s] = lvl;
    const auto* in = market::find(s);
    Item i;
    i.kind = Kind::Mover;
    i.time = t;
    i.symbol = s;
    i.changePct = q.changePct;
    i.price = q.price;
    i.source = "Deepwater";
    i.id = QString("mv:%1:%2:%3").arg(s, t.date().toString("yyyyMMdd")).arg(lvl);
    i.text = QString("%1 %2 — now %3").arg(in ? in->name : s, fmt::pct(q.changePct),
                                           fmt::num(q.price, in ? in->decimals : 2));
    m_moveSince[s] = t;
    fresh << i;
  }
  if (!fresh.isEmpty()) ingest(fresh);
  if (moved) emit moversChanged();
}

QList<Move> Wire::movers() const {
  const auto& hub = market::QuoteHub::instance();
  QList<Move> out;
  for (const auto& s : m_universe) {
    const auto q = hub.quote(s);
    if (!q.valid() || !std::isfinite(q.changePct)) continue;
    if (std::abs(q.changePct) < threshold(s)) continue;
    const auto* in = market::find(s);
    Move m;
    m.symbol = s; m.name = in ? in->name : s; m.changePct = q.changePct; m.price = q.price;
    m.decimals = in ? in->decimals : 2;
    m.since = m_moveSince.value(s, q.time.toUTC());
    out << m;
  }
  std::stable_sort(out.begin(), out.end(), [](const Move& a, const Move& b) { return a.since > b.since; });
  return out;
}

void Wire::onReleases() {
  auto* src = releaseSource();
  if (!src) return;
  const QDateTime now = QDateTime::currentDateTimeUtc();
  QList<Item> fresh;
  for (const auto& r : src->releases(now.addDays(-kLiveDays), now.addSecs(60))) {
    if (!r.released() || r.impact <= 0) continue;
    Item i;
    i.kind = Kind::Release;
    i.time = r.time.toUTC();
    i.id = "rel:" + r.id;
    i.currency = r.currency; i.impact = r.impact;
    i.actual = r.actual; i.forecast = r.forecast; i.previous = r.previous;
    i.source = src->attribution().isEmpty() ? QString("Calendar") : src->attribution();
    i.text = releaseText(r.currency, r.title, r.actual, r.forecast, r.previous);
    fresh << i;
  }
  ingest(fresh);
}
}
