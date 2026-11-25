#include "positioning/Retail.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "net/Http.h"
#include "net/Sources.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimeZone>
#include <QUrl>
#include <QRegularExpression>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace positioning {
static const sources::Registrar kMyfxbook({"myfxbook", "Myfxbook community outlook", {"www.myfxbook.com"},
  sources::Access::Account, 2000,
  "Community outlook: retail long/short %, volume, positions and average entry prices per symbol",
  "Myfxbook API with the user's own account (login → session); at most one outlook request per 30 minutes; "
  "data shown to the account holder only, password never stored",
  {"positioning/retail-sentiment", "market/markets-watch"}});

namespace {
const char* kEmail = "positioning.myfxbookEmail";
const char* kSession = "positioning.myfxbookSession";
const char* kLast = "positioning.myfxbookLastRequest";

QString dir() { return paths::data("positioning/myfxbook"); }

double jnum(const QJsonValue& v) {
  if (v.isDouble()) return v.toDouble();
  if (v.isString()) { bool ok = false; QString s = v.toString(); s.remove(','); const double d = s.toDouble(&ok); return ok ? d : NAN; }
  return NAN;
}
QJsonObject toJson(const RetailSymbol& s) {
  QJsonObject o;
  auto put = [&](const char* k, double v) { if (std::isfinite(v)) o.insert(k, v); };
  o.insert("n", s.name);
  put("lp", s.longPct); put("sp", s.shortPct); put("lv", s.longVolume); put("sv", s.shortVolume);
  put("lpos", s.longPositions); put("spos", s.shortPositions); put("tpos", s.totalPositions);
  put("al", s.avgLong); put("as", s.avgShort);
  return o;
}
RetailSymbol fromJson(const QJsonObject& o) {
  RetailSymbol s; s.name = o.value("n").toString();
  auto g = [&](const char* k) { return o.contains(k) ? o.value(k).toDouble() : NAN; };
  s.longPct = g("lp"); s.shortPct = g("sp"); s.longVolume = g("lv"); s.shortVolume = g("sv");
  s.longPositions = g("lpos"); s.shortPositions = g("spos"); s.totalPositions = g("tpos");
  s.avgLong = g("al"); s.avgShort = g("as");
  return s;
}
}

QString RetailSymbol::display() const {
  const QString n = name.toUpper();
  if (n.size() == 6 && n.at(0).isLetter() && n.at(5).isLetter()) return n.left(3) + "/" + n.mid(3);
  return n;
}
QString RetailSymbol::group() const {
  static const QSet<QString> ccy{"USD", "EUR", "GBP", "JPY", "CHF", "CAD", "AUD", "NZD", "SEK", "NOK", "DKK", "PLN",
                                 "HUF", "CZK", "TRY", "ZAR", "MXN", "SGD", "HKD", "CNH", "RUB", "ILS", "THB"};
  static const QStringList metals{"XAU", "XAG", "XPT", "XPD", "XTI", "XBR", "XNG"};
  static const QStringList crypto{"BTC", "ETH", "LTC", "XRP", "BCH", "DOG", "ADA", "SOL", "DOT", "LNK", "BNB", "EOS", "XLM"};
  const QString n = name.toUpper();
  for (const auto& m : metals) if (n.startsWith(m)) return "Commodities";
  if (n.contains("OIL") || n.contains("WTI") || n.contains("BRENT") || n.contains("NGAS") || n.contains("COPPER")) return "Commodities";
  for (const auto& c : crypto) if (n.startsWith(c)) return "Crypto";
  if (n.size() == 6 && ccy.contains(n.left(3)) && ccy.contains(n.mid(3))) return "FX";
  static const QRegularExpression digit("\\d");
  if (n.contains(digit)) return "Indices";
  return "Other";
}
const RetailSymbol* RetailSnapshot::find(const QString& name) const {
  QString k = name.toUpper(); k.remove('/');
  for (const auto& s : symbols) if (s.name.toUpper() == k) return &s;
  return nullptr;
}

Retail& Retail::instance() { static Retail* r = new Retail; return *r; }

