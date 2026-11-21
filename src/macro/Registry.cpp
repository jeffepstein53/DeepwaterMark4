#include "macro/Registry.h"
#include "macro/Watchlist.h"
#include "ui/Kit.h"
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>
#include <QFile>
#include <QCoreApplication>

namespace macro {
namespace {
using R = Region;
// iso2, iso3, name, long name, region, g20, bank, bank name, BIS, OECD, currency, lat, lon
const QList<Economy>& table() {
  static const QList<Economy> t{
    // ---- America
    {"us", "USA", QT_TRANSLATE_NOOP("economy", "United States"), QT_TRANSLATE_NOOP("economy", "United States of America"), R::America, true, "FED", QT_TRANSLATE_NOOP("economy", "Federal Reserve"), "US", "USA", "USD", 39, -98},
    {"ca", "CAN", QT_TRANSLATE_NOOP("economy", "Canada"), QT_TRANSLATE_NOOP("economy", "Canada"), R::America, true, "BOC", QT_TRANSLATE_NOOP("economy", "Bank of Canada"), "CA", "CAN", "CAD", 57, -101},
    {"mx", "MEX", QT_TRANSLATE_NOOP("economy", "Mexico"), QT_TRANSLATE_NOOP("economy", "Mexico"), R::America, true, "", QT_TRANSLATE_NOOP("economy", "Banco de México"), "MX", "MEX", "MXN", 23, -102},
    {"br", "BRA", QT_TRANSLATE_NOOP("economy", "Brazil"), QT_TRANSLATE_NOOP("economy", "Brazil"), R::America, true, "", QT_TRANSLATE_NOOP("economy", "Banco Central do Brasil"), "BR", "BRA", "BRL", -10, -52},
    {"ar", "ARG", QT_TRANSLATE_NOOP("economy", "Argentina"), QT_TRANSLATE_NOOP("economy", "Argentina"), R::America, true, "", QT_TRANSLATE_NOOP("economy", "Banco Central de la República Argentina"), "AR", "ARG", "ARS", -35, -65},
    {"cl", "CHL", QT_TRANSLATE_NOOP("economy", "Chile"), QT_TRANSLATE_NOOP("economy", "Chile"), R::America, false, "", QT_TRANSLATE_NOOP("economy", "Banco Central de Chile"), "CL", "CHL", "CLP", -33, -71},
    {"co", "COL", QT_TRANSLATE_NOOP("economy", "Colombia"), QT_TRANSLATE_NOOP("economy", "Colombia"), R::America, false, "", QT_TRANSLATE_NOOP("economy", "Banco de la República"), "CO", "COL", "COP", 4, -73},
    {"pe", "PER", QT_TRANSLATE_NOOP("economy", "Peru"), QT_TRANSLATE_NOOP("economy", "Peru"), R::America, false, "", QT_TRANSLATE_NOOP("economy", "Banco Central de Reserva del Perú"), "PE", "", "PEN", -9, -75},
    {"uy", "URY", QT_TRANSLATE_NOOP("economy", "Uruguay"), QT_TRANSLATE_NOOP("economy", "Uruguay"), R::America, false, "", QT_TRANSLATE_NOOP("economy", "Banco Central del Uruguay"), "", "", "UYU", -33, -56},
    {"ec", "ECU", QT_TRANSLATE_NOOP("economy", "Ecuador"), QT_TRANSLATE_NOOP("economy", "Ecuador"), R::America, false, "", "", "", "", "USD", -1.5, -78},
    {"ve", "VEN", QT_TRANSLATE_NOOP("economy", "Venezuela"), QT_TRANSLATE_NOOP("economy", "Venezuela"), R::America, false, "", QT_TRANSLATE_NOOP("economy", "Banco Central de Venezuela"), "", "", "VES", 7, -66},
    {"cr", "CRI", QT_TRANSLATE_NOOP("economy", "Costa Rica"), QT_TRANSLATE_NOOP("economy", "Costa Rica"), R::America, false, "", QT_TRANSLATE_NOOP("economy", "Banco Central de Costa Rica"), "", "CRI", "CRC", 10, -84},
    {"pa", "PAN", QT_TRANSLATE_NOOP("economy", "Panama"), QT_TRANSLATE_NOOP("economy", "Panama"), R::America, false, "", "", "", "", "PAB", 8.5, -80},
    {"gt", "GTM", QT_TRANSLATE_NOOP("economy", "Guatemala"), QT_TRANSLATE_NOOP("economy", "Guatemala"), R::America, false, "", QT_TRANSLATE_NOOP("economy", "Banco de Guatemala"), "", "", "GTQ", 15.5, -90},
    {"do", "DOM", QT_TRANSLATE_NOOP("economy", "Dominican Rep."), QT_TRANSLATE_NOOP("economy", "Dominican Republic"), R::America, false, "", QT_TRANSLATE_NOOP("economy", "Banco Central de la República Dominicana"), "", "", "DOP", 19, -70.5},
    {"jm", "JAM", QT_TRANSLATE_NOOP("economy", "Jamaica"), QT_TRANSLATE_NOOP("economy", "Jamaica"), R::America, false, "", QT_TRANSLATE_NOOP("economy", "Bank of Jamaica"), "", "", "JMD", 18, -77.3},
    // ---- Europe
    {"ez", "EMU", QT_TRANSLATE_NOOP("economy", "Euro Zone"), QT_TRANSLATE_NOOP("economy", "Euro Area"), R::Europe, true, "ECB", QT_TRANSLATE_NOOP("economy", "European Central Bank"), "XM", "EA20", "EUR", 48, 9},
    {"de", "DEU", QT_TRANSLATE_NOOP("economy", "Germany"), QT_TRANSLATE_NOOP("economy", "Germany"), R::Europe, true, "ECB", QT_TRANSLATE_NOOP("economy", "European Central Bank"), "XM", "DEU", "EUR", 51, 10},
    {"fr", "FRA", QT_TRANSLATE_NOOP("economy", "France"), QT_TRANSLATE_NOOP("economy", "France"), R::Europe, true, "ECB", QT_TRANSLATE_NOOP("economy", "European Central Bank"), "XM", "FRA", "EUR", 46.5, 2.5},
    {"it", "ITA", QT_TRANSLATE_NOOP("economy", "Italy"), QT_TRANSLATE_NOOP("economy", "Italy"), R::Europe, true, "ECB", QT_TRANSLATE_NOOP("economy", "European Central Bank"), "XM", "ITA", "EUR", 42.5, 12.5},
    {"es", "ESP", QT_TRANSLATE_NOOP("economy", "Spain"), QT_TRANSLATE_NOOP("economy", "Spain"), R::Europe, true, "ECB", QT_TRANSLATE_NOOP("economy", "European Central Bank"), "XM", "ESP", "EUR", 40, -3.7},
    {"nl", "NLD", QT_TRANSLATE_NOOP("economy", "Netherlands"), QT_TRANSLATE_NOOP("economy", "Netherlands"), R::Europe, false, "ECB", QT_TRANSLATE_NOOP("economy", "European Central Bank"), "XM", "NLD", "EUR", 52.2, 5.5},
    {"be", "BEL", QT_TRANSLATE_NOOP("economy", "Belgium"), QT_TRANSLATE_NOOP("economy", "Belgium"), R::Europe, false, "ECB", QT_TRANSLATE_NOOP("economy", "European Central Bank"), "XM", "BEL", "EUR", 50.6, 4.6},
    {"at", "AUT", QT_TRANSLATE_NOOP("economy", "Austria"), QT_TRANSLATE_NOOP("economy", "Austria"), R::Europe, false, "ECB", QT_TRANSLATE_NOOP("economy", "European Central Bank"), "XM", "AUT", "EUR", 47.6, 14.1},
    {"ie", "IRL", QT_TRANSLATE_NOOP("economy", "Ireland"), QT_TRANSLATE_NOOP("economy", "Ireland"), R::Europe, false, "ECB", QT_TRANSLATE_NOOP("economy", "European Central Bank"), "XM", "IRL", "EUR", 53.2, -8},
    {"pt", "PRT", QT_TRANSLATE_NOOP("economy", "Portugal"), QT_TRANSLATE_NOOP("economy", "Portugal"), R::Europe, false, "ECB", QT_TRANSLATE_NOOP("economy", "European Central Bank"), "XM", "PRT", "EUR", 39.6, -8},
    {"gr", "GRC", QT_TRANSLATE_NOOP("economy", "Greece"), QT_TRANSLATE_NOOP("economy", "Greece"), R::Europe, false, "ECB", QT_TRANSLATE_NOOP("economy", "European Central Bank"), "XM", "GRC", "EUR", 39, 22},
    {"fi", "FIN", QT_TRANSLATE_NOOP("economy", "Finland"), QT_TRANSLATE_NOOP("economy", "Finland"), R::Europe, false, "ECB", QT_TRANSLATE_NOOP("economy", "European Central Bank"), "XM", "FIN", "EUR", 64, 26},
    {"sk", "SVK", QT_TRANSLATE_NOOP("economy", "Slovakia"), QT_TRANSLATE_NOOP("economy", "Slovakia"), R::Europe, false, "ECB", QT_TRANSLATE_NOOP("economy", "European Central Bank"), "XM", "SVK", "EUR", 48.7, 19.5},
    {"si", "SVN", QT_TRANSLATE_NOOP("economy", "Slovenia"), QT_TRANSLATE_NOOP("economy", "Slovenia"), R::Europe, false, "ECB", QT_TRANSLATE_NOOP("economy", "European Central Bank"), "XM", "SVN", "EUR", 46.1, 14.8},
    {"lt", "LTU", QT_TRANSLATE_NOOP("economy", "Lithuania"), QT_TRANSLATE_NOOP("economy", "Lithuania"), R::Europe, false, "ECB", QT_TRANSLATE_NOOP("economy", "European Central Bank"), "XM", "LTU", "EUR", 55.3, 23.9},
    {"lv", "LVA", QT_TRANSLATE_NOOP("economy", "Latvia"), QT_TRANSLATE_NOOP("economy", "Latvia"), R::Europe, false, "ECB", QT_TRANSLATE_NOOP("economy", "European Central Bank"), "XM", "LVA", "EUR", 56.9, 24.6},
    {"ee", "EST", QT_TRANSLATE_NOOP("economy", "Estonia"), QT_TRANSLATE_NOOP("economy", "Estonia"), R::Europe, false, "ECB", QT_TRANSLATE_NOOP("economy", "European Central Bank"), "XM", "EST", "EUR", 58.6, 25},
    {"hr", "HRV", QT_TRANSLATE_NOOP("economy", "Croatia"), QT_TRANSLATE_NOOP("economy", "Croatia"), R::Europe, false, "ECB", QT_TRANSLATE_NOOP("economy", "European Central Bank"), "XM", "HRV", "EUR", 45.1, 15.2},
    {"bg", "BGR", QT_TRANSLATE_NOOP("economy", "Bulgaria"), QT_TRANSLATE_NOOP("economy", "Bulgaria"), R::Europe, false, "ECB", QT_TRANSLATE_NOOP("economy", "European Central Bank"), "XM", "BGR", "EUR", 42.7, 25.5},
    {"gb", "GBR", QT_TRANSLATE_NOOP("economy", "United Kingdom"), QT_TRANSLATE_NOOP("economy", "United Kingdom"), R::Europe, true, "BOE", QT_TRANSLATE_NOOP("economy", "Bank of England"), "GB", "GBR", "GBP", 54, -2},
    {"ch", "CHE", QT_TRANSLATE_NOOP("economy", "Switzerland"), QT_TRANSLATE_NOOP("economy", "Switzerland"), R::Europe, true, "SNB", QT_TRANSLATE_NOOP("economy", "Swiss National Bank"), "CH", "CHE", "CHF", 46.8, 8.2},
    {"se", "SWE", QT_TRANSLATE_NOOP("economy", "Sweden"), QT_TRANSLATE_NOOP("economy", "Sweden"), R::Europe, false, "", QT_TRANSLATE_NOOP("economy", "Sveriges Riksbank"), "SE", "SWE", "SEK", 62, 15},
    {"no", "NOR", QT_TRANSLATE_NOOP("economy", "Norway"), QT_TRANSLATE_NOOP("economy", "Norway"), R::Europe, false, "", QT_TRANSLATE_NOOP("economy", "Norges Bank"), "NO", "NOR", "NOK", 61, 9},
    {"dk", "DNK", QT_TRANSLATE_NOOP("economy", "Denmark"), QT_TRANSLATE_NOOP("economy", "Denmark"), R::Europe, false, "", QT_TRANSLATE_NOOP("economy", "Danmarks Nationalbank"), "DK", "DNK", "DKK", 56, 10},
    {"pl", "POL", QT_TRANSLATE_NOOP("economy", "Poland"), QT_TRANSLATE_NOOP("economy", "Poland"), R::Europe, false, "", QT_TRANSLATE_NOOP("economy", "Narodowy Bank Polski"), "PL", "POL", "PLN", 52, 19},
    {"cz", "CZE", QT_TRANSLATE_NOOP("economy", "Czechia"), QT_TRANSLATE_NOOP("economy", "Czech Republic"), R::Europe, false, "", QT_TRANSLATE_NOOP("economy", "Česká národní banka"), "CZ", "CZE", "CZK", 49.8, 15.5},
    {"hu", "HUN", QT_TRANSLATE_NOOP("economy", "Hungary"), QT_TRANSLATE_NOOP("economy", "Hungary"), R::Europe, false, "", QT_TRANSLATE_NOOP("economy", "Magyar Nemzeti Bank"), "HU", "HUN", "HUF", 47.2, 19.5},
    {"ro", "ROU", QT_TRANSLATE_NOOP("economy", "Romania"), QT_TRANSLATE_NOOP("economy", "Romania"), R::Europe, false, "", QT_TRANSLATE_NOOP("economy", "Banca Națională a României"), "RO", "ROU", "RON", 45.9, 25},
    {"rs", "SRB", QT_TRANSLATE_NOOP("economy", "Serbia"), QT_TRANSLATE_NOOP("economy", "Serbia"), R::Europe, false, "", QT_TRANSLATE_NOOP("economy", "National Bank of Serbia"), "RS", "", "RSD", 44, 21},
    {"is", "ISL", QT_TRANSLATE_NOOP("economy", "Iceland"), QT_TRANSLATE_NOOP("economy", "Iceland"), R::Europe, false, "", QT_TRANSLATE_NOOP("economy", "Seðlabanki Íslands"), "IS", "ISL", "ISK", 65, -18},
    {"tr", "TUR", QT_TRANSLATE_NOOP("economy", "Turkey"), QT_TRANSLATE_NOOP("economy", "Türkiye"), R::Europe, true, "", QT_TRANSLATE_NOOP("economy", "Central Bank of the Republic of Türkiye"), "TR", "TUR", "TRY", 39, 35},
    {"ru", "RUS", QT_TRANSLATE_NOOP("economy", "Russia"), QT_TRANSLATE_NOOP("economy", "Russian Federation"), R::Europe, true, "", QT_TRANSLATE_NOOP("economy", "Bank of Russia"), "RU", "RUS", "RUB", 60, 90},
    // ---- Africa
    {"za", "ZAF", QT_TRANSLATE_NOOP("economy", "South Africa"), QT_TRANSLATE_NOOP("economy", "South Africa"), R::Africa, true, "", QT_TRANSLATE_NOOP("economy", "South African Reserve Bank"), "ZA", "ZAF", "ZAR", -29, 25},
    {"ng", "NGA", QT_TRANSLATE_NOOP("economy", "Nigeria"), QT_TRANSLATE_NOOP("economy", "Nigeria"), R::Africa, false, "", QT_TRANSLATE_NOOP("economy", "Central Bank of Nigeria"), "", "", "NGN", 9.5, 8},
    {"eg", "EGY", QT_TRANSLATE_NOOP("economy", "Egypt"), QT_TRANSLATE_NOOP("economy", "Egypt"), R::Africa, false, "", QT_TRANSLATE_NOOP("economy", "Central Bank of Egypt"), "", "", "EGP", 26.5, 30},
    {"ma", "MAR", QT_TRANSLATE_NOOP("economy", "Morocco"), QT_TRANSLATE_NOOP("economy", "Morocco"), R::Africa, false, "", QT_TRANSLATE_NOOP("economy", "Bank Al-Maghrib"), "MA", "", "MAD", 31.8, -7},
    {"ke", "KEN", QT_TRANSLATE_NOOP("economy", "Kenya"), QT_TRANSLATE_NOOP("economy", "Kenya"), R::Africa, false, "", QT_TRANSLATE_NOOP("economy", "Central Bank of Kenya"), "", "", "KES", 0.5, 38},
    {"gh", "GHA", QT_TRANSLATE_NOOP("economy", "Ghana"), QT_TRANSLATE_NOOP("economy", "Ghana"), R::Africa, false, "", QT_TRANSLATE_NOOP("economy", "Bank of Ghana"), "", "", "GHS", 7.9, -1},
    {"dz", "DZA", QT_TRANSLATE_NOOP("economy", "Algeria"), QT_TRANSLATE_NOOP("economy", "Algeria"), R::Africa, false, "", QT_TRANSLATE_NOOP("economy", "Banque d'Algérie"), "", "", "DZD", 28, 2.6},
    {"et", "ETH", QT_TRANSLATE_NOOP("economy", "Ethiopia"), QT_TRANSLATE_NOOP("economy", "Ethiopia"), R::Africa, false, "", QT_TRANSLATE_NOOP("economy", "National Bank of Ethiopia"), "", "", "ETB", 9, 39.5},
    {"tn", "TUN", QT_TRANSLATE_NOOP("economy", "Tunisia"), QT_TRANSLATE_NOOP("economy", "Tunisia"), R::Africa, false, "", QT_TRANSLATE_NOOP("economy", "Banque Centrale de Tunisie"), "", "", "TND", 34, 9.5},
    {"ci", "CIV", QT_TRANSLATE_NOOP("economy", "Côte d'Ivoire"), QT_TRANSLATE_NOOP("economy", "Côte d'Ivoire"), R::Africa, false, "", QT_TRANSLATE_NOOP("economy", "BCEAO"), "", "", "XOF", 7.6, -5.5},
    // ---- Asia
    {"cn", "CHN", QT_TRANSLATE_NOOP("economy", "China"), QT_TRANSLATE_NOOP("economy", "People's Republic of China"), R::Asia, true, "", QT_TRANSLATE_NOOP("economy", "People's Bank of China"), "CN", "CHN", "CNY", 35, 104},
    {"jp", "JPN", QT_TRANSLATE_NOOP("economy", "Japan"), QT_TRANSLATE_NOOP("economy", "Japan"), R::Asia, true, "BOJ", QT_TRANSLATE_NOOP("economy", "Bank of Japan"), "JP", "JPN", "JPY", 37, 139},
    {"in", "IND", QT_TRANSLATE_NOOP("economy", "India"), QT_TRANSLATE_NOOP("economy", "India"), R::Asia, true, "", QT_TRANSLATE_NOOP("economy", "Reserve Bank of India"), "IN", "IND", "INR", 22, 79},
    {"kr", "KOR", QT_TRANSLATE_NOOP("economy", "South Korea"), QT_TRANSLATE_NOOP("economy", "Republic of Korea"), R::Asia, true, "", QT_TRANSLATE_NOOP("economy", "Bank of Korea"), "KR", "KOR", "KRW", 36.5, 127.8},
    {"id", "IDN", QT_TRANSLATE_NOOP("economy", "Indonesia"), QT_TRANSLATE_NOOP("economy", "Indonesia"), R::Asia, true, "", QT_TRANSLATE_NOOP("economy", "Bank Indonesia"), "ID", "IDN", "IDR", -2, 118},
    {"sa", "SAU", QT_TRANSLATE_NOOP("economy", "Saudi Arabia"), QT_TRANSLATE_NOOP("economy", "Saudi Arabia"), R::Asia, true, "", QT_TRANSLATE_NOOP("economy", "Saudi Central Bank"), "SA", "SAU", "SAR", 24, 45},
    {"sg", "SGP", QT_TRANSLATE_NOOP("economy", "Singapore"), QT_TRANSLATE_NOOP("economy", "Singapore"), R::Asia, true, "", QT_TRANSLATE_NOOP("economy", "Monetary Authority of Singapore"), "", "", "SGD", 1.35, 103.8},
    {"hk", "HKG", QT_TRANSLATE_NOOP("economy", "Hong Kong"), QT_TRANSLATE_NOOP("economy", "Hong Kong SAR, China"), R::Asia, false, "", QT_TRANSLATE_NOOP("economy", "Hong Kong Monetary Authority"), "HK", "", "HKD", 22.3, 114.2},
    {"my", "MYS", QT_TRANSLATE_NOOP("economy", "Malaysia"), QT_TRANSLATE_NOOP("economy", "Malaysia"), R::Asia, false, "", QT_TRANSLATE_NOOP("economy", "Bank Negara Malaysia"), "MY", "", "MYR", 4, 102},
    {"th", "THA", QT_TRANSLATE_NOOP("economy", "Thailand"), QT_TRANSLATE_NOOP("economy", "Thailand"), R::Asia, false, "", QT_TRANSLATE_NOOP("economy", "Bank of Thailand"), "TH", "", "THB", 15, 101},
    {"ph", "PHL", QT_TRANSLATE_NOOP("economy", "Philippines"), QT_TRANSLATE_NOOP("economy", "Philippines"), R::Asia, false, "", QT_TRANSLATE_NOOP("economy", "Bangko Sentral ng Pilipinas"), "PH", "", "PHP", 12, 122},
    {"il", "ISR", QT_TRANSLATE_NOOP("economy", "Israel"), QT_TRANSLATE_NOOP("economy", "Israel"), R::Asia, false, "", QT_TRANSLATE_NOOP("economy", "Bank of Israel"), "IL", "ISR", "ILS", 31.4, 35},
    {"ae", "ARE", QT_TRANSLATE_NOOP("economy", "U.A.E."), QT_TRANSLATE_NOOP("economy", "United Arab Emirates"), R::Asia, false, "", QT_TRANSLATE_NOOP("economy", "Central Bank of the UAE"), "", "", "AED", 24, 54},
    {"vn", "VNM", QT_TRANSLATE_NOOP("economy", "Vietnam"), QT_TRANSLATE_NOOP("economy", "Viet Nam"), R::Asia, false, "", QT_TRANSLATE_NOOP("economy", "State Bank of Vietnam"), "", "", "VND", 16, 107},
    {"pk", "PAK", QT_TRANSLATE_NOOP("economy", "Pakistan"), QT_TRANSLATE_NOOP("economy", "Pakistan"), R::Asia, false, "", QT_TRANSLATE_NOOP("economy", "State Bank of Pakistan"), "", "", "PKR", 30, 70},
    {"kw", "KWT", QT_TRANSLATE_NOOP("economy", "Kuwait"), QT_TRANSLATE_NOOP("economy", "Kuwait"), R::Asia, false, "", QT_TRANSLATE_NOOP("economy", "Central Bank of Kuwait"), "KW", "", "KWD", 29.3, 47.6},
    {"qa", "QAT", QT_TRANSLATE_NOOP("economy", "Qatar"), QT_TRANSLATE_NOOP("economy", "Qatar"), R::Asia, false, "", QT_TRANSLATE_NOOP("economy", "Qatar Central Bank"), "", "", "QAR", 25.3, 51.2},
    // ---- Oceania
    {"au", "AUS", QT_TRANSLATE_NOOP("economy", "Australia"), QT_TRANSLATE_NOOP("economy", "Australia"), R::Oceania, true, "RBA", QT_TRANSLATE_NOOP("economy", "Reserve Bank of Australia"), "AU", "AUS", "AUD", -25, 134},
    {"fj", "FJI", QT_TRANSLATE_NOOP("economy", "Fiji"), QT_TRANSLATE_NOOP("economy", "Fiji"), R::Oceania, false, "", QT_TRANSLATE_NOOP("economy", "Reserve Bank of Fiji"), "", "", "FJD", -17.8, 178},
    {"nc", "NCL", QT_TRANSLATE_NOOP("economy", "New Caledonia"), QT_TRANSLATE_NOOP("economy", "New Caledonia"), R::Oceania, false, "", "", "", "", "XPF", -21.3, 165.5},
    {"nz", "NZL", QT_TRANSLATE_NOOP("economy", "New Zealand"), QT_TRANSLATE_NOOP("economy", "New Zealand"), R::Oceania, false, "RBNZ", QT_TRANSLATE_NOOP("economy", "Reserve Bank of New Zealand"), "NZ", "NZL", "NZD", -41.5, 173},
    {"pg", "PNG", QT_TRANSLATE_NOOP("economy", "Papua New Guinea"), QT_TRANSLATE_NOOP("economy", "Papua New Guinea"), R::Oceania, false, "", QT_TRANSLATE_NOOP("economy", "Bank of Papua New Guinea"), "", "", "PGK", -6.5, 145},
    {"sb", "SLB", QT_TRANSLATE_NOOP("economy", "Solomon Is."), QT_TRANSLATE_NOOP("economy", "Solomon Islands"), R::Oceania, false, "", QT_TRANSLATE_NOOP("economy", "Central Bank of Solomon Islands"), "", "", "SBD", -9.6, 160},
    {"vu", "VUT", QT_TRANSLATE_NOOP("economy", "Vanuatu"), QT_TRANSLATE_NOOP("economy", "Vanuatu"), R::Oceania, false, "", QT_TRANSLATE_NOOP("economy", "Reserve Bank of Vanuatu"), "", "", "VUV", -16, 167.5},
  };
  return t;
}
}

const QList<Economy>& economies() { return table(); }

QList<Economy> registry(Group g) {
  QList<Economy> out;
  if (g == Group::G20) {
    static const QStringList order{"us", "ez", "cn", "jp", "de", "gb", "fr", "in", "it", "br", "ca", "kr",
                                   "ru", "es", "au", "mx", "id", "tr", "ch", "sa", "ar", "za", "sg"};
    for (const auto& c : order) if (const auto* e = economy(c)) out << *e;
    return out;
  }
  if (g == Group::Watch) {
    for (const auto& c : Watchlist::instance().codes()) if (const auto* e = economy(c)) out << *e;
    return out;
  }
  const Region r = g == Group::America ? Region::America : g == Group::Europe ? Region::Europe
                 : g == Group::Africa ? Region::Africa : g == Group::Asia ? Region::Asia : Region::Oceania;
  for (const auto& e : table()) if (e.region == r) out << e;
  if (r == Region::Oceania)   // the reference lists Oceania alphabetically
    std::sort(out.begin(), out.end(), [](const Economy& a, const Economy& b) { return a.name < b.name; });
  return out;
}

const Economy* economy(const QString& code) {
  const QString c = code.toLower();
  for (const auto& e : table()) if (e.iso2 == c) return &e;
  if (c == "eu" || c == "ea" || c == "xm" || c == "emu" || c == "ea20") return economy("ez");
  if (c == "uk") return economy("gb");
  for (const auto& e : table()) if (e.iso3.compare(code, Qt::CaseInsensitive) == 0) return &e;
  return nullptr;
}

const Economy* economyForCurrency(const QString& ccy) {
  const QString c = ccy.toUpper();
  if (c == "EUR") return economy("ez");
  if (c == "USD") return economy("us");
  for (const auto& e : table()) if (e.currency == c) return &e;
  return nullptr;
}

QString groupLabel(Group g) {
  switch (g) {
    case Group::G20: return QString::fromUtf8("★ G20");
    case Group::America: return QCoreApplication::translate("economy", "AMERICA");
    case Group::Europe: return QCoreApplication::translate("economy", "EUROPE");
    case Group::Africa: return QCoreApplication::translate("economy", "AFRICA");
    case Group::Asia: return QCoreApplication::translate("economy", "ASIA");
    case Group::Oceania: return QCoreApplication::translate("economy", "OCEANIA");
    case Group::Watch: return QString::fromUtf8("☆ ") + QCoreApplication::translate("economy", "WATCH");
  }
  return {};
}
QString regionName(Region r) {
  switch (r) {
    case Region::America: return QCoreApplication::translate("economy", "America");
    case Region::Europe: return QCoreApplication::translate("economy", "Europe");
    case Region::Africa: return QCoreApplication::translate("economy", "Africa");
    case Region::Asia: return QCoreApplication::translate("economy", "Asia");
    case Region::Oceania: return QCoreApplication::translate("economy", "Oceania");
  }
  return {};
}
QString displayName(const Economy& e) { return QCoreApplication::translate("economy", e.name.toUtf8().constData()); }
QString displayLongName(const Economy& e) { return QCoreApplication::translate("economy", e.longName.toUtf8().constData()); }
QString displayBankName(const Economy& e) {
  return e.bankName.isEmpty() ? QString() : QCoreApplication::translate("economy", e.bankName.toUtf8().constData());
}
bool isEuroMember(const Economy& e) { return e.bank == "ECB" && e.iso2 != "ez"; }
QString rateBadge(const Economy& e) {
  if (!e.bank.isEmpty()) return e.bank;
  if (!e.bis.isEmpty()) return "BIS";
  return "WB";
}
bool hasPolicyRate(const Economy& e) { return !e.bank.isEmpty() || !e.bis.isEmpty(); }

QPixmap flag(const QString& iso2, QSize size, qreal dpr) {
  QString iso = iso2.toLower();
  if (iso == "ez") iso = "eu";
  const QString path = QString(":/macro/flags/%1.svg").arg(iso);
  if (!QFile::exists(path)) return ui::flag(iso, size, dpr);
  static QHash<QString, QPixmap> cache;
  const QString key = QString("%1_%2x%3_%4").arg(iso).arg(size.width()).arg(size.height()).arg(dpr);
  if (auto it = cache.constFind(key); it != cache.cend()) return *it;
  QPixmap pm(size * dpr); pm.setDevicePixelRatio(dpr); pm.fill(Qt::transparent);
  QSvgRenderer svg(path);
  if (svg.isValid()) {
    QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip; clip.addRoundedRect(QRectF(0, 0, size.width(), size.height()), 1.5, 1.5);
    p.setClipPath(clip);
    svg.render(&p, QRectF(0, 0, size.width(), size.height()));
  }
  cache.insert(key, pm);
  return pm;
}
}
