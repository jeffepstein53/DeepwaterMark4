#include "net/Http.h"
#include "core/Startup.h"
#include "net/Sources.h"
#include "core/Paths.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QCryptographicHash>
#include <QSaveFile>
#include <QFile>
#include <QDirIterator>
#include <QFileInfo>
#include <QDateTime>
#include <QLocale>
#include <QTimeZone>
#include <QDataStream>
#include <QTimer>
#include <QHash>
#include <QSet>
#include <deque>
#include <map>
#include <optional>
#include <algorithm>
#include <QElapsedTimer>
#include <QDebug>
#include <QMutex>
#include <QThreadPool>
#include <QSslConfiguration>
#include <QCoreApplication>
#include <atomic>

namespace net {
namespace {

// One caller waiting for an answer. Identical GET requests (same URL, cache key, headers) share one network request:
// each caller keeps its own context, callback and cache wishes.
struct Waiter { QPointer<QObject> ctx; bool hasCtx; Callback cb; int ttlSec; bool staleOnError; };
struct Job { Request req; QString merge; std::vector<Waiter> waiters; qint64 queuedMs = 0, sentMs = 0; };

// DW_NET_LOG=1: one line per network request (host and path only: a query may hold a key), with the time since the app
// started, the wait in the host's queue and the time on the wire. For diagnosing slow pages; off by default.
const bool g_netLog = qEnvironmentVariableIsSet("DW_NET_LOG");
QElapsedTimer& appClock() { static QElapsedTimer t = [] { QElapsedTimer e; e.start(); return e; }(); return t; }

QString keyOf(const Request& r) {
  if (!r.cacheKey.isEmpty()) return r.cacheKey;
  QByteArray k = r.method + ' ' + r.url.toEncoded();
  if (!r.body.isEmpty()) k += ' ' + QCryptographicHash::hash(r.body, QCryptographicHash::Sha1).toHex();
  return QString::fromUtf8(k);
}
// Key under which waiting requests are merged; empty = never merged (anything but GET/HEAD: a POST may be an action).
QString mergeKeyOf(const Request& r) {
  if (r.method != "GET" && r.method != "HEAD") return {};
  QByteArray k = r.method + ' ' + r.url.toEncoded() + '\n' + r.cacheKey.toUtf8();
  for (const auto& [a, b] : r.headers) k += '\n' + a + ": " + b;
  if (!r.body.isEmpty()) k += '\n' + QCryptographicHash::hash(r.body, QCryptographicHash::Sha1).toHex();
  return QString::fromUtf8(k);
}
QString fileOf(const Request& r) {
  const QByteArray h = QCryptographicHash::hash(keyOf(r).toUtf8(), QCryptographicHash::Sha1).toHex();
  return paths::cache("http/" + QString::fromLatin1(h.left(2))) + "/" + QString::fromLatin1(h) + ".bin";
}
bool freshIn(const Reply& c, int ttlSec) { return ttlSec > 0 && c.fetched.secsTo(QDateTime::currentDateTimeUtc()) < ttlSec; }

// Disk cache. Writes run on one background thread, in order (a body can be 14 MB, and QSaveFile syncs to disk); until an
// answer is on disk it stays in `pending`, which reads check first, so they never see the older file. An invalidated
// entry is a removal in `pending` until the file is gone.
struct DiskCache {
  struct Entry { Reply rep; bool removed = false; quint64 gen = 0; };
  QMutex mu;
  QHash<QString, Entry> pending;   // cache file → answer being written, or removal in progress
  quint64 gen = 0;
  QThreadPool pool;
  DiskCache() {
    pool.setMaxThreadCount(1);
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, [this] { pool.waitForDone(10000); });
    // Nothing else ever deletes cache files (the user's machine reached 390 MB in a week): prune a little after the
    // first use, so start-up I/O is not slowed, then once a day for sessions left open. The timer belongs to the GUI
    // thread whatever thread made the first cache access.
    const QString root = paths::cache("http");
    QMetaObject::invokeMethod(qApp, [this, root] {
      QTimer::singleShot(60 * 1000, qApp, [this, root] { schedulePrune(root); });
      auto* daily = new QTimer(qApp);
      daily->setInterval(24 * 3600 * 1000);
      QObject::connect(daily, &QTimer::timeout, qApp, [this, root] { schedulePrune(root); });
      daily->start();
    }, Qt::QueuedConnection);
  }
  // On the writer thread, so a prune never races a write of the same file; paths still in `pending` (a write or a
  // removal not yet run) are left alone. Only files of the cache folder are looked at: everything is re-downloadable.
  void schedulePrune(const QString& root) {
    pool.start([this, root] {
      constexpr qint64 kMaxAgeSec = 30LL * 24 * 3600;
      constexpr qint64 kMaxBytes = 1LL << 30;
      struct F { QString path; QDateTime mtime; qint64 size; };
      std::vector<F> files;
      const QDateTime now = QDateTime::currentDateTimeUtc();
      qint64 total = 0;
      QDirIterator it(root, {QStringLiteral("*.bin")}, QDir::Files, QDirIterator::Subdirectories);
      while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        const QString path = fi.filePath();
        { QMutexLocker l(&mu); if (pending.contains(path)) continue; }
        const QDateTime m = fi.lastModified().toUTC();
        if (m.secsTo(now) > kMaxAgeSec) { QFile::remove(path); continue; }
        files.push_back({path, m, fi.size()});
        total += fi.size();
      }
      if (total <= kMaxBytes) return;
      std::sort(files.begin(), files.end(), [](const F& a, const F& b) { return a.mtime < b.mtime; });
      for (const F& f : files) {
        if (total <= kMaxBytes) break;
        { QMutexLocker l(&mu); if (pending.contains(f.path)) continue; }
        if (QFile::remove(f.path)) total -= f.size;
      }
    });
  }
  void schedule(const QString& file, Entry e, std::function<void()> job) {
    { QMutexLocker l(&mu); e.gen = ++gen; pending.insert(file, e); }
    const quint64 g = e.gen;
    pool.start([this, file, g, job = std::move(job)] {
      job();
      QMutexLocker l(&mu);
      if (const auto it = pending.find(file); it != pending.end() && it->gen == g) pending.erase(it);
    });
  }
};
DiskCache& disk() { static DiskCache* d = new DiskCache; return *d; }

