// NativeTv on iOS (native parity, 2026-11-22): the Squawk pane's live YouTube player in a WKWebView added to the window's
// UIView, as macOS adds one to the top-level NSView (NativeTv.mm, see NativeTv.h). UIKit's coordinates start at the top
// left, like Qt's, and Qt's logical pixels are UIKit points. Inline playback (allowsInlineMediaPlayback), muted
// autoplay, nothing written to disk. The page keeps its origin through loadHTMLString's base URL, as on macOS.
// Manual retain / release, like NativeTv.mm (not in the ARC list of CMakeLists.txt).
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include "pages/news/squawk/NativeTv.h"
#include <QGuiApplication>
#include <QWidget>
#include <QWindow>
#import <UIKit/UIKit.h>
#import <WebKit/WebKit.h>

@interface DWWireTvHandlerIOS : NSObject <WKScriptMessageHandler> {
@public
  wire::squawk::NativeTv* owner;
}
@end
@implementation DWWireTvHandlerIOS
- (void)userContentController:(WKUserContentController*)c didReceiveScriptMessage:(WKScriptMessage*)m {
  (void)c;
  if (owner && [m.body isKindOfClass:[NSString class]]) owner->deliver(QString::fromNSString((NSString*)m.body));
}
@end

namespace wire::squawk {
NativeTv::NativeTv(QWidget* parent) : QObject(parent) {
  if (QGuiApplication::platformName() != QLatin1String("ios")) return;
  @autoreleasepool {
    WKWebViewConfiguration* cfg = [[WKWebViewConfiguration alloc] init];
    cfg.allowsInlineMediaPlayback = YES;
    cfg.mediaTypesRequiringUserActionForPlayback = WKAudiovisualMediaTypeNone;
    cfg.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];
    DWWireTvHandlerIOS* h = [[DWWireTvHandlerIOS alloc] init];
    h->owner = this;
    [cfg.userContentController addScriptMessageHandler:h name:@"dw"];
    WKWebView* v = [[WKWebView alloc] initWithFrame:CGRectMake(0, 0, 527, 310) configuration:cfg];
    v.opaque = NO;
    v.backgroundColor = [UIColor blackColor];
    v.scrollView.backgroundColor = [UIColor blackColor];
    v.scrollView.scrollEnabled = NO;   // the page scrolls, not the player
    v.hidden = YES;
    m_view = v;
    m_handler = h;
    [cfg release];
    m_host = parent;
  }
}

bool NativeTv::available() const { return m_view != nullptr; }
QString NativeTv::origin() const { return QStringLiteral("https://deepwater-terminal.com"); }

void NativeTv::setVisible(bool on) {
  @autoreleasepool {
    WKWebView* v = (WKWebView*)m_view;
    if (!v || !m_host) return;
    if (on && !v.superview) {
      UIView* root = (UIView*)m_host->window()->winId();   // the window's QUIView
      [root addSubview:v];
    }
    v.hidden = !on;
    m_visible = on;
  }
}

void NativeTv::setGeometry(const QRect& r) {
  if (r == m_geo) return;
  m_geo = r;
  @autoreleasepool {
    WKWebView* v = (WKWebView*)m_view;
    if (v) v.frame = CGRectMake(r.x(), r.y(), r.width(), r.height());
  }
}

NativeTv::~NativeTv() {
  @autoreleasepool {
    WKWebView* v = (WKWebView*)m_view;
    DWWireTvHandlerIOS* h = (DWWireTvHandlerIOS*)m_handler;
    if (h) h->owner = nullptr;
    if (v) {
      [v stopLoading];
      [v removeFromSuperview];
      [v.configuration.userContentController removeScriptMessageHandlerForName:@"dw"];
    }
    [h release];
    [v release];
  }
}

void NativeTv::loadHtml(const QString& html, const QUrl& base) {
  @autoreleasepool {
    WKWebView* v = (WKWebView*)m_view;
    if (v) [v loadHTMLString:html.toNSString() baseURL:base.toNSURL()];
  }
}

void NativeTv::clear() {
  @autoreleasepool {
    WKWebView* v = (WKWebView*)m_view;
    if (!v) return;
    [v stopLoading];
    [v loadHTMLString:@"<html><body style='background:#000'></body></html>" baseURL:nil];
  }
}
}  // namespace wire::squawk
#endif
