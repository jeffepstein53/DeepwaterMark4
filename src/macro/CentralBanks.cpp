#include "macro/CentralBanks.h"
#include "macro/Csv.h"
#include "macro/CentralBanksImpl.h"
#include "market/Candles.h"
#include "net/Http.h"
#include "net/Sources.h"
#include "core/Paths.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QCoreApplication>
#include "core/I18n.h"
#include <QTimeZone>
#include <QLocale>
#include <QSet>
#include <memory>

namespace macro {
namespace {
const sources::Registrar regNyFed({"nyfed", "Federal Reserve Bank of New York (Markets Data API)", {"markets.newyorkfed.org"},
    sources::Access::Keyless, 2000, "Effective federal funds rate and FOMC target range",
    "Public reference rates, free to use with attribution", {"economy/rates", "economy/macro", "global/overview", "market/fundamental-heatmap", "market/markets-watch"}});
const sources::Registrar regFed({"federalreserve", "Board of Governors of the Federal Reserve System", {"www.federalreserve.gov"},
    sources::Access::Keyless, 3000, "FOMC meeting calendar, monetary-policy press releases (statements, votes)",
    "US government work, public domain", {"economy/rates", "economy/macro", "global/overview", "market/fundamental-heatmap", "market/markets-watch"}});
const sources::Registrar regEcb({"ecb", "European Central Bank (Data Portal API, website)", {"data-api.ecb.europa.eu", "www.ecb.europa.eu"},
    sources::Access::Keyless, 1500, "Key ECB interest rates, €STR, euro area yield curve, meeting calendar",
    "ECB statistics reuse policy: free reuse with source mentioned", {"economy/rates", "economy/macro", "global/overview", "market/fundamental-heatmap", "market/markets-watch"}});
const sources::Registrar regBoe({"boe", "Bank of England (Interactive Database, website, RSS)", {"www.bankofengland.co.uk"},
    sources::Access::Keyless, 3000, "Bank Rate, SONIA, gilt par yields, MPC dates, Monetary Policy Summaries (votes)",
    "BoE terms: data may be reused with attribution for personal/display use; paced, cached", {"economy/rates", "economy/macro", "global/overview", "market/fundamental-heatmap", "market/markets-watch"}});
const sources::Registrar regBoc({"boc", "Bank of Canada (Valet API, website)", {"www.bankofcanada.ca"},
    sources::Access::Keyless, 1500, "Target overnight rate, CORRA, bond yields, rate-announcement schedule",
    "Bank of Canada terms (bankofcanada.ca/terms): free use with attribution", {"economy/rates", "economy/macro", "global/overview", "market/fundamental-heatmap", "market/markets-watch"}});
const sources::Registrar regSnb({"snb", "Swiss National Bank (data portal)", {"data.snb.ch", "www.snb.ch"},
    sources::Access::Keyless, 2000, "SNB policy rate, SARON, event schedule",
    "SNB data portal terms: free use with source mentioned", {"economy/rates", "economy/macro", "global/overview", "market/fundamental-heatmap", "market/markets-watch"}});
const sources::Registrar regBoj({"boj", "Bank of Japan (website)", {"www.boj.or.jp"},
    sources::Access::Keyless, 3000, "Monetary Policy Meeting schedule", "Public information, source mentioned", {"economy/rates", "economy/macro", "global/overview", "market/fundamental-heatmap", "market/markets-watch"}});
const sources::Registrar regMx({"montreal-exchange", "Montréal Exchange (TMX) delayed quotes page", {"www.m-x.ca"},
    sources::Access::Keyless, 60000, "3-month CORRA futures (CRA) settlement prices",
    "Public delayed quotes for personal display; one page per hour at most, cached", {"economy/rates", "global/overview"}});
const sources::Registrar regPoly({"polymarket", "Polymarket (Gamma API)", {"gamma-api.polymarket.com", "clob.polymarket.com"},
    sources::Access::Keyless, 5000, "Central-bank decision markets (probe only)",
    "Public read API. On this network the TLS certificate is reported expired: never bypassed, the Rates page says so",
    {"economy/rates", "global/overview"}});

const QLocale kEn(QLocale::English);

QDate imm(int y, int m) {   // third Wednesday
  QDate d(y, m, 1);
  while (d.dayOfWeek() != Qt::Wednesday) d = d.addDays(1);
  return d.addDays(14);
}
QString monthCode(int m) { return QString("FGHJKMNQUVXZ").mid(m - 1, 1); }

double parseFrac(QString s) {   // "3-3/4" → 3.75, "4" → 4, "1/4" → 0.25
  s = s.trimmed();
  double whole = 0, frac = 0;
  const int dash = s.indexOf('-');
  if (s.contains('/')) {
    const QString f = dash >= 0 ? s.mid(dash + 1) : s;
    if (dash >= 0) whole = s.left(dash).toDouble();
    const auto p = f.split('/');
    if (p.size() == 2 && p[1].toDouble() != 0) frac = p[0].toDouble() / p[1].toDouble();
  } else whole = s.toDouble();
  return whole + frac;
}

QString plainText(const QByteArray& html) {
  QString s = QString::fromUtf8(html);
  static const QRegularExpression script("<script.*?</script>", QRegularExpression::DotMatchesEverythingOption),
      style("<style.*?</style>", QRegularExpression::DotMatchesEverythingOption), tag("<[^>]+>");
  s.remove(script);
  s.remove(style);
  s.replace(tag, " ");
  s = QTextDocumentFragment::fromHtml(s).toPlainText();
  s.replace(QChar(0xA0), ' ');
  return s.simplified();
}

QString pct2(double v) { return QString::number(v, 'f', 2) + "%"; }

// a decision's sentence in the interface language, or in English (en = true: what Facts stores)
QString tx(const char* s, bool en) { return en ? QString::fromUtf8(s) : QCoreApplication::translate("economy", s); }
QString moveText(double delta, double after, bool depositRate, bool en = false) {
  const int bp = qRound(delta * 100);
  if (depositRate) {
    if (bp == 0) return tx(QT_TRANSLATE_NOOP("economy", "Held the deposit facility rate at %1"), en).arg(pct2(after));
    return (bp > 0 ? tx(QT_TRANSLATE_NOOP("economy", "Raised the deposit facility rate by %1bp to %2"), en)
                   : tx(QT_TRANSLATE_NOOP("economy", "Cut the deposit facility rate by %1bp to %2"), en)).arg(std::abs(bp)).arg(pct2(after));
  }
  if (bp == 0) return tx(QT_TRANSLATE_NOOP("economy", "Held the policy rate at %1"), en).arg(pct2(after));
  return (bp > 0 ? tx(QT_TRANSLATE_NOOP("economy", "Raised the policy rate by %1bp to %2"), en)
                 : tx(QT_TRANSLATE_NOOP("economy", "Cut the policy rate by %1bp to %2"), en)).arg(std::abs(bp)).arg(pct2(after));
}

}  // namespace

QString decisionTextShown(const QString& en) {
  static const char* templates[] = {
      QT_TRANSLATE_NOOP("economy", "Held the deposit facility rate at %1"), QT_TRANSLATE_NOOP("economy", "Raised the deposit facility rate by %1bp to %2"),
      QT_TRANSLATE_NOOP("economy", "Cut the deposit facility rate by %1bp to %2"), QT_TRANSLATE_NOOP("economy", "Held the policy rate at %1"),
      QT_TRANSLATE_NOOP("economy", "Raised the policy rate by %1bp to %2"), QT_TRANSLATE_NOOP("economy", "Cut the policy rate by %1bp to %2"),
      QT_TRANSLATE_NOOP("economy", "Raised the three key rates by %1bp — deposit facility %2"),
      QT_TRANSLATE_NOOP("economy", "Cut the three key rates by %1bp — deposit facility %2"),
      QT_TRANSLATE_NOOP("economy", "Held the target range at %1"), QT_TRANSLATE_NOOP("economy", "Raised the target range by %1bp to %2"),
      QT_TRANSLATE_NOOP("economy", "Cut the target range by %1bp to %2"), QT_TRANSLATE_NOOP("economy", "Held Bank Rate at %1"),
      QT_TRANSLATE_NOOP("economy", "Raised Bank Rate by %1bp to %2"), QT_TRANSLATE_NOOP("economy", "Cut Bank Rate by %1bp to %2")};
  // the vote stays as it is ("(vote 6–3)")
  static const QRegularExpression voteRe(R"(^(.*?)( \(vote [^)]*\))?$)");
  const auto vm = voteRe.match(en);
  const QString body = vm.captured(1), vote = vm.captured(2);
  // the English templates as patterns, compiled once (the Facts canvas calls this per decision block per paint)
  static const QVector<QRegularExpression> patterns = [] {
    QVector<QRegularExpression> out;
    for (const char* t : templates) {
      QString pattern = QRegularExpression::escape(QString::fromUtf8(t));
      pattern.replace(QRegularExpression::escape("%1"), "(.+?)").replace(QRegularExpression::escape("%2"), "(.+?)");
      out << QRegularExpression("^" + pattern + "$");
    }
    return out;
  }();
  for (int i = 0; i < patterns.size(); ++i) {
    const char* t = templates[i];
    const auto m = patterns[i].match(body);
    if (!m.hasMatch()) continue;
    QString out = QCoreApplication::translate("economy", t);
    for (int k = 1; k <= m.lastCapturedIndex(); ++k) out = out.arg(m.captured(k));
    return out + vote;
  }
  return en;
}

namespace {
// Futures snapshot used by the solver.
struct Quote { QString sym; RateContract c; QVector<QPair<qint64, double>> bars; };
}

// ------------------------------------------------------------------------------------------------ odds
double Odds::at(int b) const {
  switch (b) { case 0: return cut50; case 1: return cut25; case 2: return hold; case 3: return hike25; default: return hike50; }
}

QVector<double> solveMeetingRates(double r0, const QVector<QDate>& eff, const QVector<RateContract>& cs) {
  const int M = eff.size();
  QVector<double> r(M + 1, NAN); r[0] = r0;
  int solved = 0;   // highest regime index with a known rate
  for (const auto& c : cs) {
    if (!std::isfinite(c.avg) || !c.start.isValid() || c.end <= c.start) continue;
    QVector<double> w(M + 1, 0.0);
    int kmax = 0;
    for (QDate d = c.start; d < c.end; d = d.addDays(1)) {
      int k = 0; while (k < M && eff[k] <= d) ++k;
      w[k] += 1; kmax = qMax(kmax, k);
    }
    const double T = c.end.daysTo(c.start) * -1.0;
    if (kmax <= solved) continue;   // nothing new in this contract
    double known = 0, wU = 0, wUk = 0;
    for (int k = 0; k <= kmax; ++k) {
      if (k <= solved) known += w[k] * r[k];
      else { wU += w[k]; wUk += w[k] * (k - solved); }
    }
    if (wU / T < 0.2 || wUk <= 0) continue;   // too little weight: let a later contract decide
    const double delta = (c.avg * T - known - r[solved] * wU) / wUk;
    for (int k = solved + 1; k <= kmax; ++k) r[k] = r[solved] + (k - solved) * delta;
    solved = kmax;
  }
  return r.mid(1);
}

QVector<Odds> oddsFromPath(double r0, const QVector<double>& rates) {
  QVector<Odds> out;
  // distribution over cumulative 25 bp steps, index offset 40
  const int N = 81, Z = 40;
  QVector<double> dist(N, 0.0); dist[Z] = 1;
  double prev = r0;
  for (double r : rates) {
    if (!std::isfinite(r)) break;
    const double steps = (r - prev) / 0.25;
    const int k = int(std::floor(steps));
    const double f = steps - k;
    QVector<double> nd(N, 0.0);
    for (int i = 0; i < N; ++i) {
      if (dist[i] == 0) continue;
      const int a = qBound(0, i + k, N - 1), b = qBound(0, i + k + 1, N - 1);
      nd[a] += dist[i] * (1 - f); nd[b] += dist[i] * f;
    }
    dist = nd; prev = r;
    Odds o; o.cut50 = 0; o.cut25 = dist[Z - 1]; o.hold = dist[Z]; o.hike25 = dist[Z + 1]; o.hike50 = 0;
    for (int i = 0; i < Z - 1; ++i) o.cut50 += dist[i];
    for (int i = Z + 2; i < N; ++i) o.hike50 += dist[i];
    out << o;
  }
  return out;
}

// ------------------------------------------------------------------------------------------------ bank
MeetingDate Bank::nextMeeting() const {
  const QDate today = QDate::currentDate();
  for (const auto& m : meetings) if (m.decision >= today) return m;
  return {};
}
int Bank::daysToNext() const {
  const auto m = nextMeeting();
  // decision day of a two-day meeting: the first day has passed, the meeting is under way (0, not -1 = unknown)
  return m.decision.isValid() ? qMax(0, int(QDate::currentDate().daysTo(m.first.isValid() ? m.first : m.decision))) : -1;
}
QList<MeetingDate> Bank::upcoming(int n) const {
  QList<MeetingDate> out;
  const QDate today = QDate::currentDate();
  for (const auto& m : meetings) if (m.decision >= today && out.size() < n) out << m;
  return out;
}
QString Bank::action() const {
  const Odds o = nextOdds();
  if (!o.valid()) return {};
  if (o.hike() > o.hold && o.hike() >= o.cut()) return "Hike";
  if (o.cut() > o.hold && o.cut() > o.hike()) return "Cut";
  return "Hold";
}
QString Bank::bias(int* pct) const {
  const Odds o = nextOdds();
  if (!o.valid()) { if (pct) *pct = -1; return {}; }
  if (o.hike() >= 0.10 && o.hike() >= o.cut()) { if (pct) *pct = qRound(o.hike() * 100); return "HAWKISH"; }
  if (o.cut() >= 0.10) { if (pct) *pct = qRound(o.cut() * 100); return "DOVISH"; }
  if (pct) *pct = qRound(o.hold * 100);
  return "NEUTRAL";
}

// ------------------------------------------------------------------------------------------------ service
CentralBanks& CentralBanks::instance() { static CentralBanks* s = new CentralBanks; return *s; }

CentralBanks::CentralBanks() {
  auto mk = [](QString id, QString code, QString name, QString iso, QString ccy, QString com, QString note, QString url) {
    Bank b; b.id = id; b.code = code; b.name = name; b.iso2 = iso; b.currency = ccy; b.committee = com;
    b.committeeNote = note; b.officialUrl = url; return b;
  };
  m_banks = {
    mk("FED", "Fed", QT_TRANSLATE_NOOP("economy", "Federal Reserve"), "us", "USD", "FOMC", QT_TRANSLATE_NOOP("economy", "Each statement lists every member's vote and names any dissent."),
       "https://www.federalreserve.gov/newsevents/pressreleases.htm"),
    mk("ECB", "ECB", QT_TRANSLATE_NOOP("economy", "European Central Bank"), "ez", "EUR", QT_TRANSLATE_NOOP("economy", "Governing Council"), QT_TRANSLATE_NOOP("economy", "Individual votes are not published."),
       "https://www.ecb.europa.eu/press/pr/html/index.en.html"),
    mk("BOE", "BoE", QT_TRANSLATE_NOOP("economy", "Bank of England"), "gb", "GBP", "MPC", QT_TRANSLATE_NOOP("economy", "The minutes name each member's vote."),
       "https://www.bankofengland.co.uk/monetary-policy-summary-and-minutes/monetary-policy-summary-and-minutes"),
    mk("BOJ", "BoJ", QT_TRANSLATE_NOOP("economy", "Bank of Japan"), "jp", "JPY", QT_TRANSLATE_NOOP("economy", "Policy Board"), QT_TRANSLATE_NOOP("economy", "The vote count is in each statement (PDF)."),
       "https://www.boj.or.jp/en/mopo/mpmdeci/index.htm"),
    mk("SNB", "SNB", QT_TRANSLATE_NOOP("economy", "Swiss National Bank"), "ch", "CHF", QT_TRANSLATE_NOOP("economy", "Governing Board"), QT_TRANSLATE_NOOP("economy", "Decided by the Governing Board; no vote is published."),
       "https://www.snb.ch/en/the-snb/mandates-goals/monetary-policy/decisions"),
    mk("RBA", "RBA", QT_TRANSLATE_NOOP("economy", "Reserve Bank of Australia"), "au", "AUD", QT_TRANSLATE_NOOP("economy", "Monetary Policy Board"), QT_TRANSLATE_NOOP("economy", "The statement gives the vote count; rba.gov.au refuses programs."),
       "https://www.rba.gov.au/media-releases/"),
    mk("RBNZ", "RBNZ", QT_TRANSLATE_NOOP("economy", "Reserve Bank of New Zealand"), "nz", "NZD", QT_TRANSLATE_NOOP("economy", "Monetary Policy Committee"), QT_TRANSLATE_NOOP("economy", "Decisions by consensus; rbnz.govt.nz refuses programs."),
       "https://www.rbnz.govt.nz/monetary-policy/monetary-policy-decisions"),
    mk("BOC", "BoC", QT_TRANSLATE_NOOP("economy", "Bank of Canada"), "ca", "CAD", QT_TRANSLATE_NOOP("economy", "Governing Council"), QT_TRANSLATE_NOOP("economy", "Decisions by consensus; no vote is published."),
       "https://www.bankofcanada.ca/core-functions/monetary-policy/key-interest-rate/"),
  };
  for (auto& b : m_banks) {
    b.pricingNote = QCoreApplication::translate("economy", "No free futures or OIS feed is wired for the %1; Polymarket, the reference's source, is unreachable here.")
                      .arg(QCoreApplication::translate("economy", b.name.toUtf8().constData()));
  }
  loadSchedule();
  m_polyStatus = QCoreApplication::translate("economy", "Not checked yet");
  connect(&i18n::Notifier::instance(), &i18n::Notifier::changed, this, &CentralBanks::retranslate);
  m_activeTimer.setInterval(60 * 1000);
  connect(&m_activeTimer, &QTimer::timeout, this, [this] { ensureLoaded(); });   // each part at its own pace inside
}

void CentralBanks::retranslate() {
  for (auto& b : m_banks) {
    if (!b.priced)
      b.pricingNote = QCoreApplication::translate("economy", "No free futures or OIS feed is wired for the %1; Polymarket, the reference's source, is unreachable here.")
                        .arg(QCoreApplication::translate("economy", b.name.toUtf8().constData()));
  }
  m_polyStatus = QCoreApplication::translate("economy", "Not checked yet");
  m_started.clear();
  ensureLoaded();
  changed();
}

Bank* CentralBanks::mut(const QString& id) {
  for (auto& b : m_banks) if (b.id == id) return &b;
  return nullptr;
}
const Bank* CentralBanks::bank(const QString& key) const {
  for (const auto& b : m_banks)
    if (b.id.compare(key, Qt::CaseInsensitive) == 0 || b.code.compare(key, Qt::CaseInsensitive) == 0 ||
        b.currency.compare(key, Qt::CaseInsensitive) == 0 || b.iso2.compare(key, Qt::CaseInsensitive) == 0)
      return &b;
  return nullptr;
}

void CentralBanks::changed() {
  if (m_emitQueued) return;
  m_emitQueued = true;
  QTimer::singleShot(0, this, [this] { m_emitQueued = false; emit updated(); });
}

void CentralBanks::loadSchedule() {
  QFile f(":/macro/meetings.json");
  if (!f.open(QIODevice::ReadOnly)) return;
  const auto o = QJsonDocument::fromJson(f.readAll()).object();
  const auto banks = o.value("banks").toObject();
  const auto heads = o.value("heads").toObject();
  for (auto& b : m_banks) {
    const auto j = banks.value(b.id).toObject();
    b.scheduleUrl = j.value("source").toString();
    QSet<QString> unv; for (const auto& u : j.value("unverified").toArray()) unv.insert(u.toString());
    b.meetings.clear();
    for (const auto& m : j.value("meetings").toArray()) {
      const auto a = m.toArray();
      MeetingDate md;
      md.decision = QDate::fromString(a.last().toString(), "yyyy-MM-dd");
      md.first = QDate::fromString(a.first().toString(), "yyyy-MM-dd");
      md.verified = !unv.contains(a.last().toString());
      if (md.decision.isValid()) b.meetings << md;
    }
    const auto h = heads.value(b.id).toArray();
    if (h.size() == 2) { b.headTitle = h[0].toString(); b.head = h[1].toString(); }
  }
}

void CentralBanks::ensureLoaded() {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  auto due = [&](const QString& k, qint64 every) {
    const auto it = m_started.constFind(k);
    if (it != m_started.cend() && now - *it < every) return false;
    m_started[k] = now; return true;
  };
  const qint64 M = 60 * 1000;
  const bool shown = !m_active.isEmpty();
  if (due("rates", 30 * M)) loadRates();
  if (due("fedschedule", 24 * 60 * M)) loadFedSchedule();
  if (due("fedpricing", (shown ? 5 : 15) * M)) loadFedPricing();
  if (due("ecbpricing", (shown ? 5 : 60) * M)) loadEcbPricing();
  if (due("bocpricing", 60 * M)) loadBocPricing();
  if (due("boepricing", 120 * M)) loadBoePricing();
  if (due("bojpricing", 120 * M)) loadBojPricing();
  if (due("snbpricing", 120 * M)) loadSnbPricing();
  if (due("fjpricing", 30 * M)) loadFjPricing();
  if (due("decisions", 6 * 60 * M)) loadDecisions();
  if (due("poly", 6 * 60 * M)) probePolymarket();
}

void CentralBanks::setActive(QObject* owner, bool active) {
  if (active) {
    if (!m_active.contains(owner)) connect(owner, &QObject::destroyed, this, [this, owner] { setActive(owner, false); });
    m_active.insert(owner);
    ensureLoaded();
    if (!m_activeTimer.isActive()) m_activeTimer.start();
  } else {
    if (m_active.remove(owner)) disconnect(owner, &QObject::destroyed, this, nullptr);   // connected again at the next visit
    if (m_active.isEmpty()) m_activeTimer.stop();
  }
}

void CentralBanks::refreshOdds(int maxAgeSec) {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  for (const char* k : {"fedpricing", "ecbpricing"}) {
    const auto it = m_started.constFind(k);
    if (it != m_started.cend() && now - *it >= maxAgeSec * 1000LL) m_started.remove(k);
  }
  ensureLoaded();
}

// ------------------------------------------------------------------------------------------------ rates
void CentralBanks::loadRates() {
  // BIS daily for all 8 (history for decisions; rate where no official feed answers)
  {
    net::Request r; r.ttlSec = 3 * 3600;
    const QString from = QDate::currentDate().addDays(-420).toString("yyyy-MM-dd");
    r.url = QUrl("https://stats.bis.org/api/v1/data/WS_CBPOL/D.US+XM+GB+JP+CH+AU+NZ+CA/all?startPeriod=" + from +
                 "&format=csv&detail=dataonly");
    net::get(r, this, [this](const net::Reply& rep) {
      if (!rep.ok()) return;
      const auto rows = csv::parse(rep.body);
      if (rows.isEmpty()) return;
      const int ia = rows[0].indexOf("REF_AREA"), it = rows[0].indexOf("TIME_PERIOD"), iv = rows[0].indexOf("OBS_VALUE");
      if (ia < 0 || it < 0 || iv < 0) return;
      static const QHash<QString, QString> area{{"US", "FED"}, {"XM", "ECB"}, {"GB", "BOE"}, {"JP", "BOJ"},
                                                {"CH", "SNB"}, {"AU", "RBA"}, {"NZ", "RBNZ"}, {"CA", "BOC"}};
      QHash<QString, QVector<QPair<QDate, double>>> hist;
      for (int i = 1; i < rows.size(); ++i) {
        const auto& row = rows[i];
        if (row.size() <= qMax(ia, qMax(it, iv))) continue;
        bool ok = false; const double v = row[iv].toDouble(&ok);
        const QDate d = QDate::fromString(row[it], "yyyy-MM-dd");
        if (ok && d.isValid() && area.contains(row[ia])) hist[area[row[ia]]].append({d, row[ia] == "US" ? v + 0.125 : v});   // US: BIS mid-point → upper bound
      }
      for (auto it2 = hist.begin(); it2 != hist.end(); ++it2) {
        std::sort(it2->begin(), it2->end(), [](auto& a, auto& b) { return a.first < b.first; });
        m_rateHist[it2.key()] = *it2;
        Bank* b = mut(it2.key());
        if (!b || it2->isEmpty()) continue;
        if (!std::isfinite(b->rate) || b->rateSourceEn.startsWith("BIS")) {   // English: the shown text is translated
          b->rate = it2->last().second; b->rateAsOf = it2->last().first; b->rateFetched = rep.fetched.toLocalTime();
          b->rateSourceEn = QStringLiteral("BIS central bank policy rates (daily)"); b->rateSource = QCoreApplication::translate("economy", "BIS central bank policy rates (daily)");
          b->rateNote = QCoreApplication::translate("economy", "as of %1").arg(i18n::locale().toString(b->rateAsOf, QCoreApplication::translate("economy", "MMM d")));
        }
        derive(*b);
      }
      changed();
    });
  }
  // Fed: NY Fed EFFR and target range
  {
    net::Request r; r.ttlSec = 1800;
    r.url = QUrl("https://markets.newyorkfed.org/api/rates/unsecured/effr/last/1.json");
    net::get(r, this, [this](const net::Reply& rep) {
      const auto a = QJsonDocument::fromJson(rep.body).object().value("refRates").toArray();
      if (!rep.ok() || a.isEmpty()) return;
      const auto o = a[0].toObject();
      Bank* b = mut("FED");
      b->rate = o.value("targetRateTo").toDouble(NAN);
      b->rateText = QString("%1–%2%").arg(o.value("targetRateFrom").toDouble(), 0, 'f', 2).arg(b->rate, 0, 'f', 2);
      b->overnight = o.value("percentRate").toDouble(NAN);
      b->rateAsOf = QDate::fromString(o.value("effectiveDate").toString(), "yyyy-MM-dd");
      b->rateNote = QCoreApplication::translate("economy", "as of %1").arg(i18n::locale().toString(b->rateAsOf, QCoreApplication::translate("economy", "MMM d")));
      b->rateSourceEn = QStringLiteral("Federal Reserve Bank of New York (EFFR, target range upper bound)"); b->rateSource = QCoreApplication::translate("economy", "Federal Reserve Bank of New York (EFFR, target range upper bound)");
      b->rateFetched = rep.fetched.toLocalTime();
      if (m_fedWaits > 15) { m_fedWaits = 0; m_started.remove("fedpricing"); ensureLoaded(); }   // pricing gave up waiting for it
      changed();
    });
  }
  // ECB: deposit facility (changes) + €STR
  {
    net::Request r; r.ttlSec = 3600;
    r.url = QUrl("https://data-api.ecb.europa.eu/service/data/FM/B.U2.EUR.4F.KR.DFR.LEV?lastNObservations=12&format=csvdata");
    net::get(r, this, [this](const net::Reply& rep) {
      const auto rows = csv::parse(rep.body);
      if (!rep.ok() || rows.size() < 2) return;
      const int it = rows[0].indexOf("TIME_PERIOD"), iv = rows[0].indexOf("OBS_VALUE");
      if (it < 0 || iv < 0) return;
      QVector<QPair<QDate, double>> ch;
      for (int i = 1; i < rows.size(); ++i)
        if (rows[i].size() > qMax(it, iv)) ch.append({QDate::fromString(rows[i][it], "yyyy-MM-dd"), rows[i][iv].toDouble()});
      std::sort(ch.begin(), ch.end(), [](auto& a, auto& b) { return a.first < b.first; });
      if (ch.isEmpty()) return;
      Bank* b = mut("ECB");
      b->rate = ch.last().second; b->rateAsOf = QDate::currentDate();
      b->rateNote = QCoreApplication::translate("economy", "since %1").arg(i18n::locale().toString(ch.last().first, QCoreApplication::translate("economy", "MMM d")));
      b->rateSince = ch.last().first;
      b->rateSourceEn = QStringLiteral("ECB deposit facility rate (ECB Data Portal)"); b->rateSource = QCoreApplication::translate("economy", "ECB deposit facility rate (ECB Data Portal)");
      b->rateFetched = rep.fetched.toLocalTime();
      changed();
    });
    net::Request e; e.ttlSec = 3600;
    e.url = QUrl("https://data-api.ecb.europa.eu/service/data/EST/B.EU000A2X2A25.WT?lastNObservations=1&format=csvdata");
    net::get(e, this, [this](const net::Reply& rep) {
      const auto rows = csv::parse(rep.body);
      if (!rep.ok() || rows.size() < 2) return;
      const int iv = rows[0].indexOf("OBS_VALUE");
      if (iv >= 0 && rows.last().size() > iv) { mut("ECB")->overnight = rows.last()[iv].toDouble(); loadEcbPricing(); }
    });
  }
  // BoE: Bank Rate + SONIA
  {
    net::Request r; r.ttlSec = 3600;
    const QString from = kEn.toString(QDate::currentDate().addDays(-30), "dd/MMM/yyyy");
    r.url = QUrl("https://www.bankofengland.co.uk/boeapps/database/_iadb-fromshowcolumns.asp?csv.x=yes&Datefrom=" + from +
                 "&Dateto=now&SeriesCodes=IUDBEDR,IUDSOIA&CSVF=TN&UsingCodes=Y&VPD=Y&VFD=N");
    net::get(r, this, [this](const net::Reply& rep) {
      const auto rows = csv::parse(rep.body);
      if (!rep.ok() || rows.size() < 2 || rows[0].value(0) != "DATE") return;
      Bank* b = mut("BOE");
      for (int i = rows.size() - 1; i >= 1; --i) {
        bool ok = false; const double v = rows[i].value(1).toDouble(&ok);
        if (!ok) continue;
        b->rate = v; b->rateAsOf = kEn.toDate(rows[i][0], "dd MMM yyyy");
        b->rateNote = QCoreApplication::translate("economy", "as of %1").arg(i18n::locale().toString(b->rateAsOf, QCoreApplication::translate("economy", "MMM d")));
        b->rateSourceEn = QStringLiteral("Bank of England Bank Rate (IADB IUDBEDR)"); b->rateSource = QCoreApplication::translate("economy", "Bank of England Bank Rate (IADB IUDBEDR)"); b->rateFetched = rep.fetched.toLocalTime();
        break;
      }
      QVector<QPair<QDate, double>> sonia;
      for (int i = 1; i < rows.size(); ++i) {
        bool ok = false; const double v = rows[i].value(2).toDouble(&ok);
        const QDate d = kEn.toDate(rows[i].value(0), "dd MMM yyyy");
        if (ok && d.isValid()) sonia.append({d, v});
      }
      std::sort(sonia.begin(), sonia.end(), [](auto& x, auto& y) { return x.first < y.first; });
      if (!sonia.isEmpty()) {
        m_sonia = sonia; b->overnight = sonia.last().second;
        if (m_boeNoSonia) { m_boeNoSonia = false; loadBoePricing(); }   // priced without it a moment ago
      }
      changed();
    });
  }
  // BoC: target + CORRA
  {
    net::Request r; r.ttlSec = 3600;
    r.url = QUrl("https://www.bankofcanada.ca/valet/observations/V39079,AVG.INTWO/json?recent=10");
    net::get(r, this, [this](const net::Reply& rep) {
      const auto obs = QJsonDocument::fromJson(rep.body).object().value("observations").toArray();
      if (!rep.ok() || obs.isEmpty()) return;
      Bank* b = mut("BOC");
      bool gotRate = false, gotOn = false;
      for (int i = obs.size() - 1; i >= 0; --i) {
        const auto o = obs[i].toObject();
        const auto t = o.value("V39079").toObject().value("v").toString();
        if (!t.isEmpty() && !gotRate) {
          gotRate = true;
          b->rate = t.toDouble(); b->rateAsOf = QDate::fromString(o.value("d").toString(), "yyyy-MM-dd");
          b->rateNote = QCoreApplication::translate("economy", "as of %1").arg(i18n::locale().toString(b->rateAsOf, QCoreApplication::translate("economy", "MMM d")));
          b->rateSourceEn = QStringLiteral("Bank of Canada target for the overnight rate (Valet V39079)"); b->rateSource = QCoreApplication::translate("economy", "Bank of Canada target for the overnight rate (Valet V39079)"); b->rateFetched = rep.fetched.toLocalTime();
        }
        const auto c = o.value("AVG.INTWO").toObject().value("v").toString();
        if (!c.isEmpty() && !gotOn) { gotOn = true; b->overnight = c.toDouble(); }
      }
      changed();
      loadBocPricing();
    });
  }
  // SNB: policy rate + SARON
  {
    net::Request r; r.ttlSec = 3600;
    r.url = QUrl("https://data.snb.ch/api/cube/snbgwdzid/data/csv/en?fromDate=" + QDate::currentDate().addDays(-40).toString("yyyy-MM-dd"));
    net::get(r, this, [this](const net::Reply& rep) {
      if (!rep.ok()) return;
      const auto rows = csv::parse(rep.body, ';');
      Bank* b = mut("SNB");
      for (const auto& row : rows) {
        if (row.size() < 3) continue;
        const QDate d = QDate::fromString(row[0], "yyyy-MM-dd");
        if (!d.isValid()) continue;
        if (row[1] == "LZ" && (!b->rateAsOf.isValid() || d >= b->rateAsOf || b->rateSourceEn.startsWith("BIS"))) {   // English: the shown text is translated
          b->rate = row[2].toDouble(); b->rateAsOf = d; b->rateNote = QCoreApplication::translate("economy", "as of %1").arg(i18n::locale().toString(d, QCoreApplication::translate("economy", "MMM d")));
          b->rateSourceEn = QStringLiteral("SNB policy rate (SNB data portal)"); b->rateSource = QCoreApplication::translate("economy", "SNB policy rate (SNB data portal)"); b->rateFetched = rep.fetched.toLocalTime();
        }
        if (row[1] == "SARON") b->overnight = row[2].toDouble();
      }
      changed();
    });
  }
}

void CentralBanks::loadFedSchedule() {
  net::Request r; r.ttlSec = 24 * 3600;
  r.url = QUrl("https://www.federalreserve.gov/monetarypolicy/fomccalendars.htm");
  net::get(r, this, [this](const net::Reply& rep) {
    if (!rep.ok()) return;
    const QString s = QString::fromUtf8(rep.body);
    QList<QPair<int, int>> heads;   // position, year
    QRegularExpression rh("(\\d{4}) FOMC Meetings");
    for (auto it = rh.globalMatch(s); it.hasNext();) { const auto m = it.next(); heads.append({int(m.capturedStart()), m.captured(1).toInt()}); }
    std::sort(heads.begin(), heads.end());
    QList<MeetingDate> out;
    QRegularExpression rm("fomc-meeting__month[^>]*>\\s*<strong>([^<]+)</strong>"), rd("fomc-meeting__date[^>]*>([^<]+)<");
    for (int i = 0; i < heads.size(); ++i) {
      const int y = heads[i].second;
      if (y < QDate::currentDate().year() - 1) continue;
      const QString seg = s.mid(heads[i].first, (i + 1 < heads.size() ? heads[i + 1].first : s.size()) - heads[i].first);
      QStringList months, dates;
      for (auto it = rm.globalMatch(seg); it.hasNext();) months << it.next().captured(1).trimmed();
      for (auto it = rd.globalMatch(seg); it.hasNext();) dates << it.next().captured(1).trimmed();
      for (int k = 0; k < qMin(months.size(), dates.size()); ++k) {
        if (dates[k].contains("notation") || dates[k].contains("unscheduled", Qt::CaseInsensitive)) continue;
        const QStringList mm = months[k].split('/');
        QString d = dates[k]; d.remove('*'); d = d.section(' ', 0, 0);
        const QStringList dd = d.split('-');
        const int m1 = kEn.toDate(mm.first().left(3) + " 1 2000", "MMM d yyyy").month();
        const int m2 = kEn.toDate(mm.last().left(3) + " 1 2000", "MMM d yyyy").month();
        if (m1 <= 0 || m2 <= 0) continue;
        MeetingDate md;
        md.first = QDate(y, m1, dd.first().toInt());
        md.decision = QDate(y, m2, dd.last().toInt());
        if (md.decision.isValid()) out << md;
      }
    }
    if (out.size() < 6) return;   // page changed: keep the bundled schedule
    std::sort(out.begin(), out.end(), [](const MeetingDate& a, const MeetingDate& b) { return a.decision < b.decision; });
    mut("FED")->meetings = out;
    changed();
    loadFedPricing();
  });
}

// ------------------------------------------------------------------------------------------------ pricing
namespace detail {
QDate immDate(int y, int m) { return imm(y, m); }

void computeFromQuotes(Bank& b, const QVector<QDate>& eff, const QList<MeetingDate>& meets,
                       const QList<QPair<QString, RateContract>>& contracts, const Gather& g, int effLagDays) {
  Q_UNUSED(effLagDays);
  // latest prices
  auto solveAt = [&](qint64 tMax, bool hourly) {
    QVector<RateContract> cs;
    for (const auto& [sym, c0] : contracts) {
      const auto& s = hourly ? g.hourly.value(sym) : g.daily.value(sym);
      double px = NAN;
      for (const auto& bar : s.bars) if (bar.t <= tMax) px = bar.c;
      if (!std::isfinite(px)) continue;
      RateContract c = c0; c.avg = 100.0 - px; cs << c;
    }
    return solveMeetingRates(b.overnight, eff, cs);
  };
  const qint64 now = QDateTime::currentSecsSinceEpoch();
  const QVector<double> r = solveAt(now + 86400, false);
  const QVector<Odds> odds = oddsFromPath(b.overnight, r);
  b.path.clear();
  double prev = b.overnight;
  for (int i = 0; i < odds.size() && i < meets.size() && i < 10; ++i) {
    PathPoint p; p.meeting = meets[i]; p.rate = b.rate + (r[i] - b.overnight); p.bp = (r[i] - prev) * 100; p.odds = odds[i];
    prev = r[i]; b.path << p;
  }
  b.priced = !b.path.isEmpty();
  b.impliedBp = b.priced ? b.path.first().bp : NAN;
  b.pricedAt = QDateTime::currentDateTime();
  const QVector<double> rw = solveAt(now - 7 * 86400, false);
  b.impliedBpWeekAgo = (!rw.isEmpty() && std::isfinite(rw[0])) ? (rw[0] - b.overnight) * 100 : NAN;
  // history: hourly when available, else daily
  QSet<qint64> ts;
  bool useHourly = false;
  for (const auto& s : g.hourly) if (s.bars.size() > 24) useHourly = true;
  for (const auto& s : (useHourly ? g.hourly : g.daily)) for (const auto& bar : s.bars) if (bar.t > now - 31 * 86400) ts.insert(bar.t);
  QList<qint64> tl(ts.begin(), ts.end()); std::sort(tl.begin(), tl.end());
  b.history.clear();
  for (qint64 t : tl) {
    const QVector<Odds> o = oddsFromPath(b.overnight, solveAt(t, useHourly));
    if (o.isEmpty()) continue;
    OddsSample smp; smp.t = QDateTime::fromSecsSinceEpoch(t, QTimeZone::UTC); smp.meetings = o.mid(0, b.path.size());
    b.history << smp;
  }
  b.historySource = useHourly ? "hourly closes" : "daily settlements";
}
QString withSharedContractCaveat(const QString& note, const Bank& b, const QVector<QDate>& eff,
                                 const QList<QPair<QString, RateContract>>& contracts) {
  if (!b.priced) return note;
  const int n = qMin(int(eff.size()), int(b.path.size()));
  for (const auto& [sym, c] : contracts)
    for (int i = 0; i + 1 < n; ++i)
      if (c.start < eff[i] && eff[i + 1] < c.end) {
        const QString cav = QCoreApplication::translate("economy", "Two meetings fall inside one 3-month contract: a move priced only for the "
                                                        "second one is shown split between both (the futures can't tell them apart).");
        return note.isEmpty() ? cav : note + " " + cav;
      }
  return note;
}
}  // namespace detail
using detail::Gather;
using detail::computeFromQuotes;
using detail::fomcDissenters;

void CentralBanks::loadFedPricing() {
  Bank* b = mut("FED");
  const QList<MeetingDate> meets = b->upcoming(10);
  if (meets.isEmpty()) return;
  QVector<QDate> eff; for (const auto& m : meets) eff << m.decision.addDays(1);
  QList<QPair<QString, RateContract>> contracts;
  QDate m0(QDate::currentDate().year(), QDate::currentDate().month(), 1);
  const QDate last = meets.last().decision.addMonths(2);
  for (QDate m = m0; m <= last && contracts.size() < 16; m = m.addMonths(1)) {
    const QString sym = QString("ZQ%1%2.CBT").arg(monthCode(m.month())).arg(m.year() % 100, 2, 10, QChar('0'));
    contracts.append({sym, RateContract{m, m.addMonths(1), NAN}});
  }
  auto g = std::make_shared<Gather>();
  g->pending = contracts.size() * 2;
  auto finish = [this, g, eff, meets, contracts] {
    if (--g->pending > 0) return;
    Bank* fb = mut("FED");
    if (!std::isfinite(fb->overnight) || !std::isfinite(fb->rate)) {
      // EFFR not in yet: retry shortly, for a minute; then wait for the EFFR answer (loadRates) or the next refresh
      if (++m_fedWaits <= 15) QTimer::singleShot(4000, this, [this] { m_started.remove("fedpricing"); ensureLoaded(); });
      else if (!fb->priced) { fb->pricingNote = QCoreApplication::translate("economy", "The NY Fed effective rate (EFFR) did not answer: no odds without today's level."); changed(); }
      return;
    }
    m_fedWaits = 0;
    computeFromQuotes(*fb, eff, meets, contracts, *g, 1);
    fb->pricingSourceEn = QStringLiteral("computed by Deepwater from CME 30-day fed funds futures (ZQ, via Yahoo Finance)"); fb->pricingSource = QCoreApplication::translate("economy", "computed by Deepwater from CME 30-day fed funds futures (ZQ, via Yahoo Finance)");
    fb->pricingNote = fb->priced ? QString() : QCoreApplication::translate("economy", "Fed funds futures did not answer.");
    changed();
  };
  for (const auto& [sym, c] : contracts) {
    // the daily series carries today's price (the odds shown): under 4 min old, where the 30 min of a daily series' usual
    // cache made the "every 15 min" odds move every 30; the hourly one only draws the history
    market::candles(sym, "1d", "3mo", this, [g, sym, finish](const market::Series& s) { g->daily[sym] = s; finish(); }, 240);
    market::candles(sym, "1h", "1mo", this, [g, sym, finish](const market::Series& s) { g->hourly[sym] = s; finish(); }, 1800);
  }
}

void CentralBanks::loadEcbPricing() {
  Bank* b = mut("ECB");
  if (!std::isfinite(b->overnight)) return;   // called again when €STR arrives
  const QList<MeetingDate> meets = b->upcoming(10);
  if (meets.isEmpty()) return;
  QVector<QDate> eff; for (const auto& m : meets) eff << m.decision.addDays(6);   // new rates apply the next Wednesday
  QList<QPair<QString, RateContract>> contracts;
  const QDate today = QDate::currentDate();
  int y = today.year(), m = ((today.month() - 1) / 3) * 3 + 3;   // current quarter month (H M U Z)
  for (int k = 0; k < 7; ++k) {
    const QDate s = imm(y, m), e = imm(m + 3 > 12 ? y + 1 : y, m + 3 > 12 ? m - 9 : m + 3);
    if (e > today) contracts.append({QString("ESR%1%2.CME").arg(monthCode(m)).arg(y % 100, 2, 10, QChar('0')), RateContract{s, e, NAN}});
    m += 3; if (m > 12) { m -= 12; ++y; }
  }
  auto g = std::make_shared<Gather>();
  g->pending = contracts.size();
  auto finish = [this, g, eff, meets, contracts] {
    if (--g->pending > 0) return;
    Bank* eb = mut("ECB");
    if (!std::isfinite(eb->rate)) return;
    computeFromQuotes(*eb, eff, meets, contracts, *g, 6);
    eb->pricingSourceEn = QStringLiteral("computed by Deepwater from CME 3-month €STR futures (ESR, via Yahoo Finance)"); eb->pricingSource = QCoreApplication::translate("economy", "computed by Deepwater from CME 3-month €STR futures (ESR, via Yahoo Finance)");
    eb->pricingNote = eb->priced ? QCoreApplication::translate("economy", "Thin market: few trades per day, odds can lag.") : QCoreApplication::translate("economy", "€STR futures did not answer.");
    eb->pricingNote = detail::withSharedContractCaveat(eb->pricingNote, *eb, eff, contracts);
    changed();
  };
  for (const auto& [sym, c] : contracts)
    market::candles(sym, "1d", "6mo", this, [g, sym, finish](const market::Series& s) { g->daily[sym] = s; finish(); }, 240);
}

void CentralBanks::loadBocPricing() {
  Bank* b = mut("BOC");
  if (!std::isfinite(b->overnight) || !std::isfinite(b->rate)) return;   // called again when Valet answers
  net::Request r; r.ttlSec = 3600;
  r.url = QUrl("https://www.m-x.ca/en/trading/data/quotes?symbol=CRA*");
  net::get(r, this, [this](const net::Reply& rep) {
    if (!rep.ok()) return;
    Bank* bb = mut("BOC");
    const QString s = QString::fromUtf8(rep.body);
    QRegularExpression rx("data-row='([^']*)'");
    QList<QPair<QString, RateContract>> contracts;
    Gather g;
    const qint64 fetched = rep.fetched.toSecsSinceEpoch();
    for (auto it = rx.globalMatch(s); it.hasNext();) {
      QString js = it.next().captured(1);
      js.replace("&quot;", "\"").replace("&amp;", "&");
      const auto o = QJsonDocument::fromJson(js.toUtf8()).object();
      const QString sym = o.value("symbol").toString();
      const QDate exp = QDate::fromString(o.value("expiry_date").toString(), "yyyy-MM-dd");
      double px = o.value("settlement_price").toDouble();
      if (px <= 0) px = o.value("last_price").toDouble();
      if (sym.size() < 6 || !exp.isValid() || px <= 0 || exp <= QDate::currentDate()) continue;
      const QString mc = sym.mid(3, 1);
      const int mon = QString("FGHJKMNQUVXZ").indexOf(mc) + 1;
      const int yr = 2000 + sym.mid(4, 2).toInt();
      if (mon <= 0) continue;
      contracts.append({sym, RateContract{imm(yr, mon), exp, NAN}});
      market::Series ser; ser.bars.push_back({fetched, px, px, px, px, 0});
      g.daily[sym] = ser;
    }
    if (contracts.isEmpty()) return;
    // record a snapshot (the terminal's own odds history for the BoC), keep 120 days
    const QString file = paths::data("macro") + "/odds-boc.json";
    bool readOk = true;
    QJsonArray hist = detail::readJson(file, &readOk).array();
    const qint64 lastT = hist.isEmpty() ? 0 : qint64(hist.last().toObject().value("t").toDouble());
    if (readOk && fetched - lastT > 50 * 60) {
      QJsonObject snap; snap.insert("t", double(fetched));
      QJsonObject px; for (auto it = g.daily.cbegin(); it != g.daily.cend(); ++it) px.insert(it.key(), it->bars.last().c);
      snap.insert("px", px); hist.append(snap);
      while (!hist.isEmpty() && fetched - qint64(hist.first().toObject().value("t").toDouble()) > 120 * 86400) hist.removeFirst();
      detail::writeJson(file, QJsonDocument(hist));
    }
    // merge recorded snapshots into the price series
    for (const auto& h : hist) {
      const auto o = h.toObject(); const qint64 t = qint64(o.value("t").toDouble());
      if (t == fetched) continue;
      const auto px = o.value("px").toObject();
      for (auto it = px.begin(); it != px.end(); ++it)
        if (g.daily.contains(it.key())) {
          const double v = it.value().toDouble();
          g.daily[it.key()].bars.prepend({t, v, v, v, v, 0});
        }
    }
    for (auto& ser : g.daily) std::sort(ser.bars.begin(), ser.bars.end(), [](auto& a, auto& b) { return a.t < b.t; });
    const QList<MeetingDate> meets = bb->upcoming(10);
    QVector<QDate> eff; for (const auto& m : meets) eff << m.decision.addDays(1);
    computeFromQuotes(*bb, eff, meets, contracts, g, 1);
    bb->pricingSourceEn = QStringLiteral("computed by Deepwater from Montréal Exchange 3-month CORRA futures (CRA, settlement prices)"); bb->pricingSource = QCoreApplication::translate("economy", "computed by Deepwater from Montréal Exchange 3-month CORRA futures (CRA, settlement prices)");
    bb->pricingNote = bb->priced ? QString() : QCoreApplication::translate("economy", "CORRA futures did not answer.");
    bb->pricingNote = detail::withSharedContractCaveat(bb->pricingNote, *bb, eff, contracts);
    bb->historySource = "snapshots recorded by this app";
    changed();
  });
}

// ------------------------------------------------------------------------------------------------ decisions
void CentralBanks::derive(Bank& b) {
  if (b.id == "FED" || b.id == "BOE") { if (!b.decisions.isEmpty()) return; }
  const auto h = m_rateHist.value(b.id);
  if (h.isEmpty()) return;
  QList<Decision> out;
  const QDate today = QDate::currentDate();
  for (int i = b.meetings.size() - 1; i >= 0 && out.size() < 2; --i) {
    const QDate d = b.meetings[i].decision;
    if (d >= today) continue;
    double before = NAN, after = NAN;
    for (const auto& [dt, v] : h) { if (dt < d) before = v; }
    QDate lastDt;
    for (const auto& [dt, v] : h) { if (dt > d && dt <= d.addDays(8)) { after = v; lastDt = dt; } }
    if (!std::isfinite(before) || !std::isfinite(after)) continue;
    Decision dc; dc.date = d; dc.inferred = true;
    for (bool en : {false, true}) {
      QString t = moveText(after - before, after, b.id == "ECB", en);
      if (b.id == "ECB" && std::abs(after - before) > 1e-9)
        t = (after > before ? tx(QT_TRANSLATE_NOOP("economy", "Raised the three key rates by %1bp — deposit facility %2"), en)
                            : tx(QT_TRANSLATE_NOOP("economy", "Cut the three key rates by %1bp — deposit facility %2"), en))
                .arg(qRound(std::abs(after - before) * 100)).arg(pct2(after));
      (en ? dc.textEn : dc.text) = t;
    }
    dc.url = b.officialUrl;
    out << dc;
  }
  if (!out.isEmpty()) b.decisions = out;
}

void CentralBanks::loadDecisions() {
  // Fed: monetary press releases RSS → last FOMC statements
  {
    net::Request r; r.ttlSec = 3 * 3600;
    r.url = QUrl("https://www.federalreserve.gov/feeds/press_monetary.xml");
    net::get(r, this, [this](const net::Reply& rep) {
      if (!rep.ok()) return;
      const QString x = QString::fromUtf8(rep.body);
      QRegularExpression ri("<item>(.*?)</item>", QRegularExpression::DotMatchesEverythingOption);
      QStringList links;
      for (auto it = ri.globalMatch(x); it.hasNext() && links.size() < 2;) {
        const QString item = it.next().captured(1);
        if (!item.contains("FOMC statement")) continue;
        auto m = QRegularExpression("<link>(?:<!\\[CDATA\\[)?([^<\\]]+)").match(item);
        if (m.hasMatch()) links << m.captured(1).trimmed();
      }
      auto results = std::make_shared<QMap<QDate, Decision>>();
      auto left = std::make_shared<int>(links.size());
      for (const auto& link : links) {
        net::Request s; s.ttlSec = 7 * 24 * 3600; s.url = QUrl(link);
        net::get(s, this, [this, link, results, left](const net::Reply& sr) {
          if (sr.ok()) {
            const QString t = plainText(sr.body);
            Decision d; d.url = link;
            auto md = QRegularExpression("(January|February|March|April|May|June|July|August|September|October|November|December) (\\d{1,2}), (\\d{4}) Federal Reserve issues FOMC statement").match(t);
            if (md.hasMatch()) d.date = kEn.toDate(md.captured(1) + " " + md.captured(2) + " " + md.captured(3), "MMMM d yyyy");
            auto mv = QRegularExpression("decided to (maintain|raise|lower) the target range for the federal funds rate(?: by ([\\d/]+) percentage point)? (?:at|to) ([\\d\\-/]+) to ([\\d\\-/]+) percent").match(t);
            if (mv.hasMatch()) {
              const QString range = QString("%1–%2%").arg(parseFrac(mv.captured(3)), 0, 'f', 2).arg(parseFrac(mv.captured(4)), 0, 'f', 2);
              for (bool en : {false, true}) {
                const QString t = mv.captured(1) == "maintain" ? tx(QT_TRANSLATE_NOOP("economy", "Held the target range at %1"), en).arg(range)
                    : (mv.captured(1) == "raise" ? tx(QT_TRANSLATE_NOOP("economy", "Raised the target range by %1bp to %2"), en)
                                                 : tx(QT_TRANSLATE_NOOP("economy", "Cut the target range by %1bp to %2"), en))
                          .arg(qRound(parseFrac(mv.captured(2)) * 100)).arg(range);
                (en ? d.textEn : d.text) = t;
              }
            }
            auto vc = QRegularExpression("by an? (\\d+)\\s*[–-]\\s*(\\d+) vote").match(t);
            if (vc.hasMatch()) { d.vote = vc.captured(1) + "–" + vc.captured(2); d.unanimous = vc.captured(2) == "0"; }
            auto va = QRegularExpression("Voting against (?:the|this) (?:monetary policy )?action (?:were|was) (.+?)(?: For media inquiries| Implementation Note|$)").match(t);
            if (va.hasMatch()) {
              // every group of names with the move it preferred (size and "no change" included)
              QStringList names;
              for (const auto& x : fomcDissenters(va.captured(1))) {
                const QString dir = x.bp == INT_MIN ? QString()
                    : x.bp > 0 ? QCoreApplication::translate("economy", "+%1bp hike").arg(x.bp)
                    : x.bp < 0 ? QCoreApplication::translate("economy", "-%1bp cut").arg(-x.bp) : QCoreApplication::translate("economy", "hold");
                names << x.name + (dir.isEmpty() ? "" : " (" + dir + ")");
              }
              d.dissent = names.join(", ");
              if (d.vote.isEmpty() && !names.isEmpty()) d.vote = QCoreApplication::translate("economy", "%1 against").arg(names.size());
            } else if (d.vote.isEmpty() && t.contains("Voting for the monetary policy action were")) {
              d.unanimous = true;
            }
            if (d.vote.isEmpty() && d.unanimous) d.vote = QCoreApplication::translate("economy", "Unanimous");
            if (d.date.isValid() && !d.text.isEmpty()) (*results)[d.date] = d;
          }
          if (--*left == 0 && !results->isEmpty()) {
            Bank* b = mut("FED");
            b->decisions.clear();
            for (auto it = results->cend(); it != results->cbegin();) { --it; b->decisions << *it; }
            changed();
          }
        });
      }
    });
  }
  // BoE: news RSS → Monetary Policy Summary pages
  {
    net::Request r; r.ttlSec = 3 * 3600;
    r.url = QUrl("https://www.bankofengland.co.uk/rss/news");
    net::get(r, this, [this](const net::Reply& rep) {
      if (!rep.ok()) return;
      const QString x = QString::fromUtf8(rep.body);
      QRegularExpression ri("<item>(.*?)</item>", QRegularExpression::DotMatchesEverythingOption);
      QStringList links;
      for (auto it = ri.globalMatch(x); it.hasNext() && links.size() < 3;) {
        const QString item = it.next().captured(1);
        if (!item.contains("Monetary Policy Summary")) continue;
        auto m = QRegularExpression("<link>([^<]+)</link>").match(item);
        if (m.hasMatch() && m.captured(1).contains("monetary-policy-summary-and-minutes") && !links.contains(m.captured(1).trimmed()))
          links << m.captured(1).trimmed();
      }
      auto results = std::make_shared<QMap<QDate, Decision>>();
      auto left = std::make_shared<int>(links.size());
      for (const auto& link : links) {
        net::Request s; s.ttlSec = 7 * 24 * 3600; s.url = QUrl(link);
        net::get(s, this, [this, link, results, left](const net::Reply& sr) {
          if (sr.ok()) {
            const QString t = plainText(sr.body);
            Decision d; d.url = link;
            auto mp = QRegularExpression("Published on (\\d{1,2} \\w+ \\d{4})").match(t);
            if (mp.hasMatch()) d.date = kEn.toDate(mp.captured(1), "dd MMMM yyyy");
            auto mv = QRegularExpression("voted (unanimously|by a majority of (\\d+)\\s*[–-]\\s*(\\d+)) to (increase|reduce|maintain) Bank Rate(?: by ([\\d.]+) percentage points?)?,? (?:to|at) ([\\d.]+)%").match(t);
            if (mv.hasMatch()) {
              const double lvl = mv.captured(6).toDouble();
              const QString act = mv.captured(4);
              for (bool en : {false, true}) {
                const QString t = act == "maintain" ? tx(QT_TRANSLATE_NOOP("economy", "Held Bank Rate at %1"), en).arg(pct2(lvl))
                    : (act == "increase" ? tx(QT_TRANSLATE_NOOP("economy", "Raised Bank Rate by %1bp to %2"), en)
                                         : tx(QT_TRANSLATE_NOOP("economy", "Cut Bank Rate by %1bp to %2"), en))
                          .arg(qRound(mv.captured(5).toDouble() * 100)).arg(pct2(lvl));
                (en ? d.textEn : d.text) = t;
              }
              if (mv.captured(1) == "unanimously") { d.vote = "9–0"; d.unanimous = true; }
              else d.vote = mv.captured(2) + "–" + mv.captured(3);
            }
            QStringList dis;
            QRegularExpression rd("(?:([A-Z][\\w.' ]+?)|\\w+ members \\(([^)]+)\\)) voted against (?:the|this) proposition, preferring to (increase|reduce|maintain) Bank Rate(?: by ([\\d.]+) percentage points?)?");
            for (auto it = rd.globalMatch(t); it.hasNext();) {
              const auto m = it.next();
              QString who = m.captured(2).isEmpty() ? m.captured(1) : m.captured(2);
              who = who.section(". ", -1).trimmed();
              const int bp = qRound(m.captured(4).toDouble() * 100);
              const QString dir = m.captured(3) == "increase" ? QCoreApplication::translate("economy", "+%1bp hike").arg(bp)
                                : m.captured(3) == "reduce" ? QCoreApplication::translate("economy", "-%1bp cut").arg(bp)
                                                            : QCoreApplication::translate("economy", "hold");
              for (auto n : who.split(QRegularExpression(",\\s*|\\s+and\\s+"), Qt::SkipEmptyParts)) dis << n.trimmed() + " (" + dir + ")";
            }
            d.dissent = dis.join(", ");
            if (d.date.isValid() && !d.text.isEmpty()) (*results)[d.date] = d;
          }
          if (--*left == 0 && !results->isEmpty()) {
            Bank* b = mut("BOE");
            b->decisions.clear();
            for (auto it = results->cend(); it != results->cbegin();) { --it; b->decisions << *it; }
            changed();
          }
        });
      }
    });
  }
}

void CentralBanks::probePolymarket() {
  net::Request r; r.ttlSec = 3600; r.staleOnError = false; r.timeoutMs = 15000;
  r.url = QUrl("https://gamma-api.polymarket.com/events?limit=1&tag_slug=fed");
  net::get(r, this, [this](const net::Reply& rep) {
    m_polyOk = rep.ok();
    m_polyStatus = rep.ok() ? QCoreApplication::translate("economy", "Polymarket answers, but its markets are not wired in this build.")
                            : QCoreApplication::translate("economy", "Polymarket is unreachable from this network (%1). The connection is not forced; "
                                      "odds above are computed from futures.").arg(rep.error.isEmpty() ? QCoreApplication::translate("economy", "no answer") : rep.error);
    changed();
  });
}
}