constexpr qint32 kMagic = 0x44573031;
// The cached answer of `r`. With freshFor > 0 the body is read only when the entry is younger than that (the header —
// magic, fetched, status — comes first in the file): an expired entry costs a few bytes, not the whole body.
bool readCache(const Request& r, Reply& out, int freshFor = 0) {
  const QString file = fileOf(r);
  {
    QMutexLocker l(&disk().mu);
    if (const auto it = disk().pending.constFind(file); it != disk().pending.cend()) {
      if (it->removed) return false;
      out.fetched = it->rep.fetched; out.status = it->rep.status; out.body = it->rep.body;
      out.fromCache = true;
      return freshFor <= 0 || freshIn(out, freshFor);
    }
  }
  QFile f(file);
  if (!f.open(QIODevice::ReadOnly)) return false;
  QDataStream ds(&f);
  qint32 magic = 0; ds >> magic;
  if (magic != kMagic) return false;
  ds >> out.fetched >> out.status;
  if (ds.status() != QDataStream::Ok) return false;
  if (freshFor > 0 && !freshIn(out, freshFor)) return false;
  ds >> out.body;
  out.fromCache = true;
  return ds.status() == QDataStream::Ok;
}
void writeCache(const Request& r, const Reply& rep) {
  const QString file = fileOf(r);
  DiskCache::Entry e; e.rep.fetched = rep.fetched; e.rep.status = rep.status; e.rep.body = rep.body;
  disk().schedule(file, e, [file, e] {
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) return;
    QDataStream ds(&f);
    ds << kMagic << e.rep.fetched << e.rep.status << e.rep.body;
    f.commit();
  });
}
void removeCache(const Request& r) {
  const QString file = fileOf(r);
  DiskCache::Entry e; e.removed = true;
  disk().schedule(file, e, [file] { QFile::remove(file); });
}

