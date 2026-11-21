#include "ai/McpServer.h"
#include "ai/Blind.h"
#include "core/Paths.h"
#include "ai/Engine.h"
#include "core/Paths.h"
#include "ai/Slots.h"
#include "ai/Tools.h"
#include "core/Settings.h"
#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QCoreApplication>
#include <memory>

namespace ai {
namespace {
constexpr int kMaxRequest = 1 << 20;   // 1 MB: a JSON-RPC request is a few hundred bytes
QString randomToken() {
  QByteArray b(32, Qt::Uninitialized);
  QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(b.data()), 8);
  return QString::fromLatin1(b.toHex());
}
QByteArray rpcResult(const QJsonValue& id, const QJsonObject& result) {
  return QJsonDocument(QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}}).toJson(QJsonDocument::Compact);
}
QByteArray rpcError(const QJsonValue& id, int code, const QString& msg) {
  return QJsonDocument(QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"error", QJsonObject{{"code", code}, {"message", msg}}}})
      .toJson(QJsonDocument::Compact);
}
}  // namespace

McpServer& McpServer::instance() { static McpServer* s = new McpServer; return *s; }

McpServer::McpServer() {
  for (int i = 0; i < Slots::kCount; ++i) m_tokens << randomToken();
  m_extToken = randomToken();
}

// ------------------------------------------------------------------------------------------------ outside clients
static const QStringList kExtSkills{"read_data"};

QString McpServer::connectionFile() { return paths::data("mcp") + "/connection.json"; }
bool McpServer::externalEnabled() const { return settings::value("ai.mcp.external", false).toBool(); }

bool McpServer::writeConnection() {
  const QString path = connectionFile();
  paths::dropLink(path);
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return false;
  f.write(QJsonDocument(QJsonObject{{"port", int(port())}, {"token", m_extToken}, {"path", "/mcp/ext"},
                                    {"pid", double(QCoreApplication::applicationPid())}}).toJson());
  if (!f.commit()) return false;
  QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  return true;
}

void McpServer::startExternal() {
  if (!externalEnabled()) { QFile::remove(connectionFile()); return; }
  if (listen()) writeConnection();
  emit externalChanged();
}

void McpServer::setExternalEnabled(bool on) {
  settings::setValue("ai.mcp.external", on);
  startExternal();
}

quint16 McpServer::port() const { return m_srv && m_srv->isListening() ? m_srv->serverPort() : 0; }

bool McpServer::listen() {
  if (m_srv && m_srv->isListening()) return true;
  if (!m_srv) {
    m_srv = new QTcpServer(this);
    connect(m_srv, &QTcpServer::newConnection, this, [this] {
      while (QTcpSocket* s = m_srv->nextPendingConnection()) {
        m_buf.insert(s, {});
        // a connection that sends nothing for 30 s is closed (one left open idle held a buffer and a socket for ever)
        auto* idle = new QTimer(s);
        idle->setSingleShot(true);
        idle->setInterval(30000);
        connect(idle, &QTimer::timeout, s, [s] { s->abort(); });
        idle->start();
        connect(s, &QTcpSocket::readyRead, this, [this, s, idle] { idle->start(); onReadable(s); });
        connect(s, &QTcpSocket::disconnected, this, [this, s] { m_buf.remove(s); s->deleteLater(); });
      }
    });
  }
  return m_srv->listen(QHostAddress::LocalHost, 0);
}

QString McpServer::writeConfig(int env) {
  if (env < 0 || env >= Slots::kCount || !listen()) return {};
  const QString dir = Slots::instance().folder(env);
  QDir().mkpath(dir);
  const QJsonObject cfg{{"mcpServers", QJsonObject{{"deepwater", QJsonObject{
      {"type", "http"},
      {"url", QString("http://127.0.0.1:%1/mcp/%2").arg(port()).arg(env + 1)},
      {"headers", QJsonObject{{"Authorization", "Bearer " + m_tokens[env]}}}}}}}};
  const QString path = dir + "/mcp.json";
  paths::dropLink(path);   // QSaveFile writes through a link: the token must not land elsewhere
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return {};
  f.write(QJsonDocument(cfg).toJson());
  if (!f.commit()) return {};
  QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  return path;
}

