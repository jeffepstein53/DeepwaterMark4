// Odds for the BoE, BoJ, SNB, RBA and RBNZ (the Fed, ECB and BoC are in CentralBanks.cpp).
#include "macro/CentralBanks.h"
#include "macro/CentralBanksImpl.h"
#include "macro/Csv.h"
#include "macro/Ocr.h"
#include "macro/RateTables.h"
#include "macro/Xlsx.h"
#include "net/Http.h"
#include "net/Sources.h"
#include "core/I18n.h"
#include "core/Paths.h"
#include "wire/Wire.h"
#include <QFileInfo>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QFile>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimeZone>
#include <QTimer>
#include <QCoreApplication>
#include <memory>

namespace macro {
namespace {
const sources::Registrar regJpx({"jpx", "Japan Exchange Group (JPX / OSE) daily settlement prices", {"www.jpx.co.jp"},
    sources::Access::Keyless, 5000, "3-month TONA futures settlement prices (daily CSV of all OSE derivatives)",
    "Public file on jpx.co.jp (robots.txt allows all); JPX terms: information for personal use, source credited; "
    "one index page + one CSV per day, cached", {"economy/rates", "global/overview"}});
const sources::Registrar regBojApi({"boj-stat", "Bank of Japan Time-Series Data Search (API)", {"www.stat-search.boj.or.jp"},
    sources::Access::Keyless, 3000, "Uncollateralized overnight call rate (TONA), daily",
    "BoJ statistics: free use with the source credited", {"economy/rates", "global/overview"}});
const sources::Registrar regEurex({"eurex", "Eurex (Deutsche Börse) public product statistics", {"www.eurex.com"},
    sources::Access::Keyless, 30000, "3M SARON futures (FSR3) daily settlement prices",
    "Delayed public data from Eurex's own product page API, personal display; robots.txt crawl-delay 30 s respected "
    "(requests 30 s apart), past days cached 30 days", {"economy/rates", "global/overview"}});

const sources::Registrar regTe({"tradingeconomics", "Trading Economics (implied rates page)", {"tradingeconomics.com"},
    sources::Access::Keyless, 60000, "What the market prices for the RBA's next meeting, and for the other banks' as a second reading",
    "Public page, robots.txt restricts nothing; personal display, the source credited; off until it is switched on in the Sources tab; "
    "one page at most every 2 hours, not asked again the same day after a refusal", {"economy/rates", "global/overview"}, {}, 1, true});

QDate parseYmd(const QString& s) { return QDate::fromString(s, "yyyyMMdd"); }

template <class T>
void runAsync(QObject* ctx, std::function<T()> work, std::function<void(T)> done) {
  auto* w = new QFutureWatcher<T>(ctx);
  QObject::connect(w, &QFutureWatcher<T>::finished, ctx, [w, done] { done(w->result()); w->deleteLater(); });
  w->setFuture(QtConcurrent::run(std::move(work)));
}

// rates after each meeting → path + odds (shared by the curve-based and table-based sources)
void applyRates(Bank& b, const QList<MeetingDate>& meets, double r0, const QVector<double>& r) {
  const QVector<Odds> odds = oddsFromPath(r0, r);
  b.path.clear();
  double prev = r0;
  for (int i = 0; i < odds.size() && i < meets.size() && i < 10; ++i) {
    PathPoint p; p.meeting = meets[i]; p.rate = b.rate + (r[i] - r0); p.bp = (r[i] - prev) * 100; p.odds = odds[i];
    prev = r[i]; b.path << p;
  }
  b.priced = !b.path.isEmpty();
  b.impliedBp = b.priced ? b.path.first().bp : NAN;
  b.pricedAt = QDateTime::currentDateTime();
}
}

namespace detail {
QJsonDocument readJson(const QString& file, bool* ok) {
  *ok = true;
  QFile f(file);
  if (!f.exists()) return {};
  if (!f.open(QIODevice::ReadOnly)) { *ok = false; return {}; }
  const QByteArray b = f.readAll();
  if (b.trimmed().isEmpty()) return {};
  QJsonParseError err;
  const QJsonDocument d = QJsonDocument::fromJson(b, &err);
  if (err.error != QJsonParseError::NoError) {   // set aside, never overwritten: the history starts again beside it
    f.close();
    const QString aside = file + ".unreadable-" + QDateTime::currentDateTimeUtc().toString("yyyyMMddHHmmss");
    *ok = QFile::rename(file, aside);
    qWarning("[macro] %s does not parse, moved to %s", qPrintable(file), qPrintable(aside));
    return {};
  }
  return d;
}
bool writeJson(const QString& file, const QJsonDocument& doc) {
  QSaveFile f(file);
  if (!f.open(QIODevice::WriteOnly)) return false;
  f.write(doc.toJson(QJsonDocument::Compact));
  return f.commit();
}

void recordSnapshots(const QString& name, qint64 fetched, Gather& g) {
  const QString file = paths::data("macro") + "/" + name;
  bool readOk = true;
  QJsonArray hist = readJson(file, &readOk).array();
  const qint64 lastT = hist.isEmpty() ? 0 : qint64(hist.last().toObject().value("t").toDouble());
  if (readOk && std::llabs(fetched - lastT) > 50 * 60 && fetched > lastT) {
    QJsonObject snap; snap.insert("t", double(fetched));
    QJsonObject px; for (auto it = g.daily.cbegin(); it != g.daily.cend(); ++it) if (!it->bars.isEmpty()) px.insert(it.key(), it->bars.last().c);
    snap.insert("px", px); hist.append(snap);
    while (!hist.isEmpty() && fetched - qint64(hist.first().toObject().value("t").toDouble()) > 120 * 86400) hist.removeFirst();
    writeJson(file, QJsonDocument(hist));
  }
  for (const auto& h : hist) {
    const auto o = h.toObject(); const qint64 t = qint64(o.value("t").toDouble());
    if (t == fetched) continue;
    const auto px = o.value("px").toObject();
    for (auto it = px.begin(); it != px.end(); ++it)
      if (g.daily.contains(it.key())) { const double v = it.value().toDouble(); g.daily[it.key()].bars.append({t, v, v, v, v, 0}); }
  }
  for (auto& ser : g.daily) std::sort(ser.bars.begin(), ser.bars.end(), [](auto& a, auto& b) { return a.t < b.t; });
}
}

// ------------------------------------------------------------------------------------------------ BoE
void CentralBanks::loadBoePricing() {
  Bank* b = mut("BOE");
  // today's level is SONIA (IADB, loadRates): wait for it a minute, then go on with the curve's own level (and say so)
  if ((!std::isfinite(b->rate) || m_sonia.isEmpty()) && m_boeWaits < 12) {
    ++m_boeWaits;
    QTimer::singleShot(5000, this, [this] { loadBoePricing(); });
    return;
  }
  m_boeWaits = 0;
  if (!std::isfinite(b->rate)) return;   // no Bank Rate from IADB or BIS: the next refresh (ensureLoaded) tries again
  net::Request r; r.ttlSec = 2 * 3600; r.timeoutMs = 60000;
  r.url = QUrl("https://www.bankofengland.co.uk/-/media/boe/files/statistics/yield-curves/latest-yield-curve-data.zip");
  net::get(r, this, [this](const net::Reply& rep) {
    Bank* bb = mut("BOE");
    if (!rep.ok()) { bb->pricingNote = QCoreApplication::translate("economy", "Bank of England yield-curve file did not answer (%1).").arg(rep.error); changed(); return; }
    using Row = detail::OisCurve;
    runAsync<QVector<Row>>(this, [body = rep.body]() {
      QVector<Row> out;
      const auto files = xlsx::unzip(body);
      QByteArray file;
      for (auto it = files.cbegin(); it != files.cend(); ++it) if (it.key().startsWith("OIS daily data")) file = *it;
      bool ok = false;
      const auto rows = xlsx::sheet(file, "1. fwds, short end", &ok);
      QVector<double> months;
      for (const auto& row : rows) {
        if (row.isEmpty()) continue;
        if (row[0].toString() == "months:") { for (int c = 1; c < row.size(); ++c) months << row[c].toDouble(); continue; }
        if (row[0].typeId() != QMetaType::Double || months.isEmpty()) continue;
        Row rr; rr.date = QDate(1899, 12, 30).addDays(qint64(row[0].toDouble()));
        for (int c = 1; c < row.size() && c - 1 < months.size(); ++c)
          if (row[c].typeId() == QMetaType::Double) { rr.months << months[c - 1]; rr.fwd << row[c].toDouble(); }
        if (rr.fwd.size() >= 6) out << rr;
      }
      return out;
    }, [this](QVector<Row> rows) {
      Bank* bb = mut("BOE");
      if (rows.isEmpty()) { bb->pricingNote = QCoreApplication::translate("economy", "Bank of England OIS forward curve: sheet not found in the file."); changed(); return; }
      const QList<MeetingDate> meets = bb->upcoming(10);
      if (meets.isEmpty()) return;
      QVector<QDate> dec; for (const auto& m : meets) dec << m.decision;
      // SONIA of a curve's day (the latest fixing on or before it, at most a week old)
      auto soniaOn = [this](const QDate& d) {
        double v = NAN;
        for (const auto& [dt, x] : m_sonia) if (dt <= d && dt.daysTo(d) <= 7) v = x;
        return v;
      };
      // expected Bank-Rate level after each meeting; today's level = SONIA of the curve's day, except for a curve older
      // than a meeting that has passed since (its SONIA predates that move: the curve's level after it is used)
      auto solve = [&](const Row& row, double* r0) {
        bool pastMeeting = false;
        for (const auto& m : bb->meetings) if (m.decision > row.date && m.decision < dec.first()) pastMeeting = true;
        return detail::boeMeetingRates(row, dec, pastMeeting ? NAN : soniaOn(row.date), r0);
      };
      double r0 = NAN;
      const QVector<double> r = solve(rows.last(), &r0);
      if (!std::isfinite(r0)) return;
      const bool noSonia = !std::isfinite(soniaOn(rows.last().date));
      m_boeNoSonia = noSonia;
      applyRates(*bb, meets, r0, r);
      bb->pricedDay = rows.last().date;
      bb->impliedBpWeekAgo = NAN;
      for (int i = rows.size() - 1; i >= 0; --i)
        if (rows[i].date <= rows.last().date.addDays(-7)) {
          double w0 = NAN; const QVector<double> rw = solve(rows[i], &w0);
          if (!rw.isEmpty() && std::isfinite(rw[0])) bb->impliedBpWeekAgo = (rw[0] - w0) * 100;
          break;
        }
      bb->history.clear();
      for (const auto& row : rows) {
        double h0 = NAN; const QVector<double> rh = solve(row, &h0);
        OddsSample s; s.t = QDateTime(row.date, QTime(16, 30), QTimeZone::UTC);
        s.meetings = oddsFromPath(h0, rh).mid(0, bb->path.size());
        if (!s.meetings.isEmpty()) bb->history << s;
      }
      bb->historySource = "daily curves of the current month";
      // Facts: the source without its date (the date is the observation's, the source stays the same)
      bb->pricingSourceEn = QStringLiteral("computed by Deepwater from the Bank of England's daily OIS (SONIA) instantaneous forward curve");
      bb->pricingSource = QCoreApplication::translate("economy", "computed by Deepwater from the Bank of England's daily OIS (SONIA) instantaneous forward curve "
                          "(Bank calculations from Bloomberg data; BoE terms: personal display), curve of %1").arg(rows.last().date.toString("yyyy-MM-dd"));
      bb->pricingNote = noSonia ? QCoreApplication::translate("economy", "SONIA (Bank of England IADB) did not answer: today's level is read from the curve, "
                                  "so a move priced for a meeting less than a month away can show as Hold.")
                                : QString();
      changed();
    });
  });
}

// ------------------------------------------------------------------------------------------------ BoJ
void CentralBanks::loadBojPricing() {
  // 1) today's TONA (BoJ), 2) JPX settlement file
  net::Request t; t.ttlSec = 6 * 3600;
  t.url = QUrl("https://www.stat-search.boj.or.jp/api/v1/getDataCode?format=json&lang=en&db=FM01&code=STRDCLUCON&startDate=" +
               QDate::currentDate().addMonths(-1).toString("yyyyMM"));
  net::get(t, this, [this](const net::Reply& rep) {
    const auto rs = QJsonDocument::fromJson(rep.body).object().value("RESULTSET").toArray();
    if (rep.ok() && !rs.isEmpty()) {
      const auto vals = rs[0].toObject().value("VALUES").toObject().value("VALUES").toArray();
      for (int i = vals.size() - 1; i >= 0; --i) if (!vals[i].isNull()) { mut("BOJ")->overnight = vals[i].toDouble(); break; }
    }
    Bank* b = mut("BOJ");
    if (!std::isfinite(b->overnight)) { b->pricingNote = QCoreApplication::translate("economy", "Bank of Japan TONA series did not answer."); changed(); return; }
    net::Request ix; ix.ttlSec = 3600;
    ix.url = QUrl("https://www.jpx.co.jp/english/markets/derivatives/settlement-price/index.html");
    net::get(ix, this, [this](const net::Reply& ir) {
      const auto m = QRegularExpression("href=\"([^\"]*rb_e(\\d{8})\\.csv)\"").match(QString::fromUtf8(ir.body));
      if (!ir.ok() || !m.hasMatch()) { mut("BOJ")->pricingNote = QCoreApplication::translate("economy", "JPX settlement-price page did not answer."); changed(); return; }
      const QDate fileDate = parseYmd(m.captured(2));
      net::Request c; c.ttlSec = 14 * 24 * 3600; c.timeoutMs = 90000;
      c.url = QUrl("https://www.jpx.co.jp" + m.captured(1));
      net::get(c, this, [this, fileDate](const net::Reply& cr) {
        Bank* bb = mut("BOJ");
        if (!cr.ok()) { bb->pricingNote = QCoreApplication::translate("economy", "JPX settlement file did not answer (%1).").arg(cr.error); changed(); return; }
        if (!std::isfinite(bb->rate)) { QTimer::singleShot(5000, this, [this] { m_started.remove("bojpricing"); ensureLoaded(); }); return; }
        QList<QPair<QString, RateContract>> contracts;
        detail::Gather g;
        const qint64 t = QDateTime(fileDate, QTime(6, 15), QTimeZone::UTC).toSecsSinceEpoch();
        for (const QByteArray& line : cr.body.split('\n')) {
          if (!line.contains("FUT_TOA3M_")) continue;
          const QList<QByteArray> f = line.split(',');
          if (f.size() < 6) continue;
          const QString name = QString::fromLatin1(f[1]);
          const QDate last = QDate::fromString("20" + name.section('_', 2, 2), "yyyyMMdd");
          const QString cm = QString::fromLatin1(f[3]);
          bool ok = false; const double px = f[5].toDouble(&ok);
          if (!ok || !last.isValid() || cm.size() != 6) continue;
          const QDate start = detail::immDate(cm.left(4).toInt(), cm.mid(4).toInt());
          contracts.append({name, RateContract{start, last.addDays(1), NAN}});
          market::Series s; s.bars.push_back({t, px, px, px, px, 0}); g.daily[name] = s;
        }
        if (contracts.isEmpty()) { bb->pricingNote = QCoreApplication::translate("economy", "No 3-month TONA futures in today's JPX file."); changed(); return; }
        detail::recordSnapshots("odds-boj.json", t, g);
        const QList<MeetingDate> meets = bb->upcoming(10);
        QVector<QDate> eff; for (const auto& md : meets) eff << md.decision.addDays(1);
        QDateTime splitAt; bool shared = false;
        detail::computeFromQuotes(*bb, eff, meets, contracts, g, 1, detail::pictureHint(*bb, meets, &splitAt));
        bb->pricedDay = fileDate;
        bb->pricingSourceEn = QStringLiteral("computed by Deepwater from JPX/OSE 3-month TONA futures settlement prices and the BoJ's TONA");
        bb->pricingSource = QCoreApplication::translate("economy", "computed by Deepwater from JPX/OSE 3-month TONA futures settlement prices of %1 (jpx.co.jp) and the BoJ's TONA")
                              .arg(fileDate.toString("yyyy-MM-dd"));
        bb->pricingNote = bb->priced ? QString() : QCoreApplication::translate("economy", "3-month TONA futures did not cover the next meeting.");
        bb->pricingNote = detail::withSharedContractCaveat(bb->pricingNote, *bb, eff, contracts, splitAt, &shared);
        m_split["BOJ"] = shared ? splitAt : QDateTime();
        bb->historySource = "daily settlements recorded by this app";
        changed();
      });
    });
  });
}

// ------------------------------------------------------------------------------------------------ SNB
void CentralBanks::loadSnbPricing() {
  // the policy rate and SARON (loadRates) may come after settlements served from the cache: waited for a minute
  if (const Bank* s = mut("SNB"); !std::isfinite(s->rate) || !std::isfinite(s->overnight)) {
    if (m_snbWaits < 12) { ++m_snbWaits; QTimer::singleShot(5000, this, [this] { loadSnbPricing(); }); return; }
    m_snbWaits = 0;
    m_started.remove("snbpricing");   // the next refresh tries again
    return;
  }
  m_snbWaits = 0;
  struct Day { QDate date; QHash<QString, double> px; };   // contract start yyyyMMdd → settlement
  auto days = std::make_shared<QMap<QDate, QHash<QString, double>>>();
  auto compute = [this, days] {   // (the settlement days of the last month)
    Bank* b = mut("SNB");
    if (days->isEmpty() || !std::isfinite(b->overnight) || !std::isfinite(b->rate)) return;
    QList<QPair<QString, RateContract>> contracts;
    detail::Gather g;
    QSet<QString> keys;
    for (const auto& d : *days) for (auto it = d.cbegin(); it != d.cend(); ++it) keys.insert(it.key());
    QList<QString> sorted(keys.begin(), keys.end()); std::sort(sorted.begin(), sorted.end());
    for (int i = 0; i < sorted.size(); ++i) {
      const QDate s = parseYmd(sorted[i]);
      const QDate e = i + 1 < sorted.size() ? parseYmd(sorted[i + 1]) : detail::immDate(s.addMonths(3).year(), s.addMonths(3).month());
      contracts.append({sorted[i], RateContract{s, e, NAN}});
    }
    for (auto it = days->cbegin(); it != days->cend(); ++it) {
      const qint64 t = QDateTime(it.key(), QTime(16, 15), QTimeZone::UTC).toSecsSinceEpoch();
      for (auto jt = it->cbegin(); jt != it->cend(); ++jt) g.daily[jt.key()].bars.push_back({t, jt.value(), jt.value(), jt.value(), jt.value(), 0});
    }
    const QList<MeetingDate> meets = b->upcoming(10);
    QVector<QDate> eff; for (const auto& md : meets) eff << md.decision.addDays(1);
    QDateTime splitAt; bool shared = false;
    detail::computeFromQuotes(*b, eff, meets, contracts, g, 1, detail::pictureHint(*b, meets, &splitAt));
    b->pricedDay = days->lastKey();
    b->pricingSourceEn = QStringLiteral("computed by Deepwater from Eurex 3M SARON futures (FSR3) daily settlement prices and the SNB's SARON");
    b->pricingSource = QCoreApplication::translate("economy", "computed by Deepwater from Eurex 3M SARON futures (FSR3) daily settlement prices of %1 (eurex.com, delayed public data) and the SNB's SARON")
                         .arg(days->lastKey().toString("yyyy-MM-dd"));
    b->pricingNote = b->priced ? QCoreApplication::translate("economy", "Settlements come from bid/ask quotes on most days (few trades).")
                               : QCoreApplication::translate("economy", "3M SARON futures did not cover the next meeting.");
    b->pricingNote = detail::withSharedContractCaveat(b->pricingNote, *b, eff, contracts, splitAt, &shared);
    m_split["SNB"] = shared ? splitAt : QDateTime();
    b->historySource = "daily settlements";
    changed();
  };
  auto parse = [](const QByteArray& body, QDate* date, QStringList* tradingDates) {
    QHash<QString, double> px;
    const auto o = QJsonDocument::fromJson(body).object();
    for (const auto& r : o.value("dataRows").toArray()) {
      const auto ro = r.toObject();
      const double v = ro.value("dSettle").toDouble();
      if (v > 50 && ro.value("contractType").toString() == "M") px.insert(ro.value("date").toString(), v);
    }
    const auto td = o.value("header").toObject().value("tradingDates").toArray();
    for (const auto& d : td) if (tradingDates) *tradingDates << d.toString().left(10);
    if (date && !td.isEmpty()) *date = QDate::fromString(td[0].toString().left(10), "dd-MM-yyyy");
    return px;
  };
  net::Request r; r.ttlSec = 2 * 3600;
  r.url = QUrl("https://www.eurex.com/api/v1/overallstatistics/1410330?filtertype=overview");
  net::get(r, this, [this, days, compute, parse](const net::Reply& rep) {
    if (!rep.ok()) { mut("SNB")->pricingNote = QCoreApplication::translate("economy", "Eurex statistics did not answer (%1).").arg(rep.error); changed(); return; }
    QDate d; QStringList td;
    const auto px = parse(rep.body, &d, &td);
    if (px.isEmpty() || !d.isValid()) { mut("SNB")->pricingNote = QCoreApplication::translate("economy", "Eurex returned no 3M SARON settlements."); changed(); return; }
    (*days)[d] = px;
    compute();
    // back-fill the previous trading days (one request every 30 s, past days cached 30 days)
    for (int k = 1; k < std::min<qsizetype>(td.size(), 23); ++k) {
      const QDate pd = QDate::fromString(td[k], "dd-MM-yyyy");
      if (!pd.isValid()) continue;
      net::Request h; h.ttlSec = 30 * 24 * 3600;
      h.url = QUrl("https://www.eurex.com/api/v1/overallstatistics/1410330?busdate=" + pd.toString("yyyyMMdd") + "&filtertype=overview");
      net::get(h, this, [days, compute, parse, pd](const net::Reply& hr) {
        if (!hr.ok()) return;
        const auto p2 = parse(hr.body, nullptr, nullptr);
        if (!p2.isEmpty()) { (*days)[pd] = p2; compute(); }
      });
    }
  });
}

// ------------------------------------------------------------------------------------------------ RBA / RBNZ
// FinancialJuice's daily picture first (whole path); when it is more than 36 market hours old, the newest of it, the
// weekly investingLive article and, for the RBA, Trading Economics' page (a source switched off by default).
namespace {
constexpr qint64 kPictureFreshSec = 36 * 3600;

QString tablesFile() { return paths::data("macro") + "/fj-tables.json"; }
// Its content, read again only when it has changed (all eight banks' tables of the last weeks).
QJsonObject tables(bool* ok) {
  static QJsonObject kept;
  static QDateTime at;
  static qint64 size = -1;
  const QFileInfo f(tablesFile());
  *ok = true;
  if (f.exists() && at.isValid() && f.lastModified() == at && f.size() == size) return kept;
  kept = detail::readJson(tablesFile(), ok).object();
  const QFileInfo g(tablesFile());
  at = *ok && g.exists() ? g.lastModified() : QDateTime();
  size = g.exists() ? g.size() : -1;
  return kept;
}

const QRegularExpression& pictureRx() {
  static const QRegularExpression rx("^(RBA|RBNZ|Fed|FOMC|ECB|BoE|BoJ|BoC|SNB)\\b.*Interest Rate Probabilit", QRegularExpression::CaseInsensitiveOption);
  return rx;
}
QString pictureBank(const wire::Item& it) {
  const QString id = pictureRx().match(it.text).captured(1).toUpper();
  return id == "FOMC" ? QStringLiteral("FED") : id;
}
bool isPicture(const wire::Item& it) { return it.source == "FinancialJuice" && !it.url.isEmpty() && pictureRx().match(it.text).hasMatch(); }
bool isWeekly(const wire::Item& it) {
  static const QRegularExpression rx("^How have interest rate expectations changed", QRegularExpression::CaseInsensitiveOption);
  return it.source == "investingLive" && !it.url.isEmpty() && rx.match(it.text).hasMatch();
}
QDateTime when(const QJsonObject& o) {
  QDateTime t = QDateTime::fromString(o.value("published").toString(), Qt::ISODate);
  if (t.isValid()) t.setTimeZone(QTimeZone::UTC);
  return t;
}
QString shownTime(const QDateTime& t) { return i18n::locale().toString(t.toLocalTime(), "dd MMM HH:mm"); }
// What is known was published since the bank's last decision.
bool usableSince(const QJsonObject& o, const Bank& b) {
  QDate lastDecision;
  for (const auto& m : b.meetings) if (m.decision < QDate::currentDate()) lastDecision = m.decision;
  const QDateTime t = when(o);
  return t.isValid() && t.date() >= lastDecision;
}
}

qint64 CentralBanks::marketSeconds(QDateTime a, const QDateTime& b) {
  qint64 s = 0;
  for (; a < b; ) {
    const QDateTime next = std::min(b, QDateTime(a.date().addDays(1), QTime(0, 0), QTimeZone::UTC));
    if (a.date().dayOfWeek() <= 5) s += a.secsTo(next);
    a = next;
  }
  return s;
}

detail::Hint detail::pictureHint(const Bank& b, const QList<MeetingDate>& meets, QDateTime* at) {
  *at = QDateTime();
  bool ok = true;
  const QJsonObject pic = tables(&ok).value(b.id).toObject();
  const QDateTime now = QDateTime::currentDateTimeUtc();
  if (!usableSince(pic, b) || CentralBanks::marketSeconds(when(pic), now) > 5 * 24 * 3600) return {};
  struct Table { qint64 t; QVector<double> moves; };
  QList<Table> tables;
  QJsonArray all = pic.value("older").toArray();
  all.append(pic);
  for (const auto& v : all) {
    const QJsonObject o = v.toObject();
    QHash<QString, double> byMonth;
    for (const auto& rv : o.value("rows").toArray()) byMonth.insert(rv.toObject().value("month").toString(), rv.toObject().value("move").toDouble());
    Table x{when(o).toSecsSinceEpoch(), {}};
    for (const auto& m : meets) x.moves << byMonth.value(m.decision.toString("yyyy-MM"), NAN) / 100.0;
    if (when(o).isValid()) tables << x;
  }
  if (tables.isEmpty() || !std::isfinite(tables.last().moves.value(0, NAN))) return {};
  std::sort(tables.begin(), tables.end(), [](const Table& a, const Table& c) { return a.t < c.t; });
  *at = when(pic);
  return [tables](qint64 t) {   // the table known at that moment; before the first one, the first
    const Table* best = &tables.first();
    for (const auto& x : tables) if (x.t <= t) best = &x;
    return best->moves;
  };
}

QDateTime CentralBanks::pictureAt(const QString& id) const {
  bool ok = true;
  return when(tables(&ok).value(id).toObject());
}

QList<OtherOdds> CentralBanks::otherOdds(const QString& id) const {
  QList<OtherOdds> out;
  const Bank* b = bank(id);
  if (!b) return out;
  const MeetingDate next = b->nextMeeting();
  if (!next.decision.isValid()) return out;
  const QDateTime now = QDateTime::currentDateTimeUtc();
  bool ok = true;
  const QJsonObject all = tables(&ok);
  const auto add = [&](const QString& source, double bp, const QDateTime& at, const QDate& meeting, bool used) {
    if (std::isfinite(bp) && at.isValid()) out << OtherOdds{source, bp, at, meeting, marketSeconds(at, now) <= kPictureFreshSec, used};
  };
  if (const QJsonObject pic = all.value(b->id).toObject(); usableSince(pic, *b))
    for (const auto& rv : pic.value("rows").toArray()) {
      const QJsonObject r = rv.toObject();
      if (r.value("month").toString() != next.decision.toString("yyyy-MM")) continue;
      const bool own = b->pricingSourceEn.startsWith("FinancialJuice");
      add(QStringLiteral("FinancialJuice"), r.value("move").toDouble(NAN), when(pic), {}, own || m_split.value(b->id).isValid());
      break;
    }
  if (const QJsonObject te = all.value("TE").toObject(); usableSince(te, *b)) {
    const bool own = b->pricingSourceEn.startsWith("Trading Economics");
    const QJsonObject o = te.value("banks").toObject().value(b->id).toObject();
    const QDate meeting = QDate::fromString(o.value("meeting").toString(), Qt::ISODate);
    const bool wired = b->id == "RBA" && te.contains("bp");
    const qsizetype n = out.size();
    add(QStringLiteral("Trading Economics"), wired ? te.value("bp").toDouble(NAN) : o.value("bp").toDouble(NAN), when(te), meeting, own);
    if (!wired && out.size() > n) out.last().level = o.value("rate").toDouble(NAN);
  }
  if (const QJsonObject wk = all.value("weekly").toObject(); usableSince(wk, *b))
    add(QStringLiteral("investingLive"), wk.value("banks").toObject().value(b->id).toObject().value("nextBp").toDouble(NAN), when(wk), {},
        b->pricingSourceEn.startsWith("investingLive"));
  return out;
}

void CentralBanks::applyFjTable(const QString& id) {
  Bank* b = mut(id);
  bool ok = true;
  const QJsonObject all = tables(&ok);
  if (!std::isfinite(b->rate)) return;
  const QDateTime now = QDateTime::currentDateTimeUtc();
  const auto usable = [&](const QJsonObject& o) { return usableSince(o, *b); };

  const QJsonObject pic = all.value(id).toObject();
  struct Single { QDateTime at; double bp = NAN; QString en, shown; };
  Single best;
  const auto offer = [&best](const Single& s) { if (std::isfinite(s.bp) && s.at.isValid() && (!best.at.isValid() || s.at > best.at)) best = s; };
  if (const QJsonObject te = all.value("TE").toObject(); id == "RBA" && usable(te) && te.contains("bp"))
    offer({when(te), te.value("bp").toDouble(), QStringLiteral("Trading Economics, Australia implied rates"),
           QCoreApplication::translate("economy", "source: Trading Economics, page read on %1 (next meeting only)").arg(shownTime(when(te)))});
  if (const QJsonObject wk = all.value("weekly").toObject(); usable(wk))
    if (const QJsonObject o = wk.value("banks").toObject().value(id).toObject(); !o.isEmpty())
      offer({when(wk), o.value("nextBp").toDouble(), QStringLiteral("investingLive weekly article on rate expectations"),
             QCoreApplication::translate("economy", "source: investingLive, weekly article of %1 (next meeting only)").arg(shownTime(when(wk)))});

  QList<MeetingDate> meets; QVector<double> r;
  double eff = NAN;
  QDateTime published;
  if (usable(pic)) {
    published = when(pic);
    eff = pic.value("effective").toDouble();
    for (const auto& rv : pic.value("rows").toArray()) {
      const auto ro = rv.toObject();
      const QDate month = QDate::fromString(ro.value("month").toString() + "-01", "yyyy-MM-dd");
      MeetingDate md;
      for (const auto& known : b->meetings)
        if (known.decision.year() == month.year() && known.decision.month() == month.month()) md = known;
      if (md.decision.isValid() && md.decision < QDate::currentDate()) continue;
      if (!md.decision.isValid()) break;   // a meeting whose day is not known: the path stops before it
      meets << md; r << eff + ro.value("total").toDouble() / 100.0;
    }
  }
  const bool pictureUsed = !meets.isEmpty() && (marketSeconds(published, now) <= kPictureFreshSec || !best.at.isValid() || published >= best.at);
  if (pictureUsed) {
    b->overnight = eff;
    applyRates(*b, meets, eff, r);
    b->pricedAt = published.toLocalTime();
    // one sample per kept picture, and the change priced a week before for the same meeting
    b->history.clear(); b->impliedBpWeekAgo = NAN;
    const QString firstMonth = meets.first().decision.toString("yyyy-MM");
    QJsonArray hist = pic.value("older").toArray();
    hist.append(pic);
    for (const auto& hv : hist) {
      const QJsonObject ho = hv.toObject();
      const QDateTime t = when(ho);
      const double e0 = ho.value("effective").toDouble();
      QVector<double> hr;
      double first = NAN;
      for (const auto& rv : ho.value("rows").toArray()) {
        const auto ro = rv.toObject();
        if (ro.value("month").toString() < firstMonth) continue;
        if (ro.value("month").toString() == firstMonth) first = ro.value("move").toDouble();
        hr << e0 + ro.value("total").toDouble() / 100.0;
      }
      if (!t.isValid() || hr.isEmpty()) continue;
      OddsSample s; s.t = t; s.meetings = oddsFromPath(e0, hr).mid(0, b->path.size());
      b->history << s;
      if (t.date() <= published.date().addDays(-7) && std::isfinite(first)) b->impliedBpWeekAgo = first;
    }
    b->historySource = "one table per published picture";
    b->pricingSourceEn = QStringLiteral("FinancialJuice probability pictures, read by Deepwater (OCR, validated)");
    b->pricingSource = QCoreApplication::translate("economy", "source: FinancialJuice picture of %1, OCR by Deepwater (validated)").arg(shownTime(published));
    b->pricingNote.clear();
    b->pricedDay = QDate();
    return;
  }
  if (best.at.isValid()) {
    const QList<MeetingDate> up = b->upcoming(1);
    if (!up.isEmpty()) {
      b->overnight = b->rate;
      applyRates(*b, up, b->rate, {b->rate + best.bp / 100.0});
      b->pricedAt = best.at.toLocalTime();
      b->history.clear(); b->impliedBpWeekAgo = NAN;
      b->historySource.clear();
      b->pricingSourceEn = best.en;
      b->pricingSource = best.shown;
      b->pricingNote = QCoreApplication::translate("economy", "No FinancialJuice picture less than 36 hours old.");
      b->pricedDay = QDate();
      return;
    }
  }
  if (!pic.isEmpty() && !usable(pic)) {
    b->priced = false; b->path.clear();
    b->pricingNote = QCoreApplication::translate("economy", "The last FinancialJuice table (%1) is older than the last decision.").arg(shownTime(when(pic)));
  }
}

void CentralBanks::loadFjPricing() {
  if (!m_fjWatching) {
    m_fjWatching = true;
    connect(&wire::Wire::instance(), &wire::Wire::itemsAdded, this, [this](const QList<wire::Item>& fresh) {
      for (const auto& it : fresh)
        if (isPicture(it) || isWeekly(it)) { QTimer::singleShot(20000, this, [this] { loadFjPricing(); }); return; }   // its page first
    });
  }
  const QDateTime now = QDateTime::currentDateTimeUtc();
  const auto items = wire::Wire::instance().between(now.addDays(-10), now.addSecs(60));
  const auto have = [](const QString& id) { bool ok = true; return when(tables(&ok).value(id).toObject()).isValid(); };
  for (const QString id : {"RBA", "RBNZ"}) {
    applyFjTable(id);
    QList<wire::Item> mine;
    for (const auto& it : items)
      if (isPicture(it) && pictureBank(it) == id && it.time > now.addDays(-7)) mine << it;
    std::sort(mine.begin(), mine.end(), [](const wire::Item& a, const wire::Item& b) { return a.time > b.time; });
    Bank* b = mut(id);
    if (!b->priced && !have(id)) {
      if (mine.isEmpty())
        b->pricingNote = QCoreApplication::translate("economy", "No FinancialJuice \"%1 Interest Rate Probabilities\" picture on the wire in the last 7 days "
                         "(ASX and the RBA/RBNZ sites forbid programs).").arg(b->shownCode());
      else if (!m_pictures.contains(mine.first().id))
        b->pricingNote = QCoreApplication::translate("economy", "Reading the FinancialJuice picture of %1…").arg(shownTime(mine.first().time));
    }
    readPictures(id, mine);
  }
  changed();
  // the six other banks' pictures, behind everything else and one at a time: their host also carries the wire
  if (!m_pictureTimer.isActive()) {
    for (const QString id : {"FED", "ECB", "BOE", "BOJ", "BOC", "SNB"}) if (!m_pictureQueue.contains(id)) m_pictureQueue << id;
    m_pictureTimer.start(60 * 1000);
  }
  // the weekly article (all eight banks), one reading per article
  const wire::Item* week = nullptr;
  for (const auto& it : items) if (isWeekly(it) && (!week || it.time > week->time)) week = &it;
  if (week) loadWeeklyArticle(week->url, week->time);
  loadTradingEconomics();
}

void CentralBanks::nextPicture() {
  const QDateTime now = QDateTime::currentDateTimeUtc();
  const auto items = wire::Wire::instance().between(now.addDays(-4), now.addSecs(60));
  while (!m_pictureQueue.isEmpty()) {
    const QString id = m_pictureQueue.takeFirst();
    const wire::Item* newest = nullptr;
    for (const auto& it : items) if (isPicture(it) && pictureBank(it) == id && (!newest || it.time > newest->time)) newest = &it;
    if (!newest) continue;
    if (const QDateTime have = pictureAt(id); have.isValid() && newest->time <= have) continue;
    const PictureTry t = m_pictures.value(newest->id);
    if (t.busy || t.done || (t.tries > 0 && QDateTime::currentMSecsSinceEpoch() - t.last < 20 * 60 * 1000)) continue;
    readPictures(id, {*newest});
    m_pictureTimer.start(4 * 60 * 1000);
    return;
  }
}

// The newest picture not yet read; an older one only while it is newer than the table in hand.
void CentralBanks::readPictures(const QString& id, const QList<wire::Item>& items) {
  bool ok = true;
  const QDateTime have = when(tables(&ok).value(id).toObject());
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  for (int k = 0; k < items.size() && k < 4; ++k) {
    const wire::Item& it = items[k];
    if (have.isValid() && it.time <= have) return;
    PictureTry& t = m_pictures[it.id];
    if (t.busy) return;
    if (t.done) continue;
    if (t.tries > 0 && nowMs - t.last < 20 * 60 * 1000) continue;   // asked again in 20 min; meanwhile the one before it
    t.busy = true; t.last = nowMs; ++t.tries;
    readPicture(id, it.id, it.url, it.time, t.tries > 1, [this, id, items] { readPictures(id, items); });
    return;
  }
}

void CentralBanks::readPicture(const QString& id, const QString& item, const QString& url, const QDateTime& published, bool again,
                               std::function<void()> next) {
  const auto failed = [this, id, item, published, next](const QString& why, bool final) {
    PictureTry& t = m_pictures[item];
    t.busy = false;
    if (final || t.tries >= 4) t.done = true;
    qWarning().noquote() << "[macro]" << id << "picture of" << published.toString(Qt::ISODate) << "not read:" << why;
    Bank* b = mut(id);
    if (!b->priced) b->pricingNote = QCoreApplication::translate("economy", "Latest FinancialJuice picture (%1) rejected: %2.").arg(shownTime(published), why);
    changed();
    next();
  };
  const auto keep = [this, id, item, url, published, failed](const ratetables::Table& t) {
    Bank* b = mut(id);
    const bool off = t.ok() && std::isfinite(b->rate) && std::fabs(t.effective - b->rate) > 0.35;
    if (!t.ok() || off) {
      using F = ratetables::Fault;
      const QString why = off || t.fault == F::Inconsistent ? QCoreApplication::translate("economy", "the recognised table did not validate (moves do not add up to the totals or rates are inconsistent)")
                        : t.fault == F::OtherBank           ? QCoreApplication::translate("economy", "the picture is another bank's table")
                                                            : QCoreApplication::translate("economy", "the picture is not a table");
      failed(why, false);
      return;
    }
    PictureTry& tr = m_pictures[item];
    tr.busy = false; tr.done = true;
    bool readOk = true;
    QJsonObject all = tables(&readOk);
    if (!readOk) return;   // the file can't be read: never overwrite it
    const QJsonObject prev = all.value(id).toObject();
    if (when(prev).isValid() && when(prev) >= published) return;   // keep the newest valid table
    QJsonArray rows;
    for (const auto& r : t.rows)
      rows.append(QJsonObject{{"month", r.month.toString("yyyy-MM")}, {"implied", r.implied}, {"move", r.move}, {"total", r.total}});
    QJsonArray older = prev.value("older").toArray();
    if (when(prev).isValid()) {
      QJsonObject p = prev; p.remove("older"); p.remove("url");
      if (!older.isEmpty() && when(older.last().toObject()).date() == when(p).date()) older.removeLast();   // one a day
      older.append(p);
    }
    while (older.size() > 45) older.removeFirst();
    all.insert(id, QJsonObject{{"published", published.toUTC().toString(Qt::ISODate)}, {"url", url}, {"effective", t.effective},
                               {"rows", rows}, {"older", older}});
    detail::writeJson(tablesFile(), QJsonDocument(all));
    if (id == "RBA" || id == "RBNZ") applyFjTable(id);
    else {   // how it shares a 3-month contract between two meetings: the odds are computed again, from the cache
      static const QHash<QString, QString> part{{"ECB", "ecbpricing"}, {"BOC", "bocpricing"}, {"BOJ", "bojpricing"}, {"SNB", "snbpricing"}};
      if (part.contains(id)) { m_started.remove(part.value(id)); ensureLoaded(); }
    }
    changed();
  };
  const auto recognise = [this, id, keep](const QByteArray& picture) {
    runAsync<ratetables::Table>(this, [picture, id]() { return ratetables::readPicture(ocr::recognize(picture), id, NAN); },
                                [keep](ratetables::Table t) { keep(t); });
  };
  const auto viaPage = [this, url, again, failed, recognise] {
    net::Request a; a.ttlSec = again ? 600 : 30 * 24 * 3600; a.url = QUrl(url);
    net::get(a, this, [this, failed, recognise](const net::Reply& ar) {
      if (!ar.ok()) { failed(QCoreApplication::translate("economy", "its page did not answer"), false); return; }
      const QString html = QString::fromUtf8(ar.body);
      auto mi = QRegularExpression("property=\"og:image\"\\s+content=\"([^\"]+)\"").match(html);
      if (!mi.hasMatch()) mi = QRegularExpression("content=\"([^\"]+)\"\\s+property=\"og:image\"").match(html);
      if (!mi.hasMatch()) { failed(QCoreApplication::translate("economy", "its page shows no picture yet"), false); return; }
      net::Request im; im.ttlSec = 30 * 24 * 3600; im.url = QUrl(mi.captured(1));
      net::get(im, this, [failed, recognise](const net::Reply& ir) {
        if (!ir.ok()) { failed(QCoreApplication::translate("economy", "the picture did not load"), false); return; }
        recognise(ir.body);
      });
    });
  };
  // the address of the picture holds the item's number: one request, its page only when that fails (RBA and RBNZ, whose
  // odds wait for it, keep the page, which always names the right picture)
  static const QRegularExpression number("/News/(\\d+)/");
  const auto num = number.match(url);
  if (id == "RBA" || id == "RBNZ" || !num.hasMatch()) { viaPage(); return; }
  net::Request d; d.ttlSec = 30 * 24 * 3600; d.background = true;
  d.url = QUrl(QStringLiteral("https://www.financialjuice.com/images/%1.png").arg(num.captured(1)));
  net::get(d, this, [viaPage, recognise](const net::Reply& r) {
    if (r.ok() && (r.body.startsWith("\x89PNG") || r.body.startsWith("\xff\xd8"))) recognise(r.body);
    else viaPage();
  });
}

void CentralBanks::loadWeeklyArticle(const QString& url, const QDateTime& published) {
  bool ok = true;
  const QDateTime have = when(tables(&ok).value("weekly").toObject());
  if (!ok || (have.isValid() && have >= published) || m_weeklyTried.value(url) >= 3) return;
  ++m_weeklyTried[url];
  net::Request r; r.ttlSec = 30 * 24 * 3600; r.timeoutMs = 30000; r.url = QUrl(url);
  net::get(r, this, [this, published, url](const net::Reply& rep) {
    const auto banks = rep.ok() ? ratetables::readWeekly(rep.body) : QHash<QString, ratetables::Weekly>{};
    if (banks.isEmpty()) { qWarning().noquote() << "[macro] weekly rate expectations not read:" << (rep.ok() ? QStringLiteral("no figures in the article") : rep.error); return; }
    bool readOk = true;
    QJsonObject all = tables(&readOk);
    if (!readOk) return;
    QJsonObject o;
    for (auto it = banks.cbegin(); it != banks.cend(); ++it)
      o.insert(it.key(), QJsonObject{{"nextBp", it->nextBp}, {"chance", it->chance}, {"what", it->what}, {"yearEndBp", it->yearEndBp},
                                     {"nextYearBp", std::isfinite(it->nextYearBp) ? QJsonValue(it->nextYearBp) : QJsonValue()}});
    all.insert("weekly", QJsonObject{{"published", published.toUTC().toString(Qt::ISODate)}, {"url", url}, {"banks", o}});
    detail::writeJson(tablesFile(), QJsonDocument(all));
    for (const QString id : {"RBA", "RBNZ"}) applyFjTable(id);
    changed();
  });
}

void CentralBanks::loadTradingEconomics() {
  if (sources::isDisabled("tradingeconomics") || m_teRefused == QDate::currentDate()) return;   // off by default
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (m_teAsked && now - m_teAsked < 2 * 3600 * 1000) return;
  m_teAsked = now;
  net::Request r; r.ttlSec = 2 * 3600; r.timeoutMs = 30000;
  r.url = QUrl("https://tradingeconomics.com/australia/implied-rates");
  net::get(r, this, [this](const net::Reply& rep) {
    if (!rep.ok()) {
      if (rep.status == 401 || rep.status == 403 || rep.status == 429) m_teRefused = QDate::currentDate();   // not asked again today
      return;
    }
    const auto page = ratetables::readTradingEconomics(rep.body);
    const double rate = mut("RBA")->rate, bp = ratetables::expectedBp(page.next, rate);
    if (page.next.isEmpty() && page.banks.isEmpty()) { qWarning("[macro] Trading Economics: no table on the page"); return; }
    bool readOk = true;
    QJsonObject all = tables(&readOk);
    if (!readOk) return;
    QJsonObject banks;
    for (auto it = page.banks.cbegin(); it != page.banks.cend(); ++it)
      banks.insert(it.key(), QJsonObject{{"current", it->current}, {"rate", it->rate}, {"bp", it->bp}, {"meeting", it->meeting.toString(Qt::ISODate)}});
    QJsonObject te{{"published", rep.fetched.toUTC().toString(Qt::ISODate)}, {"banks", banks}};
    if (std::isfinite(bp)) te.insert("bp", bp);
    all.insert("TE", te);
    detail::writeJson(tablesFile(), QJsonDocument(all));
    applyFjTable("RBA");
    changed();
  });
}
}
