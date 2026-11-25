#include "core/Settings.h"
#include "core/Secret.h"
#include <QSettings>
#include <QDir>
#include <QDebug>
#include <memory>

namespace {
QSettings& native() { static QSettings s; return s; }
QSettings* profile() {
  static std::unique_ptr<QSettings> p = [] {
    const QByteArray dir = qgetenv("DW_PROFILE");
    if (dir.isEmpty()) return std::unique_ptr<QSettings>();
    QDir().mkpath(QString::fromUtf8(dir));
    return std::make_unique<QSettings>(QString::fromUtf8(dir) + "/settings.ini", QSettings::IniFormat);
  }();
  return p.get();
}
// Keys are written with dots by callers ("keys.fred", "ui.page"). Qt's macOS backend swaps '.' and '/' between
// its keys and the plist, so the Qt key must use '/' for the plist to show "keys.fred" (what `defaults` and
// earlier builds use). Without this, saved keys were never found.
QString nk(QString k) { return k.replace('.', '/'); }
}

namespace settings {
bool isTestProfile() { return profile() != nullptr; }
QVariant value(const QString& key, const QVariant& def) {
  const QString k = nk(key);
  if (auto* p = profile()) {
    if (p->contains(k)) return p->value(k, def);
    if (p->value("__removed/" + k).toBool()) return def;
    // A test profile borrows only API keys from the user's store. Never sessions or any other state: a test once
    // used the user's real Myfxbook session through this fallback (2026-11-14).
    if (!key.startsWith(QLatin1String("keys."))) return def;
  }
  return native().value(k, def);
}
void setValue(const QString& key, const QVariant& v) {
  const QString k = nk(key);
  if (auto* p = profile()) { p->setValue(k, v); p->remove("__removed/" + k); return; }
  native().setValue(k, v);
}
void remove(const QString& key) {
  const QString k = nk(key);
  if (auto* p = profile()) { p->remove(k); p->setValue("__removed/" + k, true); return; }
  native().remove(k);
}
QString secretValue(const QString& key) {
  const QString stored = value(key).toString();
  if (stored.isEmpty()) return {};
  const QString plain = secret::open(stored);
  if (!secret::isSealed(stored)) setSecretValue(key, plain);   // written in clear before this version
  return plain;
}
bool setSecretValue(const QString& key, const QString& v) {
  const QString s = secret::seal(v);
  if (s.isEmpty() && !v.isEmpty()) return false;
  setValue(key, s);
  return true;
}
bool contains(const QString& key) { return value(key).isValid(); }
void dropRetired() {
  static const char* const kRetired[] = {"ai/slots", "ai/skills", "ai/claude/model", "ai/claude/effort", "ai/llm/part", "ml/source", "ml/prices"};
  for (const char* k : kRetired)
    if (contains(k)) { remove(k); qInfo("[settings] retired key removed: %s", k); }   // the name only, never the value
}
QString apiKey(const QString& name) { return value("keys." + name).toString().trimmed(); }
}
