// Bank Research's PDF pages on Windows (native parity, 2026-11-22): Qt PDF (PDFium, no Chromium runtime) behind the
// interface that banks/Pdf.mm gives on macOS and iOS with CoreGraphics and PDFKit (banks/Pdf.h). Before, every file
// read as "not a readable PDF" here (platform/NonApple.cpp returned nothing).
// Each call opens its own document, as Pdf.mm does, so render() may run on a worker thread: Qt PDF serialises its
// PDFium calls behind one lock.
#ifdef _WIN32
#include "banks/Pdf.h"
#include <QApplication>
#include <QFileInfo>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QPrintDialog>
#include <QPrinter>
#include <QStringList>

namespace banks::pdf {
namespace {
bool open(QPdfDocument& d, const QString& file) {
  return d.load(file) == QPdfDocument::Error::None && d.pageCount() > 0;
}
}  // namespace

Info info(const QString& file) {
  Info out;
  QPdfDocument d;
  if (!open(d, file)) return out;
  out.pages = d.pageCount();
  for (int i = 0; i < out.pages; ++i) {
    const QSizeF s = d.pagePointSize(i);
    out.sizes << (s.width() > 0 && s.height() > 0 ? s : QSizeF(595, 842));
  }
  out.title = d.metaData(QPdfDocument::MetaDataField::Title).toString();
  return out;
}

QImage render(const QString& file, int pageIndex, double scale, double dpr) {
  QPdfDocument d;
  if (!open(d, file) || pageIndex < 0 || pageIndex >= d.pageCount()) return {};
  const QSizeF sz = d.pagePointSize(pageIndex);
  // the page size comes from the file: a zero, NaN or huge one is not drawn (same bounds as Pdf.mm)
  const double fw = sz.width() * scale * dpr + 0.5, fh = sz.height() * scale * dpr + 0.5;
  if (!(sz.width() > 0 && sz.height() > 0 && fw < 16385 && fh < 16385)) return {};
  const QSize px(qMax(1, int(fw)), qMax(1, int(fh)));
  const QImage page = d.render(pageIndex, px);
  if (page.isNull()) return {};
  // on white, as CoreGraphics draws it (PDFium leaves the page's own background, often transparent)
  QImage img(px, QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::white);
  {
    QPainter p(&img);
    p.drawImage(0, 0, page);
  }
  img.setDevicePixelRatio(dpr);
  return img;
}

QString text(const QString& file, int page) {
  QPdfDocument d;
  if (!open(d, file)) return {};
  if (page >= 0) return page < d.pageCount() ? d.getAllText(page).text() : QString();
  QStringList all;
  for (int i = 0; i < d.pageCount(); ++i) all << d.getAllText(i).text();
  return all.join('\n');
}

// The system print dialog, then each page scaled to fit the printable area, centred (PDFKit's kPDFPrintPageScaleToFit).
bool print(const QString& file) {
  QPdfDocument d;
  if (!open(d, file)) return false;
  QPrinter printer(QPrinter::HighResolution);
  printer.setDocName(QFileInfo(file).completeBaseName());
  printer.setFromTo(1, d.pageCount());
  QPrintDialog dlg(&printer, QApplication::activeWindow());
  dlg.setMinMax(1, d.pageCount());
  if (dlg.exec() != QDialog::Accepted) return false;
  int from = printer.fromPage(), to = printer.toPage();
  if (from <= 0 || to <= 0) { from = 1; to = d.pageCount(); }
  QPainter p;
  if (!p.begin(&printer)) return false;
  const QRectF area = printer.pageLayout().paintRectPixels(printer.resolution());
  for (int i = from - 1; i < to && i < d.pageCount(); ++i) {
    if (i > from - 1) printer.newPage();
    const QSizeF pt = d.pagePointSize(i);
    if (!(pt.width() > 0 && pt.height() > 0)) continue;
    const double k = qMin(area.width() / pt.width(), area.height() / pt.height());
    const QSize px(qMax(1, int(pt.width() * k)), qMax(1, int(pt.height() * k)));
    const QImage img = d.render(i, px);
    if (img.isNull()) continue;
    const QPointF at((area.width() - px.width()) / 2.0, (area.height() - px.height()) / 2.0);
    p.fillRect(QRectF(at, QSizeF(px)), Qt::white);
    p.drawImage(at, img);
  }
  return p.end();
}
}  // namespace banks::pdf
#endif
