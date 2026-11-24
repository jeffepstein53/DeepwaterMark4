#pragma once
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QList>
#include <functional>

// The tools Claude can call through Deepwater's MCP server (ai::McpServer): what Deepwater's pages show, read only.
// Each tool belongs to a skill; the MCP server offers the skill "read_data" and nothing else. No tool places an order,
// writes a file or reads a key or a password.
namespace ai {

struct ToolResult {
  bool ok = true;
  QString text;          // what Claude reads (plain text / small tables / JSON)
};
using ToolDone = std::function<void(const ToolResult&)>;

struct ToolDef {
  QString name;          // get_quotes
  QString skill;         // read_data
  QString description;   // for the model (English)
  QJsonObject parameters;   // JSON schema of the arguments
  std::function<void(const QJsonObject& args, ToolDone done)> run;   // GUI thread; may answer later (data loading)
  // How long ai::McpServer waits for the answer before giving Claude an error.
  int timeoutMs = 30000;
  // Third parties' text in the result (news, wire, banks' research): it may carry instructions written for a model. The
  // server wraps it (ai::wrapExternal) and its instructions say it is data, never instructions. No tool acts on
  // anything, so such text can mislead an answer but not choose an action.
  bool external = false;
};

class Tools {
public:
  static Tools& instance();
  void add(const ToolDef& t);
  const ToolDef* find(const QString& name) const;
  QList<ToolDef> forSkills(const QStringList& skillIds) const;
private:
  Tools();
  QList<ToolDef> m_tools;
};

void registerDataTools(Tools& t);       // skill read_data (src/ai/DataTools.cpp)
void registerOrderFlowTools(Tools& t);  // skill read_data, order flow (src/ai/OrderFlowTools.cpp)

// The moment a tool answers as of: always the present (0). The tools ask through here so that a future "as of a past
// date" mode would be one change, in one place.
qint64 asOfFor(const QString& name);
// The result as Claude gets it: an external tool's text between <external-text> marks (any such mark inside it
// defused), then a reminder that it is data.
QString wrapExternal(const ToolDef& t, const QString& text);
extern const char* const kExternalRule;   // the server's instructions' rule about those marks
QString asOfStamp(qint64 asOf);   // "2026-11-10 14:00 UTC"
// "refused: …" for a time asked after the moment `asOf` (never reached while asOfFor() is 0)
QString afterClock(qint64 asOf, const QString& what);
}  // namespace ai
