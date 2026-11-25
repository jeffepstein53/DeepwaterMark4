#include "core/Secret.h"
#include <QByteArray>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace secret {
namespace { constexpr QLatin1String kMark("dpapi:"); }

bool isSealed(const QString& stored) { return stored.startsWith(kMark); }

#ifdef Q_OS_WIN
namespace {
QByteArray crypt(const QByteArray& in, bool seal) {
  QByteArray salt("Deepwater Mark 4");
  DATA_BLOB src{DWORD(in.size()), reinterpret_cast<BYTE*>(const_cast<char*>(in.constData()))};
  DATA_BLOB key{DWORD(salt.size()), reinterpret_cast<BYTE*>(salt.data())};
  DATA_BLOB out{0, nullptr};
  const BOOL ok = seal ? CryptProtectData(&src, nullptr, &key, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)
                       : CryptUnprotectData(&src, nullptr, &key, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out);
  if (!ok) return {};
  const QByteArray r(reinterpret_cast<const char*>(out.pbData), qsizetype(out.cbData));
  SecureZeroMemory(out.pbData, out.cbData);
  LocalFree(out.pbData);
  return r;
}
}
QString seal(const QString& plain) {
  if (plain.isEmpty()) return {};
  const QByteArray c = crypt(plain.toUtf8(), true);
  return c.isEmpty() ? QString() : kMark + QString::fromLatin1(c.toBase64());
}
QString open(const QString& stored) {
  if (!isSealed(stored)) return stored;
  return QString::fromUtf8(crypt(QByteArray::fromBase64(stored.mid(kMark.size()).toLatin1()), false));
}
#else
QString seal(const QString& plain) { return plain; }
QString open(const QString& stored) { return stored; }
#endif
}
