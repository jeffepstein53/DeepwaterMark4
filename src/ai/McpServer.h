#pragma once
#include <QObject>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QVector>

class QTcpServer;
class QTcpSocket;

// Deepwater's MCP server for the Claude Code integration (ai::claudecode): MCP "Streamable HTTP" with plain JSON replies,
// on 127.0.0.1 only, on a free port chosen at the first use. One endpoint per environment, /mcp/<n>, each with its own
// random token (new at every app launch, written only to that environment's private mcp.json, mode 0600).
//
// What it offers is the cube's permission and nothing else: tools/list = the tools of the skills the environment grants
// now (ai::Tools::forSkills); tools/call checks the skill again, refuses when no question of that environment is being
// answered by Claude Code, caps the calls per question, and hands each call to ai::Engine::recordTool (chat line +
// tool journal). Requests with an Origin header (browsers) or a Host other than 127.0.0.1/localhost are refused; the
// token is compared in constant time; third parties' text in a result is marked as data (ai::wrapExternal, 2026-11-20).
//
// Outside Deepwater (2026-11-21, user's request "a Deepwater MCP usable in Claude"): Claude Code and Claude Desktop
// started by the user, not by a cube. Off by default (ai.mcp.external). When on, /mcp/ext answers with its own key,
// written with the port to <data>/mcp/connection.json (mode 0600, new at each launch); the clients do not hold the key:
// they start `Deepwater --mcp-stdio` (ai/McpBridge.h), which reads that file and relays each JSON-RPC line here. Only
// the read_data skill (what the pages show; never the Sources and AI pages, keys or passwords), at most kExtPerMinute
// calls a minute, each written to <data>/mcp/journal.jsonl. ML and backtests stay with the cubes (they are named after
// an environment).
namespace ai {

class McpServer : public QObject {
  Q_OBJECT
public:
  static McpServer& instance();
  // Starts listening if needed and writes <env folder>/mcp.json for `claude --mcp-config`; empty on failure.
  QString writeConfig(int env);
  quint16 port() const;

  // outside Claude clients
  static constexpr int kExtPerMinute = 30;
  bool externalEnabled() const;
  void setExternalEnabled(bool on);   // saved; on: listens and writes connection.json, off: removes it
  void startExternal();               // at launch: listens when the setting is on
  static QString connectionFile();    // <data>/mcp/connection.json
  int externalCalls() const { return m_extCalls; }   // since launch
signals:
  void externalChanged();

private:
  McpServer();
  bool listen();
  void onReadable(QTcpSocket* s);
  // 0 when a request may go on, else the HTTP status to refuse it with: decided from the head alone, before any body
  int gate(const QByteArray& path, const QHash<QByteArray, QByteArray>& headers) const;
  void handle(QTcpSocket* s, const QByteArray& method, const QByteArray& path, const QHash<QByteArray, QByteArray>& headers,
              const QByteArray& body);
  void reply(QTcpSocket* s, int code, const QByteArray& body, const QByteArray& extraHeaders = {});
  QTcpServer* m_srv = nullptr;
  QVector<QString> m_tokens;
  QString m_extToken;
  QList<qint64> m_extTimes;   // the last minute's external calls (ms)
  int m_extCalls = 0;
  bool writeConnection();
  void handleExternalCall(QTcpSocket* s, const QJsonValue& id, const QJsonObject& params);
  QHash<QTcpSocket*, QByteArray> m_buf;
};
}  // namespace ai
