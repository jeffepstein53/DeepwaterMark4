// On-device text recognition on Windows (native parity, 2026-11-22): Windows.Media.Ocr, part of Windows 10 and 11,
// behind the interface macro/Ocr.mm gives on macOS and iOS with Apple Vision (macro/Ocr.h). It reads the RBA / RBNZ
// probability pictures (macro/CentralBanksMore.cpp), which were not read at all on Windows before.
// Synchronous like the Vision version: called from a worker thread, which joins the COM multithreaded apartment.
// Lines come back with their box in image units 0..1, origin top-left (the words' boxes joined), as Vision gives them.
#ifdef _WIN32
#include "macro/Ocr.h"
#include <QImage>
#include <QtDebug>
#include <algorithm>
#include <cstring>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>

namespace macro::ocr {
namespace {
namespace wf = winrt::Windows::Foundation;
namespace gi = winrt::Windows::Graphics::Imaging;
namespace mo = winrt::Windows::Media::Ocr;

void joinApartment() {
  thread_local bool joined = false;
  if (joined) return;
  joined = true;
  try {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
  } catch (const winrt::hresult_error&) {
    // the thread already has an apartment (RPC_E_CHANGED_MODE): OCR works from either kind
  }
}

mo::OcrEngine engine() {
  // English first (the pictures are English tables), else the user's own languages
  mo::OcrEngine e{nullptr};
  try {
    const winrt::Windows::Globalization::Language en(L"en-US");
    if (mo::OcrEngine::IsLanguageSupported(en)) e = mo::OcrEngine::TryCreateFromLanguage(en);
    if (!e) e = mo::OcrEngine::TryCreateFromUserProfileLanguages();
  } catch (const winrt::hresult_error&) {}
  return e;
}
}  // namespace

QList<Line> recognize(const QByteArray& imageData) {
  QList<Line> out;
  if (imageData.isEmpty()) return out;
  QImage img = QImage::fromData(imageData);
  if (img.isNull()) return out;
  joinApartment();
  try {
    const mo::OcrEngine e = engine();
    if (!e) {
      qWarning("[ocr] no Windows OCR language installed");
      return out;
    }
    // Windows OCR refuses pictures beyond MaxImageDimension: scaled down, the boxes are relative anyway
    const int maxSide = int(mo::OcrEngine::MaxImageDimension());
    if (img.width() > maxSide || img.height() > maxSide) img = img.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);   // little endian: B, G, R, A bytes = Bgra8
    const int w = img.width(), h = img.height();
    gi::SoftwareBitmap bmp(gi::BitmapPixelFormat::Bgra8, w, h, gi::BitmapAlphaMode::Premultiplied);
    winrt::Windows::Storage::Streams::Buffer buf(uint32_t(w) * uint32_t(h) * 4);
    for (int y = 0; y < h; ++y) std::memcpy(buf.data() + size_t(y) * size_t(w) * 4, img.constScanLine(y), size_t(w) * 4);
    buf.Length(buf.Capacity());
    bmp.CopyFromBuffer(buf);
    const mo::OcrResult r = e.RecognizeAsync(bmp).get();
    for (const mo::OcrLine& line : r.Lines()) {
      double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
      for (const mo::OcrWord& word : line.Words()) {
        const wf::Rect b = word.BoundingRect();
        x0 = std::min(x0, double(b.X)); y0 = std::min(y0, double(b.Y));
        x1 = std::max(x1, double(b.X + b.Width)); y1 = std::max(y1, double(b.Y + b.Height));
      }
      if (x1 < x0) continue;
      Line l;
      l.text = QString::fromWCharArray(line.Text().c_str());
      l.box = QRectF(x0 / w, y0 / h, (x1 - x0) / w, (y1 - y0) / h);
      out << l;
    }
  } catch (const winrt::hresult_error& err) {
    qWarning("[ocr] Windows OCR failed: %s", qPrintable(QString::fromWCharArray(err.message().c_str())));
    out.clear();
  }
  return out;
}
}  // namespace macro::ocr
#endif
