#pragma once
#include <QString>

// iOS local notifications (native parity, 2026-11-22): Charts' alerts, big trades and paper fills, as macOS shows them
// through Notification Center (ChartsApp::systemNotify). Shown as a banner while Deepwater is in front too. The first
// call asks the user's permission (iOS's own prompt); refused, nothing is shown. iOS stops the app's timers in the
// background, so alerts are only checked while Deepwater is open. A no-op elsewhere.
namespace dw::ios {
void notify(const QString& title, const QString& text);
}
