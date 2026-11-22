#include "ai/Engine.h"
#include "ai/Blind.h"
#include <QCryptographicHash>
#include "ai/ClaudeTranscripts.h"
#include "core/Paths.h"
#include "ai/ModelHub.h"
#include "ai/Slots.h"
#include "ai/Tools.h"
#include "ai/ClaudeCode.h"
#include "ai/McpServer.h"
#include <QUuid>
#include "core/I18n.h"
#include "core/Settings.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QFile>
#include <QTimer>
#include <memory>
#ifdef __APPLE__
#include <unistd.h>
#endif

namespace ai {
namespace {
QString sbQuote(const QString& s) {   // a path inside a sandbox profile string
  QString o = s;
  o.replace('\\', "\\\\").replace('"', "\\\"");
  return '"' + o + '"';
}
// The answer as the model should see it again: without its reasoning block (<think>…</think>), as chat templates expect.
// The <tool_call>{"name":…,"arguments":{…}}</tool_call> blocks of an answer (an unclosed last block counts too).
QList<QJsonObject> toolCalls(const QString& answer) {
  QList<QJsonObject> out;
  int from = 0;
  for (;;) {
    const int a = int(answer.indexOf("<tool_call>", from));
    if (a < 0) break;
    const int b = int(answer.indexOf("</tool_call>", a));
    const QString body = answer.mid(a + 11, b < 0 ? -1 : b - a - 11).trimmed();
    QJsonParseError err;
    QJsonObject o = QJsonDocument::fromJson(body.toUtf8(), &err).object();
    if (o.value("arguments").isString())   // some models send the arguments as a JSON string
      o["arguments"] = QJsonDocument::fromJson(o.value("arguments").toString().toUtf8()).object();
    if (o.isEmpty()) o = QJsonObject{{"name", QString()}, {"raw", body.left(300)}};
    out << o;
    if (b < 0) break;
    from = b + 12;
  }
  return out;
}
// The conversation keeps its latest messages only: it is written whole to conversation.json at every event (each tool
// result, each answer) and sent whole to the model, and a model in a tool loop adds thousands of characters a round.
constexpr int kKeepMessages = 400;
constexpr qsizetype kKeepChars = 1000000;
void trimConversation(QVector<ChatMessage>& msgs) {
  qsizetype chars = 0;
  for (const auto& m : msgs) chars += m.text.size();
  int drop = 0;
  while (drop < msgs.size() && (msgs.size() - drop > kKeepMessages || chars > kKeepChars)) chars -= msgs[drop++].text.size();
  // from a question on: an answer or a tool result without the question it belongs to would mislead the model
  if (drop > 0) while (drop < msgs.size() && msgs[drop].role != "user") ++drop;
  if (drop > 0) msgs.remove(0, drop);
}
QString withoutThinking(const QString& t) {
  static const QRegularExpression re("<think>.*?</think>\\s*", QRegularExpression::DotMatchesEverythingOption);
  QString o = t;
  o.remove(re);
  const int open = int(o.indexOf("<think>"));
  if (open >= 0) o.truncate(open);   // an answer stopped while still thinking
  return o.trimmed();
}
}  // namespace

QString stateLabel(Engine::State s) {
  switch (s) {
    case Engine::State::Off: return QCoreApplication::translate("ai", "STANDBY");
    case Engine::State::Starting: return QCoreApplication::translate("ai", "LOADING");
    case Engine::State::Ready: return QCoreApplication::translate("ai", "READY");
    case Engine::State::Busy: return QCoreApplication::translate("ai", "ANSWERING");
    case Engine::State::Failed: return QCoreApplication::translate("ai", "FAILED");
  }
  return {};
}

Engine::Engine(int index, QObject* parent) : QObject(parent), m_index(index) {
  load();
  // The evaluation clock changed (on, off, another date): what the model read before must not follow it, or a
  // conversation begun in the present would carry the future into the evaluation. A "clock" message marks the change;
  // the model starts afresh after it (a new Claude Code session; a local model is sent only the messages after it).
  connect(&Slots::instance(), &Slots::asOfChanged, this, [this](int i) {
    if (i != m_index) return;
    if (m_state == State::Busy || m_pending) cancel();
    syncClock();
  });
}

// The clock the conversation was held under (the last "clock" message's, 0 = the present) against the environment's
// now: when they differ, a "clock" message and a new Claude Code session. Also called at each question, so that it holds
// whatever the order in which the clock and this engine were set up.
void Engine::syncClock(bool force) {
  const qint64 now = Slots::instance().asOf(m_index);
  const double blindNow = now > 0 ? double(Slots::instance().blindSeed(m_index)) : 0;   // blind or named: what it saw differs
  qint64 held = 0;
  double heldBlind = 0;
  for (const auto& m : m_messages)
    if (m.role == "clock") { held = qint64(m.stats.value("as_of").toDouble()); heldBlind = m.stats.value("blind").toDouble(); }
  if (!force && held == now && heldBlind == blindNow) return;
  setClaudeSession(QUuid::createUuid().toString(QUuid::WithoutBraces), false);
  m_messages.append({"clock", {}, {}, QDateTime::currentDateTime(), QJsonObject{{"as_of", double(now)}, {"blind", blindNow}}});
  save();
  emit messagesChanged();
}
Engine::~Engine() { stop(true); }

void Engine::appendNote(const QString& role, const QString& text) {
  m_messages.append({role, text, {}, QDateTime::currentDateTime(), {}});
  save();
  emit messagesChanged();
}

void Engine::setState(State s, const QString& err) {
  if (s == State::Failed) m_pending = false;   // nothing will answer it; ask() takes a new question again
  if (s == m_state && err == m_error) return;
  m_state = s; m_error = err;
  emit stateChanged();
}

QString Engine::profile(const QString& helper, const QString& model) const {
  const QString bundle = QFileInfo(QCoreApplication::applicationDirPath() + "/../..").canonicalFilePath();
  const QString env = QFileInfo(Slots::instance().folder(m_index)).canonicalFilePath();
  QStringList p;
  p << "(version 1)"
    << "(deny default)"
    << "(import \"system.sb\")"
    << "(allow process-exec (literal " + sbQuote(helper) + "))"
    // the bundle is read by the system frameworks (Metal looks up the main bundle), the model file by the helper
    << "(allow file-read* (subpath " + sbQuote(bundle) + ") (literal " + sbQuote(model) + "))"
    // nothing in the environment folder: the runner writes no file (it answers on stdout), and a folder it could write
    // would let it plant links the unsandboxed app then follows (conversation.json, the journal, mcp.json and its token)
    << "(allow iokit-open (iokit-user-client-class \"AGXDeviceUserClient\") (iokit-user-client-class \"IOSurfaceRootUserClient\"))";
#ifdef __APPLE__
  // Metal's shader cache (compiled GPU shaders only), kept under the app's id in the user cache folder: without it every
  // start recompiles ggml's shaders (~6 s instead of 0.2 s)
  char buf[1024] = {0};
  if (confstr(_CS_DARWIN_USER_CACHE_DIR, buf, sizeof buf) > 0) {
    const QString root = QFileInfo(QString::fromUtf8(buf)).canonicalFilePath();
    const QString app = root + "/com.deepwater-terminal.Deepwater";
    QStringList sub;
    for (const char* d : {"com.apple.metal", "com.apple.metalfe", "com.apple.gpuarchiver"}) {
      QDir().mkpath(app + "/" + d);
      sub << "(subpath " + sbQuote(app + "/" + d) + ")";
    }
    if (!root.isEmpty()) {
      p << "(allow file-read-metadata (literal " + sbQuote(root) + ") (literal " + sbQuote(app) + "))"
        << "(allow file-read* file-write* " + sub.join(' ') + ")";
    }
  }
#endif
  return p.join('\n');
}

bool Engine::isClaude() const {
  const Slot& s = Slots::instance().slot(m_index);
  return claudecode::isKey(s.repo, s.file);
}

void Engine::start() {
  const Slot& s = Slots::instance().slot(m_index);
  if (s.isEmpty()) { stop(); return; }
  if (isClaude()) {   // nothing stays in memory: each question runs its own `claude -p`
    if (m_proc) stop();
    m_modelFile = s.displayName();
    if (m_cc) return;
    if (claudecode::binary().isEmpty()) {
      setState(State::Failed, QCoreApplication::translate("ai", "Claude Code is not installed on this computer: see AI › LLM › Integration."));
      return;
    }
    if (m_device.isEmpty()) m_device = "Claude Code";
    setState(State::Ready);
    return;
  }
  if (m_cc) stop();
  QString path;
  for (const auto& m : ModelHub::instance().installed()) if (m.repo == s.repo && m.file == s.file) path = m.path;
  const QString model = QFileInfo(path).canonicalFilePath();
  if (m_proc && m_modelPath == model && m_state != State::Failed) return;
  // a question asked while the model was off waits for it: stop() (cleaning the previous state) must not drop it,
  // or the loaded model never receives it
  const bool pending = m_pending;
  stop();
  m_pending = pending;
  m_modelFile = s.file;
  m_modelPath = model;
  if (model.isEmpty()) { setState(State::Failed, QCoreApplication::translate("ai", "The model file is missing: download it again in AI › LLM › Local models.")); return; }

  // the model stopped before is still freeing its memory: load this one once it is gone (two models at once could
  // exceed the memory; stop() used to block the app until then)
  if (!m_exiting.isEmpty()) { m_startWhenFree = true; setState(State::Starting); return; }
#ifdef _WIN32
  const QString helper = QFileInfo(QCoreApplication::applicationDirPath() + "/deepwater-llm.exe").canonicalFilePath();
#else
  const QString helper = QFileInfo(QCoreApplication::applicationDirPath() + "/deepwater-llm").canonicalFilePath();
#endif
  if (helper.isEmpty()) { setState(State::Failed, QCoreApplication::translate("ai", "The model runner (deepwater-llm) is missing from the app.")); return; }
  QDir().mkpath(Slots::instance().folder(m_index));

  m_proc = new dw::Process(this);
  m_buf.clear(); m_err.clear(); m_device.clear(); m_ctx = 0;
  connect(m_proc, &dw::Process::readyReadStandardOutput, this, &Engine::onStdout);
  connect(m_proc, &dw::Process::readyReadStandardError, this, [this] {
    m_err += m_proc->readAllStandardError();
    if (m_err.size() > 8192) m_err = m_err.right(8192);
  });
  connect(m_proc, &dw::Process::finished, this, &Engine::onFinished);
  connect(m_proc, &dw::Process::errorOccurred, this, [this](dw::Process::ProcessError e) {
    if (e == dw::Process::FailedToStart) setState(State::Failed, QCoreApplication::translate("ai", "The model runner could not start."));
  });
  m_proc->setWorkingDirectory(Slots::instance().folder(m_index));
  // a clean environment, as the ML trainer gets: the app's own may hold tokens or settings a model has no use for
  QProcessEnvironment pe;
  pe.insert("PATH", "/usr/bin:/bin");
  pe.insert("HOME", Slots::instance().folder(m_index));
  pe.insert("LANG", "en_US.UTF-8");
  m_proc->setProcessEnvironment(pe);
  const int ctx = qBound(1024, settings::value("ai.ctx", 16384).toInt(), 131072);
  const QStringList args{"--model", model, "--ctx", QString::number(ctx)};
  setState(State::Starting);
#ifdef __APPLE__
  m_proc->start("/usr/bin/sandbox-exec", QStringList{"-p", profile(helper, model), helper} + args);
#else
  m_proc->start(helper, args);   // other systems: no sandbox yet
#endif
}

// A stopped process exits on its own (the helper when its input closes, `claude` on SIGTERM); it is killed if it is
// still there after 1.5 s. Waiting for that on the GUI thread froze the app up to 2.5 s on EJECT while the helper freed
// a multi-GB model: only the app's quit still waits.
void Engine::retire(dw::Process* p, bool wait) {
  if (wait) {
    if (!p->waitForFinished(1500)) { p->kill(); p->waitForFinished(1000); }
    p->deleteLater();
    return;
  }
  if (p->state() == dw::Process::NotRunning) { p->deleteLater(); return; }
  m_exiting << p;
  connect(p, &dw::Process::finished, this, [this, p] {
    m_exiting.removeAll(p);
    p->deleteLater();
    if (m_exiting.isEmpty() && m_startWhenFree) { m_startWhenFree = false; start(); }
  });
  QTimer::singleShot(1500, p, [p] { if (p->state() != dw::Process::NotRunning) p->kill(); });
}

void Engine::stop(bool wait) {
  ++m_gen;   // a tool round of the process that ends here must not go on with the next one
  m_pending = false;
  m_startWhenFree = false;
  if (wait) for (dw::Process* p : std::as_const(m_exiting)) { p->disconnect(this); retire(p, true); }
  if (wait) m_exiting.clear();
  if (m_cc) {
    dw::Process* p = m_cc;
    m_cc = nullptr;
    p->disconnect(this);
    p->terminate();
    retire(p, wait);
    if (!m_messages.isEmpty()) save();
    m_device.clear();
    setState(State::Off);
    return;
  }
  if (!m_proc) { if (m_state != State::Off) setState(State::Off); return; }
  dw::Process* p = m_proc;
  m_proc = nullptr;
  p->disconnect(this);
  p->closeWriteChannel();            // the helper exits when its input closes
  retire(p, wait);
  // an answer cut short stays in the conversation as it is
  if (!m_messages.isEmpty() && m_messages.last().role == "assistant" && m_state == State::Busy) save();
  setState(State::Off);
}

void Engine::onFinished(int code, dw::Process::ExitStatus st) {
  if (!m_proc) return;
  m_proc->deleteLater();
  m_proc = nullptr;
  ++m_gen;   // a tool round still waiting ends without touching the Failed state below
  m_pending = false;
  QString why = m_error;
  if (why.isEmpty()) {
    const QString tail = QString::fromUtf8(m_err).trimmed().section('\n', -3);
    why = st == dw::Process::CrashExit ? QCoreApplication::translate("ai", "The model runner stopped unexpectedly.")
                                    : QCoreApplication::translate("ai", "The model runner exited (code %1).").arg(code);
    if (!tail.isEmpty()) why += "\n" + tail;
  }
  setState(State::Failed, why);
}

void Engine::onStdout() {
  m_buf += m_proc->readAllStandardOutput();
  // one line is one JSON event: a runner that sends 8 MB with no newline is broken (or worse), not a long answer
  if (m_buf.size() > (8 << 20) && m_buf.indexOf('\n') < 0) {
    m_buf.clear();
    m_proc->kill();
    setState(State::Failed, QCoreApplication::translate("ai", "The model runner sent a line over 8 MB: stopped."));
    return;
  }
  int nl;
  while ((nl = int(m_buf.indexOf('\n'))) >= 0) {
    const QByteArray line = m_buf.left(nl);
    m_buf.remove(0, nl + 1);
    const QJsonObject j = QJsonDocument::fromJson(line).object();
    const QString ev = j.value("event").toString();
    if (ev == "ready") {
      m_ctx = j.value("ctx").toInt();
      m_device = j.value("device").toString();
      setState(State::Ready);
      if (m_pending) sendPending();
    } else if (ev == "error") {
      // the process exits after it: onFinished reports it
      const QString code = j.value("code").toString();
      m_error = code == "load" ? QCoreApplication::translate("ai", "llama.cpp cannot run this file as a text model: it may be an image, "
                                   "audio or vision model, or an architecture this version does not know.")
              : code == "context" ? QCoreApplication::translate("ai", "Not enough memory to open the model's context. Free some memory "
                                   "(eject another cube) and retry.")
              : j.value("message").toString();
    } else if (ev == "token") {
      if (j.value("id").toInt() != m_reqId || m_messages.isEmpty() || m_messages.last().role != "assistant") continue;
      m_messages.last().text += j.value("text").toString();
      emit textArrived();
    } else if (ev == "done") {
      if (j.value("id").toInt() != m_reqId) continue;
      if (!m_messages.isEmpty() && m_messages.last().role == "assistant") {
        m_messages.last().stats = j;
        const double ms = j.value("gen_ms").toDouble();
        if (ms > 0) m_tps = j.value("gen_tokens").toDouble() * 1000.0 / ms;
        if (m_messages.last().text.isEmpty() && j.value("reason").toString() == "context")
          m_messages.last().text = QCoreApplication::translate("ai", "(The message is too long for this model's context.)");
      }
      save();
      emit textArrived();
      const QString reason = j.value("reason").toString();
      if (reason == "eog" && !m_messages.isEmpty() && m_messages.last().role == "assistant" &&
          !toolCalls(withoutThinking(m_messages.last().text)).isEmpty() && !m_cancelTools) {
        runTools();
        continue;
      }
      journalAnswer(reason);
      setState(State::Ready);
    }
  }
}

// the fingerprint of a text as the journal keeps it: SHA-256 of its UTF-8, the first 16 hex digits (replay_journal.py: same)
static QString fingerprint16(const QString& text) {
  return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
}

void Engine::journalLine(QJsonObject o) {
  const QString path = Slots::instance().folder(m_index) + "/tool-journal.jsonl";
  paths::dropLink(path);   // an append follows a link (one left by an older runner that could write here)
  QFile f(path);
  if (!f.open(QIODevice::Append)) return;
  o.insert("at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
  o.insert("question_id", m_questionId);
  f.write(QJsonDocument(o).toJson(QJsonDocument::Compact) + "\n");
}

void Engine::journalQuestion(const QString& question) {
  m_questionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  m_questionCalls = 0;
  const QString prompt = isClaude() ? claudeSystemPrompt() : systemPrompt();
  journalLine({{"kind", "question"}, {"model", Slots::instance().slot(m_index).displayName()}, {"question", question.left(4000)},
               {"question_sha", fingerprint16(question)}, {"system_prompt_sha", fingerprint16(prompt)},
               {"skills", QJsonArray::fromStringList(Slots::instance().skills(m_index))},
               {"as_of", double(Slots::instance().asOf(m_index))}, {"app_version", QCoreApplication::applicationVersion()}});
}

void Engine::journalAnswer(const QString& reason) {
  if (m_questionId.isEmpty()) return;
  QString text;
  for (int i = int(m_messages.size()) - 1; i >= 0 && m_messages[i].role != "user"; --i)
    if (m_messages[i].role == "assistant") text.prepend(m_messages[i].text);
  journalLine({{"kind", "answer"}, {"model", m_modelFile}, {"device", m_device}, {"reason", reason},
               {"answer_sha", fingerprint16(text)}, {"answer_chars", text.size()}, {"tool_calls", m_questionCalls}});
}

void Engine::journal(const QJsonObject& call, bool ok, const QString& result) {
  const ToolDef* t = Tools::instance().find(call.value("name").toString());
  ++m_questionCalls;
  journalLine({{"kind", "tool"}, {"model", m_modelFile}, {"tool", call.value("name").toString()}, {"arguments", call.value("arguments")},
               {"ok", ok}, {"result_chars", result.size()}, {"result_sha", fingerprint16(result)},
               {"as_of", double(Slots::instance().asOf(m_index))},
               {"clock", !t ? "unknown" : t->clock == Clock::AsOf ? "asof" : t->clock == Clock::Timeless ? "timeless" : "live"},
               {"blind", maskFor(m_index).on},
               {"external", t && t->external},
               {"ms", m_activeSince.isValid() ? double(m_activeSince.msecsTo(QDateTime::currentDateTime())) : 0.0}});
}

void Engine::runTools() {
  QList<QJsonObject> calls = toolCalls(withoutThinking(m_messages.last().text));
  if (++m_round > kMaxRounds) {
    m_messages.append({"tool", QCoreApplication::translate("ai", "Stopped: the model kept calling tools (%1 rounds).").arg(kMaxRounds),
                       {}, QDateTime::currentDateTime(), {}, QJsonObject{{"name", "—"}}, false});
    emit messagesChanged(); save(); setState(State::Ready);
    return;
  }
  if (calls.size() > 5) calls = calls.mid(0, 5);
  // run them one after the other (a tool may answer later, once its data has loaded)
  auto queue = std::make_shared<QList<QJsonObject>>(calls);
  auto anyOk = std::make_shared<bool>(false);
  auto next = std::make_shared<std::function<void()>>();
  // the round belongs to this process and this question: a model dropped on the cube (or the runner exiting) while a
  // tool waits starts another generation, and the old round must neither write into the new conversation turn nor
  // send to the new process nor replace its state (Failed by Off)
  const quint64 gen = m_gen;
  // the step holds itself only weakly (a strong self-capture never freed it); the pending tool's callback keeps it alive
  *next = [this, queue, anyOk, gen, weak = std::weak_ptr<std::function<void()>>(next)] {
    const auto next = weak.lock();
    if (!next || gen != m_gen) return;
    if (m_cancelTools || !m_proc) { m_cancelTools = false; save(); setState(m_proc ? State::Ready : State::Off); return; }
    if (queue->isEmpty()) {
      m_failedRounds = *anyOk ? 0 : m_failedRounds + 1;
      if (m_failedRounds >= 2) {   // two rounds where every call failed: the model is stuck
        m_messages.append({"tool", QCoreApplication::translate("ai", "Stopped: every tool call failed twice in a row."),
                           {}, QDateTime::currentDateTime(), {}, QJsonObject{{"name", "—"}}, false});
        emit messagesChanged(); save(); setState(State::Ready);
        return;
      }
      m_pending = true;
      sendPending();
      return;
    }
    const QJsonObject call = queue->takeFirst();
    const QString name = call.value("name").toString();
    const ToolDef* tool = Tools::instance().find(name);
    const blind::Mask mask = maskFor(m_index);   // blind trials: masked both ways (ai/Blind.h)
    auto finish = [this, call, anyOk, next, gen, mask](const ToolResult& r0) {
      const ToolResult r{r0.ok, blind::out(mask, r0.text)};
      endTool();
      if (gen != m_gen) { journal(call, r.ok, r.text); return; }   // the call ran: journaled, but its round is over
      ChatMessage m{"tool", r.text, {}, QDateTime::currentDateTime(), {}, call, r.ok};
      m_messages.append(m);
      if (r.ok) *anyOk = true;
      journal(call, r.ok, r.text);
      emit messagesChanged();
      save();
      QTimer::singleShot(0, this, [next] { (*next)(); });
    };
    if (!tool)
      finish({false, name.isEmpty() ? "error: the tool call is not valid JSON {\"name\": …, \"arguments\": {…}}"
                                    : "error: there is no tool named \"" + name + "\""});
    else if (!Slots::instance().hasSkill(m_index, tool->skill))   // checked again at the call, not only in the prompt
      finish({false, "refused: this environment does not grant the skill \"" + tool->skill + "\" any more"});
    else if (const QString why = clockGate(*tool, Slots::instance().asOf(m_index)); !why.isEmpty())   // the evaluation clock
      finish({false, why});
    else {
      auto once = std::make_shared<bool>(false);
      auto guarded = [finish, once, tool](const ToolResult& r) {
        if (*once) return;
        *once = true;
        finish({r.ok, r.ok ? wrapExternal(*tool, r.text) : r.text});   // third parties' text marked as data
      };
      const int ms = tool->timeoutMs;
      QTimer::singleShot(ms, this, [guarded, ms] {
        guarded({false, QString("error: the data did not arrive within %1 s").arg(ms / 1000)});
      });
      currentCaller() = ToolCaller{m_index, m_modelFile, Slots::instance().asOf(m_index)};
      beginTool(call);
      tool->run(blind::in(mask, call.value("arguments").toObject()), guarded);
    }
  };
  (*next)();
}

// "Today is …", or in evaluation mode the moment the tools answer as of (the model is told; the tools enforce it)
static QString todayLine(int env) {
  const qint64 asOf = Slots::instance().asOf(env);
  if (asOf <= 0) return "Today is " + QDate::currentDate().toString(Qt::ISODate) + ".";
  if (const blind::Mask m = maskFor(env); m.on)
    return "EVALUATION MODE, BLIND: the date is " + blind::shiftDate(m, asOf) + " UTC. The market is called " + blind::alias(m) +
           "; its name, the dates and its price level are masked on purpose. Every tool answers as of that moment and refuses "
           "anything later; the tools that only know the present are off. Reason only from what the tools give you.";
  return "EVALUATION MODE: the date is " + asOfStamp(asOf) + ". Every tool answers as of that moment and refuses anything "
         "later; the tools that only know the present are off. Reason only from what the tools give you: do not use anything "
         "you may remember about markets or events after that date.";
}

QString Engine::systemPrompt() const {
  const QString lang = i18n::code() == "fr" ? "French" : "English";
  const QList<ToolDef> tools = Tools::instance().forSkills(Slots::instance().skills(m_index));
  if (!tools.isEmpty()) {
    QStringList sigs;
    for (const auto& t : tools)
      sigs << QString::fromUtf8(QJsonDocument(QJsonObject{{"type", "function"}, {"function", QJsonObject{
          {"name", t.name}, {"description", t.description}, {"parameters", t.parameters}}}}).toJson(QJsonDocument::Compact));
    return QString("You are a local AI model running offline inside Deepwater, a trading terminal, in isolated environment %1. "
                   "You have no internet access and no access to the user's files. You can read the data Deepwater shows, "
                   "but ONLY through the tools below: you do not know any current price, figure, date of release or news "
                   "until a tool result gives it to you. Rules: call a tool before answering any question about markets, "
                   "news, the economy or positioning; quote figures exactly as the tool results give them, with their time; "
                   "never invent or estimate a missing figure: say what you could not read. " + QString(kExternalRule) + " %2 Answer in the "
                   "language of the user's message (the app is set to %3).\n\n"
                   "# Tools\n\nYou may call one or more functions to assist with the user query.\n\n"
                   "You are provided with function signatures within <tools></tools> XML tags:\n<tools>\n%4\n</tools>\n\n"
                   "For each function call, return a json object with function name and arguments within "
                   "<tool_call></tool_call> XML tags:\n<tool_call>\n{\"name\": <function-name>, \"arguments\": <args-json-object>}\n</tool_call>")
        .arg(m_index + 1).arg(todayLine(m_index), lang, sigs.join("\n"));
  }
  return QString("You are a local AI model running offline inside Deepwater, a trading terminal, in isolated environment %1. "
                 "You have no internet access and no access to Deepwater's data, market prices, news or the user's files, "
                 "and you have no tools. Never make up prices, figures, news or events: when asked for them, say you cannot "
                 "see them. %2 Answer in the language of the user's message (the app is set to %3).")
      .arg(m_index + 1).arg(todayLine(m_index), lang);
}

bool Engine::ask(const QString& text) {
  if (text.trimmed().isEmpty() || m_state == State::Busy || m_pending) return false;
  if (Slots::instance().slot(m_index).isEmpty()) return false;
  trimConversation(m_messages);   // between two questions: no answer or tool round is under way
  syncClock();                    // the evaluation clock changed since the last question: start afresh
  m_messages.append({"user", text.trimmed(), {}, QDateTime::currentDateTime(), {}});
  journalQuestion(text.trimmed());
  m_round = 0; m_failedRounds = 0; m_cancelTools = false;
  ++m_gen;
  emit messagesChanged();
  save();
  if (isClaude()) { runClaude(); return true; }
  m_pending = true;
  if (m_state == State::Ready && m_proc) sendPending();
  else start();
  return true;
}

void Engine::sendPending() {
  m_pending = false;
  if (!m_proc || m_messages.isEmpty() || (m_messages.last().role != "user" && m_messages.last().role != "tool")) return;
  QJsonArray msgs{QJsonObject{{"role", "system"}, {"content", systemPrompt()}}};
  QString responses;   // consecutive tool results go back as one user turn of <tool_response> blocks (Qwen / Hermes)
  auto flush = [&] { if (!responses.isEmpty()) { msgs.append(QJsonObject{{"role", "user"}, {"content", responses.trimmed()}}); responses.clear(); } };
  int from = 0;   // after the last change of the evaluation clock: nothing read before it is sent
  for (int i = 0; i < m_messages.size(); ++i) if (m_messages[i].role == "clock") from = i + 1;
  for (const auto& m : m_messages.mid(from)) {
    if (m.role == "report") continue;   // the app's own notes are not the model's conversation
    if (m.role == "tool") {
      if (m.call.value("name").toString() == "—") continue;   // the app's own stop notes
      responses += "<tool_response>\n" + QString::fromUtf8(QJsonDocument(QJsonObject{
          {"name", m.call.value("name").toString()}, {"content", m.text}}).toJson(QJsonDocument::Compact)) + "\n</tool_response>\n";
      continue;
    }
    flush();
    const QString t = m.role == "assistant" ? withoutThinking(m.text) : m.text;
    if (!t.isEmpty()) msgs.append(QJsonObject{{"role", m.role}, {"content", t}});
  }
  flush();
  QJsonObject req{{"cmd", "chat"}, {"id", ++m_reqId}, {"messages", msgs},
                  {"max_tokens", qBound(64, settings::value("ai.maxTokens", 2048).toInt(), 32768)},
                  {"temperature", settings::value("ai.temperature", 0.7).toDouble()}};
  m_messages.append({"assistant", {}, m_modelFile, QDateTime::currentDateTime(), {}});
  emit messagesChanged();
  setState(State::Busy);
  m_proc->write(QJsonDocument(req).toJson(QJsonDocument::Compact) + "\n");
}

void Engine::cancel() {
  if (m_cc) {
    m_ccStopped = true;
#ifdef Q_OS_WIN
    // QProcess::terminate() posts WM_CLOSE, which a console program (claude.exe / node) never reads: the answer went on
    m_cc->kill();
#else
    m_cc->terminate();   // Claude Code ends cleanly on SIGTERM
#endif
    return;
  }
  if (m_state == State::Busy) m_cancelTools = true;   // also ends a tool round between two calls
  // the request id: a stop read by the runner before it started this answer must not be lost (helpers/llm)
  if (m_state == State::Busy && m_proc) m_proc->write("{\"cmd\":\"stop\",\"id\":" + QByteArray::number(m_reqId) + "}\n");
  else if (m_pending) {   // still loading: forget the question's answer, keep the question
    m_pending = false;
  }
}

void Engine::clear() {
  if (m_state == State::Busy) return;
  m_messages.clear();
  QFile::remove(Slots::instance().folder(m_index) + "/claude-session.json");
  claudecode::clearTranscripts(Slots::instance().folder(m_index));   // Claude Code's own copy of the conversation
  save();
  emit messagesChanged();
}

void Engine::load() {
  const QString path = Slots::instance().folder(m_index) + "/conversation.json";
  if (!paths::insideNoLink(Slots::instance().folder(m_index), path)) return;   // a link could load any file into the chat
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return;
  for (const auto& v : QJsonDocument::fromJson(f.readAll()).object().value("messages").toArray()) {
    const QJsonObject o = v.toObject();
    m_messages.append({o.value("role").toString(), o.value("text").toString(), o.value("model").toString(),
                       QDateTime::fromString(o.value("at").toString(), Qt::ISODate), o.value("stats").toObject(),
                       o.value("call").toObject(), o.value("ok").toBool(true)});
  }
}

void Engine::save() {
  QJsonArray a;
  for (const auto& m : m_messages) {
    QJsonObject o{{"role", m.role}, {"text", m.text}, {"at", m.at.toString(Qt::ISODate)}};
    if (!m.model.isEmpty()) o["model"] = m.model;
    if (!m.stats.isEmpty()) o["stats"] = m.stats;
    if (!m.call.isEmpty()) { o["call"] = m.call; o["ok"] = m.ok; }
    a.append(o);
  }
  QDir().mkpath(Slots::instance().folder(m_index));
  paths::dropLink(Slots::instance().folder(m_index) + "/conversation.json");   // QSaveFile writes through a link
  QSaveFile f(Slots::instance().folder(m_index) + "/conversation.json");
  if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(QJsonObject{{"messages", a}}).toJson()); f.commit(); }
}

