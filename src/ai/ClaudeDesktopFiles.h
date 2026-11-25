#pragma once
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QString>
#include <QStringList>

// Where Claude Desktop keeps the "mcpServers" Deepwater adds itself to (Options > Claude > Add to Claude Desktop), and the
// reading and writing of that entry. Header only, so tests/guards can run it on made-up folders: no user file is touched.
//
// On Windows, Claude Desktop is a packaged app and has had two homes for its claude_desktop_config.json:
//   %LOCALAPPDATA%\Packages\Claude_*\LocalCache\Roaming\Claude   (the packaged app's own %APPDATA%\Claude: its log said
//                                                                 "Reading claude_desktop_config.json from" there until October)
//   %LOCALAPPDATA%\Claude-Data\claude_*                          (the data folder of the newer versions, where its settings are)
// 0.4.1 wrote only to the first and, on 2026-11-24, the second one, which holds the account's preferences, had no entry. Which
// one a given version reads cannot be told from outside, so the entry goes in every one of them that exists: the extra
// copy is harmless, a missing one is the "added, but nothing happens" that 0.4.1 gave.
namespace ai::claudedesktop {

// The configuration files to read and write, most likely first. Empty: Claude Desktop is not installed.
inline QStringList configFiles(const QString& localAppData, const QString& appData) {
  QStringList out;
  const QString name = QStringLiteral("claude_desktop_config.json");
  // the newer versions: only where the file exists (it holds the preferences: never created from nothing)
  const QDir data(localAppData + "/Claude-Data");
  for (const QString& d : data.entryList({"claude_*"}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
    if (QFileInfo::exists(data.filePath(d) + "/" + name)) out << data.filePath(d) + "/" + name;
  // the packaged app's private %APPDATA%\Claude
  const QDir packages(localAppData + "/Packages");
  const QStringList claude = packages.entryList({"Claude_*"}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  for (const QString& d : claude) out << packages.filePath(d) + "/LocalCache/Roaming/Claude/" + name;
  // not packaged: the usual %APPDATA%\Claude, when the folder is there
  if (claude.isEmpty() && QFileInfo::exists(appData + "/Claude")) out << appData + "/Claude/" + name;
  return out;
}

inline bool hasEntry(const QString& path, const QString& server) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return false;
  return QJsonDocument::fromJson(f.readAll()).object().value("mcpServers").toObject().contains(server);
}

// Reads a file for a change: its JSON object (empty when the file does not exist yet), or false with the reason.
// A file that cannot be read is never overwritten: the user's other servers and settings are in it.
inline bool load(const QString& path, QJsonObject* cfg, QString* error, const QString& notJson) {
  *cfg = QJsonObject();
  QFile in(path);
  if (!in.exists()) return true;
  if (!in.open(QIODevice::ReadOnly)) { *error = in.errorString(); return false; }
  QJsonParseError pe;
  const QJsonDocument d = QJsonDocument::fromJson(in.readAll(), &pe);
  if (pe.error != QJsonParseError::NoError || !d.isObject()) { *error = notJson.arg(path); return false; }
  *cfg = d.object();
  return true;
}

// Adds (on) or removes the `server` entry in every file of `paths`; everything else in them stays as it is. All the files are
// read first: if one cannot be, none is written. The first change to a file copies it to <file>.deepwater-backup.
// `notJson` is the sentence for an unreadable file, with %1 for its path (translated by the caller).
inline bool setEntry(const QStringList& paths, const QString& server, const QJsonObject& entry, bool on, QString* error, const QString& notJson) {
  QList<QJsonObject> configs;
  for (const QString& p : paths) {
    QJsonObject cfg;
    if (!load(p, &cfg, error, notJson)) return false;
    configs << cfg;
  }
  for (int i = 0; i < paths.size(); ++i) {
    const QString& path = paths[i];
    QJsonObject cfg = configs[i];
    QJsonObject servers = cfg.value("mcpServers").toObject();
    if (on) servers.insert(server, entry); else servers.remove(server);
    if (!on && !cfg.contains("mcpServers")) continue;   // nothing to take out of a file that never had the list
    cfg.insert("mcpServers", servers);
    if (QFileInfo::exists(path) && !QFileInfo::exists(path + ".deepwater-backup")) QFile::copy(path, path + ".deepwater-backup");
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) { *error = out.errorString(); return false; }
    out.write(QJsonDocument(cfg).toJson());
    if (!out.commit()) { *error = out.errorString(); return false; }
  }
  return true;
}
}  // namespace ai::claudedesktop