// The first HTTPS request loads the TLS backend and parses the system's CA certificates: ~140 ms on the GUI thread at
// every launch. Done on a pool thread as the application starts; requests wait in their queue (Engine::pump) until it
// is over instead of blocking the GUI thread on its lock, and are sent when it ends.
std::atomic<bool> g_tlsStarted{false}, g_tlsReady{false};
void resumeAfterTls();
void warmTls() {
  g_tlsStarted = true;
  QThreadPool::globalInstance()->start([] {
    (void)QSslConfiguration::defaultConfiguration();
    g_tlsReady = true;
    QMetaObject::invokeMethod(qApp, [] { resumeAfterTls(); }, Qt::QueuedConnection);
  });
}
DW_STARTUP_FUNCTION(warmTls)

class Engine : public QObject {
public:
  QNetworkAccessManager nam;
  // inflight: the requests on the wire (one, or the source's `parallel`), by serial number
  // holdUntilMs: the server's Retry-After (429 / 503) runs until then (ms since epoch, 0 = none)
  struct Host { std::deque<Job> queue; std::map<quint64, Job> inflight; QElapsedTimer last; bool timerArmed = false;
                qint64 holdUntilMs = 0; };
  quint64 serial = 0;
  QHash<QString, Host> hosts;
  QSet<QString> warned;

