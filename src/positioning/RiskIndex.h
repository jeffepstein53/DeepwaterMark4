#pragma once
#include <QObject>
#include <QDate>
#include <QDateTime>
#include <QVector>
#include <QHash>
#include <QString>
#include <cmath>

// Composite cross-asset risk appetite, 0–100 (computed by Deepwater from Yahoo Finance daily closes).
//
// Seven components; each one's reading of the day is ranked against its own previous 252 trading days (percentile,
// 0–100), oriented so that higher = more risk appetite:
//   vix     Volatility (VIX)       ^VIX close, inverted (lower volatility = more appetite)
//   trend   Equity trend           S&P 500 (^GSPC) vs its 125-day average, %
//   credit  Credit appetite        HYG / LQD ratio, 20-day change, %
//   audjpy  AUD/JPY                AUDJPY=X 20-day change, %
//   cugold  Copper / Gold          HG=F / GC=F ratio, 20-day change, %
//   stocks  Stocks vs safe haven   S&P 500 20-day return minus gold's 20-day return, points
//   crypto  Crypto momentum        BTC-USD 20-day change, %
// The composite is the plain average of the available components' percentiles (as the reference does), on the
// S&P 500's trading days; components trading on other days (FX, crypto) contribute their last value at that date.
//
//   auto& r = positioning::RiskIndex::instance();
//   connect(&r, &positioning::RiskIndex::updated, this, [&] { const auto& d = r.data(); ... });
//   r.load(240);       // fetches what is older than 240 s and recomputes; cheap to call again
namespace positioning {

struct RiskComponent {
  QString id, name, description;   // name / description: English source strings, display them with
                                    // QCoreApplication::translate("positioning", …) (translations/positioning_fr.ts)
  double raw = NAN;          // today's reading in its own unit
  QString rawText;           // formatted reading: "19.5", "+0.81%", "+5.64 pts"
  double percentile = NAN;   // 0..100, oriented (higher = more risk appetite)
  bool ok() const { return std::isfinite(percentile); }
};

struct RiskData {
  QVector<RiskComponent> components;   // always 7, in the order above
  QVector<QDate> dates;                // composite history dates (≈ 1 year of S&P 500 trading days), oldest first
  QVector<double> composite;           // composite per date (NAN when no component was available)
  double value = NAN;                  // latest composite
  double weekAgo = NAN, monthAgo = NAN;// composite 5 and 21 trading days earlier
  int available = 0;                   // components in today's composite
  // 1-year daily closes for the side charts
  QVector<QDate> spxDates; QVector<double> spx;
  QVector<QDate> vixDates; QVector<double> vix;
  QDateTime updated;                   // when the inputs were fetched (local time)
  QString error;                       // non-empty when nothing could be computed (English, translate("positioning", …))
  bool ok() const { return std::isfinite(value); }
};

class RiskIndex : public QObject {
  Q_OBJECT
public:
  static RiskIndex& instance();
  const RiskData& data() const { return m_data; }
  bool loading() const { return m_left > 0; }
  void load(int ttlSec = 1800);        // ttlSec: a cached answer younger than this is used (the tools Claude reads with pass 300)

  // Zone of a 0–100 value: 0 EXTREME OFF (<20), 1 RISK-OFF (<40), 2 NEUTRAL (40–59), 3 RISK-ON (60–79), 4 EXTREME ON.
  static int zone(double v);
  static QString zoneLabel(double v);          // "EXTREME OFF", "RISK-OFF", "NEUTRAL", "RISK-ON", "EXTREME ON" (in the interface language)
  static QStringList inputs();                  // the eight Yahoo tickers used

signals:
  void updated();

private:
  RiskIndex() = default;
  void compute();
  struct Series { QVector<QDate> d; QVector<double> c; };
  QHash<QString, Series> m_in;
  QDateTime m_fetched;
  int m_left = 0;
  RiskData m_data;
};
}
