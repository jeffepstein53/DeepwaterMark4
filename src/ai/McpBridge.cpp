#include "ai/McpBridge.h"
#include "ai/McpServer.h"
#include "ai/ClaudeCode.h"
#include "ai/ClaudeDesktopFiles.h"
#include "core/Process.h"
#include "core/Settings.h"
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
QByteArray rpcError(const QJsonValue& id, const QString& msg, int code = -32000) {
  return QJsonDocument(QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"error", QJsonObject{{"code", code}, {"message", msg}}}})
      .toJson(QJsonDocument::Compact);
}
QByteArray rpcResult(const QJsonValue& id, const QJsonObject& result) {
  return QJsonDocument(QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}}).toJson(QJsonDocument::Compact);
}
// a tool call that could not reach the app: an answer Claude reads (isError), not a broken server
QByteArray toolError(const QJsonValue& id, const QString& msg) {
  return rpcResult(id, QJsonObject{{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", msg}}}}, {"isError", true}});
}
const char* kClosed = "Deepwater is not open on this computer, or Claude's access is off (Options > CLAUDE). "
                      "Ask the user to open Deepwater with that switch on, then call the tool again: nothing to reconnect or restart.";

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
  QCoreApplication app(argc, argv);   // names set by main() before: the same settings as the app
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
    if (!rq.contains("id")) continue;   // notifications (initialized, cancelled…): nothing to answer, nothing to do
    const QJsonValue id = rq.value("id");
    const QString m = rq.value("method").toString();
    // The handshake never depends on the app: a client that asked while Deepwater was closed took the error for a dead
    // server, dropped its tools and never asked again, even once Deepwater was open (2026-11-25, Claude Desktop).
    if (m == "initialize") {
      out(rpcResult(id, McpServer::initializeResult(rq.value("params").toObject().value("protocolVersion").toString())));
      continue;
    }
    if (m == "ping") { out(rpcResult(id, {})); continue; }
    if (m == "tools/list") {
      out(rpcResult(id, QJsonObject{{"tools", McpServer::toolList(settings::value("ai.mcp.journal", false).toBool())}}));
      continue;
    }
    if (m != "tools/call") { out(rpcError(id, "method not found: " + m, -32601)); continue; }
    QByteArray answer; int status = 0;
    if (!post(msg, &answer, &status) || status == 404 || status == 401) {
      std::fprintf(stderr, "[deepwater] %s\n", kClosed);
      out(toolError(id, QString::fromUtf8(kClosed)));
      continue;
    }
    if (!answer.trimmed().isEmpty()) out(answer.trimmed());
    else out(toolError(id, QStringLiteral("Deepwater answered HTTP %1 without a body").arg(status)));
  }
  return 0;
}
QString program() { return QDir::toNativeSeparators(QCoreApplication::applicationFilePath()); }

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

QStringList claudeDesktopConfigs() {
#if defined(Q_OS_WIN)
  return claudedesktop::configFiles(qEnvironmentVariable("LOCALAPPDATA"), qEnvironmentVariable("APPDATA"));   // the two homes of the packaged app
#elif defined(Q_OS_MACOS)
  const QString dir = QDir::homePath() + "/Library/Application Support/Claude";
  if (!QFileInfo::exists(dir) && !QFileInfo::exists("/Applications/Claude.app")) return {};
  return {dir + "/claude_desktop_config.json"};
#else
  const QString dir = QDir::homePath() + "/.config/Claude";
  if (!QFileInfo::exists(dir)) return {};
  return {dir + "/claude_desktop_config.json"};
#endif
}

bool inClaudeDesktop() {
  for (const QString& p : claudeDesktopConfigs())
    if (claudedesktop::hasEntry(p, "deepwater")) return true;
  return false;
}

bool setInClaudeDesktop(bool on, QString* error) {
  const QStringList paths = claudeDesktopConfigs();
  if (paths.isEmpty()) { *error = QCoreApplication::translate("ai", "Claude Desktop is not installed on this computer."); return false; }
  return claudedesktop::setEntry(paths, "deepwater", QJsonObject{{"command", program()}, {"args", QJsonArray{"--mcp-stdio"}}}, on, error,
                                 QCoreApplication::translate("ai", "%1 is not valid JSON: fix it or add the entry by hand."));
}
}  // namespace ai::mcpbridge
