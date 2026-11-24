// NativeTv on Windows (native parity, 2026-11-22): the Squawk pane's live YouTube player in Qt WebView's WebView2 backend
// (Edge's engine: H.264 / AAC, hardware decoding), as macOS does with WKWebView (NativeTv.mm). See NativeTv.h.
//   - the view is a child QWindow of the top-level window, placed in its coordinates, as the macOS view is a subview of
//     the top-level NSView (no window container: that made Qt flush the whole window on every repaint on macOS);
//   - WebView2 cannot give a page a base URL (Qt's loadHtml warns and uses about:blank, which YouTube refuses with
//     error 153): the page is served at /squawk-tv by the player's own local server (PlayerServer below, 2026-11-24;
//     before, the Charts tab's local API served it) and loaded from there, so YouTube sees http://localhost:<port>.
//     The server starts at the player's first use, so a launch straight on Squawk has it at once. No server (no port
//     free on this computer): no player, the pane offers Open on YouTube;
//   - messages: the page sets document.title to "dw<n>:<text>" (TvPane's send()), read back from titleChanged.
#ifdef _WIN32
#include "pages/news/squawk/NativeTv.h"
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QHttpHeaders>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpServer>
#include <QWebView>
#include <QWidget>
#include <QWindow>

namespace wire::squawk {
namespace {
// m_handler holds a QPointer to the view: the view is a child of the top-level window, which may go first
QPointer<QWebView>& ptr(void* h) { return *static_cast<QPointer<QWebView>*>(h); }
int g_loads = 0;   // a new query string each load: the same path would not reload

// The player page's local web server: on the loopback address only (127.0.0.1: nothing outside this computer can
// reach it, and Windows asks no firewall question for it), one page, GET /squawk-tv. Made on the first use; the
// application owns it (gone at quit).
class PlayerServer {
public:
  static PlayerServer& get() {
    static PlayerServer s;
    return s;
  }
  bool ensure();
  QString url() const { return QStringLiteral("http://localhost:%1").arg(m_port); }
  void setPage(const QByteArray& html) { m_page = html; }

private:
  // a fixed port when it is free, so the page keeps the same origin from one launch to the next (WebView2 keeps a
  // site's settings by origin), then the next nine, then any free one. Not 3777: the Charts local API's own port
  static constexpr quint16 kPort = 3787;
  QPointer<QHttpServer> m_http;
  quint16 m_port = 0;
  QByteArray m_page;
  QElapsedTimer m_failed;   // the last start that failed: tried again a second later, not on every call
};

bool PlayerServer::ensure() {
  if (m_http) return true;
  if (!qApp || (m_failed.isValid() && m_failed.elapsed() < 1000)) return false;
  auto* http = new QHttpServer(qApp);
  auto* tcp = new QTcpServer(http);
  bool ok = false;
  for (quint16 p = kPort; p < kPort + 10 && !ok; ++p) ok = tcp->listen(QHostAddress::LocalHost, p);
  if (!ok) ok = tcp->listen(QHostAddress::LocalHost, 0);
  if (!ok || !http->bind(tcp)) {
    qWarning().noquote() << "[squawk] the TV player's local server could not start:" << tcp->errorString();
    delete http;   // and tcp, its child
    m_failed.start();
    return false;
  }
  http->setMissingHandler(http, [this](const QHttpServerRequest& rq, QHttpServerResponder& rs) {
    QHttpHeaders h;
    h.append(QHttpHeaders::WellKnownHeader::CacheControl, "no-store");
    if (rq.method() == QHttpServerRequest::Method::Get && rq.url().path() == QLatin1String("/squawk-tv") && !m_page.isEmpty()) {
      h.append(QHttpHeaders::WellKnownHeader::ContentType, "text/html; charset=utf-8");
      rs.write(m_page, h, QHttpServerResponder::StatusCode::Ok);
    } else {
      h.append(QHttpHeaders::WellKnownHeader::ContentType, "text/plain; charset=utf-8");
      rs.write(QByteArrayLiteral("Not found"), h, QHttpServerResponder::StatusCode::NotFound);
    }
  });
  m_http = http;
  m_port = tcp->serverPort();
  return true;
}
}  // namespace

NativeTv::NativeTv(QWidget* parent) : QObject(parent) {
  // the web view needs the Windows platform (offscreen test runs get no player)
  if (QGuiApplication::platformName() != QLatin1String("windows")) return;
  m_host = parent;
  m_handler = new QPointer<QWebView>();
}

static QWebView* viewOf(void* h) { return h ? ptr(h).data() : nullptr; }

bool NativeTv::available() const {
  return m_host != nullptr && PlayerServer::get().ensure();
}

QString NativeTv::origin() const {
  PlayerServer::get().ensure();
  return PlayerServer::get().url();   // http://localhost:<port>
}

// The view, made on first use as a child of the top-level window (hidden until the pane shows it).
static QWebView* ensureView(NativeTv* tv, QWidget* host, void* handler, const QRect& geo) {
  if (QWebView* v = viewOf(handler)) return v;
  QWindow* top = host ? host->window()->windowHandle() : nullptr;
  if (!top) return nullptr;
  auto* v = new QWebView(top);
  ptr(handler) = v;
  QPointer<NativeTv> self(tv);
  QObject::connect(v, &QWebView::titleChanged, tv, [self](const QString& t) {
    static const QRegularExpression rx(QStringLiteral("^dw\\d+:(.*)$"));
    const auto m = rx.match(t);
    if (self && m.hasMatch()) self->deliver(m.captured(1));
  });
  if (!geo.isEmpty()) v->setGeometry(geo);
  v->setVisible(false);
  return v;
}

void NativeTv::setVisible(bool on) {
  m_visible = on;
  if (!m_host) return;
  QWebView* v = on ? ensureView(this, m_host, m_handler, m_geo) : viewOf(m_handler);
  if (v) v->setVisible(on);
}

void NativeTv::setGeometry(const QRect& r) {
  if (r == m_geo) return;
  m_geo = r;
  if (QWebView* v = viewOf(m_handler)) v->setGeometry(r);
}

void NativeTv::loadHtml(const QString& html, const QUrl&) {
  if (!m_host) return;
  auto& server = PlayerServer::get();
  if (!server.ensure()) return;
  QWebView* v = ensureView(this, m_host, m_handler, m_geo);
  if (!v) return;
  server.setPage(html.toUtf8());
  v->setUrl(QUrl(server.url() + QStringLiteral("/squawk-tv?n=%1").arg(++g_loads)));
}

void NativeTv::clear() {
  if (QWebView* v = viewOf(m_handler)) {
    v->stop();
    v->loadHtml(QStringLiteral("<html><body style='background:#000'></body></html>"));
  }
  PlayerServer::get().setPage({});
}

NativeTv::~NativeTv() {
  if (QWebView* v = viewOf(m_handler)) {
    v->stop();
    delete v;
  }
  delete static_cast<QPointer<QWebView>*>(m_handler);
  m_handler = nullptr;
}
}  // namespace wire::squawk
#endif
