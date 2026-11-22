// Charts tab (C++ port of quant-terminal, MIT, Copyright (c) 2026 Lewis Jackson — see docs/charts/NOTICE.md).
// Port of server.js (see ApiServer.h). The routes are checked in the JS order, in one handler, so a method a route
// doesn't take falls through to the next routes and finally to 404 "Unknown endpoint", as in the JS.
#include "charts/data/ApiServer.h"
#include "core/Startup.h"
#include "charts/data/CustomFiles.h"
#include "charts/data/Alerts.h"
#include "charts/data/DepthStream.h"
#include "charts/data/Feeds.h"
#include "charts/data/Imports.h"
#include "charts/data/MarketInfo.h"
#include "charts/data/Providers.h"
#include "charts/data/Scan.h"
#include "charts/data/Store.h"
#include "charts/core/Backtest.h"
#include "charts/core/CustomJs.h"
#include "charts/core/Quant.h"
#include "charts/core/Series.h"
#include "core/Settings.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QLockFile>
#include <QHttpHeaders>
#include <QHttpServer>
#include <QHttpServerConfiguration>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>
#include <cmath>
#include <unordered_set>

namespace charts::api {

// A connection's key: peer address and port (see TcpServer::byPeerPort).
static QString peerKey(const QHostAddress& a, quint16 port) { return a.toString() + QLatin1Char('#') + QString::number(port); }

using data::Error;
using bt::jsString;
using bt::jsTruthy;
using StatusCode = QHttpServerResponder::StatusCode;

// The quant-terminal release this API is a port of (package.json "version"): what /api/health calls `version`.
static const char* kPortedVersion = "1.0.0";

// ---------------------------------------------------------------------------------------------- the TCP side
// Our own QTcpServer, so the sockets are known: an /api/events stream is dropped when its client goes away, and each
// request's head is looked at before QHttpServer reads its body (the gate below).
//
// The gate. QHttpServer reads a whole request body before any handler runs, with one body limit for the whole
// server; the JS checks the access rules first and stops reading a body at its route's limit (bodyLimit), and never
// reads the body of a route that doesn't use it (readsBody). So our readyRead slot is connected before QHttpServer's,
// and when the connection is between two requests (it is new, or handle() got its last request: Qt reads one request
// at a time) the bytes waiting start with the next request's head, and the gate decides from it:
// - a request the access rules refuse that carries a body: 403 (the JS text) and the connection is closed, the body
//   is never read (a web page posting to 127.0.0.1 costs nothing);
// - a body on a route that doesn't read it (GET routes, DELETE, POST /api/alerts/check...): the head is handed to Qt
//   without its Content-Length / Transfer-Encoding (taken off the socket and put back with ungetChar), and the gate
//   drops the body's bytes as they come (counted, or by following the chunks), before Qt sees them: the route answers
//   as usual, whatever the body's size, and nothing of it is kept ("100 Continue" is sent first when asked for, as Node);
// - a Content-Length over the route's limit: 413 with the JS error JSON, closed, nothing read;
// - otherwise Qt's body limit becomes that route's limit, set again before each read of this socket (the limit is
//   the server's; Qt checks it against Content-Length when the head is parsed, and against each chunk's size as a
//   chunked body comes). Qt's own 413 is a bare status line: GateSocket swaps it for the JS JSON answer of the route
//   when Qt writes it, and the connection is closed after the same drain as the gate's own refusals.
// A head that hasn't fully arrived when the gate first sees it keeps the 2 MB default (local tools send it in one
// write), and handle() then reads the path from Qt's URL.

// The client sockets: Qt's 413 (a parser error, before any handler) becomes the JS answer, see the gate.
class GateSocket : public QTcpSocket {
public:
  using QTcpSocket::QTcpSocket;
  QByteArray tooLarge;            // the JS 413 answer of the request being read ({"error": ...}); empty after handle()
  std::function<void()> onTooLarge;   // the gate takes the connection over (drain, then close)
  bool holdOpen = false;          // Qt's disconnectFromHost is ignored while the gate drains
  void disconnectFromHost() override {
    if (!holdOpen) QTcpSocket::disconnectFromHost();
  }
  void closeNow() {
    holdOpen = false;
    QTcpSocket::disconnectFromHost();
  }
protected:
  qint64 writeData(const char* data, qint64 len) override {
    // QHttpServerParser::sendError writes "HTTP/1.1 413 <reason>\r\nConnection: close\r\n\r\n" in one write.
    if (!tooLarge.isEmpty() && len >= 12 && (qstrncmp(data, "HTTP/1.1 413", 12) == 0 || qstrncmp(data, "HTTP/1.0 413", 12) == 0)) {
      const QByteArray body = std::exchange(tooLarge, QByteArray());
      const QByteArray answer = "HTTP/1.1 413 Payload Too Large\r\nContent-Type: application/json\r\nCache-Control: no-store"
                                "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
      QTcpSocket::writeData(answer.constData(), answer.size());
      if (onTooLarge) onTooLarge();
      return len;
    }
    return QTcpSocket::writeData(data, len);
  }
};
static void closeSocket(QTcpSocket* s) {
  if (auto* g = dynamic_cast<GateSocket*>(s)) g->closeNow();
  else s->disconnectFromHost();
}

// A chunked body followed as it comes (to drop it): chunk sizes, data, CRLFs, trailers, up to the end.
struct Chunks {
  enum State { Size, Ext, Data, DataEnd, Trailer, Done, Bad } state = Size;
  qint64 left = 0;      // Size: the size read so far; Data: bytes of the chunk still to come
  int digits = 0;       // Size: hex digits read
  int line = 0;         // characters on the current size/trailer line (bounded)
  // Bytes of `d` that belong to the body (all of them unless it ends inside); state Done or Bad says how it ended.
  qsizetype feed(const char* d, qsizetype n) {
    qsizetype i = 0;
    while (i < n && state != Done && state != Bad) {
      const char c = d[i];
      switch (state) {
        case Size:
        case Ext:
          ++i;
          if (++line > 4096) { state = Bad; break; }
          if (c == '\n') {
            if (digits == 0) { state = Bad; break; }
            state = left == 0 ? Trailer : Data;
            line = 0;
            break;
          }
          if (state == Ext || c == '\r') break;
          if (c == ';' || c == ' ' || c == '\t') { state = Ext; break; }
          {
            const int h = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (h < 0 || ++digits > 15) { state = Bad; break; }
            left = left * 16 + h;
          }
          break;
        case Data: {
          const qsizetype k = qsizetype(std::min<qint64>(left, n - i));
          i += k;
          left -= k;
          if (left == 0) state = DataEnd;
          break;
        }
        case DataEnd:   // CRLF (or a bare LF) after the chunk's data
          ++i;
          if (c == '\n') { state = Size; digits = 0; left = 0; line = 0; }
          else if (c != '\r') state = Bad;
          break;
        case Trailer:   // trailer lines up to an empty one
          ++i;
          if (c == '\n') {
            if (line == 0) state = Done;
            line = 0;
          } else if (c != '\r' && ++line > 8192) state = Bad;
          break;
        default: break;
      }
    }
    return i;
  }
};

class Server::TcpServer : public QTcpServer {
public:
  explicit TcpServer(Server* owner) : owner_(owner) {}
  struct Conn {
    QPointer<QTcpSocket> socket;
    qint64 limit = kDefaultBodyLimit;   // Qt's body limit while it reads this socket's current request
    QByteArray target;                  // the raw request target of the last head seen (handle() decodes it as the JS)
    bool atBoundary = true;             // the next bytes start a request head (new connection, or handle() got one)
    bool closing = false;               // answered by the gate: whatever still comes is dropped
    qint64 drain = 0;                   // body bytes still to drop before closing (so the client reads the answer)
    bool dropping = false;              // dropping the body of a request whose route doesn't read it
    qint64 dropLeft = 0;                // ... its bytes still to come (Content-Length), or
    std::optional<Chunks> dropChunks;   // ... its chunks, followed to their end
    qsizetype pendingHead = 0;          // the head put back for Qt, not read by it yet (Qt busy with the one before)
  };
  // by peer address and port: two local clients on different loopback addresses (127.0.0.1, 127.0.0.2…) can use the
  // same source port, and shared one connection state when it was keyed by the port alone
  QHash<QString, Conn> byPeerPort;
  QTcpSocket* socketOf(const QString& peer) const { return byPeerPort.value(peer).socket; }
protected:
  void incomingConnection(qintptr fd) override {
    auto* s = new GateSocket(this);
    if (!s->setSocketDescriptor(fd)) { delete s; return; }
    const QString p = peerKey(s->peerAddress(), s->peerPort());
    byPeerPort.insert(p, Conn{s, kDefaultBodyLimit, {}, true, false, 0, false, 0, std::nullopt, 0});
    s->onTooLarge = [this, s, p] { tookOver(s, p); };
    connect(s, &QObject::destroyed, this, [this, p] {
      auto it = byPeerPort.find(p);
      if (it != byPeerPort.end() && it->socket.isNull()) byPeerPort.erase(it);
    });
    // Before QHttpServer connects its own slot (it takes the socket from addPendingConnection): ours runs first.
    connect(s, &QIODevice::readyRead, this, [this, s, p] { gate(s, p); });
    addPendingConnection(s);
  }
private:
  Server* owner_;
  void gate(QTcpSocket* s, const QString& p);
  void refuse(QTcpSocket* s, Conn& c, qint64 bodyLeft, int status, const QByteArray& reason, const QByteArray& type,
              const QByteArray& body);
  void closeAfterDrain(QTcpSocket* s, Conn& c, qint64 bodyLeft, bool insideQt = false);
  void tookOver(GateSocket* s, const QString& p);
  void ignoreBody(QTcpSocket* s, Conn& c, const QByteArray& head, qsizetype headSize, qint64 contentLength, bool chunked,
                  bool expectContinue);
  bool drop(QTcpSocket* s, Conn& c);
  bool dropBody(QTcpSocket* s, Conn& c);
};

namespace {
// What the gate needs from a request head (the bytes up to the empty line).
struct Head {
  bool complete = false;
  qsizetype size = 0;   // bytes up to and with the empty line
  QByteArray method, target;
  QByteArray host, origin, xqt, fetchSite;
  bool hasOrigin = false;
  qint64 contentLength = -1;   // -1: none (or not a number: Qt refuses it)
  bool chunked = false;
  bool expectContinue = false;
  int lengths = 0;             // Content-Length lines (Qt refuses more than one)
};
Head parseHead(const QByteArray& data) {
  Head h;
  qsizetype start = 0;
  while (data.mid(start, 2) == "\r\n") start += 2;   // empty lines before a request line are allowed
  const qsizetype end = data.indexOf("\r\n\r\n", start);
  if (end < 0) return h;
  const QList<QByteArray> lines = data.mid(start, end - start).split('\n');
  const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
  if (first.size() != 3) return h;
  h.method = first[0];
  h.target = first[1];
  bool seenHost = false, seenXqt = false, seenLength = false;
  for (qsizetype i = 1; i < lines.size(); ++i) {
    const QByteArray& line = lines[i];
    const qsizetype colon = line.indexOf(':');
    if (colon <= 0) continue;
    const QByteArray name = line.left(colon).trimmed().toLower(), value = line.mid(colon + 1).trimmed();
    // The first of each, as QHttpHeaders::value gives handle().
    if (name == "host" && !seenHost) { h.host = value; seenHost = true; }
    else if (name == "origin" && !h.hasOrigin) { h.origin = value; h.hasOrigin = true; }
    else if (name == "x-qt" && !seenXqt) { h.xqt = value; seenXqt = true; }
    else if (name == "sec-fetch-site" && h.fetchSite.isEmpty()) h.fetchSite = value;
    else if (name == "content-length") {
      ++h.lengths;
      if (seenLength) continue;
      bool ok = false;
      const qint64 n = value.toLongLong(&ok);
      h.contentLength = ok && n >= 0 ? n : -1;
      seenLength = true;
    } else if (name == "transfer-encoding" && value.toLower().contains("chunked")) h.chunked = true;
    else if (name == "expect" && value.toLower() == "100-continue") h.expectContinue = true;
  }
  h.complete = true;
  h.size = end + 4;
  return h;
}
}  // namespace

void Server::TcpServer::gate(QTcpSocket* s, const QString& p) {
  auto it = byPeerPort.find(p);
  if (it == byPeerPort.end()) return;
  Conn& c = *it;
  if (c.closing) {   // answered already: drop the rest of the body without keeping it, then close
    if (s->isTransactionStarted()) s->commitTransaction();
    c.drain -= s->readAll().size();
    if (c.drain <= 0) closeSocket(s);
    return;
  }
  if (c.dropping) {   // the body of a request whose route doesn't read it; what follows it is the next request
    if (!drop(s, c)) return;
    if (c.pendingHead > 0) return;   // Qt hasn't read that request's head yet: handle() marks the next one's start
    c.atBoundary = true;   // Qt had the head (handle() ran or will): the next bytes start a new request
  }
  if (c.atBoundary && s->bytesAvailable() > 0) {
    // Decided once per request, from its head; a head still in pieces is given up on (Qt may read part of it before
    // the rest comes, so later bytes no longer start a head): the default limit, and handle() reads Qt's URL.
    c.atBoundary = false;
    const QByteArray waiting = s->peek(64 * 1024);
    const Head h = parseHead(waiting);
    // The body bytes not arrived yet (what the gate drops after answering), -1 for a chunked body.
    const qint64 bodyLeft = h.chunked ? -1 : h.contentLength - (s->bytesAvailable() - h.size);
    c.target = h.complete ? h.target : QByteArray();
    c.limit = kDefaultBodyLimit;
    BodyLimit lim = bodyLimit("POST", QString());   // the default: 2 MB, "Request too large"
    if (h.complete) {
      const bool body = h.contentLength > 0 || h.chunked;
      if (body && !allowed(QString::fromUtf8(h.host), h.method, QString::fromUtf8(h.xqt), QString::fromUtf8(h.origin), h.hasOrigin,
                           QString::fromUtf8(h.fetchSite))) {
        refuse(s, c, bodyLeft, 403, "Forbidden", "text/plain; charset=utf-8", "Forbidden: Deepwater only answers to this computer.");
        return;
      }
      QString path;
      const qsizetype q = h.target.indexOf('?');
      const QByteArray rawPath = q < 0 ? h.target : h.target.left(q);
      if (!decodeUriComponent(QString::fromLatin1(rawPath), &path)) path = QString::fromLatin1(rawPath);
      // A body Qt would refuse anyway (two Content-Lengths, both headers, a bad length) is left to it (400).
      const bool plain = h.lengths <= 1 && !(h.chunked && h.lengths > 0) && (h.lengths == 0 || h.contentLength >= 0);
      if (body && plain && !readsBody(h.method, path)) {
        ignoreBody(s, c, waiting.left(h.size), h.size, h.contentLength, h.chunked, h.expectContinue);
        if (auto* g = dynamic_cast<GateSocket*>(s)) g->tooLarge.clear();   // Qt gets no body: nothing to refuse
        owner_->setBodyLimit(c.limit);
        return;
      }
      lim = bodyLimit(h.method, path);
      if (h.contentLength > lim.bytes) {
        refuse(s, c, bodyLeft, 413, "Payload Too Large", "application/json",
               store::stringify(QJsonObject{{"error", lim.message}}, 0).toUtf8());
        return;
      }
      c.limit = lim.bytes;
    }
    // Qt's own 413 for this request (a chunked body over the limit, or the default limit of a head that came in
    // pieces) is answered with the JS JSON (GateSocket).
    if (auto* g = dynamic_cast<GateSocket*>(s))
      g->tooLarge = store::stringify(QJsonObject{{"error", lim.message}}, 0).toUtf8();
  }
  owner_->setBodyLimit(c.limit);
}

// The head goes back to Qt without its body headers; the body's bytes that came with it are dropped, the rest as they
// come (drop). The bytes after the body (a next request) stay where they are, behind the head.
void Server::TcpServer::ignoreBody(QTcpSocket* s, Conn& c, const QByteArray& head, qsizetype headSize,
                                   qint64 contentLength, bool chunked, bool expectContinue) {
  QByteArray rewritten;
  for (const QByteArray& line : head.split('\n')) {
    const QByteArray name = line.left(line.indexOf(':')).trimmed().toLower();
    if (name == "content-length" || name == "transfer-encoding" || name == "expect") continue;
    rewritten += line + '\n';
  }
  rewritten.chop(1);   // split() added one line after the last "\n"
  if (s->isTransactionStarted()) s->commitTransaction();   // Qt may have opened one (nothing read yet)
  s->skip(headSize);
  c.dropping = true;
  c.dropLeft = chunked ? 0 : contentLength;
  c.dropChunks = chunked ? std::optional<Chunks>(Chunks{}) : std::nullopt;
  c.limit = kDefaultBodyLimit;
  c.pendingHead = 0;
  drop(s, c);   // whether the whole body was there or not, handle() marks the next request's start
  // Node answers "100 Continue" by itself when the client waits for it; Qt won't (it sees no body).
  if (expectContinue) s->write("HTTP/1.1 100 Continue\r\n\r\n");
  for (qsizetype i = rewritten.size() - 1; i >= 0; --i) s->ungetChar(rewritten.at(i));
  c.pendingHead = rewritten.size();   // Qt reads it right after this slot, or once done with the request before
}

// Drops what has come of the ignored body; true when it is over (then the bytes left are the next request's). The
// head put back for Qt, if Qt hasn't read it yet, stays in front.
bool Server::TcpServer::drop(QTcpSocket* s, Conn& c) {
  if (s->isTransactionStarted()) s->commitTransaction();   // Qt has a bodyless head only: nothing of it is half read
  QByteArray head;
  if (c.pendingHead > 0) head = s->read(c.pendingHead);
  const bool over = dropBody(s, c);
  for (qsizetype i = head.size() - 1; i >= 0; --i) s->ungetChar(head.at(i));
  return over;
}

bool Server::TcpServer::dropBody(QTcpSocket* s, Conn& c) {
  if (!c.dropChunks) {
    const qint64 n = std::min(c.dropLeft, s->bytesAvailable());
    s->skip(n);
    c.dropLeft -= n;
    if (c.dropLeft > 0) return false;
    c.dropping = false;
    return true;
  }
  while (s->bytesAvailable() > 0) {
    const QByteArray piece = s->peek(256 * 1024);
    const qsizetype used = c.dropChunks->feed(piece.constData(), piece.size());
    s->skip(used);
    if (c.dropChunks->state == Chunks::Done) {
      c.dropChunks.reset();
      c.dropping = false;
      return true;
    }
    if (c.dropChunks->state == Chunks::Bad) {   // not a chunked body: nothing after it can be read, close
      c.dropChunks.reset();
      c.dropping = false;
      c.closing = true;
      c.drain = 0;
      s->readAll();
      closeSocket(s);
      return false;
    }
  }
  return false;
}

// Answers without QHttpServer and closes. The client is usually still sending its body: up to kMaxDrain more of it
// (all of a shorter one) is read and dropped first (nothing kept), for at most 2 s, so the client gets to read the
// answer (closing on unread data resets the connection, and many clients then lose the answer); the rest of a
// longer body is cut off.
void Server::TcpServer::refuse(QTcpSocket* s, Conn& c, qint64 bodyLeft, int status, const QByteArray& reason,
                               const QByteArray& type, const QByteArray& body) {
  if (auto* g = dynamic_cast<GateSocket*>(s)) g->tooLarge.clear();   // our own 413 goes out as it is
  s->write("HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\nContent-Type: " + type +
           "\r\nCache-Control: no-store\r\nContent-Length: " + QByteArray::number(body.size()) +
           "\r\nConnection: close\r\n\r\n" + body);
  closeAfterDrain(s, c, bodyLeft);
}

// insideQt: called from Qt's own read (its parser answered 413): its transaction and bytes are left to it, the next
// readyRead (ours alone now) frees them.
void Server::TcpServer::closeAfterDrain(QTcpSocket* s, Conn& c, qint64 bodyLeft, bool insideQt) {
  constexpr qint64 kMaxDrain = 16'000'000;
  // QHttpServer never sees this socket's bytes again: its slot is disconnected (the one running now is skipped).
  disconnect(s, &QIODevice::readyRead, nullptr, nullptr);
  const QString p = peerKey(s->peerAddress(), s->peerPort());
  connect(s, &QIODevice::readyRead, this, [this, s, p] { gate(s, p); });
  c.closing = true;
  c.dropping = false;
  c.dropChunks.reset();
  c.target.clear();
  c.drain = bodyLeft < 0 ? kMaxDrain : std::min(bodyLeft, kMaxDrain);   // chunked: as much as for a long one
  if (!insideQt) {
    if (s->isTransactionStarted()) s->commitTransaction();   // Qt may have opened one (nothing read yet): free what we drop
    s->readAll();
  }
  if (c.drain <= 0) closeSocket(s);
  else QTimer::singleShot(2000, s, [s] { closeSocket(s); });
}

// Qt answered 413 while reading a body (GateSocket wrote the JS answer instead): Qt is done with this connection and
// is about to close it; the gate drops what the client still sends for a moment first, as after its own refusals.
void Server::TcpServer::tookOver(GateSocket* s, const QString& p) {
  auto it = byPeerPort.find(p);
  if (it == byPeerPort.end()) return;
  s->holdOpen = true;
  closeAfterDrain(s, *it, -1, true);
}

struct Server::Req {
  QByteArray method;
  QString path;                    // decoded (JS p)
  QHash<QString, QString> q;       // Object.fromEntries(url.searchParams)
  QByteArray body;
  QString peer;   // peerKey() of the connection
};

namespace {
// ---------------------------------------------------------------------------------------------- helpers
// The responders of HEAD requests: their answers carry the headers only (as Node does for HEAD).
std::unordered_set<const QHttpServerResponder*>& headResponders() {
  static std::unordered_set<const QHttpServerResponder*> s;
  return s;
}
bool isHead(const std::shared_ptr<QHttpServerResponder>& r) { return headResponders().count(r.get()) > 0; }

void sendRaw(const std::shared_ptr<QHttpServerResponder>& r, int status, const QByteArray& body, const QByteArray& type,
             const QByteArray& cacheControl = "no-store") {
  QHttpHeaders h;
  h.append(QHttpHeaders::WellKnownHeader::ContentType, type);
  h.append(QHttpHeaders::WellKnownHeader::CacheControl, cacheControl);
  if (isHead(r)) r->write(h, StatusCode(status));   // headers only (Qt says content-length: 0; it can't take ours)
  else r->write(body, h, StatusCode(status));
}
// The JS send(res, status, body): strings as text/plain, anything else as JSON.stringify(body).
void sendJson(const std::shared_ptr<QHttpServerResponder>& r, int status, const QJsonValue& v) {
  sendRaw(r, status, store::stringify(v, 0).toUtf8(), "application/json");
}
void sendText(const std::shared_ptr<QHttpServerResponder>& r, int status, const QString& t) {
  sendRaw(r, status, t.toUtf8(), "text/plain; charset=utf-8");
}
// The JS catch: upstream failures are a bad gateway, whatever status the provider used.
void sendError(const std::shared_ptr<QHttpServerResponder>& r, const Error& e) {
  const int code = e.upstream ? 502 : (e.status >= 400 && e.status < 600 ? e.status : 500);
  sendJson(r, code, QJsonObject{{"error", e.message}});
}
Error httpError(int status, const QString& message) { return Error{message, status, false, {}}; }

struct Body { QJsonObject value; std::optional<Error> error; };
// The JS readBody(req, limit): {} for an empty body, else a JSON object.
Body readBody(const QByteArray& raw, qint64 limit = kDefaultBodyLimit) {
  if (raw.size() > limit) return {{}, httpError(413, "Request too large")};
  const QString text = QString::fromUtf8(raw);
  if (text.isEmpty()) return {};
  QJsonParseError pe;
  const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &pe);
  if (pe.error != QJsonParseError::NoError) {
    // QJsonDocument only takes objects and arrays: a scalar ("x", 5, null) is valid JSON but not an object.
    QJsonParseError pe2;
    const QJsonDocument wrapped = QJsonDocument::fromJson("[" + text.toUtf8() + "]", &pe2);
    if (pe2.error == QJsonParseError::NoError && wrapped.array().size() == 1)
      return {{}, httpError(400, "The request body must be a JSON object")};
    return {{}, httpError(400, "The request body isn't valid JSON")};
  }
  if (!doc.isObject()) return {{}, httpError(400, "The request body must be a JSON object")};
  return {doc.object(), std::nullopt};
}
// The JS readText(req, limit): a raw text body (CSV imports).
std::optional<Error> textTooLarge(const QByteArray& raw, qint64 limit) {
  if (raw.size() <= limit) return std::nullopt;
  return httpError(413, QStringLiteral("The file is over %1 MB").arg(qRound64(double(limit) / 1e6)));
}

QJsonValue orElse(const QJsonValue& v, const QJsonValue& def) { return jsTruthy(v) ? v : def; }   // JS ||
// Number(x) || def as an int for the providers' limits (they clamp it).
int limitOf(const QJsonValue& v, int def) {
  const double d = bt::jsNum(v);
  if (!std::isfinite(d) || d == 0) return def;
  return int(std::max(-1e9, std::min(d, 1e9)));
}
int limitOf(const QString& s, int def) {
  if (s.isNull()) return def;
  return limitOf(QJsonValue(data::jsNumber(s)), def);
}

QHash<QString, QString> parseQuery(const QByteArray& raw) {
  QHash<QString, QString> q;
  for (const QByteArray& part : raw.split('&')) {
    if (part.isEmpty()) continue;
    const int eq = part.indexOf('=');
    QByteArray k = eq < 0 ? part : part.left(eq), v = eq < 0 ? QByteArray() : part.mid(eq + 1);
    k.replace('+', ' ');
    v.replace('+', ' ');
    q.insert(QString::fromUtf8(QByteArray::fromPercentEncoding(k)), QString::fromUtf8(QByteArray::fromPercentEncoding(v)));
  }
  return q;
}

QString platformName() {
#if defined(Q_OS_IOS)   // before Q_OS_MACOS's neighbours: an iPhone answered "linux"
  return QStringLiteral("ios");
#elif defined(Q_OS_MACOS)
  return QStringLiteral("darwin");
#elif defined(Q_OS_WIN)
  return QStringLiteral("win32");
#else
  return QStringLiteral("linux");
#endif
}

QString mimeOf(const QString& file) {
  static const QHash<QString, QString> mime = {
      {"html", "text/html; charset=utf-8"}, {"js", "text/javascript; charset=utf-8"},
      {"mjs", "text/javascript; charset=utf-8"}, {"css", "text/css; charset=utf-8"},
      {"json", "application/json"}, {"svg", "image/svg+xml"}, {"png", "image/png"},
      {"ico", "image/x-icon"}, {"txt", "text/plain; charset=utf-8"},
  };
  const QString ext = QFileInfo(file).suffix().toLower();
  return mime.value(ext, "application/octet-stream");
}

bool usesCustom(const QJsonObject& strategy) {
  const QJsonObject inds = strategy.value("indicators").toObject();
  for (auto it = inds.begin(); it != inds.end(); ++it)
    // custom JavaScript indicators run on the GUI thread only; Pine ones run anywhere (better on a worker)
    if (it.value().toObject().value("id").toString().startsWith("custom:")) return true;
  return false;
}
}  // namespace