// ------------------------------------------------------------------------------------------------ Claude Code
QString Engine::claudeSession(bool* used) const {
  QFile f(Slots::instance().folder(m_index) + "/claude-session.json");
  if (paths::insideNoLink(Slots::instance().folder(m_index), f.fileName()) && f.open(QIODevice::ReadOnly)) {
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (!QUuid::fromString(o.value("id").toString()).isNull()) { *used = o.value("used").toBool(); return o.value("id").toString(); }
  }
  *used = false;
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void Engine::setClaudeSession(const QString& id, bool used) {
  QDir().mkpath(Slots::instance().folder(m_index));
  paths::dropLink(Slots::instance().folder(m_index) + "/claude-session.json");
  QSaveFile f(Slots::instance().folder(m_index) + "/claude-session.json");
  if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(QJsonObject{{"id", id}, {"used", used}}).toJson()); f.commit(); }
}

QString Engine::claudeSystemPrompt() const {
  const QString lang = i18n::code() == "fr" ? "French" : "English";
  return QString("You are Claude, running through Claude Code inside Deepwater, a trading terminal, in its AI environment %1. "
                 "Your built-in tools (shell, files, web search and fetch, sub-agents) are switched off. You can read Deepwater's "
                 "data ONLY through the tools of the MCP server \"deepwater\", when this environment grants them: if you have none, "
                 "you cannot see any Deepwater data. You do not know any current price, figure, release or news until a tool "
                 "result gives it to you. Rules: call a tool before answering any question about markets, news, the economy or "
                 "positioning; quote figures exactly as the tool results give them, with their time; never invent or estimate a "
                 "missing figure: say what you could not read. " + QString(kExternalRule) + " %2 Answer in the language of the user's message (the "
                 "app is set to %3), concisely, in Markdown.")
      .arg(m_index + 1).arg(todayLine(m_index), lang);
}

