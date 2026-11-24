// The live order book kept in memory. See LiveBook.h.
#include "technicaldata/LiveBook.h"
#include "technicaldata/Collector.h"
#include "technicaldata/TechnicalDataSpec.h"
#include "technicaldata/TechnicalDataStore.h"
#include "net/Sources.h"
#include <QDateTime>
#include <QSet>

namespace technicaldata {

namespace {
// The live book's hosts (the spot and perpetual streams of charts/data/DepthStream and TradeStream, the perpetual REST
// API of charts/data/BinanceFeed), declared here with this page (declare() merges hosts and pages by id), so that Sources
// lists them, paces them and lets them be switched off
const sources::Registrar regLiveSpot({"binance", "Binance (spot market data API)", {"stream.binance.com"}, sources::Access::Keyless, 200,
    "Live order book and trades over WebSocket (Data › Technical data, live book)",
    "Public market streams without key (300 connections per 5 min per IP allowed)", {"data/technical-data"}});
const sources::Registrar regLiveVision({"binance-vision", "Binance (public market-data mirror)", {"data-stream.binance.vision"},
    sources::Access::Keyless, 200, "Live order book and trades over WebSocket (mirror)", "Binance's public market-data stream mirror",
    {"data/technical-data"}});
const sources::Registrar regLiveFut({"binance-futures", "Binance USD-M futures (market data API)", {"fapi.binance.com", "fstream.binance.com"},
    sources::Access::Keyless, 200, "Perpetual order book and trades, live (Data › Technical data, live book)",
    "Public market-data endpoints and streams without key; 2400 request weight per minute per IP; we send at most 5 per second",
    {"data/technical-data"}});
}  // namespace

LiveBook& LiveBook::instance() {
  static LiveBook* b = new LiveBook;
  return *b;
}

LiveBook::LiveBook() {
  m_foldTimer.setInterval(30000);
  connect(&m_foldTimer, &QTimer::timeout, this, &LiveBook::foldAll);
  connect(&Store::instance(), &Store::pairsChanged, this, [this](const QString&) { sync(); });
  connect(&Store::instance(), &Store::pausedChanged, this, [this](const QString&) { sync(); });
}

void LiveBook::start() {
  m_started = true;
  m_foldTimer.start();
  sync();
}

void LiveBook::stop() {
  m_started = false;
  m_foldTimer.stop();
  sync();
}

QStringList LiveBook::pairs() const {
  QStringList out;
  for (auto it = m_streams.cbegin(); it != m_streams.cend(); ++it) {
    const QString p = it.key().section('|', 0, 0);
    if (!out.contains(p)) out << p;
  }
  return out;
}

std::shared_ptr<charts::data::DepthStream> LiveBook::stream(const QString& pair, const QString& venue) const {
  return m_streams.value(pair + "|" + venue);
}

std::shared_ptr<charts::data::TradeStream> LiveBook::trades(const QString& pair, const QString& venue) const {
  return m_trades.value(pair + "|" + venue);
}

void LiveBook::foldAll() {
  const qint64 now = QDateTime::currentSecsSinceEpoch();
  for (auto it = m_trades.cbegin(); it != m_trades.cend(); ++it)
    if (it.value()->ready()) foldTrades(it.value()->tape(), it.value()->ticks(), m_fp[it.key()], m_fold[it.key()], now);
}

const LiveFootprint* LiveBook::footprint(const QString& pair, const QString& venue) {
  const QString key = pair + "|" + venue;
  const auto s = m_trades.value(key);
  if (!s) return nullptr;
  if (s->ready()) foldTrades(s->tape(), s->ticks(), m_fp[key], m_fold[key], QDateTime::currentSecsSinceEpoch());
  return &m_fp[key];
}

void LiveBook::sync() {
  auto& st = Store::instance();
  QHash<QString, std::shared_ptr<charts::data::DepthStream>> keep;
  QHash<QString, std::shared_ptr<charts::data::TradeStream>> keepTrades;
  if (m_started)
    for (const QString& pair : m_markets.isEmpty() ? st.pairs() : m_markets) {
      if (!pair.startsWith("BINANCE:") || st.isPaused(pair) || !specFromJson(st.pairSpec(pair)).liveBook) continue;
      for (const auto& [venue, kind] : {std::pair{QStringLiteral("spot"), charts::data::FlowMarket::BinanceSpot},
                                        std::pair{QStringLiteral("perp"), charts::data::FlowMarket::BinanceUsdm}}) {
        const QString key = pair + "|" + venue;
        auto s = m_streams.value(key);
        if (!s) {
          s = charts::data::DepthStream::acquire(charts::data::FlowMarket{kind, binanceSymbol(pair)});
          s->record(false, 1, this);   // memory only (user's rule): this cache never asks the stream to write
        }
        keep.insert(key, s);
        auto tr = m_trades.value(key);
        if (!tr) tr = charts::data::TradeStream::acquire(charts::data::FlowMarket{kind, binanceSymbol(pair)});
        keepTrades.insert(key, tr);
      }
    }
  const bool same = QSet<QString>(keep.keyBegin(), keep.keyEnd()) == QSet<QString>(m_streams.keyBegin(), m_streams.keyEnd());
  m_streams = std::move(keep);   // the ones no longer wanted are let go (the stream ends with its last user)
  m_trades = std::move(keepTrades);
  for (const QString& k : m_fp.keys()) if (!m_trades.contains(k)) { m_fp.remove(k); m_fold.remove(k); }
  if (!same) emit changed();
}

}  // namespace technicaldata