// ---------------------------------------------------------------------------------------------- pure pieces
// The server only answers to this machine. Writes need a custom header, which a random website can't add to a
// cross-site request, so other pages can't drive your terminal.
bool allowed(const QString& hostHeader, const QByteArray& method, const QString& xqt, const QString& origin,
             bool hasOrigin, const QString& fetchSite) {
  static const QRegularExpression port(QStringLiteral(":\\d+$")), brackets(QStringLiteral("^\\[|\\]$"));
  QString host = hostHeader;
  host.remove(port);
  host.remove(brackets);
  static const QStringList ok = {"localhost", "127.0.0.1", "::1"};   // the JS adds LAN addresses only for HOST != 127.0.0.1
  if (!ok.contains(host)) return false;
  // new URL(origin).host === req.headers.host; "null" and other opaque origins don't parse
  auto originIsHost = [&] {
    const QUrl u(origin, QUrl::StrictMode);
    if (!u.isValid() || u.scheme().isEmpty() || u.host().isEmpty()) return false;
    static const QHash<QString, int> defaults = {{"http", 80}, {"https", 443}, {"ws", 80}, {"wss", 443}, {"ftp", 21}};
    QString h = u.host(QUrl::FullyEncoded).toLower();
    if (h.contains(':')) h = "[" + h + "]";
    if (u.port() >= 0 && u.port() != defaults.value(u.scheme().toLower(), -1)) h += ":" + QString::number(u.port());
    return h == hostHeader;
  };
  // ADDED: any method. A browser marks what a page of another site sends (Sec-Fetch-Site: cross-site) and adds an
  // Origin to cross-origin fetches; curl and scripts on this computer send neither.
  if (fetchSite.trimmed().compare(QLatin1String("cross-site"), Qt::CaseInsensitive) == 0) return false;
  if (hasOrigin && !origin.isEmpty() && !originIsHost()) return false;
  if (method != "GET" && method != "HEAD" && xqt != "1") return false;
  return true;
}