void Engine::runClaude(bool freshSession) {
  const QString bin = claudecode::binary();
  if (bin.isEmpty()) {
    setState(State::Failed, QCoreApplication::translate("ai", "Claude Code is not installed on this computer: see AI › LLM › Integration."));
    return;
  }
  QString question;
  for (int i = int(m_messages.size()) - 1; i >= 0; --i) if (m_messages[i].role == "user") { question = m_messages[i].text; break; }
  if (question.isEmpty()) return;
  m_modelFile = Slots::instance().slot(m_index).displayName();
  bool used = false;
  QString sid = claudeSession(&used);
  if (freshSession) { sid = QUuid::createUuid().toString(QUuid::WithoutBraces); used = false; }
  setClaudeSession(sid, used);

  QStringList args{"-p", "--output-format", "stream-json", "--verbose", "--include-partial-messages",
                   "--tools", "",                       // no built-in tool: no shell, files, web, sub-agents
                   "--setting-sources", "",             // none of the user's settings, hooks or permissions
                   "--strict-mcp-config",               // none of the user's MCP servers
                   "--disable-slash-commands",
                   "--permission-mode", "dontAsk", "--permission-prompts", "none",
                   "--system-prompt", claudeSystemPrompt(), "--system-prompt-snapshot", "off"};
  args << (used ? "--resume" : "--session-id") << sid;
  if (!Tools::instance().forSkills(Slots::instance().skills(m_index)).isEmpty()) {
    const QString cfg = McpServer::instance().writeConfig(m_index);
    if (!cfg.isEmpty()) args << "--mcp-config" << cfg << "--allowedTools" << "mcp__deepwater";
  }
  if (!claudecode::model().isEmpty()) args << "--model" << claudecode::model();
  if (!claudecode::effort().isEmpty()) args << "--effort" << claudecode::effort();

  args = claudecode::withoutUnsupported(args);   // options this Claude Code refused earlier (an older version)

  m_cc = new dw::Process(this);
  m_ccBuf.clear(); m_ccErr.clear();
  m_ccThinking = false; m_ccResult = false; m_ccStopped = false; m_ccCalls = 0;
  if (!freshSession) m_ccRetried = false;
  QProcessEnvironment env = claudecode::environment();
  env.insert("CLAUDE_CODE_DISABLE_AUTO_MEMORY", "1");
  m_cc->setProcessEnvironment(env);
  m_cc->setWorkingDirectory(Slots::instance().folder(m_index));
  connect(m_cc, &dw::Process::readyReadStandardOutput, this, [this] {
    m_ccBuf += m_cc->readAllStandardOutput();
    if (m_ccBuf.size() > (8 << 20) && m_ccBuf.indexOf('\n') < 0) {   // one JSON event per line: 8 MB without one is broken
      m_ccBuf.clear();
      m_cc->kill();
      return;
    }
    int nl;
    while ((nl = int(m_ccBuf.indexOf('\n'))) >= 0) {
      const QByteArray line = m_ccBuf.left(nl);
      m_ccBuf.remove(0, nl + 1);
      const QJsonObject j = QJsonDocument::fromJson(line).object();
      if (!j.isEmpty()) onClaudeLine(j);
    }
  });
  connect(m_cc, &dw::Process::readyReadStandardError, this, [this] {
    m_ccErr += m_cc->readAllStandardError();
    if (m_ccErr.size() > 8192) m_ccErr = m_ccErr.right(8192);
  });
  connect(m_cc, &dw::Process::finished, this, &Engine::onClaudeFinished);
  connect(m_cc, &dw::Process::errorOccurred, this, [this](dw::Process::ProcessError e) {
    if (e != dw::Process::FailedToStart || !m_cc) return;
    m_cc->deleteLater(); m_cc = nullptr;
    setState(State::Failed, QCoreApplication::translate("ai", "Claude Code could not start."));
  });
  m_messages.append({"assistant", {}, m_modelFile, QDateTime::currentDateTime(), {}});
  emit messagesChanged();
  setState(State::Busy);
  m_cc->start(bin, args);
  // Qt may report FailedToStart from inside start(): the handler above has then already cleared m_cc
  if (!m_cc) return;
  m_cc->write(question.toUtf8());
  m_cc->closeWriteChannel();
}

