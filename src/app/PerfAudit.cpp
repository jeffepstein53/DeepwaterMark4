#include "app/PerfAudit.h"
#include "app/MainWindow.h"
#include "app/Pages.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QPointer>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif
#ifdef __APPLE__
#include <mach/mach.h>
#endif

namespace perfaudit {
namespace {

double cpuSeconds() {
#ifdef _WIN32   // the Windows port: getrusage does not exist there
  FILETIME created, exited, kernel, user;
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0;
  auto sec = [](const FILETIME& f) { return double((quint64(f.dwHighDateTime) << 32) | f.dwLowDateTime) / 1e7; };
  return sec(kernel) + sec(user);
#else
  rusage u{};
  getrusage(RUSAGE_SELF, &u);
  return u.ru_utime.tv_sec + u.ru_utime.tv_usec / 1e6 + u.ru_stime.tv_sec + u.ru_stime.tv_usec / 1e6;
#endif
}

double rssMB() {
#ifdef __APPLE__
  task_vm_info_data_t info{};
  mach_msg_type_number_t n = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &n) == KERN_SUCCESS)
    return info.phys_footprint / (1024.0 * 1024.0);
#elif defined(_WIN32)
  PROCESS_MEMORY_COUNTERS_EX m{};
  m.cb = sizeof m;
  if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m), sizeof m)) return m.PrivateUsage / (1024.0 * 1024.0);
#endif
  return 0;
}

// GUI-thread responsiveness: a 5 ms timer; how late it fires is how long the event loop was busy.
struct StallMeter : QObject {
  QTimer timer;
  QElapsedTimer clock;
  qint64 last = 0, worst = 0, lostOver50 = 0;
  int over50 = 0;
  StallMeter() {
    timer.setTimerType(Qt::PreciseTimer);
    timer.setInterval(5);
    QObject::connect(&timer, &QTimer::timeout, this, [this] {
      const qint64 now = clock.elapsed();
      const qint64 late = now - last - 5;
      last = now;
      if (late > worst) worst = late;
      if (late > 50) { ++over50; lostOver50 += late; }
    });
  }
  void start() { worst = lostOver50 = 0; over50 = 0; clock.start(); last = 0; timer.start(); }
  void stop() { timer.stop(); }
};

struct FirstPaint : QObject {
  QElapsedTimer* clock;
  qint64 at = -1;
  explicit FirstPaint(QElapsedTimer* c) : clock(c) {}
  bool eventFilter(QObject*, QEvent* e) override {
    if (e->type() == QEvent::Paint && at < 0) at = clock->elapsed();
    return false;
  }
};

void settle(int ms) {
  QEventLoop l;
  QTimer::singleShot(ms, &l, &QEventLoop::quit);
  l.exec();
}

}  // namespace

void run(MainWindow* w, const QString& dir, const QStringList& onlyPages, int loadMs, int idleMs) {
  QDir().mkpath(dir);
  QStringList ids;
  for (const auto& t : app::tabs())
    for (const auto& s : t.subs) ids << app::pageId(t.id, s.id);
  if (!onlyPages.isEmpty()) ids = onlyPages;
  QTimer::singleShot(1500, w, [w, dir, ids, loadMs, idleMs] {
    QFile tsv(dir + "/perf.tsv");
    if (!tsv.open(QIODevice::WriteOnly | QIODevice::Truncate)) { qApp->quit(); return; }
    QTextStream out(&tsv);
    out << "page\tbuild_ms\tpaint_ms\tload_worst_stall_ms\tload_stalls_over50\tload_lost_ms\tload_cpu_pct\t"
           "idle_cpu_pct\tidle_worst_stall_ms\trss_mb\n";
    StallMeter meter;
    const double rss0 = rssMB();
    for (const QString& id : ids) {
      QElapsedTimer clock;
      clock.start();
      FirstPaint fp(&clock);
      qApp->installEventFilter(&fp);   // the page widget doesn't exist before openPage: watch every paint, keep the page's
      meter.start();
      const double cpuLoad0 = cpuSeconds();
      if (!w->openPage(id)) { qApp->removeEventFilter(&fp); out << id << "\tunknown\n"; continue; }
      const qint64 build = clock.elapsed();
      QPointer<QWidget> page = w->currentPage();
      qApp->removeEventFilter(&fp);
      FirstPaint fpPage(&clock);
      if (page) page->installEventFilter(&fpPage);
      settle(loadMs);
      const double loadCpu = (cpuSeconds() - cpuLoad0) / ((clock.elapsed()) / 1000.0) * 100.0;
      const qint64 loadWorst = meter.worst, loadLost = meter.lostOver50;
      const int loadOver = meter.over50;
      if (page) page->removeEventFilter(&fpPage);
      meter.start();
      const double cpuIdle0 = cpuSeconds();
      QElapsedTimer idle;
      idle.start();
      settle(idleMs);
      const double idleCpu = (cpuSeconds() - cpuIdle0) / (idle.elapsed() / 1000.0) * 100.0;
      const qint64 idleWorst = meter.worst;
      meter.stop();
      out << id << '\t' << build << '\t' << (fpPage.at >= 0 ? fpPage.at : -1) << '\t' << loadWorst << '\t' << loadOver << '\t'
          << loadLost << '\t' << QString::number(loadCpu, 'f', 1) << '\t' << QString::number(idleCpu, 'f', 1) << '\t'
          << idleWorst << '\t' << QString::number(rssMB(), 'f', 0) << '\n';
      out.flush();
    }
    // every page created and kept, the lightest one shown: what the hidden pages cost together
    const QString light = ids.contains("sources/sources-graph") ? "sources/sources-graph" : ids.first();
    w->openPage(light);
    settle(3000);
    meter.start();
    const double h0 = cpuSeconds();
    QElapsedTimer hidden;
    hidden.start();
    settle(std::max(idleMs, 15000));
    const double hiddenCpu = (cpuSeconds() - h0) / (hidden.elapsed() / 1000.0) * 100.0;
    meter.stop();
    QFile rep(dir + "/report.txt");
    if (!rep.open(QIODevice::WriteOnly | QIODevice::Truncate)) { qApp->quit(); return; }
    QTextStream r(&rep);
    r << "rss at start of audit: " << QString::number(rss0, 'f', 0) << " MB\n";
    r << "all pages created, " << light << " shown: hidden cpu " << QString::number(hiddenCpu, 'f', 1)
      << " %, worst stall " << meter.worst << " ms, rss " << QString::number(rssMB(), 'f', 0) << " MB\n";
    rep.close();
    tsv.close();
    qApp->quit();
  });
}

}  // namespace perfaudit
