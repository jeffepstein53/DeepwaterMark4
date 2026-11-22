// NativeTv on Windows (native parity, 2026-11-22): the Squawk pane's live YouTube player in Qt WebView's WebView2 backend
// (Edge's engine: H.264 / AAC, hardware decoding), as macOS does with WKWebView (NativeTv.mm). See NativeTv.h.
//   - the view is a child QWindow of the top-level window, placed in its coordinates, as the macOS view is a subview of
//     the top-level NSView (no window container: that made Qt flush the whole window on every repaint on macOS);
//   - WebView2 cannot give a page a base URL (Qt's loadHtml warns and uses about:blank, which YouTube refuses with
//     error 153): the page is served by the Charts local API at /squawk-tv and loaded from there, so YouTube sees
//     http://localhost:<port>. No local API (switched off in Charts settings, or not started yet): no player, the pane
//     offers Open on YouTube;
//   - messages: the page sets document.title to "dw<n>:<text>" (TvPane's send()), read back from titleChanged.
#ifdef _WIN32
#include "pages/news/squawk/NativeTv.h"
#include "charts/data/ApiServer.h"
#include <QGuiApplication>
#include <QPointer>
#include <QRegularExpression>
#include <QWebView>
#include <QWidget>
#include <QWindow>

namespace wire::squawk {
namespace {
// m_handler holds a QPointer to the view: the view is a child of the top-level window, which may go first
QPointer<QWebView>& ptr(void* h) { return *static_cast<QPointer<QWebView>*>(h); }
int g_loads = 0;   // a new query string each load: the same path would not reload
}  // namespace

NativeTv::NativeTv(QWidget* parent) : QObject(parent) {
  // the web view needs the Windows platform (offscreen test runs get no player)
  if (QGuiApplication::platformName() != QLatin1String("windows")) return;
  m_host = parent;
  m_handler = new QPointer<QWebView>();
}

static QWebView* viewOf(void* h) { return h ? ptr(h).data() : nullptr; }

bool NativeTv::available() const {
  return m_host != nullptr && charts::api::Server::instance().isListening();
}

QString NativeTv::origin() const {
  return charts::api::Server::instance().url();   // http://localhost:<port>
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
  auto& api = charts::api::Server::instance();
  if (!api.isListening()) return;
  QWebView* v = ensureView(this, m_host, m_handler, m_geo);
  if (!v) return;
  api.setPage(QStringLiteral("/squawk-tv"), html.toUtf8());
  v->setUrl(QUrl(api.url() + QStringLiteral("/squawk-tv?n=%1").arg(++g_loads)));
}

void NativeTv::clear() {
  if (QWebView* v = viewOf(m_handler)) {
    v->stop();
    v->loadHtml(QStringLiteral("<html><body style='background:#000'></body></html>"));
  }
  charts::api::Server::instance().setPage(QStringLiteral("/squawk-tv"), {});
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
