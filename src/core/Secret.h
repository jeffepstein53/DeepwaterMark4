#pragma once
#include <QString>

// A secret kept in the settings (Myfxbook's session and e-mail address): encrypted by Windows for this account (DPAPI)
// before it is written, so a copy of the settings is worth nothing elsewhere. Elsewhere than Windows: as it is.
namespace secret {
QString seal(const QString& plain);      // "dpapi:…"; empty for an empty text or when Windows refuses
QString open(const QString& stored);     // the text back; a value stored before this version is returned unchanged;
                                         // empty when it was sealed for another account or computer
bool isSealed(const QString& stored);
}