  Engine() {
    nam.setTransferTimeout(30000);
    nam.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);   // for callers of manager() (ModelHub)
  }

  int paceFor(const QString& host) {
    if (const int p = sources::paceForHost(host); p >= 0) return p;
    if (!warned.contains(host)) { warned.insert(host); qWarning().noquote() << "[net] undeclared host" << host; }
    return 250;
  }
  // Why a host may not be contacted (disabled in the Sources tab, forbidden by its terms); empty when it may.
  static QString refusal(const QString& host) {
    if (sources::isHostDisabled(host)) return QCoreApplication::translate("sources", "source switched off in the Sources tab");
    if (const auto* s = sources::forHost(host); s && s->access == sources::Access::Forbidden)
      return QCoreApplication::translate("sources", "source closed to programs by its terms: %1").arg(s->id);
    return {};
  }

  void enqueue(Job j) {
    const QString host = j.req.url.host().toLower();
    // web requests only: URLs from feeds (images, links) could name file://, ftp:// or an app scheme, and redirects are
    // already held to http(s) in onRedirect — the first hop now is too
    const QString scheme = j.req.url.scheme().toLower();
    if ((scheme != "http" && scheme != "https") || host.isEmpty()) {
      Reply rep; rep.error = QStringLiteral("only http and https addresses are requested");
      deliverLater(std::move(j.waiters), rep); return;
    }
    if (const QString why = refusal(host); !why.isEmpty()) {
      Reply rep; rep.error = why;
      deliverLater(std::move(j.waiters), rep); return;
    }
    Host& h = hosts[host];
    if (!j.merge.isEmpty()) {
      // an identical request already waits in the queue or is on the wire: its answer serves this caller too
      for (auto& [n, f] : h.inflight) if (f.merge == j.merge) { join(f, std::move(j)); return; }
      for (auto it = h.queue.begin(); it != h.queue.end(); ++it) {
        if (it->merge != j.merge) continue;
        const int was = rank(*it);
        const bool pri = j.req.priority, fg = !j.req.background;
        join(*it, std::move(j));
        if (pri) it->req.priority = true;
        if (fg) it->req.background = false;   // a page asks for what was being preloaded: in line with it now
        if (rank(*it) != was) { Job moved = std::move(*it); h.queue.erase(it); insertByRank(h.queue, std::move(moved)); }
        return;
      }
    }
    j.queuedMs = appClock().elapsed();
    insertByRank(h.queue, std::move(j));
    pump(host);
  }
  // Order in a host's queue: priority, then ordinary, then background requests, each kept in arrival order (each
  // priority one pushed to the front made the Overview's single tape request wait behind the eight risk series asked
  // just after it).
  // background first: a preload is never ahead of the shown page, even its "priority" requests (they keep it once
  // promoted)
  static int rank(const Job& j) { return j.req.background ? 2 : j.req.priority ? 0 : 1; }
  static void insertByRank(std::deque<Job>& q, Job&& j) {
    const int r = rank(j);
    const auto after = std::find_if(q.begin(), q.end(), [r](const Job& x) { return rank(x) > r; });
    q.insert(after, std::move(j));
  }
  int promote(const QString& tag) {
    int n = 0;
    for (auto hit = hosts.begin(); hit != hosts.end(); ++hit) {
      std::deque<Job>& q = hit->queue;
      std::vector<Job> lifted;
      for (auto it = q.begin(); it != q.end();) {
        if (!it->req.background || it->req.backgroundTag != tag) { ++it; continue; }
        it->req.background = false;
        lifted.push_back(std::move(*it));
        it = q.erase(it);
      }
      for (auto& j : lifted) insertByRank(q, std::move(j));
      n += int(lifted.size());
    }
    return n;
  }
  static void join(Job& into, Job&& j) {
    if (!into.req.preflight && j.req.preflight) into.req.preflight = std::move(j.req.preflight);
    into.req.ttlSec = std::max(into.req.ttlSec, j.req.ttlSec);
    into.req.staleOnError = into.req.staleOnError || j.req.staleOnError;
    into.req.timeoutMs = std::max(into.req.timeoutMs, j.req.timeoutMs);
    for (auto& w : j.waiters) into.waiters.push_back(std::move(w));
  }

  static bool alive(const Waiter& w) { return !w.hasCtx || w.ctx; }
  static void deliver(const Waiter& w, const Reply& rep) {
    if (!alive(w)) return;
    if (w.cb) w.cb(rep);
  }
  // asynchronous like a network answer, so callers never re-enter
  void deliverLater(std::vector<Waiter> ws, const Reply& rep) {
    QTimer::singleShot(0, this, [ws = std::move(ws), rep] { for (const auto& w : ws) deliver(w, rep); });
  }

  bool tlsWaiting = false;   // a request waits for warmTls (resumeAfterTls pumps every host)
  void pumpAll() {
    tlsWaiting = false;
    for (const QString& host : hosts.keys()) pump(host);
  }

  // The host asked to be left alone (Retry-After): a short wait holds the queue, a long one answers every queued
  // request 429 now (a stale cached answer when the request takes one), so callers don't sit on a queue for minutes.
  bool held(const QString& host) {
    Host& h = hosts[host];
    const qint64 left = h.holdUntilMs - QDateTime::currentMSecsSinceEpoch();
    if (left <= 0) { h.holdUntilMs = 0; return false; }
    if (left <= qint64(kHoldMaxSec) * 1000) {
      if (!h.timerArmed) {
        h.timerArmed = true;
        QTimer::singleShot(int(left) + 1, this, [this, host] { hosts[host].timerArmed = false; pump(host); });
      }
      return true;
    }
    std::deque<Job> jobs;
    jobs.swap(h.queue);
    for (auto& j : jobs) {
      Reply rep;
      rep.status = 429;
      rep.retryAfterSec = int((left + 999) / 1000);
      rep.error = QString("HTTP 429 (the server asked to wait %1 s)").arg(rep.retryAfterSec);
      Reply old;
      const bool haveOld = j.req.staleOnError && readCache(j.req, old);
      if (haveOld) { old.stale = true; old.error.clear(); }
      std::vector<Waiter> stale, plain;
      for (auto& w : j.waiters) (haveOld && w.staleOnError ? stale : plain).push_back(std::move(w));
      if (!stale.empty()) deliverLater(std::move(stale), old);
      if (!plain.empty()) deliverLater(std::move(plain), rep);
    }
    return true;
  }

  void pump(const QString& host) {
    Host& h = hosts[host];
    if (int(h.inflight.size()) >= sources::parallelForHost(host) || h.queue.empty()) return;
    if (g_tlsStarted && !g_tlsReady) { tlsWaiting = true; return; }
    if (held(host)) return;
    const int pace = paceFor(host);
    const qint64 since = h.last.isValid() ? h.last.elapsed() : 1 << 30;
    if (since < pace) {
      if (!h.timerArmed) {
        h.timerArmed = true;
        QTimer::singleShot(int(pace - since) + 1, this, [this, host] { hosts[host].timerArmed = false; pump(host); });
      }
      return;
    }
    Job j = std::move(h.queue.front()); h.queue.pop_front();
    // nobody waits for it any more
    j.waiters.erase(std::remove_if(j.waiters.begin(), j.waiters.end(), [](const Waiter& w) { return !alive(w); }), j.waiters.end());
    if (j.waiters.empty()) { pump(host); return; }
    // an identical request answered while this one waited in the queue: its fresh cached answer is served
    if (j.req.ttlSec > 0) {
      Reply c;
      int ttl = 0;
      for (const auto& w : j.waiters) ttl = std::max(ttl, w.ttlSec);
      if (readCache(j.req, c, ttl)) {
        std::vector<Waiter> served, rest;
        for (auto& w : j.waiters) (freshIn(c, w.ttlSec) ? served : rest).push_back(std::move(w));
        if (!served.empty()) deliverLater(std::move(served), c);
        j.waiters = std::move(rest);
        if (j.waiters.empty()) { pump(host); return; }
      }
    }
    if (j.req.preflight) {
      if (const QString why = j.req.preflight(); !why.isEmpty()) {
        Reply rep; rep.error = why;
        deliverLater(std::move(j.waiters), rep);
        pump(host);
        return;
      }
    }
    Host& hh = hosts[host];   // the preflight is outside code: `h` may not survive it
    hh.last.start();
    j.sentMs = appClock().elapsed();

    QNetworkRequest nr(j.req.url);
    nr.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    nr.setTransferTimeout(j.req.timeoutMs);
    // every redirect is checked (redirected → onRedirect): the target host goes through the registry too
    nr.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    for (const auto& [k, v] : j.req.headers) nr.setRawHeader(k, v);
    QNetworkReply* r = j.req.method == "GET" ? nam.get(nr)
                     : j.req.method == "POST" ? nam.post(nr, j.req.body)
                     : nam.sendCustomRequest(nr, j.req.method, j.req.body);
    r->setProperty("dwHop", j.req.url);   // the URL of the current hop (QNetworkReply::url() already moved on)
    const quint64 id = ++serial;
    hh.inflight.emplace(id, std::move(j));
    connect(r, &QNetworkReply::redirected, this, [this, r, host](const QUrl& to) { onRedirect(r, host, to); });
    connect(r, &QNetworkReply::finished, this, [this, r, host, id]() {
      r->deleteLater();
      auto node = hosts[host].inflight.extract(id);
      Job j = std::move(node.mapped());
      Reply rep;
      rep.fetched = QDateTime::currentDateTimeUtc();
      const QString refused = r->property("dwRefused").toString();
      if (refused.isEmpty()) {
        rep.status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        rep.body = r->readAll();
        if (rep.status == 429 || rep.status == 503) {
          rep.retryAfterSec = retryAfterSeconds(r->rawHeader("Retry-After"), rep.fetched);
          if (rep.retryAfterSec > 0) {
            Host& hh = hosts[host];
            hh.holdUntilMs = std::max(hh.holdUntilMs, QDateTime::currentMSecsSinceEpoch() + qint64(rep.retryAfterSec) * 1000);
          }
        }
      }
      if (!refused.isEmpty())
        rep.error = refused;
      else if (r->error() != QNetworkReply::NoError && !(rep.status >= 200 && rep.status < 300))
        rep.error = sources::redactUrls(r->errorString());   // the URL may hold a key or a token
      else if (rep.status < 200 || rep.status >= 300)
        rep.error = QString("HTTP %1").arg(rep.status);
      else if (r->error() != QNetworkReply::NoError)   // 2xx headers, then the body was cut off: not a usable answer
        rep.error = sources::redactUrls(r->errorString());
      sources::recordResult(host, rep.ok(), rep.error);
      if (g_netLog) {
        const qint64 now = appClock().elapsed();
        qInfo().noquote() << QString("[net] %1 ms  %2%3  %4  queued %5 ms  wire %6 ms  %7 B")
                                 .arg(now, 6).arg(host, j.req.url.path()).arg(rep.ok() ? QString("ok") : rep.error.left(60))
                                 .arg(j.sentMs - j.queuedMs).arg(now - j.sentMs).arg(rep.body.size());
      }
      Reply old;
      bool haveOld = false;
      if (rep.ok()) {
        if (j.req.ttlSec > 0 || j.req.staleOnError) writeCache(j.req, rep);
      } else if (j.req.staleOnError) {
        haveOld = readCache(j.req, old);
        old.stale = true; old.error.clear();
      }
      for (const auto& w : j.waiters) deliver(w, haveOld && w.staleOnError ? old : rep);
      pump(host);
    });
    pump(host);   // a host that takes several at once: the next one, paceMs later
  }

  // A redirect leaves the host the request was paced for: the target host is checked against the registry
  // (disabled, forbidden, pace) like a request of its own. https → http is refused (Qt's NoLessSafeRedirectPolicy).
  void onRedirect(QNetworkReply* r, const QString& host, const QUrl& toIn) {
    const QUrl from = r->property("dwHop").toUrl(), to = from.resolved(toIn);
    auto refuse = [r](const QString& why) { r->setProperty("dwRefused", why); r->abort(); };
    auto allow = [](QNetworkReply* rr, const QUrl& u) { rr->setProperty("dwHop", u); emit rr->redirectAllowed(); };
    const QString scheme = to.scheme().toLower();
    if ((scheme != "http" && scheme != "https") || (from.scheme().toLower() == "https" && scheme != "https"))
      return refuse(QStringLiteral("redirect to a less safe URL refused"));
    const QString th = to.host().toLower();
    if (th == host || th == from.host().toLower()) return allow(r, to);
    if (const QString why = refusal(th); !why.isEmpty()) return refuse("redirect to " + th + ": " + why);
    const int pace = paceFor(th);
    Host& t = hosts[th];
    const qint64 wait = t.last.isValid() ? pace - t.last.elapsed() : 0;
    if (wait <= 0) { t.last.start(); return allow(r, to); }
    if (wait > 20000) return refuse("redirect to " + th + " refused: too soon for its request pace");
    QPointer<QNetworkReply> rp(r);
    QTimer::singleShot(int(wait) + 1, this, [this, rp, th, to, allow] {
      if (!rp || rp->isFinished()) return;
      hosts[th].last.start();
      allow(rp, to);
    });
  }
};