Retail::Retail() {
  m_timer.setInterval(60 * 1000);   // checks once a minute whether the 30-minute window has passed
  connect(&m_timer, &QTimer::timeout, this, &Retail::refresh);
  const QByteArray fx = qgetenv("DW_MYFXBOOK_FIXTURE");
  if (!fx.isEmpty() && settings::isTestProfile()) { loadFixture(QString::fromUtf8(fx)); return; }
  loadRecordings();   // on a worker thread; changed() when they are in
  settings::secretValue(kEmail);   // encrypts what an older version wrote in clear
  m_state = settings::secretValue(kSession).isEmpty() ? State::NotConnected : State::Connected;
}

QString Retail::email() const { return settings::secretValue(kEmail); }

QDateTime Retail::lastRequest() const {
  const QVariant v = settings::value(kLast);
  if (!v.isValid()) return {};
  QDateTime t = v.toDateTime();
  if (!t.isValid()) {
    bool ok = false; const qint64 n = v.toString().toLongLong(&ok);
    if (ok) t = n > 100000000000LL ? QDateTime::fromMSecsSinceEpoch(n, QTimeZone::UTC) : QDateTime::fromSecsSinceEpoch(n, QTimeZone::UTC);
    else t = QDateTime::fromString(v.toString(), Qt::ISODate);
  }
  return t.isValid() ? t.toUTC() : QDateTime();
}
QDateTime Retail::nextAllowed() const {
  const QDateTime l = lastRequest();
  return l.isValid() ? l.addSecs(kMinIntervalSec) : QDateTime::currentDateTimeUtc();
}

void Retail::setState(State s, const QString& msg) { m_state = s; m_message = msg; emit changed(); }

void Retail::setActive(QObject* owner, bool active) {
  if (active) {
    m_active.insert(owner);
    if (!m_watched.contains(owner)) {   // once per owner, not on every show
      m_watched.insert(owner);
      connect(owner, &QObject::destroyed, this, [this, owner] { m_watched.remove(owner); m_active.remove(owner); if (m_active.isEmpty()) m_timer.stop(); });
    }
    if (!m_fixture && !m_timer.isActive()) m_timer.start();
    refresh();
  } else {
    m_active.remove(owner);
    if (m_active.isEmpty()) m_timer.stop();
  }
}

void Retail::login(const QString& email, const QString& password) {
  if (m_fixture) { setState(State::Connected); return; }
  if (email.trimmed().isEmpty() || password.isEmpty()) { setState(State::Error, QCoreApplication::translate("positioning", "Enter your Myfxbook email and password.")); return; }
  settings::setSecretValue(kEmail, email.trimmed());
  setState(State::LoggingIn);
  // Myfxbook's API takes the credentials as GET parameters (HTTPS). Never cached, never logged.
  const QByteArray q = "email=" + QUrl::toPercentEncoding(email.trimmed()) + "&password=" + QUrl::toPercentEncoding(password);
  net::Request r;
  r.url = QUrl::fromEncoded("https://www.myfxbook.com/api/login.json?" + q);
  r.ttlSec = 0; r.staleOnError = false; r.priority = true; r.cacheKey = "myfxbook login";
  net::get(r, this, [this](const net::Reply& rep) {
    if (!rep.ok()) { setState(State::Error, QCoreApplication::translate("positioning", "Myfxbook did not answer (%1).").arg(rep.error)); return; }
    const auto o = QJsonDocument::fromJson(rep.body).object();
    if (o.value("error").toBool(true)) {
      const QString m = o.value("message").toString();
      setState(State::Error, m.isEmpty() ? QCoreApplication::translate("positioning", "Login refused by Myfxbook.") : m);   // Myfxbook's own message is shown as it comes
      return;
    }
    const QString session = o.value("session").toString();
    if (session.isEmpty()) { setState(State::Error, QCoreApplication::translate("positioning", "Myfxbook returned no session.")); return; }
    if (!settings::setSecretValue(kSession, session)) {
      setState(State::Error, QCoreApplication::translate("positioning", "Windows could not encrypt the Myfxbook session: it is not kept."));
      return;
    }
    setState(State::Connected);
    refresh();
  });
}

void Retail::logout() {
  if (m_fixture) { setState(State::NotConnected); return; }
  const QString session = settings::secretValue(kSession);
  settings::remove(kSession);
  setState(State::NotConnected);
  if (session.isEmpty()) return;
  net::Request r;
  r.url = QUrl::fromEncoded("https://www.myfxbook.com/api/logout.json?session=" + session.toUtf8(), QUrl::TolerantMode);
  r.ttlSec = 0; r.staleOnError = false; r.cacheKey = "myfxbook logout";
  net::get(r, nullptr, [](const net::Reply&) {});
}