QString slug(const QString& s) { return data::customSlug(s); }

bool decodeUriComponent(const QString& in, QString* out) {
  QByteArray bytes;
  const QByteArray src = in.toUtf8();
  const auto hex = [](char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (qsizetype i = 0; i < src.size(); ++i) {
    if (src[i] != '%') { bytes.append(src[i]); continue; }
    if (i + 2 >= src.size()) return false;   // "%" needs two hex digits after it
    const int a = hex(src[i + 1]), b = hex(src[i + 2]);
    if (a < 0 || b < 0) return false;
    bytes.append(char(a * 16 + b));
    i += 2;
  }
  QStringDecoder dec(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
  const QString s = dec(bytes);
  if (dec.hasError()) return false;
  *out = s;
  return true;
}

int envPort(const QString& value) {
  bool ok = false;
  const double d = value.trimmed().toDouble(&ok);
  if (!ok || d == 0) return 3777;   // Number(process.env.PORT) || 3777: unset, "", "abc", "0"
  return d >= 1 && d <= 65535 && d == std::trunc(d) ? int(d) : -1;
}

BodyLimit bodyLimit(const QByteArray& method, const QString& path) {
  if (method == "POST" && path == "/api/import/bars") return {qint64(300e6), QStringLiteral("The file is over 300 MB")};
  if (method == "POST" && path == "/api/import/signal") return {qint64(100e6), QStringLiteral("The file is over 100 MB")};
  if (method == "PUT" && path == "/api/state") return {qint64(20e6), QStringLiteral("Request too large")};
  return {kDefaultBodyLimit, QStringLiteral("Request too large")};
}

bool readsBody(const QByteArray& m, const QString& p) {
  // The routes of route() that call readBody / textTooLarge, in their order (the JS readBody / readText calls).
  if (m == "POST")
    return p == "/api/scan" || p == "/api/backtest" || p == "/api/import/bars" || p == "/api/import/signal" ||
           p == "/api/alerts" || p == "/api/webhook-test" || p == "/api/custom";
  if (m == "PUT") return p == "/api/state";
  if (m == "PATCH") return p.startsWith("/api/alerts/") && p != "/api/alerts/log";
  return false;
}

// ---------------------------------------------------------------------------------------------- server
Server& Server::instance() {
  static Server* s = new Server;
  return *s;
}

Server::Server() {
  // server-sent events: alerts pop up in every client (the JS alerts.onFire -> broadcast("alert", e))
  connect(&alerts::Alerts::instance(), &alerts::Alerts::fired, this,
          [this](const QJsonObject& e) { broadcast(QStringLiteral("alert"), e); });
  keepAlive_ = new QTimer(this);
  keepAlive_->setInterval(25000);
  connect(keepAlive_, &QTimer::timeout, this, [this] {
    for (auto& c : clients_) c.res->writeChunk(": keep-alive\n\n");
  });
}

void Server::setPage(const QString& path, const QByteArray& html) {
  if (html.isEmpty()) pages_.remove(path);
  else pages_.insert(path, html);
}

bool Server::isListening() const { return tcp_ && tcp_->isListening(); }
QString Server::url() const { return QStringLiteral("http://localhost:%1").arg(port_); }

bool Server::start(int port, int tries) {
  if (isListening()) return true;
  if (port < 0) {
    const QString env = qEnvironmentVariable("PORT");
    port = envPort(env);
    if (port < 0) {   // the JS listen() would throw a RangeError; the app keeps its API on the default port instead
      qWarning().noquote() << "[charts] PORT" << env.trimmed() << "isn't a port number (1 to 65535): using 3777";
      port = 3777;
    }
  }
  error_.clear();
  if (port > 65535) {   // quint16 would wrap it (70000 -> 4464)
    error_ = QStringLiteral("port %1 is out of range (0 to 65535)").arg(port);
    return false;
  }
  auto* tcp = new TcpServer(this);
  for (;;) {
    if (tcp->listen(QHostAddress::LocalHost, quint16(port))) break;
    if (tcp->serverError() == QAbstractSocket::AddressInUseError && tries > 0 && port > 0 && port < 65535) {
      ++port;
      --tries;
      continue;
    }
    error_ = tcp->errorString();
    delete tcp;
    return false;
  }
  http_ = new QHttpServer(this);
  bodyLimit_ = 0;
  setBodyLimit(kDefaultBodyLimit);   // the gate raises it for the routes that take more (bodyLimit)
  http_->setMissingHandler(this, [this](const QHttpServerRequest& rq, QHttpServerResponder& rs) { handle(rq, rs); });
  if (!http_->bind(tcp)) {
    error_ = QStringLiteral("could not bind the HTTP server");
    delete http_;
    http_ = nullptr;
    delete tcp;
    return false;
  }
  tcp_ = tcp;
  port_ = tcp->serverPort();
  keepAlive_->start();
  return true;
}

void Server::setBodyLimit(qint64 bytes) {
  if (!http_ || bytes == bodyLimit_) return;   // setConfiguration also restarts Qt's keep-alive timer: only on a change
  QHttpServerConfiguration cfg = http_->configuration();
  cfg.setMaximumBodySize(bytes);
  http_->setConfiguration(cfg);
  bodyLimit_ = bytes;
}

void Server::stop() {
  keepAlive_->stop();
  for (auto& c : clients_) {
    QObject::disconnect(c.conn);
    c.res->writeEndChunked(QByteArray());
  }
  clients_.clear();
  if (http_) {
    delete http_;   // owns the TCP server and the connections
    http_ = nullptr;
  }
  port_ = 0;
}

void Server::broadcast(const QString& type, const QJsonValue& payload) {
  const QByteArray msg = ("event: " + type + "\ndata: " + store::stringify(payload, 0) + "\n\n").toUtf8();
  for (auto& c : clients_) c.res->writeChunk(msg);
}

void Server::dropClient(quint64 id) {
  auto it = clients_.find(id);
  if (it == clients_.end()) return;
  QObject::disconnect(it->conn);
  clients_.erase(it);   // destroys the responder: the connection is released
}

void Server::openEvents(const std::shared_ptr<Req>& req, const Resp& res) {
  QHttpHeaders h;
  h.append(QHttpHeaders::WellKnownHeader::ContentType, "text/event-stream");
  h.append(QHttpHeaders::WellKnownHeader::CacheControl, "no-cache");
  if (isHead(res)) {   // headers only; no stream to keep open
    res->write(h, StatusCode::Ok);
    return;
  }
  res->writeBeginChunked(h, StatusCode::Ok);
  res->writeChunk("retry: 3000\n\n");
  const quint64 id = ++nextClient_;
  Client c{res, {}};
  if (tcp_) {
    if (QTcpSocket* s = tcp_->socketOf(req->peer))
      c.conn = connect(s, &QTcpSocket::disconnected, this, [this, id] { dropClient(id); });
  }
  clients_.insert(id, c);
}

// ---------------------------------------------------------------------------------------------- custom indicators
// One implementation shared with the indicator editor (charts::data CustomFiles: one customErrors map, as in the JS).
QJsonArray Server::listCustom() { return data::listCustomJson(); }
QString Server::loadCustom(const QString& file) { return data::loadCustom(file); }
void Server::loadAllCustom() { data::loadAllCustom(); }

// ---------------------------------------------------------------------------------------------- requests
void Server::handle(const QHttpServerRequest& rq, QHttpServerResponder& rs) {
  auto* responder = new QHttpServerResponder(std::move(rs));
  if (rq.method() == QHttpServerRequest::Method::Head) headResponders().insert(responder);
  const Resp res(responder, [](QHttpServerResponder* r) {
    headResponders().erase(r);
    delete r;
  });
  auto req = std::make_shared<Req>();
  switch (rq.method()) {
    case QHttpServerRequest::Method::Get: req->method = "GET"; break;
    case QHttpServerRequest::Method::Head: req->method = "HEAD"; break;
    case QHttpServerRequest::Method::Post: req->method = "POST"; break;
    case QHttpServerRequest::Method::Put: req->method = "PUT"; break;
    case QHttpServerRequest::Method::Delete: req->method = "DELETE"; break;
    case QHttpServerRequest::Method::Patch: req->method = "PATCH"; break;
    case QHttpServerRequest::Method::Options: req->method = "OPTIONS"; break;
    case QHttpServerRequest::Method::Trace: req->method = "TRACE"; break;
    case QHttpServerRequest::Method::Connect: req->method = "CONNECT"; break;
    default: req->method = "UNKNOWN"; break;
  }
  const QHttpHeaders& hd = rq.headers();
  const QString host = QString::fromUtf8(hd.value(QHttpHeaders::WellKnownHeader::Host).toByteArray());
  const bool hasOrigin = hd.contains(QHttpHeaders::WellKnownHeader::Origin);
  const QString origin = QString::fromUtf8(hd.value(QHttpHeaders::WellKnownHeader::Origin).toByteArray());
  const QString xqt = QString::fromUtf8(hd.value("x-qt").toByteArray());
  const QString fetchSite = QString::fromUtf8(hd.value("sec-fetch-site").toByteArray());
  if (!allowed(host, req->method, xqt, origin, hasOrigin, fetchSite)) {
    sendText(res, 403, "Forbidden: Deepwater only answers to this computer.");
    return;
  }
  const QUrl u = rq.url();
  // The JS decodes the raw target's path once (decodeURIComponent, which throws on a bad escape or invalid UTF-8).
  // The gate kept this request's raw target: used when it is this request's (QUrl reads it to the same path) and
  // plain ASCII. Otherwise (a head that came in pieces), QHttpServerRequest's URL: it keeps a "%" it could not decode
  // as a literal "%" (FullyEncoded "%25"), so a "%" escape left in the once-decoded text that doesn't decode is a
  // "Bad URL" there (which also refuses a literal "%25" followed by such text, like /custom/100%25.js).
  QByteArray rawPath;
  if (tcp_) {
    auto conn = tcp_->byPeerPort.find(peerKey(rq.remoteAddress(), rq.remotePort()));
    const QByteArray target = conn != tcp_->byPeerPort.end() ? conn->target : QByteArray();
    if (conn != tcp_->byPeerPort.end()) {   // Qt has the whole request: the next bytes start the next head
      conn->target.clear();
      if (!conn->dropping) conn->atBoundary = true;   // else the gate marks it when the ignored body is over
      conn->pendingHead = 0;                          // Qt read the head the gate put back
      conn->limit = kDefaultBodyLimit;
      if (auto* g = dynamic_cast<GateSocket*>(conn->socket.data())) g->tooLarge.clear();   // a 413 from now on is ours
    }
    qsizetype cut = target.size();
    for (char c : {'?', '#'})
      if (const qsizetype i = target.indexOf(c); i >= 0) cut = std::min(cut, i);
    const QByteArray path = target.left(cut);
    const bool ascii = std::all_of(path.begin(), path.end(), [](char c) { return uchar(c) > 0x20 && uchar(c) < 0x7f; });
    if (path.startsWith('/') && ascii && QUrl::fromEncoded(path).path(QUrl::FullyEncoded) == u.path(QUrl::FullyEncoded))
      rawPath = path;
  }
  QString once;
  const bool good = !rawPath.isEmpty()
                        ? decodeUriComponent(QString::fromLatin1(rawPath), &req->path)
                        : decodeUriComponent(u.path(QUrl::FullyEncoded), &req->path) &&
                              decodeUriComponent(u.path(QUrl::FullyDecoded), &once);
  if (!good) {
    sendText(res, 400, "Bad URL");
    return;
  }
  if (req->path.isEmpty()) req->path = "/";
  req->q = parseQuery(u.query(QUrl::FullyEncoded).toUtf8());
  req->body = rq.body();
  req->peer = peerKey(rq.remoteAddress(), rq.remotePort());
  try {
    route(req, res);
  } catch (const data::ImportError& e) {
    sendError(res, httpError(e.status, e.message));
  } catch (const std::exception& e) {
    sendJson(res, 500, QJsonObject{{"error", QString::fromUtf8(e.what())}});
  }
}

void Server::route(const std::shared_ptr<Req>& req, const Resp& res) {
  const QString& p = req->path;
  const QByteArray& m = req->method;
  const auto& q = req->q;
  const auto qv = [&](const char* k) { return q.value(QString::fromLatin1(k)); };
  // Callback that answers 200 + toJson(value), or the error.
  const auto answer = [res](auto toJson) {
    return [res, toJson](const auto& r) {
      if (!r.ok()) sendError(res, *r.error);
      else sendJson(res, 200, toJson(r.value));
    };
  };

  if (!p.startsWith("/api/")) {
    // Deepwater has no web UI: the Charts tab is the app. Your custom indicator files are still served.
    if (p == "/" || p == "/index.html") {
      sendJson(res, 200, QJsonObject{
          {"ok", true},
          {"app", "quant-terminal"},
          {"server", "Deepwater"},
          {"message", "The Charts tab's local API (same routes as Quant Terminal), served by Deepwater while it runs. There is no web page here: "
                      "use the Charts tab, or call /api/... (docs/charts/api.md)."},
      });
      return;
    }
    if (const auto page = pages_.constFind(p); page != pages_.cend() && m == "GET") {
      sendRaw(res, 200, *page, "text/html; charset=utf-8");
      return;
    }
    if (p.startsWith("/custom/")) {
      const QString base = QDir::cleanPath(data::customDir());
      const QString file = QDir::cleanPath(base + "/" + p.mid(8));
      if (!file.startsWith(base + "/")) { sendText(res, 403, "Forbidden"); return; }
      QFile f(file);
      if (!QFileInfo(file).isFile() || !f.open(QIODevice::ReadOnly)) { sendText(res, 404, "Not found"); return; }
      sendRaw(res, 200, f.readAll(), mimeOf(file).toUtf8(), "no-cache");
      return;
    }
    sendText(res, 404, "Not found");
    return;
  }

  if (p == "/api/health") {
    int active = 0;
    for (const QJsonValue& a : alerts::Alerts::instance().list()) active += jsTruthy(a.toObject().value("active"));
    sendJson(res, 200, QJsonObject{
        {"ok", true},
        {"app", "quant-terminal"},
        {"server", "Deepwater"},
        {"deepwater", QCoreApplication::applicationVersion()},
        {"root", QCoreApplication::applicationDirPath()},
        {"dataDir", store::dataDir()},
        {"version", kPortedVersion},
        {"qt", QString::fromLatin1(qVersion())},
        {"platform", platformName()},
        {"providers", data::healthJson()},
        {"feeds", QJsonArray::fromStringList(data::feeds::configured())},
        {"yahoo", data::yahooAvailable() ? "available" : "rate limited"},
        {"alerts", active},
    });
    return;
  }
  if (p == "/api/probe") {
    data::probeAll(this, answer([](const data::Probes& v) { return QJsonValue(data::probesToJson(v)); }));
    return;
  }
  if (p == "/api/resolve") {
    data::resolve(qv("symbol"), this, answer([](const data::Symbol& s) { return QJsonValue(s.toJson()); }));
    return;
  }
  if (p == "/api/search") {
    data::search(qv("q"), this, answer([](const QList<data::SearchHit>& h) { return QJsonValue(data::hitsToJson(h)); }));
    return;
  }
  if (p == "/api/spread") {
    data::getSpread(qv("symbol"), this, answer([](const data::SpreadQuote& s) { return QJsonValue(s.toJson()); }));
    return;
  }
  if (p == "/api/candles") {
    const QString tf = qv("tf").isEmpty() ? QStringLiteral("1D") : qv("tf");
    data::getCandles(qv("symbol"), tf, limitOf(q.contains("limit") ? qv("limit") : QString(), 1500), this,
                     answer([](const data::CandlesResult& c) { return QJsonValue(c.toJson()); }));
    return;
  }
  if (p == "/api/quotes") {
    QStringList symbols;
    for (const QString& s : qv("symbols").split(',')) if (!s.isEmpty()) symbols << s;
    data::getQuotes(symbols.mid(0, 200), this, answer([](const data::Quotes& v) { return QJsonValue(data::quotesToJson(v)); }));
    return;
  }
  if (p == "/api/calendar") {
    data::economicCalendar(this, answer([](const QList<data::CalendarEvent>& e) { return QJsonValue(data::eventsToJson(e)); }));
    return;
  }
  if (p == "/api/earnings" || p == "/api/dividends") {
    const int days = limitOf(qv("days"), 5);   // Number(q.days) || 5
    const auto done = [res](const QJsonArray& a) { sendJson(res, 200, a); };
    if (p == "/api/earnings") data::earnings(days, this, done);
    else data::dividends(days, this, done);
    return;
  }
  if (p == "/api/news") {
    data::news(qv("symbol"), this, answer([](const QJsonObject& o) { return QJsonValue(o); }));
    return;
  }
  if (p == "/api/financials") {
    data::financials(qv("symbol"), q.contains("frequency") ? qv("frequency") : QStringLiteral("annual"), this,
                     answer([](const QJsonValue& v) { return v; }));
    return;
  }
  if (p == "/api/screener") {
    const QString u = q.value("universe").isEmpty() ? QStringLiteral("crypto") : q.value("universe");
    const auto done = answer([](const QList<data::ScreenerRow>& rows) { return QJsonValue(data::rowsToJson(rows)); });
    if (u == "crypto") data::cryptoScreener(this, done);
    else data::nasdaqScreener(u == "etf" ? "etf" : "stocks", this, done);
    return;
  }
  if (p == "/api/scan" && m == "POST") {
    const Body b = readBody(req->body);
    if (b.error) { sendError(res, *b.error); return; }
    const QJsonValue rulesV = b.value.value("rules");
    const bool rulesOk = rulesV.isUndefined() || rulesV.isNull() || rulesV.isArray();   // body.rules ?? []
    if (!b.value.value("symbols").isArray() || !rulesOk) {
      sendError(res, httpError(400, "Send { symbols: [...], rules: [...] }"));
      return;
    }
    data::scan(b.value.value("symbols").toArray(), jsString(orElse(b.value.value("tf"), "1D")),
               orElse(b.value.value("indicators"), QJsonObject()).toObject(), rulesV.toArray(), this,
               [res](const QJsonArray& out) { sendJson(res, 200, out); });
    return;
  }
  // The strategy tester on the server, for your own code (docs/api.md). Same rule format as the tester.
  if (p == "/api/backtest" && m == "POST") {
    const Body b = readBody(req->body);
    if (b.error) { sendError(res, *b.error); return; }
    const QJsonValue strategyV = b.value.value("strategy");
    if (!jsTruthy(b.value.value("symbol")) || !strategyV.isObject()) {
      sendError(res, httpError(400, "Send { symbol, tf, strategy: { indicators, entryLong, exitLong, ... } }"));
      return;
    }
    const QJsonObject body = b.value, strategy = strategyV.toObject();
    const QString tf = jsString(orElse(body.value("tf"), "1D"));
    // JS iterates strategy.indicators in insertion order (it picks the quant checks' tunables): read it from the text.
    const QStringList order = data::jsonKeysInOrder(req->body, {"strategy", "indicators"});
    data::getCandles(jsString(body.value("symbol")), tf, limitOf(orElse(body.value("limit"), 1500), 1500), this,
        [this, res, body, strategy, tf, order](const data::Result<data::CandlesResult>& cr) {
      if (!cr.ok()) { sendError(res, *cr.error); return; }
      const data::CandlesResult c = cr.value;
      if (c.candles.size() < 2) { sendError(res, httpError(422, "Not enough bars to test on")); return; }
      // Runs the tester (worker thread unless a custom indicator is involved) and answers.
      auto run = [this, res, body, strategy, tf, order, c](const Bars& subBars) {
        const bool wantChecks = jsTruthy(body.value("quantChecks")), wantEquity = jsTruthy(body.value("equity"));
        auto work = [c, strategy, subBars, wantChecks, wantEquity, tf, order]() -> std::pair<QJsonObject, QString> {
          try {
            const bt::Result r = bt::backtest(c.candles, strategy, subBars);
            const QJsonObject rj = r.toJson();
            QJsonObject out{
                {"symbol", c.symbol}, {"tf", tf}, {"provider", c.provider}, {"bars", int(c.candles.size())},
                {"from", double(c.candles.first().time)}, {"to", double(c.candles.last().time)},
                {"stats", rj.value("stats")}, {"trades", rj.value("trades")},
            };
            if (wantEquity) out["equity"] = rj.value("equity");
            if (wantChecks) {
              quant::Options o;
              o.subBars = subBars;
              o.indicatorOrder = order;
              const QJsonObject qj = quant::quantChecks(c.candles, strategy, o).toJson();
              QJsonArray gates;
              for (const QJsonValue& g : qj.value("gates").toArray()) {
                const QJsonObject go = g.toObject();
                gates.append(QJsonObject{{"label", go.value("label")}, {"value", go.value("value")}, {"pass", go.value("pass")}});
              }
              out["quantChecks"] = QJsonObject{
                  {"passed", qj.value("passed")}, {"total", int(gates.size())}, {"gates", gates},
                  {"walkForward", qj.value("walkForward")}, {"monteCarlo", qj.value("monteCarlo")},
              };
            }
            return {out, QString()};
          } catch (const std::exception& e) {
            return {QJsonObject(), QString::fromUtf8(e.what())};
          }
        };
        const auto finish = [res](const std::pair<QJsonObject, QString>& r) {
          if (!r.second.isNull()) sendError(res, httpError(400, r.second));
          else sendJson(res, 200, r.first);
        };
        if (usesCustom(strategy)) finish(work());
        else QtConcurrent::run(work).then(this, finish);
      };
      if (strategy.value("intrabar") == QJsonValue("magnifier")) {
        const double sec = series::parseTf(tf).seconds;
        const double span = double(c.candles.last().time - c.candles.first().time) + sec;
        if (const auto mg = bt::magnifierTf(sec, span)) {
          const qint64 from = c.candles.first().time, to = c.candles.last().time;
          data::getCandles(c.symbol, mg->tf, int(std::ceil(span / mg->seconds)) + 10, this,
              [res, run, from, to, sec](const data::Result<data::CandlesResult>& sr) {
            if (!sr.ok()) { sendError(res, *sr.error); return; }
            Bars sub;
            for (const Bar& x : sr.value.candles)
              if (x.time >= from && double(x.time) < double(to) + sec) sub.push_back(x);
            run(sub);
          });
          return;
        }
      }
      run(Bars());
    });
    return;
  }
  // Your own data files (Imports.h): bars chart as CSV:<NAME>; a signal becomes an indicator.
  // Both read or parse whole files (seconds for a 100 MB CSV): on the pool, answered on the GUI thread (audit P8).
  if (p == "/api/imports") {
    QtConcurrent::run([] { return data::listImportsJson(); }).then(this, [res](const QJsonArray& a) { sendJson(res, 200, a); });
    return;
  }
  if (p == "/api/import/bars" && m == "POST") {
    if (auto e = textTooLarge(req->body, bodyLimit(m, p).bytes)) { sendError(res, *e); return; }
    QtConcurrent::run([name = qv("name"), body = req->body]() -> std::pair<QJsonObject, std::optional<Error>> {
      try {
        return {data::importBars(name, QString::fromUtf8(body)).toJson(), std::nullopt};
      } catch (const data::ImportError& e) {
        return {QJsonObject(), httpError(e.status, e.message)};
      } catch (const std::exception& e) {
        return {QJsonObject(), Error{QString::fromUtf8(e.what()), 500, false, {}}};
      }
    }).then(this, [res](const std::pair<QJsonObject, std::optional<Error>>& r) {
      if (r.second) sendError(res, *r.second);
      else sendJson(res, 200, r.first);
    });
    return;
  }
  if (p == "/api/import/signal" && m == "POST") {
    const QString name = data::cleanName(qv("name"));
    if (auto e = textTooLarge(req->body, bodyLimit(m, p).bytes)) { sendError(res, *e); return; }
    const QJsonObject r = data::importSignal(qv("name"), QString::fromUtf8(req->body));   // throws ImportError (400)
    emit customChanged(r.value("file").toString());
    sendJson(res, 200, r);
    return;
  }
  if (p == "/api/indicators") {
    QJsonArray out;
    for (const ind::Def& d : ind::all()) {
      QJsonArray inputs, outputs;
      for (const ind::Input& in : d.inputs) {
        QJsonObject o{{"key", in.key}, {"type", in.type}};
        if (!in.def.isUndefined()) o["default"] = in.def;
        if (!in.options.isEmpty()) o["options"] = QJsonArray::fromStringList(in.options);
        inputs.append(o);
      }
      for (const ind::Output& o : d.outputs) outputs.append(o.key);
      out.append(QJsonObject{{"id", d.id}, {"name", d.name}, {"inputs", inputs}, {"outputs", outputs}});
    }
    sendJson(res, 200, out);
    return;
  }
  if (p == "/api/state") {
    if (m == "GET") { sendJson(res, 200, store::read("state", QJsonObject())); return; }
    if (m == "PUT") {
      const Body b = readBody(req->body, bodyLimit(m, p).bytes);
      if (b.error) { sendError(res, *b.error); return; }
      QString err;
      const QJsonObject next = store::mergeState(b.value, &err);
      if (!err.isEmpty()) { sendJson(res, 500, QJsonObject{{"error", err}}); return; }
      emit stateChanged(next, b.value);
      sendJson(res, 200, QJsonObject{{"ok", true}});
      return;
    }
  }
  if (p == "/api/events") { openEvents(req, res); return; }
  auto& al = alerts::Alerts::instance();
  if (p == "/api/alerts") {
    if (m == "GET") { sendJson(res, 200, al.list()); return; }
    if (m == "POST") {
      const Body b = readBody(req->body);
      if (b.error) { sendError(res, *b.error); return; }
      const auto r = al.add(b.value);
      if (!r.ok()) sendError(res, *r.error);
      else sendJson(res, 200, r.value);
      return;
    }
  }
  if (p == "/api/alerts/log") { sendJson(res, 200, al.history()); return; }
  if (p == "/api/alerts/check" && m == "POST") {
    al.evaluate([res] { sendJson(res, 200, QJsonObject{{"ok", true}}); });
    return;
  }
  if (p.startsWith("/api/alerts/")) {
    const QString id = p.split('/').value(3);
    if (m == "DELETE") { al.remove(id); sendJson(res, 200, QJsonObject{{"ok", true}}); return; }
    if (m == "PATCH") {
      const Body b = readBody(req->body);
      if (b.error) { sendError(res, *b.error); return; }
      const auto r = al.update(id, b.value);
      if (!r.ok()) sendError(res, *r.error);
      else sendJson(res, 200, r.value);
      return;
    }
  }
  if (p == "/api/webhook-test" && m == "POST") {
    const Body b = readBody(req->body);
    if (b.error) { sendError(res, *b.error); return; }
    const QString hook = jsTruthy(b.value.value("url")) ? jsString(b.value.value("url")) : QString();
    static const QRegularExpression url(QStringLiteral("^https?://[^\\s]+$"), QRegularExpression::CaseInsensitiveOption);
    if (!url.match(hook).hasMatch()) { sendError(res, httpError(400, "Webhook must be an http(s) URL")); return; }
    const QJsonObject body{{"text", "Deepwater test alert"}, {"content", "Deepwater test alert"}};
    alerts::Alerts::postJson(hook, body, [res](int status, const QString& error) {
      if (status == 0) sendJson(res, 500, QJsonObject{{"error", error}});
      else sendJson(res, 200, QJsonObject{{"ok", status >= 200 && status < 300}, {"status", status}});
    });
    return;
  }
  if (p == "/api/custom") {
    if (m == "GET") { sendJson(res, 200, listCustom()); return; }
    if (m == "POST") {
      const Body b = readBody(req->body);
      if (b.error) { sendError(res, *b.error); return; }
      const QJsonValue codeV = b.value.value("code");
      const data::CustomSave r = data::saveCustom(jsString(b.value.value("name")), jsTruthy(codeV) ? jsString(codeV) : QString());
      if (r.status == 200) emit customChanged(r.file);
      sendJson(res, r.status, r.toJson());
      return;
    }
  }
  if (p.startsWith("/api/custom/") && m == "DELETE") {
    const QString file = QFileInfo(p.mid(12)).fileName();   // path.basename
    data::deleteCustom(file);
    emit customChanged(file);
    sendJson(res, 200, QJsonObject{{"ok", true}});
    return;
  }
  if (p == "/api/shutdown" && m == "POST") {
    // The JS lets its installer stop a running copy. Deepwater is a desktop app: it is quit from its own window.
    sendJson(res, 403, QJsonObject{{"error", "Deepwater serves this API while it runs and isn't shut down over HTTP. "
                                             "Quit Deepwater to stop it."}});
    return;
  }
  sendJson(res, 404, QJsonObject{{"error", "Unknown endpoint"}});
}

// ---------------------------------------------------------------------------------------------- launch
namespace {
void startAlertsChecker() {
  // One checker per data folder. Two Deepwaters on the same profile (the second gets port 3778) would each check
  // alerts.json: a "once" webhook sent twice, and each overwriting the other's list. A lock file is simpler and
  // surer than going by the port (the API may be off, or PORT set): the first instance keeps it while it runs;
  // QLockFile takes over a lock whose process is gone (a crash), so a restart checks again.
  static QLockFile* lock = new QLockFile(QDir(store::dataDir()).filePath("alerts-checker.lock"));
  lock->setStaleLockTime(0);   // never stale while its process lives
  if (lock->tryLock(0)) alerts::Alerts::instance().start();
  else qWarning().noquote() << "[charts] another Deepwater checks the alerts of this data folder: not checking them here";
}
}  // namespace

void launch() {
  if (qEnvironmentVariable("DW_CHARTS_API") == "0") return;
  // A moment after launch, so the first window paints first. The JS loads the custom indicators before listening.
  QTimer::singleShot(1500, &Server::instance(), [] {
    // Depth recordings of every market, not only those whose heatmap opens this session (audit G2): on the thread
    // pool, as it lists and deletes files. Each folder keeps the days its recording last asked for.
    (void)QtConcurrent::run([root = data::DepthStream::recordingsRoot(), now = QDateTime::currentMSecsSinceEpoch()] {
      data::DepthStream::pruneAllRecordings(root, now);
    });
    // The custom indicators, the .pine ones checked off the GUI thread (audit P1); the alerts checker starts once they
    // are registered, so a rule naming one finds it. The charts refresh for each Pine file through customChanged.
    data::loadAllCustomAsync(&Server::instance(), [](const QStringList& pineFiles) {
      for (const QString& f : pineFiles) emit Server::instance().customChanged(f);
      startAlertsChecker();
    });
    if (settings::value("charts.api", true).toBool()) {
      if (!Server::instance().start())
        qWarning().noquote() << "[charts] local API not started:" << Server::instance().lastError();
    }
  });
}

}  // namespace charts::api

// The app starts the API and the alerts checker at launch, like the JS server (nothing to wire in main.cpp). Only in
// Deepwater itself: the test programs and the stand-alone Charts harness link this file too.
static void dwChartsApiStartup() {
  QTimer::singleShot(0, qApp, [] {
    if (QCoreApplication::applicationName() == QLatin1String("Deepwater")) charts::api::launch();
  });
}
DW_STARTUP_FUNCTION(dwChartsApiStartup)
