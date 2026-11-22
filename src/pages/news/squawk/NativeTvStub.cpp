// NativeTv on platforms without a system web view host yet (see NativeTv.h): no player, the pane links out.
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#if (!defined(__APPLE__) && !defined(_WIN32)) || TARGET_OS_IPHONE   // iOS: no NSView host (NativeTv.mm is macOS only); Windows: NativeTvWin.cpp
#include "pages/news/squawk/NativeTv.h"
namespace wire::squawk {
NativeTv::NativeTv(QWidget* parent) : QObject(reinterpret_cast<QObject*>(parent)) {}
NativeTv::~NativeTv() = default;
void NativeTv::loadHtml(const QString&, const QUrl&) {}
void NativeTv::clear() {}
QString NativeTv::origin() const { return {}; }
bool NativeTv::available() const { return false; }
void NativeTv::setGeometry(const QRect&) {}
void NativeTv::setVisible(bool) {}
}
#endif
