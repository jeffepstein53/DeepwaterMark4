#include "app/Zoom.h"
#include "core/Settings.h"
#include "ui/Kit.h"
#include "core/Process.h"
#include <QCoreApplication>
#include <QMessageBox>
#include <QTimer>
#include <QWidget>
#include <algorithm>

namespace zoom {
namespace {
int g_active = 100;
int clampLevel(int v) { return levels().contains(v) ? v : 100; }
}

const QList<int>& levels() { static const QList<int> l{50, 75, 100, 125, 150}; return l; }
int saved() { return clampLevel(settings::value("ui.zoom", 100).toInt()); }
int active() { return g_active; }

void installSaved() {
#ifndef Q_OS_IOS
  if (qEnvironmentVariableIsSet("QT_SCALE_FACTOR")) return;   // set by hand: it wins (and active() stays 100)
  g_active = saved();
  if (g_active != 100) qputenv("QT_SCALE_FACTOR", QByteArray::number(g_active / 100.0));
#endif
}

void restart() {
#if QT_CONFIG(process)
  QStringList args = QCoreApplication::arguments().mid(1);
  // a test's one-shot flags must not run again in the new instance
  for (const char* once : {"--screenshot", "--quit-after", "--layout-audit", "--perf-audit"}) {
    const int i = int(args.indexOf(QString::fromLatin1(once)));
    if (i >= 0) args.remove(i, std::min<qsizetype>(2, args.size() - i));
  }
  if (dw::Process::startDetached(QCoreApplication::applicationFilePath(), args)) QTimer::singleShot(0, qApp, &QCoreApplication::quit);
#endif
}

void choose(QWidget* parent, int percent) {
  percent = clampLevel(percent);
  if (percent == saved()) return;
  settings::setValue("ui.zoom", percent);
  if (percent == active()) return;   // back to the level this launch runs at: nothing to restart
  const auto r = ui::askPlain(parent, QCoreApplication::translate("options", "Zoom"),
                              QCoreApplication::translate("options", "The interface will be shown at %1 % after Deepwater restarts. Restart now?").arg(percent),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
  if (r == QMessageBox::Yes) restart();
}

void step(QWidget* parent, int dir) {
  const int cur = saved();
  if (dir == 0) { choose(parent, 100); return; }
  const int i = int(levels().indexOf(cur)) + (dir > 0 ? 1 : -1);
  if (i >= 0 && i < levels().size()) choose(parent, levels()[i]);
}
}  // namespace zoom
