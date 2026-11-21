#include "ai/McpBridge.h"
#include "ai/McpServer.h"
#include "ai/ClaudeCode.h"
#include "core/Process.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <memory>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

namespace ai::mcpbridge {
namespace {
void out(const QByteArray& line) {
  std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
  std::fputc('\n', stdout);
  std::fflush(stdout);
}
QByteArray rpcError(const QJsonValue& id, const QString& msg) {
  return QJsonDocument(QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"error", QJsonObject{{"code", -32000}, {"message", msg}}}})
      .toJson(QJsonDocument::Compact);
}
const char* kClosed = "Deepwater is not open on this computer, or \"Claude outside Deepwater\" is off (AI › LLM › Integration). "
                      "Open Deepwater, then reconnect this MCP server (/mcp in Claude Code; restart Claude Desktop).";

// one HTTP POST to the app; false when it could not be reached
bool post(const QByteArray& body, QByteArray* answer, int* status) {
  QFile f(McpServer::connectionFile());
  if (!f.open(QIODevice::ReadOnly)) return false;
  const QJsonObject c = QJsonDocument::fromJson(f.readAll()).object();
  const int port = c.value("port").toInt();
  const QByteArray token = c.value("token").toString().toLatin1(), path = c.value("path").toString("/mcp/ext").toLatin1();
  if (port <= 0 || token.isEmpty()) return false;
  QTcpSocket s;
  s.connectToHost(QStringLiteral("127.0.0.1"), quint16(port));
  if (!s.waitForConnected(3000)) return false;
  s.write("POST " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port) + "\r\nAuthorization: Bearer " + token +
          "\r\nContent-Type: application/json\r\nAccept: application/json, text/event-stream\r\nContent-Length: " +
          QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
  if (!s.waitForBytesWritten(5000)) return false;
  QByteArray buf;
  int headEnd = -1, len = -1;
  // a tool may take up to a few minutes (its own time limit decides): wait as long as the socket stays open
  while (s.state() == QAbstractSocket::ConnectedState || s.bytesAvailable() > 0) {
    if (s.bytesAvailable() == 0 && !s.waitForReadyRead(310000)) break;
    buf += s.readAll();
    if (headEnd < 0 && (headEnd = int(buf.indexOf("\r\n\r\n"))) >= 0) {
      *status = buf.mid(9, 3).toInt();
      for (const QByteArray& l : buf.left(headEnd).split('\n'))
        if (l.toLower().startsWith("content-length:")) len = l.mid(15).trimmed().toInt();
    }
    if (headEnd >= 0 && len >= 0 && buf.size() >= headEnd + 4 + len) break;
  }
  if (headEnd < 0) return false;
  *answer = buf.mid(headEnd + 4, len >= 0 ? len : -1);
  return true;
}
}  // namespace

bool wanted(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) if (std::strcmp(argv[i], "--mcp-stdio") == 0) return true;
  return false;
}

int run(int argc, char** argv) {
  QCoreApplication app(argc, argv);   // names set by main() before: the same data folder as the app
#ifdef Q_OS_WIN
  // one JSON message per line, "\n" only: text mode would write "\r\n" and stop reading at a Ctrl-Z byte
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  std::string line;
  while (std::getline(std::cin, line)) {
    const QByteArray msg = QByteArray::fromStdString(line).trimmed();
    if (msg.isEmpty()) continue;
    const QJsonObject rq = QJsonDocument::fromJson(msg).object();
    const bool request = rq.contains("id");
    QByteArray answer; int status = 0;
    if (!post(msg, &answer, &status) || status == 404 || status == 401) {
      std::fprintf(stderr, "[deepwater] %s\n", kClosed);
      if (request) out(rpcError(rq.value("id"), QString::fromUtf8(kClosed)));
      continue;
    }
    if (request && !answer.trimmed().isEmpty()) out(answer.trimmed());
    else if (request) out(rpcError(rq.value("id"), QStringLiteral("Deepwater answered HTTP %1 without a body").arg(status)));
  }
  return 0;
}
QString program() { return QCoreApplication::applicationFilePath(); }

bool inClaudeCode() {
  QFile f(QDir::homePath() + "/.claude.json");
  if (!f.open(QIODevice::ReadOnly)) return false;
  return QJsonDocument::fromJson(f.readAll()).object().value("mcpServers").toObject().contains("deepwater");
}

