#pragma once
// `Deepwater --mcp-stdio`: the MCP server Claude Code and Claude Desktop start (2026-11-21). No window, no Dock icon:
// it reads JSON-RPC messages from its standard input, one per line (MCP's stdio transport), posts each to the Deepwater
// that is open on this computer (ai::McpServer, /mcp/ext, port and key from <data>/mcp/connection.json, re-read for
// every message so an app restarted meanwhile is found again) and writes the answers to its standard output. When the
// app is closed or "Claude outside Deepwater" is off, requests get a JSON-RPC error that says so; it never starts the
// app. Diagnostics go to standard error (Claude's MCP log).
#include <QString>
#include <functional>

namespace ai::mcpbridge {
bool wanted(int argc, char** argv);   // --mcp-stdio among the arguments
int run(int argc, char** argv);       // runs until standard input ends; the exit code

// Declaring the bridge to the clients (AI › LLM › Integration buttons), under the name "deepwater":
//   Claude Code: `claude mcp add --scope user deepwater -- <this program> --mcp-stdio` (its own command, so its config
//   stays its own); Claude Desktop: the "deepwater" entry of claude_desktop_config.json's mcpServers, the file copied to
//   claude_desktop_config.json.deepwater-backup before the first change. Neither holds a key.
QString program();                    // this program's path, as the clients start it
bool inClaudeCode();                  // ~/.claude.json declares "deepwater" (user scope)
void setInClaudeCode(bool on, std::function<void(bool ok, QString error)> done);   // runs `claude mcp add/remove`
QString claudeDesktopConfig();        // its config file's path; empty when Claude Desktop is not installed
bool inClaudeDesktop();
bool setInClaudeDesktop(bool on, QString* error);
}
