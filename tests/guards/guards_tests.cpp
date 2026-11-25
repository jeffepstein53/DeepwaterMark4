// Tests of ai/ClaudeDesktopFiles.h, app/SingleInstance.h and settings::dropRetired(): see CMakeLists.txt.
#include "ai/ClaudeDesktopFiles.h"
#include "app/SingleInstance.h"
#include "core/Settings.h"
#include <QCoreApplication>
#include <QProcess>
#include <QThread>
#include <QTemporaryDir>
#include <cstdio>

static int g_ok = 0, g_bad = 0;
static void check(const char* what, bool ok) {
  if (ok) ++g_ok; else ++g_bad;
  std::printf("%s  %s\n", ok ? "ok " : "BAD", what);
}
static void write(const QString& path, const QByteArray& data) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  if (f.open(QIODevice::WriteOnly)) f.write(data);
}
static QJsonObject read(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return {};
  return QJsonDocument::fromJson(f.readAll()).object();
}

static void paths() {
  using namespace ai::claudedesktop;
  QTemporaryDir t;
  const QString local = t.path() + "/Local", roaming = t.path() + "/Roaming";
  check("nothing installed: no file", configFiles(local, roaming).isEmpty());
  QDir().mkpath(roaming + "/Claude");
  check("a plain Claude folder in %APPDATA%: its file", configFiles(local, roaming) == QStringList{roaming + "/Claude/claude_desktop_config.json"});
  QDir().mkpath(local + "/Packages/Claude_abc/LocalCache/Roaming/Claude");
  check("packaged: the package's file, and no %APPDATA% one",
        configFiles(local, roaming) == QStringList{local + "/Packages/Claude_abc/LocalCache/Roaming/Claude/claude_desktop_config.json"});
  QDir().mkpath(local + "/Claude-Data/claude_abc");
  check("a Claude-Data folder without its file is not made one", configFiles(local, roaming).size() == 1);
  write(local + "/Claude-Data/claude_abc/claude_desktop_config.json", R"({"preferences":{"a":1}})");
  const QStringList both = configFiles(local, roaming);
  check("Claude-Data's file first, then the package's", both.size() == 2 && both[0].contains("Claude-Data") && both[1].contains("Packages"));
}

static void entries() {
  using namespace ai::claudedesktop;
  QTemporaryDir t;
  const QString data = t.path() + "/Claude-Data/claude_abc/claude_desktop_config.json";
  const QString pkg = t.path() + "/Packages/Claude_abc/LocalCache/Roaming/Claude/claude_desktop_config.json";
  write(data, R"({"preferences":{"sidebar":"expanded","n":3},"coworkUserFilesPath":"C:/x","mcpServers":{"other":{"command":"o"}}})");
  QDir().mkpath(QFileInfo(pkg).absolutePath());
  const QStringList files{data, pkg};
  const QJsonObject entry{{"command", "C:/Deepwater/Deepwater.exe"}, {"args", QJsonArray{"--mcp-stdio"}}};
  const QString notJson = "%1 is not valid JSON";
  QString err;
  check("add: written in both", setEntry(files, "deepwater", entry, true, &err, notJson) && hasEntry(data, "deepwater") && hasEntry(pkg, "deepwater"));
  const QJsonObject d = read(data);
  check("add: the preferences and the other server are still there",
        d.value("preferences").toObject().value("n").toInt() == 3 && d.value("coworkUserFilesPath").toString() == "C:/x" &&
        d.value("mcpServers").toObject().contains("other"));
  check("add: the entry is the one given", d.value("mcpServers").toObject().value("deepwater").toObject() == entry);
  check("add: the existing file has its backup, the new one none", QFileInfo::exists(data + ".deepwater-backup") && !QFileInfo::exists(pkg + ".deepwater-backup"));
  check("the backup is the file as it was", !read(data + ".deepwater-backup").value("mcpServers").toObject().contains("deepwater"));
  check("remove: gone from both, the other server kept",
        setEntry(files, "deepwater", {}, false, &err, notJson) && !hasEntry(data, "deepwater") && !hasEntry(pkg, "deepwater") &&
        read(data).value("mcpServers").toObject().contains("other") && read(data).value("preferences").toObject().value("sidebar").toString() == "expanded");
  // removing from a file that never had the list leaves it alone
  QTemporaryDir u;
  const QString plain = u.path() + "/c.json";
  write(plain, R"({"preferences":{}})");
  const auto before = QFileInfo(plain).size();
  check("remove: a file without a server list is not rewritten", setEntry({plain}, "deepwater", {}, false, &err, notJson) && QFileInfo(plain).size() == before && !QFileInfo::exists(plain + ".deepwater-backup"));
  // a file that cannot be read stops everything: the other one is not written either
  QTemporaryDir v;
  const QString good = v.path() + "/good.json", bad = v.path() + "/bad.json";
  write(good, R"({"preferences":{}})");
  write(bad, "{ not json");
  err.clear();
  check("an unreadable file: refused, naming it", !setEntry({good, bad}, "deepwater", entry, true, &err, notJson) && err.contains("bad.json"));
  check("an unreadable file: nothing written anywhere", !hasEntry(good, "deepwater") && !QFileInfo::exists(good + ".deepwater-backup") && read(bad).isEmpty());
}