namespace {
// one `claude …` run: its exit code and output
void runClaude(const QStringList& args, std::function<void(int code, QString out)> done) {
#if QT_CONFIG(process)
  auto* p = new dw::Process;
  p->setProcessEnvironment(claudecode::environment());
  p->setProcessChannelMode(dw::Process::MergedChannels);
  auto fired = std::make_shared<bool>(false);
  QObject::connect(p, &dw::Process::finished, p, [p, done, fired](int code, dw::Process::ExitStatus) {
    if (*fired) return; *fired = true;
    const QString out = QString::fromUtf8(p->readAll()).trimmed();
    p->deleteLater();
    done(code, out);
  });
  QObject::connect(p, &dw::Process::errorOccurred, p, [p, done, fired](dw::Process::ProcessError e) {
    if (e != dw::Process::FailedToStart || *fired) return; *fired = true;
    p->deleteLater();
    done(-1, QCoreApplication::translate("ai", "Claude Code could not start."));
  });
  QTimer::singleShot(30000, p, [p] { p->kill(); });
  p->start(claudecode::binary(), args);
#else
  done(-1, QCoreApplication::translate("ai", "Not available on this device."));
#endif
}
}  // namespace

void setInClaudeCode(bool on, std::function<void(bool, QString)> done) {
  if (claudecode::binary().isEmpty()) { done(false, QCoreApplication::translate("ai", "Claude Code is not installed on this computer.")); return; }
  // removed first in both cases: an entry left by an older copy of the app (another path) is replaced, not kept
  runClaude({"mcp", "remove", "--scope", "user", "deepwater"}, [on, done](int code, const QString& out) {
    const bool gone = code == 0 || out.contains("No MCP server");
    if (!on) { done(gone, gone ? QString() : out.section('\n', -2)); return; }
    runClaude({"mcp", "add", "--scope", "user", "deepwater", "--", program(), "--mcp-stdio"}, [done](int code, const QString& out) {
      done(code == 0, code == 0 ? QString() : out.section('\n', -2));
    });
  });
}

QString claudeDesktopConfig() {
#if defined(Q_OS_MACOS)
  const QString dir = QDir::homePath() + "/Library/Application Support/Claude";
  if (!QFileInfo::exists(dir) && !QFileInfo::exists("/Applications/Claude.app")) return {};
#elif defined(Q_OS_WIN)
  const QString dir = qEnvironmentVariable("APPDATA") + "/Claude";
  if (!QFileInfo::exists(dir)) return {};
#else
  const QString dir = QDir::homePath() + "/.config/Claude";
  if (!QFileInfo::exists(dir)) return {};
#endif
  return dir + "/claude_desktop_config.json";
}

bool inClaudeDesktop() {
  QFile f(claudeDesktopConfig());
  if (f.fileName().isEmpty() || !f.open(QIODevice::ReadOnly)) return false;
  return QJsonDocument::fromJson(f.readAll()).object().value("mcpServers").toObject().contains("deepwater");
}

bool setInClaudeDesktop(bool on, QString* error) {
  const QString path = claudeDesktopConfig();
  if (path.isEmpty()) { *error = QCoreApplication::translate("ai", "Claude Desktop is not installed on this computer."); return false; }
  QJsonObject cfg;
  QFile in(path);
  if (in.exists()) {
    if (!in.open(QIODevice::ReadOnly)) { *error = in.errorString(); return false; }
    QJsonParseError pe;
    const QJsonDocument d = QJsonDocument::fromJson(in.readAll(), &pe);
    in.close();
    // a file Deepwater cannot read is never overwritten: the user's other servers are in it
    if (pe.error != QJsonParseError::NoError || !d.isObject()) {
      *error = QCoreApplication::translate("ai", "%1 is not valid JSON: fix it or add the entry by hand.").arg(path);
      return false;
    }
    cfg = d.object();
    if (!QFileInfo::exists(path + ".deepwater-backup")) QFile::copy(path, path + ".deepwater-backup");
  }
  QJsonObject servers = cfg.value("mcpServers").toObject();
  if (on) servers.insert("deepwater", QJsonObject{{"command", program()}, {"args", QJsonArray{"--mcp-stdio"}}});
  else servers.remove("deepwater");
  cfg.insert("mcpServers", servers);
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile out(path);
  if (!out.open(QIODevice::WriteOnly)) { *error = out.errorString(); return false; }
  out.write(QJsonDocument(cfg).toJson());
  if (!out.commit()) { *error = out.errorString(); return false; }
  return true;
}
}  // namespace ai::mcpbridge
