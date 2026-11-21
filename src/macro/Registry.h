#pragma once
#include <QString>
#include <QList>
#include <QPixmap>
#include <QSize>

// Asset registry: the economies the app follows (82), grouped by region, with the institution that sets
// their policy rate. Shared by Economy › Macro (globe registry) and Global › Overview (Asset Registry panel).
//
//   for (const auto& e : macro::registry(macro::Group::G20)) {
//     QString badge = macro::rateBadge(e);         // "FED", "ECB", … "BIS" or "WB"
//     bool live = macro::hasPolicyRate(e);         // highlighted row (rate from a central bank or BIS)
//     double r = macro::MacroStore::instance().policyRate(e.iso2).rate;
//   }
namespace macro {

enum class Region { America, Europe, Africa, Asia, Oceania };
enum class Group { G20, America, Europe, Africa, Asia, Oceania, Watch };   // Watch: the user's list (macro/Watchlist.h)
constexpr int kGroupCount = 7;

struct Economy {
  QString iso2;       // "us"; the euro area is "ez" (flag "eu")
  QString iso3;       // World Bank code: "USA"; euro area "EMU"
  QString name;       // registry name: "United States"
  QString longName;   // Asset File title: "United States of America"
  Region region;
  bool g20 = false;
  QString bank;       // central bank code when the app follows it: FED ECB BOE BOJ SNB BOC RBA RBNZ (euro members: ECB)
  QString bankName;   // "Federal Reserve", "Reserve Bank of India"…
  QString bis;        // BIS area code with a CBPOL policy-rate series ("US", "XM"), empty if none
  QString oecd;       // OECD area code ("USA", "EA20"), empty if the OECD does not cover it
  QString currency;   // "USD"
  double lat = 0, lon = 0;   // globe fly-to point
};

const QList<Economy>& economies();                 // all 82, registry order
QList<Economy> registry(Group g);                  // G20 = 23 in the reference order; regions alphabetical by weight
const Economy* economy(const QString& iso2);       // case-insensitive; also accepts ISO-3 and "EU"/"EA"
const Economy* economyForCurrency(const QString& ccy);   // USD → us, EUR → ez, GBP → gb …
QString groupLabel(Group g);                       // "★ G20", "AMERICA" …
QString regionName(Region r);                      // translated
// Names in the interface language (the table keeps English; French translations in translations/economy_fr.ts).
QString displayName(const Economy& e);             // "États-Unis"
QString displayLongName(const Economy& e);         // "États-Unis d'Amérique"
QString displayBankName(const Economy& e);         // "Réserve fédérale"
QString rateBadge(const Economy& e);               // bank code, else "BIS" when BIS publishes its rate, else "WB"
bool hasPolicyRate(const Economy& e);              // central bank or BIS policy-rate series
bool isEuroMember(const Economy& e);

// Flag of an economy (resources/flags, then resources/macro/flags for the smaller countries).
QPixmap flag(const QString& iso2, QSize size, qreal dpr = 2.0);
}
