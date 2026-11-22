#pragma once
#include <QString>

// `Deepwater --selftest <file.json>` (native parity, 2026-11-22): the platform engines a CI run can check without a
// person in front of the screen. It writes a two-page PDF, reads it back with Bank Research's PDF reader (pages, title,
// a rendered page, its text), then reads the rendered page with the on-device OCR the RBA / RBNZ pictures use. The
// Windows workflow runs it on the packed app, where these engines are Windows' own (platform/WinPdf.cpp, WinOcr.cpp);
// on macOS and iOS they are Apple's. Writes {ok, platform, checks: [{name, ok, detail}]} and returns 0 when all pass.
namespace app::selftest {
int run(const QString& outFile);
}
