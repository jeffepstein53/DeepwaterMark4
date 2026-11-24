#include "ai/ClaudeCode.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace ai::claudecode {

QProcessEnvironment environment() {
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  const QString home = QDir::homePath();
  // ';' on Windows, ':' elsewhere (a ':' split cut "C:\…" entries in two)
  QStringList path = env.value("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts);
#ifdef Q_OS_WIN
  const QStringList extra{home + "/.local/bin", qEnvironmentVariable("APPDATA") + "/npm"};
#else
  const QStringList extra{home + "/.local/bin", home + "/.claude/local", "/opt/homebrew/bin", "/usr/local/bin", "/usr/bin", "/bin"};
#endif
  for (const QString& d : extra)
    if (!path.contains(d)) path << d;
  env.insert("PATH", path.join(QDir::listSeparator()));
  return env;
}

QString binary() {
  const QString home = QDir::homePath();
  for (const QString& p : {home + "/.local/bin/claude", home + "/.local/bin/claude.exe", home + "/.claude/local/claude",
                           QString("/opt/homebrew/bin/claude"), QString("/usr/local/bin/claude")}) {
    const QFileInfo f(p);
    if (f.exists() && f.isExecutable()) return f.absoluteFilePath();
  }
  return QStandardPaths::findExecutable("claude", environment().value("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts));
}
}  // namespace ai::claudecode