Engine* g_engine = nullptr;
Engine& engine() { static Engine* e = g_engine = new Engine; return *e; }
void resumeAfterTls() { if (g_engine && g_engine->tlsWaiting) g_engine->pumpAll(); }
}

QByteArray userAgent() { return "Deepwater/1.0 (desktop market terminal)"; }
QNetworkAccessManager* manager() { return &engine().nam; }

namespace { thread_local int g_backgroundDepth = 0; thread_local QString g_backgroundTag; }
BackgroundScope::BackgroundScope(const QString& tag) : m_previous(g_backgroundTag) { ++g_backgroundDepth; g_backgroundTag = tag; }
BackgroundScope::~BackgroundScope() { --g_backgroundDepth; g_backgroundTag = m_previous; }
int promote(const QString& tag) { return engine().promote(tag); }

void get(const Request& rIn, QObject* context, Callback cb) {
  Request r = rIn;
  if (g_backgroundDepth > 0) { r.background = true; r.backgroundTag = g_backgroundTag; }
  Job j{r, mergeKeyOf(r), {}};
  j.waiters.push_back(Waiter{context, context != nullptr, std::move(cb), r.ttlSec, r.staleOnError});
  if (r.ttlSec > 0) {
    Reply c;
    if (readCache(r, c, r.ttlSec)) { engine().deliverLater(std::move(j.waiters), c); return; }
  }
  engine().enqueue(std::move(j));
}

