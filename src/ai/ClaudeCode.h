#pragma once
#include <QString>
#include <QProcessEnvironment>

// Claude Code (Anthropic's command-line agent) as Deepwater finds it on this computer: only what registering Deepwater's
// MCP server needs (ai/McpBridge.h). Deepwater never holds a key for it: the `claude` program runs with the user's own
// sign-in.
namespace ai::claudecode {
QString binary();                       // the `claude` program, searched where its installers put it; empty if none
QProcessEnvironment environment();      // PATH completed for a GUI app
}  // namespace ai::claudecode
