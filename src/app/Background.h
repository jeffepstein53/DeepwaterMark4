#pragma once
#include <QObject>
#include <QTimer>

// Keeps the data warm while the page that shows it is not on screen (Options > Data, on by default).
//
// The services poll only while a page that uses them is shown (QuoteHub, Feed, Calendar): with Deepwater open on another
// page, quotes were half an hour old and the news an hour old when you came back. Switched on, this:
//   - asks the ~24 main quotes again every minute (one request of two chunks of Yahoo; a shown page already polls its own
//     symbols every 20 s, then there is nothing to ask),
//   - keeps the news feed and the calendar active, each source at its own pace (news 2 to 10 min, the calendar's actuals
//     at most every 5 min and ForexFactory every 30: what those publishers ask for),
//   - reloads the risk sentiment inputs every 15 min and lets the central banks' loader follow its own paces.
// It never goes faster than a source's declared pace: it only keeps the existing paces running when no page is shown.
namespace app {
class Background : public QObject {
public:
  static Background& instance();
  static bool enabled();                 // settings key data.keepFresh, on by default
  static void setEnabled(bool on);       // saves the choice and starts or stops the work at once
  void start();                          // once, shortly after launch; does nothing while the setting is off
  static QStringList coreSymbols();      // the quotes kept warm (app symbols)

private:
  Background();
  void apply();
  void tick();
  QTimer m_timer;
  QObject m_owner;                       // how Feed and Calendar know this "page"
  bool m_started = false;
};
}
