#include "app/Watchdog.h"
#include "app/Background.h"
#include "macro/CentralBanks.h"
#include "macro/KeyFigures.h"
#include "macro/MacroStore.h"
#include "net/Http.h"
#include "net/Sources.h"
#include "news/Calendar.h"
#include "core/I18n.h"
#include <QCoreApplication>
#include <QSet>

namespace app {
namespace {
QString tx(const char* s) { return QCoreApplication::translate("checks", s); }
QString hm(const QDateTime& t) {
  const QDateTime l = t.toLocalTime();
  return i18n::locale().toString(l, l.date() == QDate::currentDate() ? "HH:mm" : "d MMM HH:mm");
}
constexpr int kWarmUpSec = 180;           // the first figures of a session arrive over a few minutes
constexpr int kAllSilent = 6;             // that many sources silent together: the connection, not the sources
}

Watchdog& Watchdog::instance() { static Watchdog* w = new Watchdog; return *w; }

Watchdog::Watchdog() {
  m_timer.setInterval(60 * 1000);
  connect(&m_timer, &QTimer::timeout, this, &Watchdog::look);
  m_soon.setSingleShot(true); m_soon.setInterval(5000);
  connect(&m_soon, &QTimer::timeout, this, &Watchdog::look);
}

void Watchdog::start() {
  if (m_started) return;
  m_started = true;
  m_from = QDateTime::currentDateTimeUtc();
  macro::KeyFigures::instance().start();
  connect(&macro::KeyFigures::instance(), &macro::KeyFigures::changed, this, &Watchdog::later);
  connect(&sources::Notifier::instance(), &sources::Notifier::changed, this, &Watchdog::later);
  connect(&i18n::Notifier::instance(), &i18n::Notifier::changed, this, &Watchdog::later);
  m_timer.start();
  look();
}

void Watchdog::look() {
  const QDateTime now = QDateTime::currentDateTimeUtc();
  if (Background::enabled()) {   // the figures compared stay current while no page shows them; each at its own pace
    net::BackgroundScope bg(QStringLiteral("economy/macro"));
    macro::MacroStore::instance().ensureLoaded();
  }
  QList<Issue> out;
  // ---- sources that have stopped answering
  const QList<sources::Silent> silent = sources::silent(now);
  if (silent.size() >= kAllSilent) {
    QDateTime last;
    QStringList names;
    for (const auto& s : silent) { if (s.lastOk > last) last = s.lastOk; names << sources::shownPublisher(s.publisher); }
    Issue i; i.kind = Issue::Network; i.key = "network"; i.page = "sources/sources";
    i.title = tx("Internet connection");
    i.detail = tx("%1 sources have stopped answering, the last answer at %2: %3").arg(silent.size()).arg(hm(last), names.join(", "));
    out << i;
  } else {
    for (const auto& s : silent) {
      Issue i; i.kind = Issue::Source; i.key = "source:" + s.id; i.page = "sources/sources";
      i.title = sources::shownPublisher(s.publisher);
      i.detail = tx("No answer since %1 (%2 failures in a row: %3)").arg(hm(s.lastOk)).arg(s.failures).arg(s.error);
      out << i;
    }
  }
  if (m_from.secsTo(now) >= kWarmUpSec) {
    // ---- key figures
    const bool loading = macro::MacroStore::instance().loading();
    for (const macro::check::Item& it : macro::KeyFigures::instance().items()) {
      using V = macro::check::Verdict;
      if (it.verdict != V::Differ && it.verdict != V::Late && (it.verdict != V::Missing || loading)) continue;
      if (it.figure == "Odds" && m_from.secsTo(now) < 4 * kWarmUpSec) continue;
      if (it.verdict == V::Missing && (it.figure == "Meeting" || m_from.secsTo(now) < 4 * kWarmUpSec)) continue;   // the paced series come last
      Issue i; i.kind = it.figure == "Odds" || it.figure == "Meeting" ? Issue::Odds : Issue::Figure;
      i.key = "figure:" + it.ccy + ":" + it.figure + ":" + QString::number(int(it.verdict));
      i.page = it.figure == "Odds" || it.figure == "Meeting" || it.figure == "Policy rate" ? "economy/rates" : "market/fundamental-heatmap";
      i.title = it.ccy + " · " + macro::checkFigureName(it.figure);
      i.detail = macro::checkLine(it);
      out << i;
    }
    // ---- results the calendar still misses
    for (const news::Event& e : news::Calendar::instance().missingResults()) {
      Issue i; i.kind = Issue::Result; i.key = "result:" + e.key; i.page = "news/calendar";
      i.title = e.currency + " · " + e.title;
      i.detail = tx("Published at %1, no figure yet").arg(hm(e.time));
      out << i;
    }
  }
  QHash<QString, QDateTime> since;
  QList<Issue> fresh;
  for (Issue& i : out) {
    const auto it = m_since.constFind(i.key);
    i.since = it == m_since.cend() ? now : *it;
    if (it == m_since.cend()) fresh << i;
    since.insert(i.key, i.since);
  }
  bool same = out.size() == m_issues.size();
  for (int k = 0; same && k < out.size(); ++k) same = out[k].key == m_issues[k].key && out[k].detail == m_issues[k].detail && out[k].title == m_issues[k].title;
  m_issues = out;
  m_since = since;
  if (!same) emit changed();
  for (const Issue& i : std::as_const(fresh)) emit appeared(i);
}
}
