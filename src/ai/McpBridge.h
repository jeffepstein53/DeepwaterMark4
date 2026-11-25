#pragma once
// `Deepwater --mcp-stdio`: the MCP server Claude Code and Claude Desktop start (2026-11-21). No window, no Dock icon:
// it reads JSON-RPC messages from its standard input, one per line (MCP's stdio transport), and writes the answers to its
// standard output. The handshake (initialize, tools/list, ping) is answered here, Deepwater open or not, so the client always
// has the tools; each tools/call is posted to the Deepwater that is open on this computer (ai::McpServer, /mcp/ext, port and
// key from McpServer::connectionFile(), re-read for every call so an app opened or restarted meanwhile is found). When the
// app is closed or Claude's access is off (Options › Claude), the call answers an error that says so; it never
// starts the app. Diagnostics go to standard error (Claude's MCP log).
#include <QString>
#include <QStringList>
#include <functional>

namespace ai::mcpbridge {
bool wanted(int argc, char** argv);   // --mcp-stdio among the arguments
int run(int argc, char** argv);       // runs until standard input ends; the exit code

// Declaring the bridge to the clients (Options › Claude buttons), under the name "deepwater":
//   Claude Code: `claude mcp add --scope user deepwater -- <this program> --mcp-stdio` (its own command, so its config
//   stays its own); Claude Desktop: the "deepwater" entry of claude_desktop_config.json's mcpServers, in every copy of that
//   file the machine has (ClaudeDesktopFiles.h), each copied to claude_desktop_config.json.deepwater-backup before the
//   first change. Neither holds a key.
QString program();                    // this program's path, as the clients start it
bool inClaudeCode();                  // ~/.claude.json declares "deepwater" (user scope)
void setInClaudeCode(bool on, std::function<void(bool ok, QString error)> done);   // runs `claude mcp add/remove`
// Claude Desktop's config files; empty when Claude Desktop is not installed. A Claude Desktop installed as a packaged Windows
// app (Claude_*) has two homes for it, under %LOCALAPPDATA%\Packages\Claude_*\LocalCache\Roaming\Claude and under
// %LOCALAPPDATA%\Claude-Data\claude_*: both are written (the log of Claude Desktop does not say which one a version reads).
QStringList claudeDesktopConfigs();
bool inClaudeDesktop();
bool setInClaudeDesktop(bool on, QString* error);
}