void Engine::onClaudeLine(const QJsonObject& j) {
  const QString type = j.value("type").toString();
  auto current = [this]() -> ChatMessage& {   // the answer being written (a new one after tool lines)
    if (m_messages.isEmpty() || m_messages.last().role != "assistant") {
      m_messages.append({"assistant", {}, m_modelFile, QDateTime::currentDateTime(), {}});
      emit messagesChanged();
    }
    return m_messages.last();
  };
  if (type == "system" && j.value("subtype").toString() == "init") {
    bool used; const QString sid = claudeSession(&used);
    setClaudeSession(j.value("session_id").toString(sid), true);
    const QString v = j.value("claude_code_version").toString(), model = j.value("model").toString();
    m_device = "Claude Code" + (v.isEmpty() ? QString() : " " + v) + (model.isEmpty() ? QString() : " · " + model);
    emit stateChanged();
    return;
  }
  if (type == "stream_event") {
    const QJsonObject ev = j.value("event").toObject();
    const QString et = ev.value("type").toString();
    if (et == "content_block_start") {
      if (ev.value("content_block").toObject().value("type").toString() == "thinking") {
        current().text += "<think>"; m_ccThinking = true;
      }
    } else if (et == "content_block_delta") {
      const QJsonObject d = ev.value("delta").toObject();
      const QString dt = d.value("type").toString();
      if (dt == "text_delta") {
        current().text += d.value("text").toString();
      } else if (dt == "thinking_delta") {
        current().text += d.value("thinking").toString();
      } else {
        return;
      }
      emit textArrived();
    } else if (et == "content_block_stop" && m_ccThinking) {
      m_ccThinking = false;
      current().text += "</think>\n";
      emit textArrived();
    }
    return;
  }
  if (type == "result") {
    m_ccResult = true;
    const QJsonObject u = j.value("usage").toObject();
    const bool err = j.value("is_error").toBool() || j.value("subtype").toString() != "success";
    ChatMessage& m = current();
    if (withoutThinking(m.text).isEmpty() && !j.value("result").toString().isEmpty()) m.text += j.value("result").toString();
    if (err && m.text.trimmed().isEmpty())
      m.text = QCoreApplication::translate("ai", "Claude Code stopped with an error (%1).").arg(j.value("subtype").toString());
    m.stats = QJsonObject{{"gen_tokens", u.value("output_tokens").toInt()}, {"duration_ms", j.value("duration_ms").toDouble()},
                          {"reason", m_ccStopped ? "stopped" : err ? "error" : "eog"},
                          {"cost_usd", j.value("total_cost_usd").toDouble()}, {"turns", j.value("num_turns").toInt()}};
    save();
    emit textArrived();
  }
}

