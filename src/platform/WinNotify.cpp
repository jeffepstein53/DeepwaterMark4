#include "platform/WinNotify.h"
#ifdef Q_OS_WIN
#include <QByteArray>
#include <QCoreApplication>
#include <QtEndian>
#include <cmath>
#include <windows.h>
#include <shellapi.h>
#include <mmsystem.h>
#endif

namespace winnotify {
#ifdef Q_OS_WIN
namespace {
constexpr UINT kMessage = WM_APP + 41, kId = 1;
HWND g_wnd = nullptr;
bool g_added = false;
UINT g_taskbar = 0;
std::function<void()> g_click;

NOTIFYICONDATAW base() {
  NOTIFYICONDATAW d{};
  d.cbSize = sizeof d;
  d.hWnd = g_wnd;
  d.uID = kId;
  return d;
}
void add() {
  static HICON icon = nullptr;
  if (!icon) ExtractIconExW(reinterpret_cast<const wchar_t*>(QCoreApplication::applicationFilePath().replace('/', '\\').utf16()), 0, nullptr, &icon, 1);
  NOTIFYICONDATAW d = base();
  d.uFlags = NIF_MESSAGE | NIF_TIP | (icon ? NIF_ICON : 0);
  d.uCallbackMessage = kMessage;
  d.hIcon = icon;
  wcscpy_s(d.szTip, L"Deepwater");
  g_added = Shell_NotifyIconW(NIM_ADD, &d);
  if (!g_added) return;
  d.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION, &d);
}
LRESULT CALLBACK proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
  if (m == kMessage) {
    const UINT what = LOWORD(lp);
    if (what == NIN_BALLOONUSERCLICK || what == NIN_SELECT || what == NIN_KEYSELECT) { if (g_click) g_click(); }
    return 0;
  }
  if (m == g_taskbar && g_taskbar) { if (g_added) add(); return 0; }   // Explorer started again: the icon is put back
  return DefWindowProcW(w, m, wp, lp);
}
bool ready() {
  if (!g_wnd) {
    WNDCLASSW c{};
    c.lpfnWndProc = proc;
    c.hInstance = GetModuleHandleW(nullptr);
    c.lpszClassName = L"DeepwaterNotify";
    RegisterClassW(&c);
    g_taskbar = RegisterWindowMessageW(L"TaskbarCreated");
    g_wnd = CreateWindowExW(0, c.lpszClassName, L"Deepwater", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, c.hInstance, nullptr);
    if (!g_wnd) return false;
    qAddPostRoutine([] {
      if (g_added) { NOTIFYICONDATAW d = base(); Shell_NotifyIconW(NIM_DELETE, &d); g_added = false; }
      DestroyWindow(g_wnd); g_wnd = nullptr;
    });
  }
  if (!g_added) add();
  return g_added;
}
void copy(wchar_t* to, size_t room, const QString& s) {
  const QString cut = s.size() < int(room) ? s : s.left(int(room) - 2) + QChar(0x2026);
  wcsncpy_s(to, room, reinterpret_cast<const wchar_t*>(cut.utf16()), _TRUNCATE);
}

// Two soft notes, 16-bit mono.
QByteArray wav(double f1, double f2) {
  constexpr int kRate = 22050, kNote = 4200;
  QByteArray pcm;
  pcm.resize(2 * kNote * 2);
  auto* out = reinterpret_cast<qint16*>(pcm.data());
  for (int n = 0; n < 2; ++n)
    for (int i = 0; i < kNote; ++i) {
      const double t = double(i) / kRate, env = std::min(1.0, i / 200.0) * std::exp(-t * 16.0), f = n ? f2 : f1;
      out[n * kNote + i] = qToLittleEndian(qint16(9000 * env * (std::sin(6.283185307 * f * t) + 0.25 * std::sin(6.283185307 * 2 * f * t))));
    }
  QByteArray h;
  const auto u32 = [&h](quint32 v) { v = qToLittleEndian(v); h.append(reinterpret_cast<const char*>(&v), 4); };
  const auto u16 = [&h](quint16 v) { v = qToLittleEndian(v); h.append(reinterpret_cast<const char*>(&v), 2); };
  h += "RIFF"; u32(36 + pcm.size()); h += "WAVEfmt "; u32(16); u16(1); u16(1); u32(kRate); u32(kRate * 2); u16(2); u16(16);
  h += "data"; u32(pcm.size());
  return h + pcm;
}
}

void show(const QString& title, const QString& text, std::function<void()> onClick) {
  if (!ready()) return;
  g_click = std::move(onClick);
  NOTIFYICONDATAW d = base();
  d.uFlags = NIF_INFO;
  d.dwInfoFlags = NIIF_NONE | NIIF_NOSOUND | NIIF_RESPECT_QUIET_TIME;   // Windows' "do not disturb" is respected; the sound is chime()'s
  copy(d.szInfoTitle, ARRAYSIZE(d.szInfoTitle), title);
  copy(d.szInfo, ARRAYSIZE(d.szInfo), text);
  Shell_NotifyIconW(NIM_MODIFY, &d);
}

void chime(bool warning) {
  static const QByteArray up = wav(659.3, 987.8), down = wav(587.3, 440.0);
  PlaySoundW(reinterpret_cast<const wchar_t*>((warning ? down : up).constData()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}
#else
void show(const QString&, const QString&, std::function<void()>) {}
void chime(bool) {}
#endif
}
