#include "positioning/Cot.h"
#include "positioning/CotPace.h"
#include "net/Http.h"
#include "net/Sources.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>
#include <QMap>
#include <QTimer>
#include <QFutureWatcher>
#include <QtConcurrent>

namespace positioning {
static const sources::Registrar kCftc({"cftc", "U.S. Commodity Futures Trading Commission (CFTC)",
  {"publicreporting.cftc.gov"}, sources::Access::Keyless, 1000,
  "Commitments of Traders, futures only: Legacy (6dca-aqww), Traders in Financial Futures (gpe5-46if), "
  "Disaggregated (72hh-3qpy) — weekly positions, changes, % of OI, traders",
  "U.S. government work, public domain; Socrata Open Data API without app token (paced 1 req/s)",
  {"positioning/cot", "positioning/retail-sentiment", "market/markets-watch"}});

namespace {
// Field names of one category in one Socrata dataset (empty = not published for that category).
struct Fields { const char* name; const char* l; const char* s; const char* sp; const char* cl; const char* cs;
                const char* pl; const char* ps; const char* tl; const char* ts; };

const QList<Fields>& legacyFields() {
  static const QList<Fields> f{
    {QT_TRANSLATE_NOOP("positioning", "Commercials"), "comm_positions_long_all", "comm_positions_short_all", "", "change_in_comm_long_all",
     "change_in_comm_short_all", "pct_of_oi_comm_long_all", "pct_of_oi_comm_short_all", "traders_comm_long_all",
     "traders_comm_short_all"},
    {QT_TRANSLATE_NOOP("positioning", "Large Speculators"), "noncomm_positions_long_all", "noncomm_positions_short_all", "noncomm_postions_spread_all",
     "change_in_noncomm_long_all", "change_in_noncomm_short_all", "pct_of_oi_noncomm_long_all",
     "pct_of_oi_noncomm_short_all", "traders_noncomm_long_all", "traders_noncomm_short_all"},
    {QT_TRANSLATE_NOOP("positioning", "Total Reportable"), "tot_rept_positions_long_all", "tot_rept_positions_short", "", "change_in_tot_rept_long_all",
     "change_in_tot_rept_short", "pct_of_oi_tot_rept_long_all", "pct_of_oi_tot_rept_short",
     "traders_tot_rept_long_all", "traders_tot_rept_short_all"},
    {QT_TRANSLATE_NOOP("positioning", "Small Traders"), "nonrept_positions_long_all", "nonrept_positions_short_all", "", "change_in_nonrept_long_all",
     "change_in_nonrept_short_all", "pct_of_oi_nonrept_long_all", "pct_of_oi_nonrept_short_all", "", ""}};
  return f;
}
const Fields kTotal{QT_TRANSLATE_NOOP("positioning", "Total Reportable"), "tot_rept_positions_long_all", "tot_rept_positions_short", "",
                    "change_in_tot_rept_long_all", "change_in_tot_rept_short", "pct_of_oi_tot_rept_long_all",
                    "pct_of_oi_tot_rept_short", "traders_tot_rept_long_all", "traders_tot_rept_short_all"};
const Fields kNonrep{QT_TRANSLATE_NOOP("positioning", "Nonreportable"), "nonrept_positions_long_all", "nonrept_positions_short_all", "",
                     "change_in_nonrept_long_all", "change_in_nonrept_short_all", "pct_of_oi_nonrept_long_all",
                     "pct_of_oi_nonrept_short_all", "", ""};
const Fields kOther{QT_TRANSLATE_NOOP("positioning", "Other Reportables"), "other_rept_positions_long", "other_rept_positions_short",
                    "other_rept_positions_spread", "change_in_other_rept_long", "change_in_other_rept_short",
                    "pct_of_oi_other_rept_long", "pct_of_oi_other_rept_short", "traders_other_rept_long_all",
                    "traders_other_rept_short"};
const QList<Fields>& tffFields() {
  static const QList<Fields> f{
    {QT_TRANSLATE_NOOP("positioning", "Dealer/Intermediary"), "dealer_positions_long_all", "dealer_positions_short_all", "dealer_positions_spread_all",
     "change_in_dealer_long_all", "change_in_dealer_short_all", "pct_of_oi_dealer_long_all",
     "pct_of_oi_dealer_short_all", "traders_dealer_long_all", "traders_dealer_short_all"},
    {QT_TRANSLATE_NOOP("positioning", "Asset Manager"), "asset_mgr_positions_long", "asset_mgr_positions_short", "asset_mgr_positions_spread",
     "change_in_asset_mgr_long", "change_in_asset_mgr_short", "pct_of_oi_asset_mgr_long", "pct_of_oi_asset_mgr_short",
     "traders_asset_mgr_long_all", "traders_asset_mgr_short_all"},
    {QT_TRANSLATE_NOOP("positioning", "Leveraged Funds"), "lev_money_positions_long", "lev_money_positions_short", "lev_money_positions_spread",
     "change_in_lev_money_long", "change_in_lev_money_short", "pct_of_oi_lev_money_long", "pct_of_oi_lev_money_short",
     "traders_lev_money_long_all", "traders_lev_money_short_all"},
    kOther, kTotal, kNonrep};
  return f;
}
const QList<Fields>& disFields() {
  static const QList<Fields> f{
    {QT_TRANSLATE_NOOP("positioning", "Producer/Merchant"), "prod_merc_positions_long", "prod_merc_positions_short", "", "change_in_prod_merc_long",
     "change_in_prod_merc_short", "pct_of_oi_prod_merc_long", "pct_of_oi_prod_merc_short",
     "traders_prod_merc_long_all", "traders_prod_merc_short_all"},
    {QT_TRANSLATE_NOOP("positioning", "Swap Dealers"), "swap_positions_long_all", "swap__positions_short_all", "swap__positions_spread_all",
     "change_in_swap_long_all", "change_in_swap_short_all", "pct_of_oi_swap_long_all", "pct_of_oi_swap_short_all",
     "traders_swap_long_all", "traders_swap_short_all"},
    {QT_TRANSLATE_NOOP("positioning", "Managed Money"), "m_money_positions_long_all", "m_money_positions_short_all", "m_money_positions_spread",
     "change_in_m_money_long_all", "change_in_m_money_short_all", "pct_of_oi_m_money_long_all",
     "pct_of_oi_m_money_short_all", "traders_m_money_long_all", "traders_m_money_short_all"},
    kOther, kTotal, kNonrep};
  return f;
}

QString selectOf(const QList<Fields>& fs) {
  QStringList s{"report_date_as_yyyy_mm_dd", "open_interest_all", "change_in_open_interest_all", "traders_tot_all"};
  for (const auto& f : fs)
    for (const char* k : {f.l, f.s, f.sp, f.cl, f.cs, f.pl, f.ps, f.tl, f.ts})
      if (*k && !s.contains(k)) s << k;
  return s.join(',');
}

double num(const QJsonObject& o, const char* k) {
  if (!*k) return NAN;
  const auto v = o.value(QLatin1String(k));
  if (v.isUndefined() || v.isNull()) return NAN;
  bool ok = false;
  const double d = v.toString().trimmed().toDouble(&ok);
  return ok ? d : (v.isDouble() ? v.toDouble() : NAN);
}

CotCategory category(const QJsonObject& o, const Fields& f) {
  CotCategory c; c.name = f.name;
  c.lng = {num(o, f.l), num(o, f.cl), num(o, f.pl), num(o, f.tl)};
  c.sht = {num(o, f.s), num(o, f.cs), num(o, f.ps), num(o, f.ts)};
  c.spread = num(o, f.sp);
  return c;
}

QUrl urlOf(const char* dataset, const QString& code, const QList<Fields>& fs, bool deep = false) {
  // first day of the month 5 years and 2 months ago: stable within a month, so the cache key stays the same;
  // deep: everything the CFTC publishes (the legacy report starts in 1986)
  const QDate today = QDate::currentDate();
  const QDate since = deep ? QDate(1986, 1, 1) : QDate(today.year(), today.month(), 1).addMonths(-62);
  QUrl u(QString("https://publicreporting.cftc.gov/resource/%1.json").arg(dataset));
  QUrlQuery q;
  // :created_at (Socrata system field) = when the CFTC published the row: the real release time (holiday weeks
  // move it from Friday to Monday); only for the page's window, the deep history keeps its URL and cache
  q.addQueryItem("$select", selectOf(fs) + (deep ? QString() : QString(",:created_at")));
  q.addQueryItem("$where", QString("cftc_contract_market_code='%1' AND report_date_as_yyyy_mm_dd>='%2'")
                               .arg(code, since.toString("yyyy-MM-dd")));
  q.addQueryItem("$order", "report_date_as_yyyy_mm_dd");
  q.addQueryItem("$limit", deep ? "5000" : "2000");
  u.setQuery(q);
  return u;
}
}

QStringList CotHistory::secondNames() const {
  QStringList s;
  for (const auto& f : market.second == CotReport::Tff ? tffFields() : disFields()) s << f.name;
  return s;
}

QVector<double> CotHistory::netSeries(bool second, int idx, int n) const {
  QVector<double> out;
  const int from = n > 0 ? std::max(0, int(weeks.size()) - n) : 0;
  for (int i = from; i < weeks.size(); ++i) {
    const auto& v = second ? weeks[i].second : weeks[i].legacy;
    out << (idx < v.size() ? v[idx].net() : NAN);
  }
  return out;
}

double CotHistory::rangePosition(bool second, int idx, int n) const {
  const auto s = netSeries(second, idx, n);
  if (s.isEmpty() || !std::isfinite(s.last())) return NAN;
  double lo = INFINITY, hi = -INFINITY;
  for (double v : s) if (std::isfinite(v)) { lo = std::min(lo, v); hi = std::max(hi, v); }
  if (!(hi > lo)) return NAN;
  return (s.last() - lo) / (hi - lo) * 100.0;
}

Cot& Cot::instance() { static Cot* c = new Cot; return *c; }

const QList<CotMarket>& Cot::markets() {
  using R = CotReport;
  static const QList<CotMarket> m{
    {"USD", "USD", "fx", "098662", "ICE US Dollar Index (cash)", "USD INDEX", "DX-Y.NYB", "USD", R::Tff},
    {"EUR", "EUR", "fx", "099741", "EUR FX Futures (6E)", "EURO FX", "6E=F", "EUR", R::Tff},
    {"GBP", "GBP", "fx", "096742", "British Pound Futures (6B)", "BRITISH POUND", "6B=F", "GBP", R::Tff},
    {"JPY", "JPY", "fx", "097741", "Japanese Yen Futures (6J)", "JAPANESE YEN", "6J=F", "JPY", R::Tff},
    {"CHF", "CHF", "fx", "092741", "Swiss Franc Futures (6S)", "SWISS FRANC", "6S=F", "CHF", R::Tff},
    {"CAD", "CAD", "fx", "090741", "Canadian Dollar Futures (6C)", "CANADIAN DOLLAR", "6C=F", "CAD", R::Tff},
    {"AUD", "AUD", "fx", "232741", "Australian Dollar Futures (6A)", "AUSTRALIAN DOLLAR", "6A=F", "AUD", R::Tff},
    {"NZD", "NZD", "fx", "112741", "NZ Dollar Futures (6N)", "NZ DOLLAR", "6N=F", "NZD", R::Tff},
    {"GOLD", QT_TRANSLATE_NOOP("positioning", "GOLD"), "commodities", "088691", "COMEX Gold (GC)", "GOLD", "GC=F", "", R::Disaggregated},
    {"SILVER", QT_TRANSLATE_NOOP("positioning", "SILVER"), "commodities", "084691", "COMEX Silver (SI)", "SILVER", "SI=F", "", R::Disaggregated},
    {"COPPER", QT_TRANSLATE_NOOP("positioning", "COPPER"), "commodities", "085692", "COMEX Copper (HG)", "COPPER", "HG=F", "", R::Disaggregated},
    {"WTI", "WTI", "commodities", "067651", "NYMEX WTI Crude (CL)", "WTI CRUDE", "CL=F", "", R::Disaggregated},
    {"NATGAS", QT_TRANSLATE_NOOP("positioning", "NAT GAS"), "commodities", "023651", "NYMEX Natural Gas (NG)", "NATURAL GAS", "NG=F", "", R::Disaggregated},
    {"CORN", QT_TRANSLATE_NOOP("positioning", "CORN"), "commodities", "002602", "CBOT Corn (ZC)", "CORN", "ZC=F", "", R::Disaggregated},
    {"WHEAT", QT_TRANSLATE_NOOP("positioning", "WHEAT"), "commodities", "001602", "CBOT SRW Wheat (ZW)", "WHEAT", "ZW=F", "", R::Disaggregated},
    {"SOY", QT_TRANSLATE_NOOP("positioning", "SOYBEANS"), "commodities", "005602", "CBOT Soybeans (ZS)", "SOYBEANS", "ZS=F", "", R::Disaggregated},
    {"ES", "S&P 500", "indices", "13874A", "CME E-mini S&P 500 (ES)", "E-MINI S&P 500", "ES=F", "USD", R::Tff},
    {"NQ", "NASDAQ 100", "indices", "209742", "CME E-mini Nasdaq-100 (NQ)", "NASDAQ MINI", "NQ=F", "USD", R::Tff},
    {"YM", QT_TRANSLATE_NOOP("positioning", "DOW"), "indices", "124603", "CBOT E-mini Dow ($5) (YM)", "DJIA x $5", "YM=F", "USD", R::Tff},
    {"RTY", "RUSSELL 2000", "indices", "239742", "CME E-mini Russell 2000 (RTY)", "RUSSELL E-MINI", "RTY=F", "USD", R::Tff},
    {"NIKKEI", "NIKKEI", "indices", "240743", "CME Nikkei 225 Yen (NIY)", "NIKKEI YEN", "NIY=F", "JPY", R::Tff},
    {"VIX", "VIX", "indices", "1170E1", "CBOE VIX Futures (VX)", "VIX FUTURES", "^VIX", "", R::Tff},
    {"BTC", QT_TRANSLATE_NOOP("positioning", "BITCOIN"), "indices", "133741", "CME Bitcoin Futures (BTC)", "BITCOIN", "BTC=F", "", R::Tff},
  };
  return m;
}
QList<CotMarket> Cot::markets(const QString& cls) {
  QList<CotMarket> out;
  for (const auto& m : markets()) if (m.assetClass == cls) out << m;
  return out;
}
const CotMarket* Cot::market(const QString& id) {
  for (const auto& m : markets()) if (m.id == id) return &m;
  return nullptr;
}
const CotMarket* Cot::marketForSymbol(const QString& symbol) {
  QString s = symbol.toUpper(); s.remove('/');
  if (s.startsWith("XAU")) return market("GOLD");
  if (s.startsWith("XAG")) return market("SILVER");
  if (s.startsWith("BTC")) return market("BTC");
  if (s.contains("WTI") || s.contains("OIL")) return market("WTI");
  if (s.size() != 6) return nullptr;
  const QString a = s.left(3), b = s.mid(3);
  // only pairs against USD have a matching standalone CME currency future
  if (b == "USD" && a != "USD") return market(a);
  if (a == "USD" && b != "USD") return market(b);
  return nullptr;
}

const CotHistory* Cot::cachedHistory(const QString& id) const {
  auto it = m_done.constFind(id);
  return it == m_done.cend() ? nullptr : &*it;
}

void Cot::history(const QString& id, QObject* ctx, Callback cb) { load(id, ctx, std::move(cb)); }
void Cot::deepHistory(const QString& id, QObject* ctx, Callback cb) { load(id + "#deep", ctx, std::move(cb)); }

void Cot::load(const QString& key, QObject* ctx, Callback cb) {
  const QString id = key.section('#', 0, 0);
  const bool deep = key.endsWith("#deep");
  const CotMarket* mk = market(id);
  if (!mk) { CotHistory h; h.error = "unknown market " + id; QTimer::singleShot(0, ctx ? ctx : this, [cb, h] { cb(h); }); return; }
  const QDateTime now = QDateTime::currentDateTimeUtc();
  const auto known = m_done.constFind(key);
  // the whole history (Facts) is kept 6 h whatever happens; the pages' one follows the weekly publication
  const int maxAge = deep || known == m_done.cend() ? cotpace::kUpToDateSec : cotpace::maxAgeSec(known->weeks.last().date, now);
  if (known != m_done.cend() && known->fetched.secsTo(now) < maxAge) {
    const CotHistory h = *known;
    QTimer::singleShot(0, ctx ? ctx : this, [cb, h] { cb(h); });
    return;
  }
  const bool already = m_loading.contains(key);
  Load& ld = m_loading[key];
  ld.waiters << Pending{ctx, ctx != nullptr, std::move(cb)};
  if (already) return;
  issue(key, deep ? 24 * 3600 : maxAge);
}

void Cot::issue(const QString& key, int ttlSec) {
  const bool deep = key.endsWith("#deep");
  const CotMarket* mk = market(key.section('#', 0, 0));
  m_loading[key].left = 2;
  const auto& second = mk->second == CotReport::Tff ? tffFields() : disFields();
  auto ask = [this, key, deep, ttlSec](const QUrl& u, bool isSecond) {
    net::Request r; r.url = u; r.ttlSec = ttlSec; r.timeoutMs = deep ? 120000 : 45000;
    net::get(r, this, [this, key, isSecond](const net::Reply& rep) {
      Load& l = m_loading[key];
      if (!rep.ok()) l.error = rep.error.isEmpty() ? QString("HTTP %1").arg(rep.status) : rep.error;
      else (isSecond ? l.second : l.legacy) = rep.body;
      l.stale = l.stale || rep.stale;
      if (!l.fetched.isValid() || rep.fetched < l.fetched) l.fetched = rep.fetched;
      if (--l.left == 0) finish(key);
    });
  };
  ask(urlOf("6dca-aqww", mk->code, legacyFields(), deep), false);
  ask(urlOf(mk->second == CotReport::Tff ? "gpe5-46if" : "72hh-3qpy", mk->code, second, deep), true);
}

bool Cot::due(const QString& id) const {
  const auto it = m_done.constFind(id);
  if (it == m_done.cend()) return true;
  const QDateTime now = QDateTime::currentDateTimeUtc();
  return it->fetched.secsTo(now) >= cotpace::maxAgeSec(it->weeks.last().date, now);
}

// Both answers are in: the reports (the whole CFTC history for a Facts import: a few MB of JSON) are parsed on a pool
// thread; callers asking for the same key meanwhile wait in m_loading with the first ones.
void Cot::finish(const QString& key) {
  const Load& in = m_loading[key];
  const QByteArray legacyBody = in.legacy, secondBody = in.second;
  auto* w = new QFutureWatcher<CotHistory>(this);
  connect(w, &QFutureWatcher<CotHistory>::finished, this, [this, w, key] {
    w->deleteLater();
    // served from the cache, and a report published since is not in it: asked again, once, before anyone is answered
    if (Load& cur = m_loading[key]; !key.endsWith("#deep") && !cur.again && !cur.stale && cur.error.isEmpty()) {
      const CotHistory first = w->result();
      const QDateTime now = QDateTime::currentDateTimeUtc();
      const int maxAge = first.weeks.isEmpty() ? cotpace::kUpToDateSec : cotpace::maxAgeSec(first.weeks.last().date, now);
      if (cur.fetched.isValid() && cur.fetched.secsTo(now) >= maxAge) {
        cur.again = true;
        cur.legacy.clear(); cur.second.clear(); cur.fetched = {};
        issue(key, maxAge);
        return;
      }
    }
    Load ld = m_loading.take(key);
    CotHistory h = w->result();
    h.stale = ld.stale; h.fetched = ld.fetched;
    if (h.weeks.isEmpty()) h.error = ld.error.isEmpty() ? "no CFTC rows for " + h.market.code : ld.error;
    else if (!ld.error.isEmpty()) h.warning = ld.error;   // one report missing: shown, but asked again next time
    // only a complete history is kept: a partial one would leave the other report's panels empty for hours
    if (h.ok() && h.warning.isEmpty()) m_done.insert(key, h);
    for (const auto& p : ld.waiters) {
      if (p.hasCtx && !p.ctx) continue;
      if (p.cb) p.cb(h);
    }
  });
  w->setFuture(QtConcurrent::run([key, legacyBody, secondBody] {
    CotHistory h;
    h.market = *market(key.section('#', 0, 0));
    const auto& secondF = h.market.second == CotReport::Tff ? tffFields() : disFields();
    QMap<QDate, CotWeek> byDate;
    const auto legacy = QJsonDocument::fromJson(legacyBody).array();
    for (const auto& v : legacy) {
      const auto o = v.toObject();
      CotWeek w;
      w.date = QDate::fromString(o.value("report_date_as_yyyy_mm_dd").toString().left(10), Qt::ISODate);
      if (!w.date.isValid()) continue;
      w.oi = num(o, "open_interest_all"); w.oiChg = num(o, "change_in_open_interest_all");
      w.traders = num(o, "traders_tot_all");
      w.released = QDateTime::fromString(o.value(":created_at").toString(), Qt::ISODateWithMs).toUTC();
      for (const auto& f : legacyFields()) w.legacy << category(o, f);
      byDate.insert(w.date, w);
    }
    const auto second = QJsonDocument::fromJson(secondBody).array();
    for (const auto& v : second) {
      const auto o = v.toObject();
      const QDate d = QDate::fromString(o.value("report_date_as_yyyy_mm_dd").toString().left(10), Qt::ISODate);
      auto it = byDate.find(d);
      if (it == byDate.end()) continue;
      for (const auto& f : secondF) it->second << category(o, f);
    }
    h.weeks = byDate.values().toVector();
    return h;
  }));
}
}
