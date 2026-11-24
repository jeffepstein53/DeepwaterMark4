#pragma once
// The key indicators of every stored bar (user's list, 2026-11-17), computed by Charts' engine (charts::ind) and no
// other (user's rule: one engine, the same values as the charts; its definitions are Pine's, checked by
// tests/charts/core/pine_reference_tests.cpp), stored in technical-data.sqlite's ind table, one column a value.
//
// Over the whole history of each timeframe, as if computed on all of it at once, in pieces that fit in memory: each
// piece is computed from OVERLAP bars before its first one (5000: an EMA 200 forgets its seed to 1e-22 by then, Wilder
// smoothings faster), the OBV (a running sum from the first bar) is carried over from the bar before the piece, and a
// VWAP restarts every day, inside the overlap. tests/facts/market_tests.cpp checks pieces against one computation.
//   - New bars: only the bars after the last computed one (and from a hole the collector filled, if older).
//   - New definitions (another column, another setting, another start): the pair's version changes (versionOf), its
//     series are computed again, each row written over (the values of columns kept stay readable meanwhile).
//   - warm = 1 for the first WARMUP bars of a series: long averages are not settled yet (an EMA 200 is still close to
//     its seed); machine learning should leave them out.
//   - VWAP only on intraday timeframes (a daily VWAP anchored on the day is just the bar's hlc3): empty on 1D and 1W.
#include "technicaldata/TechnicalDataStore.h"
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <atomic>

namespace technicaldata {

struct IndColumn {
  QString name;          // column, e.g. "bb_upper"
  QString id;            // Charts indicator, e.g. "bb"
  QJsonObject params;    // its settings
  QString output;        // its output key, e.g. "upper"
  bool intradayOnly = false;
  bool running = false;  // a running sum from the first bar (OBV): carried over from the bar before a piece
  QVector<int> tfs;      // stored on these timeframes only (empty = all)
  bool appliesTo(int tf) const { return !(intradayOnly && tf >= 86400) && (tfs.isEmpty() || tfs.contains(tf)); }
};
// BTCUSDT's 23 columns (the default pair, TechnicalDataSpec.h defaultSpec()); each pair has its own (columnsOf)
const QVector<IndColumn>& indicatorColumns();
QStringList indicatorColumnNames();
QString indicatorVersion();   // changes with the columns' definitions

constexpr int OVERLAP = 5000;
constexpr int WARMUP = 600;   // bars: three times the longest period (200); a pair with longer ones has more (warmupOf)

// Computes the columns on bars [0, n) and returns the rows of bars [from, n); carried = the stored values of bar
// from - 1, one per column (only running sums use theirs; NaN when from is 0 or unknown). Pure: tests use it directly.
QVector<IndRow> computeRows(const QVector<Kline>& bars, int from, int tf, qint64 seriesFirst, const QVector<IndColumn>& cols,
                            const QVector<double>& carried, int warmup = WARMUP);

class Indicators : public QObject {
  Q_OBJECT
public:
  static Indicators& instance();
  explicit Indicators(Store* store, QObject* parent = nullptr);

  void setMarkets(const QStringList& m) { m_markets = m; }   // tests; by default the store's pairs
  void setTimeframes(const QVector<int>& tfs) { m_tfs = tfs; }
  void setPiece(int bars) { m_piece = bars; }      // bars written per piece (tests: small)
  void setOverlap(int bars) { m_overlap = bars; }  // tests only
  void setDebounce(int ms) { m_debounce.setInterval(ms); }   // tests only (2 s by default)

  void start();                                     // follows the store: computes what is missing, then each new bar
  void schedule(const QString& market, int tf, qint64 from = 0);   // from: an older bar changed (0 = after the last)
  bool busy() const { return m_running || !m_queue.isEmpty() || m_debounce.isActive(); }
  // Deepwater closing: nothing new starts, the computation running stops after its piece (its rows and where it stands
  // are written together, so the next run goes on from there); waits for it up to waitMs
  void shutdown(int waitMs = 20000);
  bool started() const { return m_started; }
  QString status() const { return m_status; }

signals:
  void progressChanged();
  void idle();   // nothing queued, running or still waiting for its 2 s

private:
  struct Job { QString market; int tf = 0; qint64 from = 0; };
  void next();
  void run(const Job& j);   // on a worker thread

  QStringList markets() const;
  QVector<int> timeframesOf(const QString& market) const;
  Store* m_store;
  QStringList m_markets;
  QVector<int> m_tfs;
  int m_piece = 200000, m_overlap = OVERLAP;
  QList<Job> m_queue;
  bool m_running = false, m_started = false;
  std::atomic_bool m_stopping{false};
  QPointer<QThread> m_thread;
  QString m_status;
  QTimer m_debounce;
  QHash<QString, qint64> m_pending;   // market|tf → earliest changed bar (0 = only new ones)
  QHash<QString, qint64> m_held;      // market|tf → earliest bar a backfill added so far (Store::backfilling)
};

}  // namespace technicaldata
