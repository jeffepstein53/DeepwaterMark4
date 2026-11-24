#pragma once
// dw::Process: the app's child processes (the translation helper). QProcess wherever Qt has it; on iOS (2026-11-21), where an app cannot start another program,
// a stand-in with the same calls whose start() always fails with FailedToStart, so every feature that needs a process
// takes the path it already has for "the program could not start" instead of the app not building.
#include <QtGlobal>
#include <QByteArray>
#include <QObject>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QTimer>

#if QT_CONFIG(process)
#include <QProcess>
namespace dw { using Process = QProcess; }
#else
namespace dw {
class Process : public QObject {
  Q_OBJECT
public:
  enum ProcessError { FailedToStart, Crashed, Timedout, ReadError, WriteError, UnknownError };
  enum ProcessState { NotRunning, Starting, Running };
  enum ExitStatus { NormalExit, CrashExit };
  enum ProcessChannelMode { SeparateChannels, MergedChannels, ForwardedChannels };
  explicit Process(QObject* parent = nullptr) : QObject(parent) {}
  void setProgram(const QString& p) { m_program = p; }
  QString program() const { return m_program; }
  void setArguments(const QStringList& a) { m_args = a; }
  void setProcessEnvironment(const QProcessEnvironment&) {}
  void setWorkingDirectory(const QString&) {}
  void setProcessChannelMode(ProcessChannelMode) {}
  void setStandardErrorFile(const QString&) {}
  void setStandardOutputFile(const QString&) {}
  void start(const QString& program, const QStringList& args = {}) { m_program = program; m_args = args; start(); }
  void start() { QTimer::singleShot(0, this, [this] { emit errorOccurred(FailedToStart); }); }
  bool waitForStarted(int = 30000) { return false; }
  bool waitForFinished(int = 30000) { return false; }
  bool waitForReadyRead(int = 30000) { return false; }
  bool waitForBytesWritten(int = 30000) { return false; }
  void kill() {}
  void terminate() {}
  void closeWriteChannel() {}
  qint64 write(const QByteArray&) { return -1; }
  QByteArray readAll() { return {}; }
  QByteArray readAllStandardOutput() { return {}; }
  QByteArray readAllStandardError() { return {}; }
  ProcessState state() const { return NotRunning; }
  ProcessError error() const { return FailedToStart; }
  int exitCode() const { return -1; }
  ExitStatus exitStatus() const { return CrashExit; }
  qint64 processId() const { return 0; }
  static QString nullDevice() { return QStringLiteral("/dev/null"); }
  static bool startDetached(const QString&, const QStringList& = {}, const QString& = {}, qint64* = nullptr) { return false; }
signals:
  void started();
  void finished(int exitCode, dw::Process::ExitStatus status);
  void errorOccurred(dw::Process::ProcessError error);
  void readyRead();
  void readyReadStandardOutput();
  void readyReadStandardError();
  void stateChanged(dw::Process::ProcessState state);
private:
  QString m_program;
  QStringList m_args;
};
}  // namespace dw
#endif
