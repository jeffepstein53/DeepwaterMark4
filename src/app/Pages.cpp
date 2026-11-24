#include "app/Pages.h"
#include "ui/Theme.h"
#include "ui/Kit.h"
#include <QHash>
#include <QVBoxLayout>
#include <QLabel>
#include <QCoreApplication>

namespace app {
const QList<MainTab>& tabs() {
  static const QList<MainTab> t{
    {"global", QT_TRANSLATE_NOOP("nav", "Global"), {
      {"overview", QT_TRANSLATE_NOOP("nav", "Overview"), QT_TRANSLATE_NOOP("nav", "The command post: market tape, Squawk wire, risk appetite, currency strength, central banks and world map.")},
      {"session-recap", QT_TRANSLATE_NOOP("nav", "Session Recap"), QT_TRANSLATE_NOOP("nav", "The recap of each session (Tokyo, London, New York): key moves, top headlines and data released.")},
      {"market-hours", QT_TRANSLATE_NOOP("nav", "Market Hours"), QT_TRANSLATE_NOOP("nav", "The four FX sessions live on your own clock, their overlaps and a countdown to every open.")}}},
    {"news", QT_TRANSLATE_NOOP("nav", "News"), {
      {"calendar", QT_TRANSLATE_NOOP("nav", "Calendar"), QT_TRANSLATE_NOOP("nav", "The economic calendar: actual, forecast, previous, impact and the range of bank forecasts.")},
      {"news-feed", QT_TRANSLATE_NOOP("nav", "News Feed"), QT_TRANSLATE_NOOP("nav", "Financial news as a visual feed, with the lead story, latest headlines and macro, inflation and earnings filters.")},
      {"bank-research", QT_TRANSLATE_NOOP("nav", "Bank Research"), QT_TRANSLATE_NOOP("nav", "Notes and forecasts from the major investment banks, gathered and sorted by institution.")},
      {"squawk", QT_TRANSLATE_NOOP("nav", "Squawk"), QT_TRANSLATE_NOOP("nav", "The real-time wire: headlines, data releases and price moves, timestamped to the second.")}}},
    {"economy", QT_TRANSLATE_NOOP("nav", "Economy"), {
      {"macro", QT_TRANSLATE_NOOP("nav", "Macro"), QT_TRANSLATE_NOOP("nav", "The macro globe: rates, inflation, growth and unemployment for each economy with history back to 2001.")},
      {"rates", QT_TRANSLATE_NOOP("nav", "Rates"), QT_TRANSLATE_NOOP("nav", "Central bank rate probabilities, meeting by meeting, and the implied path.")}}},
    {"positioning", QT_TRANSLATE_NOOP("nav", "Positioning"), {
      {"retail-sentiment", QT_TRANSLATE_NOOP("nav", "Retail Sentiment"), QT_TRANSLATE_NOOP("nav", "Retail traders' long / short positioning on every pair, and how it moves.")},
      {"cot", QT_TRANSLATE_NOOP("nav", "COT"), QT_TRANSLATE_NOOP("nav", "The Commitment of Traders report by trader category, as charts and tables.")},
      {"bank-positioning", QT_TRANSLATE_NOOP("nav", "Bank Positioning"), QT_TRANSLATE_NOOP("nav", "Banks' bias and positioning, currency by currency and asset class by asset class.")},
      {"risk-sentiment", QT_TRANSLATE_NOOP("nav", "Risk Sentiment"), QT_TRANSLATE_NOOP("nav", "The risk-on / risk-off gauge and its drivers: volatility, credit, equities, safe havens.")}}},
    {"market", QT_TRANSLATE_NOOP("nav", "Market"), {
      {"markets-watch", QT_TRANSLATE_NOOP("nav", "Markets Watch"), QT_TRANSLATE_NOOP("nav", "More than 100 live markets: currencies, indices, commodities, stocks and crypto.")},
      {"seasonality", QT_TRANSLATE_NOOP("nav", "Seasonality"), QT_TRANSLATE_NOOP("nav", "Seasonal behaviour over up to 20 years: average path, win rate, weekday and month effects.")},
      {"fundamental-heatmap", QT_TRANSLATE_NOOP("nav", "Fundamental Heatmap"), QT_TRANSLATE_NOOP("nav", "Currencies scored on their fundamentals: growth, inflation, rates, employment.")},
      {"technical-heatmap", QT_TRANSLATE_NOOP("nav", "Technical Heatmap"), QT_TRANSLATE_NOOP("nav", "Every market's technical signals at a glance: trend, momentum, volatility.")},
      {"liquidity", QT_TRANSLATE_NOOP("nav", "Liquidity"), QT_TRANSLATE_NOOP("nav", "Liquidity zones and key levels for each instrument, with the market's current state.")}}},
    {"account", QT_TRANSLATE_NOOP("nav", "Account"), {
      {"track-record", QT_TRANSLATE_NOOP("nav", "Track Record"), QT_TRANSLATE_NOOP("nav", "Your trading journal: statistics, drawdown, streaks, an MT4, MT5 and cTrader sync.")},
      {"risk-calculator", QT_TRANSLATE_NOOP("nav", "Risk Calculator"), QT_TRANSLATE_NOOP("nav", "Position size, risk in currency and risk / reward computed before you enter.")}}},
    {"data", QT_TRANSLATE_NOOP("nav", "Data"), {
      {"macro-data", QT_TRANSLATE_NOOP("nav", "Macro data"), QT_TRANSLATE_NOOP("nav", "Every economic fact Deepwater records, real or forecast, linked in chains per indicator.")},
      {"technical-data", QT_TRANSLATE_NOOP("nav", "Technical data"), QT_TRANSLATE_NOOP("nav", "Market bars of the pairs you record, on the timeframes and from the day you choose, with their indicators, for machine learning.")}}},
    {"sources", QT_TRANSLATE_NOOP("nav", "Sources"), {
      {"sources", QT_TRANSLATE_NOOP("nav", "Data Sources"), QT_TRANSLATE_NOOP("nav", "Every source Deepwater reads: status, keys and passwords, and a switch to turn each one off.")},
      {"sources-graph", QT_TRANSLATE_NOOP("nav", "Sources graph"), QT_TRANSLATE_NOOP("nav", "Where Deepwater's data comes from, and which tabs use it.")}}},
  };
  return t;
}
QString pageId(const QString& m, const QString& s) { return m + "/" + s; }
QString label(const QString& english) { return QCoreApplication::translate("nav", english.toUtf8().constData()); }

static QHash<QString, Factory>& reg() { static QHash<QString, Factory> r; return r; }
bool registerPage(const QString& id, Factory f) { reg().insert(id, std::move(f)); return true; }

QWidget* createPage(const QString& id, QWidget* parent) {
  if (auto it = reg().constFind(id); it != reg().cend()) return (*it)(parent);
  auto* w = new QWidget(parent);
  auto* v = new QVBoxLayout(w);
  QString desc;
  for (const auto& m : tabs()) for (const auto& s : m.subs) if (pageId(m.id, s.id) == id)
    desc = label(s.label) + " — " + label(s.description);
  auto* l = ui::label(desc, theme::mono(12), theme::pal().textMute, w);
  l->setAlignment(Qt::AlignCenter); l->setWordWrap(true);
  v->addWidget(l);
  return w;
}
}
