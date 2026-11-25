#pragma once
#include <QLockFile>
#include <QString>
#include <memory>

// One Deepwater per profile (0.4.3). Two windows on the same data folder wrote to the same journal and settings, and each
// one's Claude access and background refresh doubled the requests to every source. The first Deepwater takes a lock file in
// its data folder; a second one on the same folder finds it taken and says so instead of opening.
//
// The lock belongs to the process: when it ends, however it ends (closed, killed, crashed), the next Deepwater finds it free,
// so an update's relaunch (app/Updater waits for the old process to end before it starts the new one) is never refused. A
// test profile (DW_PROFILE) has its own folder, so the audits and tests still run beside a Deepwater in use.
namespace app::singleinstance {

// The lock for `dataDir`, or nullptr when another Deepwater holds it. `waitMs`: how long to wait for a Deepwater that is
// just ending to let go. The returned lock is kept until the application ends.
inline std::unique_ptr<QLockFile> acquire(const QString& dataDir, int waitMs = 0) {
  auto lock = std::make_unique<QLockFile>(dataDir + "/instance.lock");
  lock->setStaleLockTime(0);   // never "old": only the process's end frees it (Qt's default 30 s would take it from a live one)
  if (!lock->tryLock(waitMs)) return nullptr;
  return lock;
}
}  // namespace app::singleinstance