static void lock() {
  QTemporaryDir t;
  auto first = app::singleinstance::acquire(t.path());
  check("the first Deepwater gets the lock", first != nullptr);
  check("a second one on the same folder does not", app::singleinstance::acquire(t.path()) == nullptr);
  QTemporaryDir other;
  check("another folder (another profile) is free", app::singleinstance::acquire(other.path()) != nullptr);
  first.reset();
  check("once the first has ended, the next gets it", app::singleinstance::acquire(t.path()) != nullptr);
  // a real second process holds the lock, then is killed: what a crashed or killed Deepwater leaves behind
  QTemporaryDir k;
  QProcess child;
  child.setProgram(QCoreApplication::applicationFilePath());
  child.setArguments({"--hold", k.path()});
  child.start();
  const bool held = child.waitForStarted(10000) && child.waitForReadyRead(10000) && child.readAllStandardOutput().contains("held");
  check("another process holds the lock", held && app::singleinstance::acquire(k.path()) == nullptr);
  QThread::msleep(2000);
  check("a live process keeps it, however long", app::singleinstance::acquire(k.path()) == nullptr);
  child.kill();
  child.waitForFinished(10000);
  check("killed: its lock is taken over", app::singleinstance::acquire(k.path()) != nullptr);
}

static void retired() {
  QTemporaryDir t;
  qputenv("DW_PROFILE", t.path().toUtf8());   // before the first use: a test profile, never the user's store
  const char* gone[] = {"ai/slots", "ai/skills", "ai/claude/model", "ai/claude/effort", "ai/llm/part", "ml/source", "ml/prices"};
  for (const char* k : gone) settings::setValue(k, "x");
  const char* kept[] = {"ai/mcp/external", "ai/mcp/journal", "ai/claude/binary", "ui.page", "data.keepFresh", "ml/other"};
  for (const char* k : kept) settings::setValue(k, "y");
  check("a test profile is in use", settings::isTestProfile());
  settings::dropRetired();
  bool allGone = true, allKept = true;
  for (const char* k : gone) allGone = allGone && !settings::contains(k);
  for (const char* k : kept) allKept = allKept && settings::contains(k);
  check("the 7 retired keys are removed", allGone);
  check("every other key stays, the Claude ones included", allKept);
  settings::dropRetired();
  check("a second run changes nothing", !settings::contains("ai/slots") && settings::contains("ai/mcp/external"));
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().value(1) == "--hold") {   // the helper process of lock(): takes the lock and waits to be killed
    auto held = app::singleinstance::acquire(app.arguments().value(2));
    std::puts(held ? "held" : "refused");
    std::fflush(stdout);
    QThread::sleep(60);
    return 0;
  }
  paths();
  entries();
  lock();
  retired();
  std::printf("%d checks passed, %d failed\n", g_ok, g_bad);
  return g_bad ? 1 : 0;
}
