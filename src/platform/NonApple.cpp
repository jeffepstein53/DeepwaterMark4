// What the Apple modules (banks/Pdf.mm, macro/Ocr.mm) give where there is no engine, as their headers promise: empty
// results. Windows has its own (platform/WinPdf.cpp, platform/WinOcr.cpp, 2026-11-22); this is left for other
// platforms (Linux, not shipped). Bank Research then shows no PDF pages and the RBA / RBNZ pictures are not read.
// (The Squawk pane's web view has its own stand-in, pages/news/squawk/NativeTvStub.cpp.)
#if !defined(__APPLE__) && !defined(_WIN32)
#include "banks/Pdf.h"
#include "macro/Ocr.h"

namespace banks::pdf {
Info info(const QString&) { return {}; }
QImage render(const QString&, int, double, double) { return {}; }
QString text(const QString&, int) { return {}; }
bool print(const QString&) { return false; }
}  // namespace banks::pdf

namespace macro::ocr {
QList<Line> recognize(const QByteArray&) { return {}; }
}  // namespace macro::ocr

#endif
