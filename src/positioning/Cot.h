#pragma once
#include <QObject>
#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QPointer>
#include <QVector>
#include <QString>
#include <functional>
#include <cmath>

// CFTC Commitments of Traders, futures only, from the CFTC's public Socrata API (publicreporting.cftc.gov):
//   Legacy report            dataset 6dca-aqww  (Commercials / Large Speculators / Small Traders)
//   Traders in Financial Fut dataset gpe5-46if  (Dealer / Asset Manager / Leveraged Funds / Other) — currencies, indices
//   Disaggregated            dataset 72hh-3qpy  (Producer / Swap Dealers / Managed Money / Other)  — commodities
// One request per dataset and market, about five years of weekly rows, cached on disk 6 hours (the report is weekly:
// Tuesday positions, published Friday 15:30 ET), except while a published report is missing from the copy in hand: then
// it is asked again at once, then every 10 minutes (positioning/CotPace.h).
//
//   positioning::Cot::instance().history("EUR", this, [](const positioning::CotHistory& h) { ... });
//   const auto& w = h.weeks.last();               // latest report
//   w.legacy[0].net(), w.oi, h.netSeries(...)     // see below
namespace positioning {

// One side (long or short) of one trader category in one report.
struct CotSide {
  double pos = NAN;      // contracts
  double chg = NAN;      // change vs the previous report
  double pctOi = NAN;    // % of open interest
  double traders = NAN;  // number of traders (NAN when the CFTC does not publish it)
};

struct CotCategory {
  QString name;          // "Commercials", "Leveraged Funds" … (English; display with translate("positioning", …))
  CotSide lng, sht;
  double spread = NAN;   // spreading positions (NAN when the report has none for this category)
  double net() const { return lng.pos - sht.pos; }
  double longPct() const { const double t = lng.pos + sht.pos; return t > 0 ? lng.pos / t * 100.0 : NAN; }
  // (long - short) / (long + short), in %: the "STATS" bars of the COT page
  double netPct() const { const double t = lng.pos + sht.pos; return t > 0 ? (lng.pos - sht.pos) / t * 100.0 : NAN; }
};

struct CotWeek {
  QDate date;                       // report date (positions as of that Tuesday)
  double oi = NAN, oiChg = NAN;     // open interest and its weekly change
  double traders = NAN;             // total traders
  QDateTime released;               // UTC, when the CFTC published it (Socrata :created_at); invalid if unknown
  // Legacy rows, always in this order: Commercials, Large Speculators, Total Reportable, Small Traders
  QVector<CotCategory> legacy;
  // Second report rows: TFF (Dealer/Intermediary, Asset Manager, Leveraged Funds, Other Reportables, Total Reportable,
  // Nonreportable) or Disaggregated (Producer/Merchant, Swap Dealers, Managed Money, Other Reportables, Total
  // Reportable, Nonreportable). Empty when the second report has no row for that week.
  QVector<CotCategory> second;
};

enum class CotReport { Tff, Disaggregated };

struct CotMarket {
  QString id;            // "USD", "EUR", "GOLD", "ES" … (stable, used in settings)
  QString label;         // button label (English; display with translate("positioning", …))
  QString assetClass;    // "fx", "commodities", "indices"
  QString code;          // CFTC contract market code, e.g. "099741"
  QString contract;      // what the COT measures, e.g. "EUR FX Futures (6E)"
  QString shortName;     // for titles, e.g. "EURO FX"
  QString futures;       // Yahoo ticker of the contract's continuous future (price panel), e.g. "6E=F"
  QString flagCode;      // ui::flag() code, empty for none
  CotReport second = CotReport::Tff;
};

struct CotHistory {
  CotMarket market;
  QVector<CotWeek> weeks;           // oldest first
  QString error;
  QString warning;                  // one of the two reports failed: the history is partial (not cached)
  bool stale = false;               // served from cache after a network failure
  QDateTime fetched;                // UTC
  bool ok() const { return error.isEmpty() && !weeks.isEmpty(); }
  QString secondName() const { return market.second == CotReport::Tff ? "TFF" : "DISAGGREGATED"; }
  QStringList secondNames() const;  // category names of the second report (6)
  // Net (long - short) of category `idx` of the legacy (second=false) or second report, last `n` weeks (n<=0: all).
  QVector<double> netSeries(bool second, int idx, int n = 0) const;
  // Position of the latest net value within the min..max of the last `n` weekly nets: 0..100 (NAN if flat/missing).
  double rangePosition(bool second, int idx, int n = 52) const;
};

class Cot : public QObject {
  Q_OBJECT
public:
  static Cot& instance();
  static const QList<CotMarket>& markets();              // catalogue (currencies, commodities, indices)
  static QList<CotMarket> markets(const QString& assetClass);
  static const CotMarket* market(const QString& id);
  // Matching standalone currency/commodity contract for a Myfxbook / app symbol ("EURUSD" → EUR, "XAUUSD" → GOLD);
  // nullptr for crosses (no standalone CFTC contract).
  static const CotMarket* marketForSymbol(const QString& symbol);

  // Weekly history of one market (both reports merged by date). Answers from memory when already loaded.
  using Callback = std::function<void(const CotHistory&)>;
  void history(const QString& id, QObject* ctx, Callback cb);
  // the whole history the CFTC publishes (legacy report since 1986, TFF/disaggregated since 2006), for Facts imports;
  // kept apart from the pages' 5-year history
  void deepHistory(const QString& id, QObject* ctx, Callback cb);
  const CotHistory* cachedHistory(const QString& id) const;
  // True when asking history(id) now would go to the CFTC: nothing in memory, or older than its pace allows.
  bool due(const QString& id) const;

private:
  Cot() = default;
  struct Pending { QPointer<QObject> ctx; bool hasCtx; Callback cb; };
  struct Load { int left = 0; QByteArray legacy, second; QString error; bool stale = false; QDateTime fetched; QList<Pending> waiters;
                bool again = false; };   // again: this is the second asking (the cached copy missed a published report)
  void issue(const QString& key, int ttlSec);
  void finish(const QString& id);
  void load(const QString& key, QObject* ctx, Callback cb);   // key: market id, + "#deep" for the whole history
  QHash<QString, CotHistory> m_done;
  QHash<QString, Load> m_loading;
};
}
