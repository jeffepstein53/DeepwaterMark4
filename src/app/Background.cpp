#include "app/Background.h"
#include "core/Settings.h"
#include "macro/CentralBanks.h"
#include "market/Instruments.h"
#include "market/QuoteHub.h"
#include "news/Calendar.h"
#include "news/Feed.h"
#include "positioning/RiskIndex.h"
#include <QDateTime>

namespace app {
namespace {
constexpr const char* kKey = "data.keepFresh";
constexpr int kTickMs = 60 * 1000;
constexpr qint64 kQuoteMaxAgeMs = 55 * 1000;    // a quote asked less than 55 s ago is left alone (a shown page keeps its own 20 s poll)
constexpr int kRiskEverySec = 15 * 60;
}

Background& Background::instance() { static Background* b = new Background; return *b; }

Background::Background() {
  m_timer.setInterval(kTickMs);
  connect(&m_timer, &QTimer::timeout, this, [this] { tick(); });
}

bool Background::enabled() { return settings::value(kKey, true).toBool(); }

void Background::setEnabled(bool on) {
  settings::setValue(kKey, on);
  instance().apply();
}

// The quotes of the Overview's tape and the main markets of each class: 24 symbols, two Yahoo requests a minute.
QStringList Background::coreSymbols() {
  QStringList out;
  for (const char* s : {"EURUSD", "GBPUSD", "USDJPY", "USDCHF", "AUDUSD", "USDCAD", "NZDUSD", "EURJPY", "GBPJPY", "EURGBP",
                        "SPX", "NDX", "VIX", "DXY", "DAX", "FTSE", "NIKKEI", "XAUUSD", "XAGUSD", "BRENT", "CL", "BTCUSD", "ETHUSD", "US10Y"})
    if (market::find(s)) out << s;   // a symbol this build does not know is left out, never an error
  return out;
}

void Background::start() {
  if (m_started) return;
  m_started = true;
  apply();
}

void Background::apply() {
  if (!m_started) return;
  auto& feed = news::Feed::instance();
  auto& cal = news::Calendar::instance();
  if (enabled()) {
    feed.setActive(&m_owner, true);    // polls each source at its own pace while the app runs
    cal.setActive(&m_owner, true);     // actuals at most every 5 min, ForexFactory every 30 min
    m_timer.start();
    tick();
  } else {
    m_timer.stop();
    feed.setActive(&m_owner, false);
    cal.setActive(&m_owner, false);
  }
}

void Background::tick() {
  if (!enabled()) return;
  market::QuoteHub::instance().refreshStale(coreSymbols(), kQuoteMaxAgeMs, false);   // not "urgent": it must not queue ahead of a page
  auto& ri = positioning::RiskIndex::instance();
  if (!ri.loading() && (!ri.data().ok() || !ri.data().updated.isValid() || ri.data().updated.secsTo(QDateTime::currentDateTime()) > kRiskEverySec))
    ri.load(kRiskEverySec);
  macro::CentralBanks::instance().ensureLoaded();   // each of its series has its own pace inside
}
}