void McpServer::onReadable(QTcpSocket* s) {
  QByteArray& b = m_buf[s];
  b += s->readAll();
  for (;;) {
    const int end = int(b.indexOf("\r\n\r\n"));
    if (end < 0) { if (b.size() > 16384) s->abort(); return; }
    const QList<QByteArray> lines = b.left(end).split('\n');
    const QList<QByteArray> req = lines.value(0).trimmed().split(' ');
    QHash<QByteArray, QByteArray> h;
    for (int i = 1; i < lines.size(); ++i) {
      const int colon = int(lines[i].indexOf(':'));
      if (colon > 0) h.insert(lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
    }
    const int len = h.value("content-length", "0").toInt();
    if (len < 0 || len > kMaxRequest || req.size() < 2) { s->abort(); return; }
    // the head decides: a request without the token is refused before its body is buffered (up to 1 MB each)
    if (const int code = gate(req[1], h)) {
      reply(s, code, {});
      b.clear();
      s->disconnectFromHost();
      return;
    }
    if (b.size() < end + 4 + len) return;   // body still arriving
    const QByteArray body = b.mid(end + 4, len);
    b.remove(0, end + 4 + len);
    handle(s, req[0], req[1], h, body);
  }
}

void McpServer::reply(QTcpSocket* s, int code, const QByteArray& body, const QByteArray& extra) {
  if (!s || s->state() != QAbstractSocket::ConnectedState) return;
  const char* text = code == 200 ? "OK" : code == 202 ? "Accepted" : code == 400 ? "Bad Request" : code == 401 ? "Unauthorized"
                   : code == 403 ? "Forbidden" : code == 404 ? "Not Found" : code == 405 ? "Method Not Allowed" : "Error";
  QByteArray head = "HTTP/1.1 " + QByteArray::number(code) + " " + text + "\r\n";
  if (!body.isEmpty()) head += "Content-Type: application/json\r\n";
  head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n" + extra + "Cache-Control: no-store\r\n\r\n";
  s->write(head + body);
}

int McpServer::gate(const QByteArray& path, const QHash<QByteArray, QByteArray>& h) const {
  // only local programs that hold the token: no browser page (Origin), no DNS-rebound host name
  const QByteArray hostPort = h.value("host");
  const QByteArray host = hostPort.left(hostPort.indexOf(':') < 0 ? hostPort.size() : hostPort.indexOf(':'));
  if (h.contains("origin") || (host != "127.0.0.1" && host != "localhost")) return 403;
  if (!path.startsWith("/mcp/")) return 404;
  if (path == "/mcp/ext") {
    if (!externalEnabled()) return 404;
    const QByteArray want = "Bearer " + m_extToken.toLatin1(), got = h.value("authorization");
    unsigned char diff = got.size() == want.size() ? 0 : 1;
    for (int i = 0; i < want.size(); ++i) diff |= static_cast<unsigned char>(want[i] ^ (i < got.size() ? got[i] : 0));
    return diff ? 401 : 0;
  }
  const int env = path.mid(5).toInt() - 1;
  if (env < 0 || env >= Slots::kCount) return 404;
  // compared in constant time: how long a wrong token takes to refuse must not tell how much of it was right
  const QByteArray want = "Bearer " + m_tokens[env].toLatin1(), got = h.value("authorization");
  unsigned char diff = got.size() == want.size() ? 0 : 1;
  for (int i = 0; i < want.size(); ++i) diff |= static_cast<unsigned char>(want[i] ^ (i < got.size() ? got[i] : 0));
  if (m_tokens[env].isEmpty() || diff) return 401;
  return 0;
}

void McpServer::handle(QTcpSocket* s, const QByteArray& method, const QByteArray& path,
                       const QHash<QByteArray, QByteArray>& h, const QByteArray& body) {
  if (const int code = gate(path, h)) { reply(s, code, {}); return; }
  const bool ext = path == "/mcp/ext";
  const int env = ext ? -1 : path.mid(5).toInt() - 1;
  if (method == "DELETE") { reply(s, 200, {}); return; }
  if (method != "POST") { reply(s, 405, {}, "Allow: POST, DELETE\r\n"); return; }

  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) { reply(s, 400, rpcError(QJsonValue::Null, -32700, "parse error")); return; }
  const QJsonObject rq = doc.object();
  const QString m = rq.value("method").toString();
  if (!rq.contains("id")) { reply(s, 202, {}); return; }   // notifications (initialized, cancelled…)
  const QJsonValue id = rq.value("id");
  const QJsonObject params = rq.value("params").toObject();

  if (m == "initialize") {
    const QString v = params.value("protocolVersion").toString();
    reply(s, 200, rpcResult(id, QJsonObject{
        {"protocolVersion", v.isEmpty() ? QStringLiteral("2025-06-18") : v},
        {"capabilities", QJsonObject{{"tools", QJsonObject{{"listChanged", false}}}}},
        {"serverInfo", QJsonObject{{"name", "deepwater"}, {"version", "1.0"}}},
        {"instructions", ext ? "Deepwater's data, as the trading terminal open on this computer shows it (markets, news, calendar, economy, "
                               "positioning…). Quote figures as the tools give them, with their time; never invent a missing one."
                             : "Deepwater's data, as the trading terminal shows it. Only the tools this environment's skills grant."}}));
    return;
  }
  if (m == "ping") { reply(s, 200, rpcResult(id, {})); return; }
  if (m == "tools/list") {
    QJsonArray tools;
    for (const auto& t : Tools::instance().forSkills(ext ? kExtSkills : Slots::instance().skills(env)))
      tools.append(QJsonObject{{"name", t.name}, {"description", t.description}, {"inputSchema", t.parameters}});
    reply(s, 200, rpcResult(id, QJsonObject{{"tools", tools}}));
    return;
  }
  if (m == "tools/call" && ext) { handleExternalCall(s, id, params); return; }
  if (m == "tools/call") {
    const QString name = params.value("name").toString();
    const QJsonObject args = params.value("arguments").toObject();
    const QJsonObject call{{"name", name}, {"arguments", args}};
    QPointer<QTcpSocket> sock(s);
    const blind::Mask mask = maskFor(env);   // blind trials: dates, the market's name and price level masked both ways
    auto answer = [this, sock, id, env, call, mask](const ToolResult& r0) {
      const ToolResult r{r0.ok, blind::out(mask, r0.text)};
      if (Engine* e = Engines::instance().at(env)) e->recordTool(call, r);
      reply(sock, 200, rpcResult(id, QJsonObject{{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", r.text}}}},
                                                 {"isError", !r.ok}}));
    };
    // a call refused before it runs is answered without a "TOOL" line in the conversation (anything holding the token
    // could otherwise write lines there)
    auto refuse = [this, sock, id](const QString& why) {
      reply(sock, 200, rpcResult(id, QJsonObject{{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", why}}}}, {"isError", true}}));
    };
    Engine* e = Engines::instance().at(env);
    const QString toolGate = e ? e->toolGate() : QStringLiteral("refused: no such environment");
    const ToolDef* tool = Tools::instance().find(name);
    if (!toolGate.isEmpty()) { refuse(toolGate); return; }
    if (!tool) { refuse("error: there is no tool named \"" + name + "\""); return; }
    if (!Slots::instance().hasSkill(env, tool->skill)) {
      refuse("refused: this environment does not grant the skill \"" + tool->skill + "\" any more");
      return;
    }
    // the evaluation clock: a tool that only knows the present is refused, and the refusal is journaled
    const qint64 asOf = Slots::instance().asOf(env);
    if (const QString why = clockGate(*tool, asOf); !why.isEmpty()) { answer({false, why}); return; }
    auto once = std::make_shared<bool>(false);
    auto guarded = [answer, once, tool](const ToolResult& r) {
      if (*once) return;
      *once = true;
      answer({r.ok, r.ok ? wrapExternal(*tool, r.text) : r.text});   // third parties' text marked as data
    };
    const int ms = tool->timeoutMs;
    QTimer::singleShot(ms, this, [guarded, ms] {
      guarded({false, QString("error: the data did not arrive within %1 s").arg(ms / 1000)});
    });
    currentCaller() = ToolCaller{env, QStringLiteral("Claude Code"), asOf};
    e->beginTool(call);
    tool->run(blind::in(mask, args), guarded);
    return;
  }
  reply(s, 200, rpcError(id, -32601, "method not found: " + m));
}

void McpServer::handleExternalCall(QTcpSocket* s, const QJsonValue& id, const QJsonObject& params) {
  const QString name = params.value("name").toString();
  const QJsonObject args = params.value("arguments").toObject();
  QPointer<QTcpSocket> sock(s);
  auto send = [this, sock, id](bool ok, const QString& text) {
    reply(sock, 200, rpcResult(id, QJsonObject{{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", text}}}}, {"isError", !ok}}));
  };
  const ToolDef* tool = Tools::instance().find(name);
  if (!tool || !kExtSkills.contains(tool->skill)) { send(false, "error: there is no tool named \"" + name + "\""); return; }
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  while (!m_extTimes.isEmpty() && now - m_extTimes.first() > 60000) m_extTimes.removeFirst();
  if (m_extTimes.size() >= kExtPerMinute) {
    send(false, QString("refused: at most %1 Deepwater tool calls a minute; wait a little and answer with what you have read").arg(kExtPerMinute));
    return;
  }
  m_extTimes << now;
  ++m_extCalls;
  emit externalChanged();
  // the journal: which tool, its arguments, whether it answered (not the data itself)
  auto journal = [name, args](bool ok, int chars) {
    QFile f(paths::data("mcp") + "/journal.jsonl");
    paths::dropLink(f.fileName());
    if (f.open(QIODevice::Append)) {
      f.write(QJsonDocument(QJsonObject{{"at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}, {"tool", name}, {"arguments", args},
                                        {"ok", ok}, {"chars", chars}}).toJson(QJsonDocument::Compact) + "\n");
    }
  };
  auto once = std::make_shared<bool>(false);
  const ToolDef t = *tool;
  auto guarded = [send, once, t, journal](const ToolResult& r) {
    if (*once) return;
    *once = true;
    journal(r.ok, int(r.text.size()));
    send(r.ok, r.ok ? wrapExternal(t, r.text) : r.text);
  };
  const int ms = tool->timeoutMs;
  QTimer::singleShot(ms, this, [guarded, ms] { guarded({false, QString("error: the data did not arrive within %1 s").arg(ms / 1000)}); });
  currentCaller() = ToolCaller{-1, QStringLiteral("Claude (outside Deepwater)"), 0};
  tool->run(args, guarded);
}
}  // namespace ai