void Engine::onClaudeFinished(int code, dw::Process::ExitStatus st) {
  if (!m_cc) return;
  m_cc->deleteLater();
  m_cc = nullptr;
  const QString err = QString::fromUtf8(m_ccErr).trimmed();
  // the saved session is gone (Claude Code's history cleaned up): start a new one once, the question stays
  // an older Claude Code that does not know an optional option: ask again without it (once per option)
  if (!m_ccResult && !m_ccStopped && claudecode::markUnsupported(err)) {
    if (!m_messages.isEmpty() && m_messages.last().role == "assistant" && m_messages.last().text.isEmpty()) m_messages.removeLast();
    runClaude(false);
    return;
  }
  if (!m_ccResult && !m_ccStopped && !m_ccRetried && err.contains("No conversation found")) {
    m_ccRetried = true;
    if (!m_messages.isEmpty() && m_messages.last().role == "assistant" && m_messages.last().text.isEmpty()) m_messages.removeLast();
    runClaude(true);
    return;
  }
  if (!m_messages.isEmpty() && m_messages.last().role == "assistant") {
    ChatMessage& m = m_messages.last();
    if (m_ccThinking) { m.text += "</think>\n"; m_ccThinking = false; }
    if (m.stats.isEmpty()) m.stats = QJsonObject{{"reason", m_ccStopped ? "stopped" : "error"}};
  }
  save();
  emit textArrived();
  journalAnswer(m_ccResult ? "eog" : m_ccStopped ? "stopped" : "error");
  if (m_ccResult || m_ccStopped) { setState(State::Ready); return; }
  QString why = st == dw::Process::CrashExit ? QCoreApplication::translate("ai", "Claude Code stopped unexpectedly.")
                                          : QCoreApplication::translate("ai", "Claude Code exited (code %1).").arg(code);
  if (const QString opt = claudecode::unknownOption(err); !opt.isEmpty())   // a required option: too old to run safely
    why += "\n" + QCoreApplication::translate("ai", "This Claude Code is too old for Deepwater (it does not know %1). Update it: run `claude update` in a terminal.").arg(opt);
  const QString tail = err.section('\n', -3);
  if (!tail.isEmpty()) why += "\n" + tail;
  setState(State::Failed, why);
}