void Retail::refresh() {
  if (m_fixture || m_busy || m_state != State::Connected) return;
  if (QDateTime::currentDateTimeUtc() < nextAllowed()) return;
  request();
}

void Retail::request() {
  const QString session = settings::secretValue(kSession);
  if (session.isEmpty()) { setState(State::NotConnected); return; }
  m_busy = true;
  settings::setValue(kLast, QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
  net::Request r;
  // the session is passed exactly as Myfxbook returned it (it may already be percent-encoded)
  r.url = QUrl::fromEncoded("https://www.myfxbook.com/api/get-community-outlook.json?session=" + session.toUtf8(), QUrl::TolerantMode);
  r.ttlSec = 0; r.staleOnError = false; r.cacheKey = "myfxbook outlook";
  net::get(r, this, [this](const net::Reply& rep) {
    m_busy = false;
    if (!rep.ok()) { m_message = QCoreApplication::translate("positioning", "Myfxbook did not answer (%1).").arg(rep.error); emit changed(); return; }
    QString err; bool sessionErr = false;
    RetailSnapshot s = parse(rep.body, &err, &sessionErr);
    if (sessionErr) { settings::remove(kSession); setState(State::Expired, err); return; }
    if (!err.isEmpty()) { m_message = err; emit changed(); return; }
    s.time = rep.fetched.isValid() ? rep.fetched : QDateTime::currentDateTimeUtc();
    m_latest = s;
    record(s);
    m_message.clear();
    emit changed();
  });
}

RetailSnapshot Retail::parse(const QByteArray& body, QString* error, bool* sessionError) {
  RetailSnapshot s;
  const auto o = QJsonDocument::fromJson(body).object();
  if (o.isEmpty()) { *error = QCoreApplication::translate("positioning", "Unreadable answer from Myfxbook."); return s; }
  if (o.value("error").toBool(false)) {
    const QString m = o.value("message").toString();   // Myfxbook's own message (shown as it comes)
    *sessionError = m.contains("session", Qt::CaseInsensitive);
    *error = m.isEmpty() ? QCoreApplication::translate("positioning", "Myfxbook refused the request.") : m;
    return s;
  }
  for (const auto& v : o.value("symbols").toArray()) {
    const auto x = v.toObject();
    RetailSymbol r;
    r.name = x.value("name").toString().toUpper();
    if (r.name.isEmpty()) continue;
    r.longPct = jnum(x.value("longPercentage")); r.shortPct = jnum(x.value("shortPercentage"));
    r.longVolume = jnum(x.value("longVolume")); r.shortVolume = jnum(x.value("shortVolume"));
    r.longPositions = jnum(x.value("longPositions")); r.shortPositions = jnum(x.value("shortPositions"));
    r.totalPositions = jnum(x.value("totalPositions"));
    if (!std::isfinite(r.totalPositions) && std::isfinite(r.longPositions) && std::isfinite(r.shortPositions))
      r.totalPositions = r.longPositions + r.shortPositions;
    r.avgLong = jnum(x.value("avgLongPrice")); r.avgShort = jnum(x.value("avgShortPrice"));
    s.symbols << r;
  }
  if (s.symbols.isEmpty()) *error = QCoreApplication::translate("positioning", "Myfxbook returned no symbols.");
  return s;
}

void Retail::record(const RetailSnapshot& s) {
  m_history << s;
  // memory holds the last kKeepDays only (the files keep everything)
  const QDateTime from = QDateTime::currentDateTimeUtc().addDays(-kKeepDays);
  int old = 0; while (old < m_history.size() && m_history[old].time < from) ++old;
  if (old) m_history.remove(0, old);
  QJsonArray arr; for (const auto& x : s.symbols) arr << toJson(x);
  QJsonObject o; o.insert("t", s.time.toUTC().toString(Qt::ISODate)); o.insert("symbols", arr);
  QFile f(dir() + "/outlook-" + s.time.toUTC().toString("yyyy-MM") + ".jsonl");
  if (f.open(QIODevice::Append)) f.write(QJsonDocument(o).toJson(QJsonDocument::Compact) + "\n");
}

// Snapshots of the last kKeepDays from the monthly files (worker thread: no QObject, no settings).
static QVector<RetailSnapshot> readRecordings(const QString& path, const QDateTime& from) {
  QVector<RetailSnapshot> out;
  QDir d(path);
  const QString firstFile = "outlook-" + from.toString("yyyy-MM") + ".jsonl";   // older months: not even opened
  for (const auto& fn : d.entryList({"outlook-*.jsonl"}, QDir::Files, QDir::Name)) {
    if (fn < firstFile) continue;
    QFile f(d.filePath(fn));
    if (!f.open(QIODevice::ReadOnly)) continue;
    while (!f.atEnd()) {
      const auto o = QJsonDocument::fromJson(f.readLine()).object();
      RetailSnapshot s;
      s.time = QDateTime::fromString(o.value("t").toString(), Qt::ISODate);
      if (!s.time.isValid() || s.time < from) continue;
      for (const auto& v : o.value("symbols").toArray()) s.symbols << fromJson(v.toObject());
      if (!s.symbols.isEmpty()) out << s;
    }
  }
  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
  return out;
}

void Retail::loadRecordings() {
  const QString path = dir();   // paths:: on the GUI thread
  const QDateTime from = QDateTime::currentDateTimeUtc().addDays(-kKeepDays);
  auto* w = new QFutureWatcher<QVector<RetailSnapshot>>(this);
  connect(w, &QFutureWatcher<QVector<RetailSnapshot>>::finished, this, [this, w] {
    QVector<RetailSnapshot> h = w->result();
    w->deleteLater();
    // answers recorded while the files were read are newer: keep them after
    const QDateTime lastRead = h.isEmpty() ? QDateTime() : h.last().time;
    for (const auto& s : std::as_const(m_history)) if (!lastRead.isValid() || s.time > lastRead) h << s;
    m_history = std::move(h);
    if (!m_history.isEmpty() && (m_latest.isEmpty() || m_latest.time < m_history.last().time)) m_latest = m_history.last();
    emit changed();
  });
  w->setFuture(QtConcurrent::run([path, from] { return readRecordings(path, from); }));
}

void Retail::loadFixture(const QString& path) {
  m_fixture = true;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) { m_state = State::Error; m_message = "fixture not found: " + path; return; }   // test hook only (not translated)
  const auto doc = QJsonDocument::fromJson(f.readAll()).object();
  QString err; bool se = false;
  if (doc.contains("snapshots")) {
    for (const auto& v : doc.value("snapshots").toArray()) {
      const auto o = v.toObject();
      RetailSnapshot s = parse(QJsonDocument(o.value("response").toObject()).toJson(), &err, &se);
      s.time = QDateTime::fromString(o.value("time").toString(), Qt::ISODate);
      if (!s.isEmpty() && s.time.isValid()) m_history << s;
    }
    std::sort(m_history.begin(), m_history.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
  } else {
    RetailSnapshot s = parse(QJsonDocument(doc).toJson(), &err, &se);
    s.time = QDateTime::currentDateTimeUtc();
    if (!s.isEmpty()) m_history << s;
  }
  if (!m_history.isEmpty()) m_latest = m_history.last();
  const QByteArray st = qgetenv("DW_MYFXBOOK_STATE");   // "disconnected" shows the login form over the fixture
  m_state = st == "disconnected" ? State::NotConnected : m_history.isEmpty() ? State::Error : State::Connected;
  if (st == "disconnected") { m_latest = {}; m_history.clear(); }
  m_message = m_history.isEmpty() ? "fixture has no symbols" : QString();
}

const RetailSymbol* Retail::symbolAt(const QString& name, const QDateTime& t, int tol, QDateTime* when) const {
  // m_history is oldest first (sorted at load, appended in time order): only the snapshots within the tolerance are
  // looked at, found by binary search; the page asks once per row, and a full scan per row was 60 days x 48 a day.
  const RetailSnapshot* best = nullptr; qint64 bestD = tol + 1LL;
  const QDateTime lo = t.addSecs(-tol);
  auto it = std::lower_bound(m_history.cbegin(), m_history.cend(), lo, [](const RetailSnapshot& s, const QDateTime& v) { return s.time < v; });
  for (; it != m_history.cend(); ++it) {
    const qint64 dd = std::llabs(it->time.secsTo(t));
    if (it->time > t && dd > tol) break;
    if (dd < bestD && it->find(name)) { bestD = dd; best = &*it; }
  }
  if (!best) return nullptr;
  if (when) *when = best->time;
  return best->find(name);
}
}