Reply cached(const Request& r) { Reply c; if (!readCache(r, c)) c = Reply{}; return c; }
void invalidate(const Request& r) { removeCache(r); }

int retryAfterSeconds(const QByteArray& value, const QDateTime& nowUtc) {
  const QByteArray v = value.trimmed();
  if (v.isEmpty()) return -1;
  bool num = true;
  for (char c : v) if (c < '0' || c > '9') { num = false; break; }
  if (num) {
    if (v.size() > 9) return kRetryAfterCapSec;   // past int, and far past the cap anyway
    return std::min(v.toInt(), kRetryAfterCapSec);
  }
  // IMF-fixdate "Wed, 21 Oct 2026 07:28:00 GMT" (the obsolete RFC 850 / asctime forms are not sent by any source)
  // (Qt::RFC2822Date refuses the "GMT" zone, measured: the format is written out, in the C locale's English names)
  QDateTime at = QLocale::c().toDateTime(QString::fromLatin1(v), "ddd, dd MMM yyyy HH:mm:ss 'GMT'");
  if (!at.isValid()) return -1;
  at.setTimeZone(QTimeZone::UTC);
  const qint64 s = nowUtc.secsTo(at);
  return int(std::clamp<qint64>(s, 0, kRetryAfterCapSec));
}
}
