#pragma once
#include <QString>
#include <QVariant>

// App preferences. Normal runs use the native store (macOS domain com.deepwater-terminal.Deepwater).
// Test runs set DW_PROFILE=<dir>: reads and writes use <dir>/settings.ini only; the one exception is API keys
// ("keys.*"), read (never written) from the native store. Sessions and other state are never borrowed.
namespace settings {
QVariant value(const QString& key, const QVariant& def = {});
void setValue(const QString& key, const QVariant& v);
void remove(const QString& key);
bool contains(const QString& key);
// A secret (core/Secret): encrypted when it is written; one written in clear by an older version is encrypted on its
// first reading. Never log the result.
QString secretValue(const QString& key);
bool setSecretValue(const QString& key, const QString& v);   // false: Windows could not encrypt it, nothing is written
// API key saved by the user (key "keys.<name>"), empty if none. Never log the result.
QString apiKey(const QString& name);
bool isTestProfile();
// Removes the keys of tabs that no longer exist (Graphics and AI, retired in 0.3.1 and 0.4.0), left in the store by older versions.
// Only those exact keys: nothing the app still reads. Run once a launch; a key already gone costs nothing.
void dropRetired();
}
