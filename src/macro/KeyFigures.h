#pragma once
#include "macro/CrossCheck.h"
#include <QObject>
#include <QTimer>

// The key figures of the eight currencies (policy rate, inflation, core inflation, unemployment, GDP, what the market
// prices for the next meeting and its date), each against the other publishers Deepwater already reads: the BIS and the
// OECD beside the statistics offices, the release calendar, FinancialJuice, Trading Economics, investingLive. Nothing is
// requested for it. Both values are shown when two sources differ (Sources tab, the instrument sheet, the Fundamental
// Heatmap, Economy › Rates, Claude's get_data_checks).
namespace macro {
class KeyFigures : public QObject {
  Q_OBJECT
public:
  static KeyFigures& instance();
  void start();                                   // once: follows the stores (MainWindow, pages)
  const QList<check::Item>& items() const { return m_items; }
  const check::Item* item(const QString& ccy, const QString& figure) const;
  check::Summary summary() const { return check::summary(m_items); }
  QDateTime checkedAt() const { return m_at; }    // UTC
  void recompute();

signals:
  void changed();

private:
  KeyFigures();
  void later() { if (!m_timer.isActive()) m_timer.start(8000); }
  QList<check::Item> m_items;
  QDateTime m_at;
  QTimer m_timer;
  QDateTime m_from;         // UTC: when the figures began to be gathered
  bool m_started = false;
};

// Names and sentences in the interface language.
QString checkValue(const check::Item& it, double v);   // the value with its unit, in the interface language
QString checkSource(const QString& source);       // "BIS" → "BRI"
QString checkFigureName(const QString& figure);    // "CPI y/y" → "Inflation (12 mois)"
QString checkVerdictName(check::Verdict v);        // "CONFIRMED", "DIFFERS"…
QString checkPeriod(const check::Item& it);        // "Oct 2026", "Q3 2026", "17 Dec 2026"
QString checkOthers(const check::Item& it);        // "BIS 2.6% · calendar 2.6%"; with the period when it is another one
// One line for a tooltip: "Differs: BIS 4.0% against 3.8% (Oct 2026)", "Confirmed by BIS (Oct 2026)"; empty when alone.
QString checkLine(const check::Item& it);
// The same for a figure's own tooltip, with the other sources; empty when there is nothing to say.
QString checkTip(const QString& ccy, const QString& figure);
bool checkDiffers(const QString& ccy, const QString& figure);
}
