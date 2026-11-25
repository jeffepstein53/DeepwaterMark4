#pragma once
#include <QString>
#include <QStringList>
#include <optional>
#include "news/Calendar.h"

// Economic data headlines from the wire, parsed into figures (fills calendar actuals).
//
//   investingLive: "US October PPI +6.5% vs +6.6% expected"
//                  "Germany October wholesale price index +0.4% vs +1.9% m/m prior"
//                  "China October 2026 CPI 0.7% y/y, expected 0.8%, prior 0.9%"
//                  "Canada employment change for October 4.7K vs 13.6K estimate"
//   FinancialJuice: "US Initial Jobless Claims Actual 224K (Forecast 220K, Previous 218K)"
//
// The parser only reads what the headline states. Matching it to a calendar event (Calendar::applyHeadline)
// additionally requires the same currency, the release time within the window, shared indicator words, a
// consistent unit and a stated forecast or prior equal to the calendar's — otherwise nothing is recorded.
namespace news {
struct DataHeadline {
  QString currency;          // "USD"
  QString indicator;         // words between the region and the figures, lowercased ("october ppi")
  QString period;            // "y/y", "m/m", "q/q" when the headline states it
  Value actual;
  Value forecast;            // "expected", "estimate", "Forecast"
  Value previous;            // "prior", "Previous", "prelim"
  Value revised;             // "Revised" / "Revision": the previous figure as revised
  QString country;           // "US", "DE", "FR", "EZ"… (the euro covers several)
  QString name;              // the headline's words before its figures ("US Core PCE Price Index YoY")
  bool structured = false;   // FinancialJuice's fixed format, every figure labelled
};
std::optional<DataHeadline> parseDataHeadline(const QString& text);
// Runs the parser on real headline samples; prints failures to stderr; returns the number of failures.
int selfTestDataHeadlines();
}
