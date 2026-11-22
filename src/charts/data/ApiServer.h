#pragma once
// Charts tab (C++ port of quant-terminal, MIT, Copyright (c) 2026 Lewis Jackson — see docs/charts/NOTICE.md).
// Port of server.js: the local HTTP API, served by Deepwater while it runs (docs/charts/api.md).
//
//   http://127.0.0.1:3777 (or the next free port, up to 20 more; env PORT picks the first one)
//
// Same routes, status codes, error JSON ({ "error": "..." }), body limits and security check as the JS (this
// computer only: Host must be localhost / 127.0.0.1 / ::1; writes need the header x-qt: 1 and a same-host Origin),
// no CORS headers (as the JS). Left out: the PTQ routes (/api/ptq/*) and the web UI's static files (Deepwater has
// no web UI: "/" answers a short JSON saying so). /custom/<file> still serves your custom indicator files.
// POST /api/shutdown is refused (403): Deepwater is an app, not a server you stop from outside; quit the app.
//
// Threading: the server lives on the GUI thread (the providers' callbacks come back there); backtests and quant
// checks run on a worker thread (QtConcurrent) unless the strategy uses a custom indicator (those run on the GUI
// thread only, see CustomJs.h), and the answer is written back on the server's thread.
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QPointer>
#include <memory>

class QHttpServer;
class QHttpServerRequest;
class QHttpServerResponder;
class QTcpSocket;
class QTimer;

namespace charts::api {

class Server : public QObject {
  Q_OBJECT
public:
  static Server& instance();

  // Listens on 127.0.0.1. port < 0: env PORT, else 3777 (a PORT that isn't a port number, 1..65535, is logged and
  // 3777 used: the JS would stop with a RangeError). port 0: any free port (tests). When the port is taken, tries the
  // next one, `tries` times (JS start(port, 20)). false + lastError() when no port could be had or port > 65535.
  bool start(int port = -1, int tries = 20);
  void stop();
  bool isListening() const;
  quint16 port() const { return port_; }
  QString url() const;              // "http://localhost:<port>" (what the JS prints)
  QString lastError() const { return error_; }
  int eventClients() const { return int(clients_.size()); }   // open /api/events streams

  // SSE to every /api/events client: "event: <type>\ndata: <JSON>\n\n" (Alerts::fired is broadcast as "alert").
  void broadcast(const QString& type, const QJsonValue& payload);

  // A page of the app served at `path` (GET, text/html), e.g. the Squawk pane's YouTube player on Windows, where the
  // web view cannot give a page an origin of its own (pages/news/squawk/NativeTvWin.cpp). Empty html removes it.
  void setPage(const QString& path, const QByteArray& html);

  // Custom indicators (server.js listCustom / loadCustom): the folder paths::data("charts/custom").
  QJsonArray listCustom();                    // [{file, id, mtime, error|null}] in name order
  QString loadCustom(const QString& file);    // registers it; the error (also kept for listCustom) or empty
  void loadAllCustom();                       // every *.js file (the JS does this before listening)

signals:
  // PUT /api/state merged a patch into state.json: the running ChartsApp re-reads it. state = the whole new state.
  void stateChanged(const QJsonObject& state, const QJsonObject& patch);
  // A custom indicator file was saved, imported (signal) or deleted through the API.
  void customChanged(const QString& file);

private:
  Server();
  struct Req;
  using Resp = std::shared_ptr<QHttpServerResponder>;
  void handle(const QHttpServerRequest& request, QHttpServerResponder& responder);
  void route(const std::shared_ptr<Req>& req, const Resp& res);
  void openEvents(const std::shared_ptr<Req>& req, const Resp& res);
  void dropClient(quint64 id);
  void setBodyLimit(qint64 bytes);   // QHttpServer's body limit (the gate sets it per request, see ApiServer.cpp)

  QHttpServer* http_ = nullptr;
  class TcpServer;
  QPointer<TcpServer> tcp_;
  quint16 port_ = 0;
  QString error_;
  qint64 bodyLimit_ = 0;
  struct Client { Resp res; QMetaObject::Connection conn; };
  QHash<quint64, Client> clients_;
  quint64 nextClient_ = 0;
  QTimer* keepAlive_ = nullptr;
  QHash<QString, QByteArray> pages_;   // setPage
};

// What the app does at launch (called from a Q_COREAPP_STARTUP_FUNCTION in ApiServer.cpp when the application is
// Deepwater; nothing to wire): load the custom indicators, start the alerts checker, and start the server if the
// setting charts.api is on (default true; the JS always serves). Env DW_CHARTS_API=0 turns both off for a run.
// One alerts checker per data folder: a second Deepwater on the same profile (which gets port 3778) leaves the
// checking to the first (lock file <data>/charts/alerts-checker.lock), so "once" alerts don't fire twice.
void launch();

// ---- pieces of server.js, exposed for tests
// The JS allowed(req): Host header, method, x-qt header, Origin header (empty = absent), plus (ADDED, audit 2026-11-18)
// the Sec-Fetch-Site header: a request a web page sends from another site is refused for GET too, or any page in the
// browser could make Deepwater query the providers in a loop (/api/candles, /api/quotes, /api/probe).
bool allowed(const QString& host, const QByteArray& method, const QString& xqt, const QString& origin, bool hasOrigin,
             const QString& fetchSite = {});
// String(s).toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "").slice(0, 60) || "indicator"
QString slug(const QString& s);
// JS decodeURIComponent; false when it would throw (bad % escape or invalid UTF-8).
bool decodeUriComponent(const QString& in, QString* out);
// The JS body limits (readBody / readText): 2 MB of JSON ("Request too large"), 20 MB for PUT /api/state, 300 MB and
// 100 MB for the bar and signal CSV imports ("The file is over 300 MB"). A body over its route's limit is answered
// 413 {"error": message}: before any of it is read when its Content-Length says so, at the chunk that goes over for a
// chunked body.
constexpr qint64 kDefaultBodyLimit = 2000000;
// The first port for env PORT: Number(PORT) || 3777, and -1 for a number that isn't a port (1..65535, whole).
int envPort(const QString& value);
struct BodyLimit { qint64 bytes; QString message; };
BodyLimit bodyLimit(const QByteArray& method, const QString& path);
// Whether the route reads the request body (the JS readBody / readText). Other routes answer as usual whatever the
// body's size: it is dropped as it comes, never kept (the JS never reads it).
bool readsBody(const QByteArray& method, const QString& path);

}  // namespace charts::api
