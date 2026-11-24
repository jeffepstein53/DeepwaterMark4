#pragma once
#include <QObject>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QString>

class QTcpServer;
class QTcpSocket;

// Deepwater's MCP server for Claude Code and Claude Desktop (user's request 2026-11-21: "a Deepwater MCP usable in Claude").
// MCP "Streamable HTTP" with plain JSON replies, on 127.0.0.1 only, on a free port. Off by default (ai.mcp.external, the
// Options dialog's Claude section). When on, /mcp/ext answers with its own key, written with the port to
// connectionFile() (new at each launch, in the user's own profile folder, outside %APPDATA%); the clients do not hold
// the key: they start `Deepwater --mcp-stdio` (ai/McpBridge.h), which reads that file and relays each JSON-RPC line here.
//
// What it offers: the tools of the skill read_data (what the pages show; never keys or passwords), read only, at most
// kExtPerMinute calls a minute, each written to <data>/mcp/journal.jsonl (which tool, its arguments; not the data). The
// trading journal's tool (get_track_record) is left out unless the user switched it on (ai.mcp.journal). Requests with an
// Origin header (browsers) or a Host other than 127.0.0.1/localhost are refused; the key is compared in constant time;
// third parties' text in a result is marked as data (ai::wrapExternal).
namespace ai {

class McpServer : public QObject {
  Q_OBJECT
public:
  static McpServer& instance();
  static constexpr int kExtPerMinute = 30;
  bool externalEnabled() const;
  void setExternalEnabled(bool on);   // saved; on: listens and writes connectionFile(), off: removes it
  bool journalIncluded() const;       // the trading journal's tool is offered too
  void setJournalIncluded(bool on);   // saved
  void startExternal();               // at launch: listens when the setting is on
  quint16 port() const;
  // <profile folder>/.deepwater-mark4/mcp/connection.json; with DW_PROFILE set (tests): <DW_PROFILE>/mcp/connection.json.
  // Not under %APPDATA%: Claude Desktop is a packaged Windows app that starts the bridge in its own sandbox, where
  // %APPDATA% is a private copy.
  static QString connectionFile();
  int externalCalls() const { return m_extCalls; }   // since launch
signals:
  void externalChanged();

private:
  McpServer();
  bool listen();
  bool writeConnection();
  void onReadable(QTcpSocket* s);
  // 0 when a request may go on, else the HTTP status to refuse it with: decided from the head alone, before any body
  int gate(const QByteArray& path, const QHash<QByteArray, QByteArray>& headers) const;
  void handle(QTcpSocket* s, const QByteArray& method, const QByteArray& path, const QHash<QByteArray, QByteArray>& headers,
              const QByteArray& body);
  void reply(QTcpSocket* s, int code, const QByteArray& body, const QByteArray& extraHeaders = {});
  void handleCall(QTcpSocket* s, const QJsonValue& id, const QJsonObject& params);
  bool offered(const QString& toolName) const;
  QTcpServer* m_srv = nullptr;
  QString m_extToken;
  QList<qint64> m_extTimes;   // the last minute's calls (ms)
  int m_extCalls = 0;
  QHash<QTcpSocket*, QByteArray> m_buf;
};
}  // namespace ai
