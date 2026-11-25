#pragma once
#include <QString>
#include <QStringList>
#include <QList>
#include <QDateTime>
#include <QObject>

// Registry of every outside data source. Each service declares the hosts it uses, next to its code:
//
//   static const sources::Registrar reg({ "cftc", "CFTC Socrata", {"publicreporting.cftc.gov"},
//       sources::Access::Keyless, 1000, "COT reports", "Public domain (US gov)", {"positioning/cot"} });
//
// net::Http paces requests per host from it, refuses Forbidden hosts and warns about undeclared ones.
namespace sources {
enum class Access { Keyless, Key, Account, Bundled, Forbidden };

struct Source {
  QString id;            // short id, e.g. "yahoo"
  QString publisher;     // who publishes the data
  QStringList hosts;     // exact hosts
  Access access = Access::Keyless;
  int paceMs = 1000;     // minimum interval between two requests to one host
  QString data;          // what we take from it
  QString terms;         // terms of use summary, why we may use it
  QStringList pages;     // pages using it, "tab/sub"
  QString keyName = {};  // settings key "keys.<keyName>" when access == Key
  int parallel = 1;      // requests on the wire at once to one host, paceMs apart (a host answering slowly: Dukascopy)
  bool optIn = false;    // off until it is switched on in the Sources tab (settings key sources.enabled)
};

void declare(const Source& s);
const Source* forHost(const QString& host);
// Minimum interval between two requests to the host: the largest paceMs among the declarations naming it; -1 if none.
int paceForHost(const QString& host);
int parallelForHost(const QString& host);   // the smallest `parallel` among the declarations naming it; 1 if none
QList<Source> all();
Source shown(const Source& s);   // the same with its publisher, data and terms in the interface language
QString shownPublisher(const QString& publisher);
const Source* byId(const QString& id);

struct Registrar { explicit Registrar(const Source& s) { declare(s); } };

// ---- Health and switches (Sources tab)
// Every network answer is recorded per host by net::Http; a source's health aggregates its hosts. Kept across runs
// in <data>/sources/health.json (cache hits are not requests and change nothing).
struct Health {
  QDateTime lastOk, lastFail;    // UTC
  QString lastError;             // e.g. "HTTP 403", "Host not found"
  int okCount = 0, failCount = 0;
  int failStreak = 0;            // consecutive failures since the last success
};
enum class Status { Waiting, Active, Failing, Disabled };
Health health(const QString& id);
Status status(const QString& id);       // Failing: ≥ 2 failures in a row, or never answered and ≥ 1 failure
void recordResult(const QString& host, bool ok, const QString& error);   // net::Http only
// Sources that used to answer and have stopped: every host of the source asked lately failed its last requests (two in a
// row at least), the last answer is between ten minutes and two weeks old and the last failure less than three hours.
// A source that never answered (a site closed to programs from here) or is switched off is not one.
struct Silent { QString id, publisher, error; QDateTime lastOk; int failures = 0; };
QList<Silent> silent(const QDateTime& nowUtc);
// The rule itself, on one source's hosts (tests).
bool isSilent(const QList<Health>& hosts, const QDateTime& nowUtc);
// Network error texts (QNetworkReply::errorString) carry the full URL, and URLs carry keys and tokens (?api_key=…,
// Telegram /bot<token>/, Discord /webhooks/<id>/<token>). Keeps only scheme://host of every URL in the text.
// recordResult and net::Http apply it, so no key reaches health.json, the Sources tab or a page's error message.
QString redactUrls(const QString& text);

// A disabled source is never contacted: net::Http refuses every host it declares (settings key sources.disabled).
bool isDisabled(const QString& id);
bool isHostDisabled(const QString& host);
void setDisabled(const QString& id, bool disabled);
QStringList sharingHosts(const QString& id);   // other sources declaring one of its hosts

class Notifier : public QObject {
  Q_OBJECT
public:
  static Notifier& instance();
signals:
  void changed();                 // health or switches changed (coalesced)
};
}