QString Engine::toolGate() {
  if (!isClaude() || !m_cc || m_state != State::Busy) return QStringLiteral("refused: no question of this environment is being answered");
  if (++m_ccCalls > kMaxClaudeCalls)
    return QString("refused: the limit of %1 tool calls per question is reached; answer with what you have read").arg(kMaxClaudeCalls);
  return {};
}

void Engine::beginTool(const QJsonObject& call) {
  m_activeTool = call;
  m_activeSince = QDateTime::currentDateTime();
  emit toolActivity();
}

void Engine::endTool() {
  if (m_activeTool.isEmpty()) return;
  m_activeTool = {};
  emit toolActivity();
}

void Engine::recordTool(const QJsonObject& call, const ToolResult& r) {
  endTool();
  if (!m_messages.isEmpty() && m_messages.last().role == "assistant") {
    ChatMessage& a = m_messages.last();   // the text written before the call is complete
    if (m_ccThinking) { a.text += "</think>\n"; m_ccThinking = false; }
    if (a.stats.isEmpty()) a.stats = QJsonObject{{"partial", true}};
  }
  m_messages.append({"tool", r.text, {}, QDateTime::currentDateTime(), {}, call, r.ok});
  journal(call, r.ok, r.text);
  emit messagesChanged();
  save();
}

// ------------------------------------------------------------------------------------------------ Engines
Engines& Engines::instance() { static Engines* e = new Engines; return *e; }

Engines::Engines() {
  for (int i = 0; i < Slots::kCount; ++i) m_engines << new Engine(i, this);
  // a model dropped on a cube loads at once; EJECT or a move stops the process that held it
  connect(&Slots::instance(), &Slots::changed, this, [this](int i) {
    Engine* e = at(i);
    if (!e) return;
    e->stop();
    if (!Slots::instance().slot(i).isEmpty()) e->start();
  });
  connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { for (auto* e : m_engines) e->stop(true); });
}
}  // namespace ai
