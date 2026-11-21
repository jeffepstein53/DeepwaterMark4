#pragma once
#include <QObject>
#include <QDateTime>
#include <QList>
#include <QHash>
#include <functional>

// Research notes the banks publish themselves (title, bank, date, URL, PDF), kept in a local library.
//
//   auto& r = banks::Research::instance();
//   r.refresh();                                    // polls the feeds (paced, cached); emits updated()
//   for (const banks::Note& n : r.notes()) …        // newest first
//   r.notes("ing")                                  // one bank's notes
//   r.fetchPdf(note.id, this, [](QString file, QString err) { … });   // cached under paths::cache("banks")
//
// Only banks whose terms allow reuse are read (banks::Use::Full in Registry.h); for JPM, BofA, Goldman Sachs and Morgan
// Stanley (links only), their public podcast and YouTube feeds: titles, dates, short summaries and links. The library is an archive under
// paths::data("banks")/notes.json: feeds only show their latest items, the archive keeps what was seen before.
namespace banks {

struct Note {
  QString id;            // stable id (hash of the URL)
  QString bankId;        // banks::Bank id
  QString type;          // publication type, uppercase: ARTICLE, SNAP, OPINION, REPORT, FORECASTS, PODCAST, VIDEO
                         // (PODCAST / VIDEO: the US banks' public feeds; topic = the show, url = episode or video)
  QString topic;         // the bank's own topic tag ("Czech Republic", "FX"), may be empty
  QString title;
  QString summary;       // the feed's teaser, may be empty
  QString url;           // web page of the note
  QString pdfUrl;        // direct PDF URL, empty when the note has none
  QDateTime published;   // UTC
  QString localFile() const;   // cached PDF path when downloaded, empty otherwise
};

class Research : public QObject {
  Q_OBJECT
public:
  static Research& instance();
  void refresh();                                  // at most every 15 min (feeds cached 30 min)
  QList<Note> notes(const QString& bankId = {}) const;  // newest first
  const Note* note(const QString& id) const;
  int publishedToday() const;                      // notes published since local midnight
  int librarySize() const { return int(m_notes.size()); }
  QStringList types() const;                       // publication types present, sorted
  QStringList sources() const;                     // research names of the banks read ("ING THINK")
  void fetchPdf(const QString& noteId, QObject* ctx, std::function<void(const QString& file, const QString& error)> cb);
  QDateTime lastUpdate() const { return m_last; }
signals:
  void updated();
private:
  Research();
  void load();
  void save() const;
  void merge(const QList<Note>& incoming);
  void readIngRss(const QByteArray& body);
  void readIngListing(const QByteArray& body);
  void readFeed(const QString& bank, const QString& show, bool video, const QByteArray& body);
  QHash<QString, Note> m_notes;
  QDateTime m_last, m_lastPoll;
};

QString pdfCacheFile(const QString& url);
// Display label of a publication type in the interface language ("ARTICLE", plural "ARTICLES").
QString typeLabel(const QString& type, bool plural = false);   // where a PDF URL is cached (exists or not)
}
