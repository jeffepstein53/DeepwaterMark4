#include "app/Alerts.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "news/Calendar.h"
#include "platform/WinNotify.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace app {
namespace {
QString tx(const char* s) { return QCoreApplication::translate("alerts", s); }
constexpr int kOldResultSec = 2 * 3600;      // a figure filled in later than that is history, not news
constexpr int kTellAgainDays = 7;
const char* key(Alerts::Kind k) { return k == Alerts::Results ? "alerts.results" : k == Alerts::Problems ? "alerts.problems" : "alerts.sound"; }
QString toldFile() { return paths::data("sources") + "/alerts-told.json"; }
}

Alerts& Alerts::instance() { static Alerts* a = new Alerts; return *a; }

Alerts::Alerts() {
  m_resultTimer.setSingleShot(true); m_resultTimer.setInterval(4000);     // a release is several figures within seconds
  m_problemTimer.setSingleShot(true); m_problemTimer.setInterval(15000);
  connect(&m_resultTimer, &QTimer::timeout, this, &Alerts::flushResults);
  connect(&m_problemTimer, &QTimer::timeout, this, &Alerts::flushProblems);
}

bool Alerts::on(Kind k) { return settings::value(key(k), true).toBool(); }
void Alerts::setOn(Kind k, bool v) { settings::setValue(key(k), v); }

void Alerts::start() {
  if (m_started) return;
  m_started = true;
  loadTold();
  connect(&news::Calendar::instance(), &news::Calendar::actualArrived, this, &Alerts::result);
  connect(&Watchdog::instance(), &Watchdog::appeared, this, &Alerts::problem);
}

QPair<QString, QString> Alerts::resultText(const news::Event& e) {
  QStringList t;
  if (e.forecast.has()) t << tx("Forecast %1").arg(e.forecast.text);
  const QString prev = e.revised.has() ? e.revised.text : e.previous.text;
  if (!prev.isEmpty()) t << tx("previous %1").arg(prev);
  return {QString("%1 · %2: %3").arg(e.currency, e.title, e.actual.text), t.join(" · ")};
}

void Alerts::result(const news::Event& e) {
  if (!on(Results) || e.impact != news::Impact::High || !e.actual.has()) return;
  const qint64 age = e.time.secsTo(QDateTime::currentDateTimeUtc());
  if (age < -120 || age > kOldResultSec) return;
  if (m_figures.contains(e.key)) return;             // the same release, from a second source
  m_figures.insert(e.key, e.actual.text);
  const auto r = resultText(e);
  m_results << Note{r.first, r.second};
  m_resultTimer.start();
}

void Alerts::problem(const Issue& i) {
  const QDateTime now = QDateTime::currentDateTimeUtc();
  if (!on(Problems)) return;                         // told when it is switched on again and still there at the next start
  const QDateTime told = m_told.value(i.key);
  if (told.isValid() && told.daysTo(now) < kTellAgainDays) return;
  m_told.insert(i.key, now);
  m_problems << i;
  m_problemTimer.start();
}

void Alerts::flushResults() {
  if (m_results.isEmpty()) return;
  if (m_results.size() == 1) send(m_results[0].title, m_results[0].text, QStringLiteral("news/calendar"), false);
  else {
    QStringList l;
    for (const Note& n : std::as_const(m_results)) l << n.title;
    send(tx("%1 figures published").arg(m_results.size()), l.join("\n"), QStringLiteral("news/calendar"), false);
  }
  m_results.clear();
}

void Alerts::flushProblems() {
  if (m_problems.isEmpty()) return;
  if (m_problems.size() == 1) send(m_problems[0].title, m_problems[0].detail, QStringLiteral("sources/sources"), true);
  else {
    QStringList l;
    for (const Issue& i : std::as_const(m_problems)) l << i.title;
    send(tx("Data check: %1 points to see").arg(m_problems.size()), l.join("\n"), QStringLiteral("sources/sources"), true);
  }
  m_problems.clear();
  saveTold();
}

void Alerts::send(const QString& title, const QString& text, const QString& page, bool warning) {
  if (settings::isTestProfile() && qEnvironmentVariable("DW_ALERT_SHOW") != "1") {   // a trial never notifies: it says so in its log
    qInfo("[alert] %s | %s | %s", qPrintable(title), qPrintable(QString(text).replace('\n', " / ")), qPrintable(page));
    return;
  }
  winnotify::show(title, text.isEmpty() ? QStringLiteral(" ") : text, [this, page] { emit openRequested(page); });
  if (on(Sound)) winnotify::chime(warning);
}

void Alerts::tryOne() {
  send(tx("Deepwater's notifications work"), tx("This is how a figure is announced when it is published."), QStringLiteral("news/calendar"), false);
}

void Alerts::loadTold() {
  QFile f(toldFile());
  if (!f.open(QIODevice::ReadOnly)) return;
  const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
  const QDateTime now = QDateTime::currentDateTimeUtc();
  for (auto it = o.begin(); it != o.end(); ++it) {
    const QDateTime t = QDateTime::fromString(it.value().toString(), Qt::ISODate);
    if (t.isValid() && t.daysTo(now) < kTellAgainDays) m_told.insert(it.key(), t);
  }
}

void Alerts::saveTold() const {
  QJsonObject o;
  for (auto it = m_told.cbegin(); it != m_told.cend(); ++it) o.insert(it.key(), it.value().toString(Qt::ISODate));
  QSaveFile f(toldFile());
  if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(o).toJson(QJsonDocument::Compact)); f.commit(); }
}
}
