#include "news/DataHeadline.h"
#include <QRegularExpression>
#include <QTextStream>
#include <cstdio>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include "core/Paths.h"

namespace news {
namespace {
using RE = QRegularExpression;
const auto CI = RE::CaseInsensitiveOption;

// A figure: sign, optional currency, digits, optional unit. "m/m" is not a unit (negative lookahead).
const QString NUM = R"((?<![A-Za-z0-9.])[-+]?(?:C\$|A\$|NZ\$|\$|€|£|¥)?\d[\d,]*(?:\.\d+)?(?:\s?(?:%|bn\b|million\b|billion\b|trillion\b|[KkMmBbTt](?![/A-Za-z])))?)";
const QString PER = R"((?:y/y|m/m|q/q|yoy|mom|qoq|annuali[sz]ed|saar))";

Value toValue(QString s) {
  s = s.trimmed();
  s.remove(RE(R"(C\$|A\$|NZ\$|\$|€|£|¥)"));
  s.replace(RE(R"(\s?million$)", CI), "M").replace(RE(R"(\s?billion$|\s?bn$)", CI), "B").replace(RE(R"(\s?trillion$)", CI), "T");
  s.replace(RE(R"(\s+(?=[%KkMmBbTt]$))"), "");
  if (s.endsWith('k') || s.endsWith('m') || s.endsWith('b') || s.endsWith('t')) s = s.left(s.size() - 1) + s.right(1).toUpper();
  if (s.startsWith('+')) s.remove(0, 1);
  return parseValue(s);
}

struct Region { const char* re; const char* ccy; const char* country; };
const Region kRegions[] = {
  {R"(\b(US|U\.S\.|United States|American)\b)", "USD", "US"}, {R"(\b(Canada|Canadian)\b)", "CAD", "CA"},
  {R"(\b(UK|U\.K\.|Britain|British)\b)", "GBP", "GB"}, {R"(\b(Eurozone|Euro ?zone|Euro area|EZ)\b)", "EUR", "EZ"},
  {R"(\b(Germany|German)\b)", "EUR", "DE"}, {R"(\b(France|French)\b)", "EUR", "FR"},
  {R"(\b(Italy|Italian)\b)", "EUR", "IT"}, {R"(\b(Spain|Spanish)\b)", "EUR", "ES"},
  {R"(\b(Japan|Japanese|Tokyo)\b)", "JPY", "JP"},
  {R"(\b(Australia|Australian)\b)", "AUD", "AU"}, {R"(\b(NZ|New Zealand)\b)", "NZD", "NZ"},
  {R"(\b(Switzerland|Swiss)\b)", "CHF", "CH"}, {R"(\b(China|Chinese)\b)", "CNY", "CN"}};
// US releases often named without the country.
const char* kUsOnly = R"(\b(JOLTS|EIA|ISM|UMich|University of Michigan|ADP|Richmond Fed|Philly Fed|Philadelphia Fed|Empire State|Case-Shiller|FHFA|Chicago PMI|crude oil inventories|initial jobless claims|non-?farm payrolls)\b)";

QString cleanIndicator(QString s) {
  s = s.toLower();
  static const RE months(R"(\b(january|february|march|april|may|june|july|august|september|october|november|december|jan|feb|mar|apr|jun|jul|aug|sep|sept|oct|nov|dec)\b)");
  static const RE noise(R"(\b(20\d\d|q[1-4]|for|the|weekly|of|at|in|month|reading|data)\b|[,:()–—-]+)");
  s.remove(months); s.remove(noise);
  for (const auto& r : kRegions) s.remove(RE(r.re, CI));
  return s.simplified();
}
}

std::optional<DataHeadline> parseDataHeadline(const QString& in) {
  QString t = in.simplified();
  t.replace("−", "-").replace("–", "-");
  DataHeadline h;
  QString head;
  // FinancialJuice: "<name> Actual X (Forecast Y, Previous Z)"
  static const RE fj(R"(^(.*?)\s+Actual\s+()" + NUM + R"()\s*\(\s*Forecast\s+()" + NUM + R"(|-|N/A)?\s*,\s*Previous\s+()" + NUM +
                     R"(|-|N/A)\s*(?:,\s*Revis(?:ed|ion)\s+()" + NUM + R"(|-|N/A)\s*)?\))", CI);
  // investingLive: "<name> X [per] vs Y [per] <label>"
  static const RE vs(R"(^(.*?)\s*()" + NUM + R"()\s*()" + PER + R"()?\s*,?\s*(?:vs\.?|versus)\s+()" + NUM + R"()\s*()" + PER + R"()?\s*(expected|exp\.?|estimates?|est\.?|consensus|forecast|prior|previous|prelim(?:inary)?)\b)", CI);
  // "<name> X [per], expected Y[, prior Z]"
  static const RE exp(R"(^(.*?)\s*()" + NUM + R"()\s*()" + PER + R"()?\s*,?\s*(?:expected|exp\.?|estimate|est\.?)\s+()" + NUM + R"()(?:\s*,?\s*prior\s+()" + NUM + R"())?)", CI);
  if (auto m = fj.match(t); m.hasMatch()) {
    head = m.captured(1);
    h.actual = toValue(m.captured(2));
    if (m.captured(3) != "-" && m.captured(3).compare("N/A", Qt::CaseInsensitive)) h.forecast = toValue(m.captured(3));
    if (m.captured(4) != "-" && m.captured(4).compare("N/A", Qt::CaseInsensitive)) h.previous = toValue(m.captured(4));
    if (!m.captured(5).isEmpty() && m.captured(5) != "-" && m.captured(5).compare("N/A", Qt::CaseInsensitive)) h.revised = toValue(m.captured(5));
    h.structured = true;
  } else if (auto m = vs.match(t); m.hasMatch()) {
    head = m.captured(1);
    h.actual = toValue(m.captured(2));
    const QString label = m.captured(6).toLower();
    const Value second = toValue(m.captured(4));
    if (label.startsWith("prior") || label.startsWith("previous") || label.startsWith("prelim")) h.previous = second;
    else h.forecast = second;
    h.period = (m.captured(3).isEmpty() ? m.captured(5) : m.captured(3)).toLower();
  } else if (auto m = exp.match(t); m.hasMatch()) {
    head = m.captured(1);
    h.actual = toValue(m.captured(2));
    h.forecast = toValue(m.captured(4));
    if (!m.captured(5).isEmpty()) h.previous = toValue(m.captured(5));
    h.period = m.captured(3).toLower();
  } else return std::nullopt;
  if (!h.actual.num() || (!h.forecast.num() && !h.previous.num())) return std::nullopt;
  if (head.contains(RE(R"(\b(sets|reference rate|fixing|mid-?point|is expected to|will|to set|preview|poll)\b)", CI))) return std::nullopt;
  if (h.period.isEmpty()) { const auto pm = RE(PER, CI).match(t); if (pm.hasMatch()) h.period = pm.captured(0).toLower(); }
  if (h.period == "yoy") h.period = "y/y"; else if (h.period == "mom") h.period = "m/m"; else if (h.period == "qoq") h.period = "q/q";
  // region: in the words before the figures
  for (const auto& r : kRegions) if (RE(r.re, CI).match(head).hasMatch()) { h.currency = r.ccy; h.country = r.country; break; }
  if (h.currency.isEmpty() && RE(kUsOnly, CI).match(head).hasMatch()) { h.currency = "USD"; h.country = "US"; }
  if (h.currency.isEmpty()) return std::nullopt;
  h.name = head.simplified();
  h.indicator = cleanIndicator(head);
  if (h.indicator.isEmpty()) return std::nullopt;
  return h;
}

int selfTestDataHeadlines() {
  struct Case { const char* text; const char* ccy; const char* a; const char* f; const char* p; const char* per; };
  // Real headlines (investingLive, wire archive and its latest-news sitemap, Nov 2026), plus negative cases.
  const Case cases[] = {
    {"US October PPI +6.5% vs +6.6% expected", "USD", "6.5%", "6.6%", "", ""},
    {"Germany October wholesale price index +0.4% vs +1.9% m/m prior", "EUR", "0.4%", "", "1.9%", "m/m"},
    {"Crude oil inventories +4.657 million versus -1.109 million estimate", "USD", "4.657M", "-1.109M", "", ""},
    {"China October 2026 CPI 0.7% y/y, expected 0.8%, prior 0.9%", "CNY", "0.7%", "0.8%", "0.9%", "y/y"},
    {"Canada employment change for October 4.7K vs 13.6K estimate", "CAD", "4.7K", "13.6K", "", ""},
    {"US new home sales for September 0.676m annualized vs 0.650m estimate", "USD", "0.676M", "0.650M", "", "annualized"},
    {"US Initial Jobless Claims Actual 224K (Forecast 220K, Previous 218K)", "USD", "224K", "220K", "218K", ""},
    {"Italy Q3 preliminary GDP 0.0% vs 0.1% q/q expected", "EUR", "0.0%", "0.1%", "", "q/q"},
    {"US ADP Wkly Employment Change Actual 21k (Forecast -, Previous 23.75k, Revised 23k)", "USD", "21K", "", "23.75K", ""},
    {"US PCE Price Index YoY Actual 3.8% (Forecast 3.8%, Previous 3.6% ,Revision 3.7%)", "USD", "3.8%", "3.8%", "3.6%", "y/y"},
    {"German Service PMI Flash Actual 54 (Forecast 52.3, Previous 52.5)", "EUR", "54", "52.3", "52.5", ""},
    {" PBOC sets USD/ CNY reference rate for today at 6.7152 (vs. estimate at 6.6756)", nullptr, nullptr, nullptr, nullptr, nullptr},
    {"PBOC is expected to set the USD/CNY reference rate at 6.6756 – Reuters estimate", nullptr, nullptr, nullptr, nullptr, nullptr},
    {"Eurozone Q3 GDP unchanged in second estimate as uneven growth clouds the outlook", nullptr, nullptr, nullptr, nullptr, nullptr},
    {"Fed's Collins: essential for Fed to reduce inflation to 2%", nullptr, nullptr, nullptr, nullptr, nullptr},
    {"BoE's Pill says market expectations implying rates near 5% in 2027 are 'not outrageous' - FT", nullptr, nullptr, nullptr, nullptr, nullptr},
  };
  int fails = 0;
  for (const auto& c : cases) {
    const auto r = parseDataHeadline(QString::fromUtf8(c.text));
    QString got = !r ? "none" : QString("%1 a=%2 f=%3 p=%4 per=%5").arg(r->currency, r->actual.text, r->forecast.text, r->previous.text, r->period);
    QString want = !c.ccy ? "none" : QString("%1 a=%2 f=%3 p=%4 per=%5").arg(c.ccy, c.a, c.f, c.p, c.per);
    const bool ok = got == want;
    if (!ok) ++fails;
    std::fprintf(stderr, "[headline-test] %s  %s\n    got  %s\n    want %s\n", ok ? "PASS" : "FAIL", c.text,
                 qPrintable(got), qPrintable(want));
  }
  // every archived wire headline of this profile: what the parser would read (no expectation, for review)
  QDir dir(paths::data("wire"));
  int seen = 0, parsed = 0;
  for (const auto& fi : dir.entryInfoList({"*.jsonl"}, QDir::Files, QDir::Name)) {
    QFile f(fi.absoluteFilePath());
    if (!f.open(QIODevice::ReadOnly)) continue;
    for (const QByteArray& line : f.readAll().split('\n')) {
      const auto o = QJsonDocument::fromJson(line).object();
      if (o.isEmpty() || o["k"].toInt() != 0) continue;
      ++seen;
      if (const auto r = parseDataHeadline(o["x"].toString())) {
        ++parsed;
        std::fprintf(stderr, "[headline-archive] %s → %s a=%s f=%s p=%s per=%s ind='%s'\n", qPrintable(o["x"].toString()),
                     qPrintable(r->currency), qPrintable(r->actual.text), qPrintable(r->forecast.text),
                     qPrintable(r->previous.text), qPrintable(r->period), qPrintable(r->indicator));
      }
    }
  }
  std::fprintf(stderr, "[headline-archive] %d headlines, %d parsed as data\n", seen, parsed);
  return fails;
}
}
