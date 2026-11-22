#include "pages/news/squawk/NativeTv.h"
#include <QPointer>
#include <QWidget>
#include <QWindow>
#include <QGuiApplication>
#import <WebKit/WebKit.h>

@interface DWWireTvHandler : NSObject <WKScriptMessageHandler> {
@public
  wire::squawk::NativeTv* owner;
}
@end
@implementation DWWireTvHandler
- (void)userContentController:(WKUserContentController*)c didReceiveScriptMessage:(WKScriptMessage*)m {
  (void)c;
  if (owner && [m.body isKindOfClass:[NSString class]]) owner->deliver(QString::fromNSString((NSString*)m.body));
}
@end

namespace wire::squawk {
NativeTv::NativeTv(QWidget* parent) : QObject(parent) {
  // the native view needs the Cocoa platform (window ids are NSViews); offscreen test runs get no player
  if (QGuiApplication::platformName() != QLatin1String("cocoa")) return;
  @autoreleasepool {
    WKWebViewConfiguration* cfg = [[WKWebViewConfiguration alloc] init];
    cfg.mediaTypesRequiringUserActionForPlayback = WKAudiovisualMediaTypeNone;
    cfg.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];   // nothing written to disk
    DWWireTvHandler* h = [[DWWireTvHandler alloc] init];
    h->owner = this;
    [cfg.userContentController addScriptMessageHandler:h name:@"dw"];
    WKWebView* v = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 527, 310) configuration:cfg];
    [v setValue:@NO forKey:@"drawsBackground"];
    if (@available(macOS 12.0, *)) v.underPageBackgroundColor = [NSColor blackColor];
    v.allowsMagnification = NO;
    m_view = v;
    m_handler = h;
#if !__has_feature(objc_arc)
    [cfg release];
#endif
    v.hidden = YES;
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
      NSView* root = (NSView*)m_host->window()->winId();   // the top-level QNSView (flipped: y from the top)
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
    if (!v) return;
    NSView* sv = v.superview;
    if (sv && !sv.isFlipped) v.frame = NSMakeRect(r.x(), sv.bounds.size.height - r.y() - r.height(), r.width(), r.height());
    else v.frame = NSMakeRect(r.x(), r.y(), r.width(), r.height());
  }
}

NativeTv::~NativeTv() {
  @autoreleasepool {
    WKWebView* v = (WKWebView*)m_view;
    DWWireTvHandler* h = (DWWireTvHandler*)m_handler;
    if (h) h->owner = nullptr;
    if (v) {
      [v stopLoading];
      [v removeFromSuperview];
      [v.configuration.userContentController removeScriptMessageHandlerForName:@"dw"];
    }
#if !__has_feature(objc_arc)
    [h release];
    [v release];
#endif
  }
}

void NativeTv::loadHtml(const QString& html, const QUrl& base) {
  @autoreleasepool {
    WKWebView* v = (WKWebView*)m_view;
    if (!v) return;
    [v loadHTMLString:html.toNSString() baseURL:base.toNSURL()];
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
}
