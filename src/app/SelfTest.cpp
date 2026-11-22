#include "app/SelfTest.h"
#include "banks/Pdf.h"
#include "macro/Ocr.h"
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QStandardPaths>
#include <QSysInfo>
#include <QtConcurrent>

namespace app::selftest {
namespace {
QJsonObject check(const QString& name, bool ok, const QString& detail) {
  return QJsonObject{{"name", name}, {"ok", ok}, {"detail", detail}};
}
}  // namespace

int run(const QString& outFile) {
  QJsonArray checks;
  bool allOk = true;
  auto add = [&](const QString& name, bool ok, const QString& detail) {
    checks << check(name, ok, detail);
    allOk = allOk && ok;
    qInfo("[selftest] %s %s: %s", ok ? "ok  " : "FAIL", qPrintable(name), qPrintable(detail));
  };

  // 1. a two-page PDF written here, so the test needs no file of its own
  const QString dir = QDir::tempPath() + QStringLiteral("/deepwater-selftest");
  QDir().mkpath(dir);
  const QString pdf = dir + QStringLiteral("/selftest.pdf");
  {
    QPdfWriter w(pdf);
    w.setTitle(QStringLiteral("Deepwater selftest"));
    w.setPageSize(QPageSize(QPageSize::A4));
    w.setResolution(300);
    QPainter p(&w);
    QFont f(QStringLiteral("Arial"));
    f.setPointSize(40);
    f.setBold(true);
    p.setFont(f);
    p.drawText(QRect(0, 300, w.width(), 600), Qt::AlignHCenter | Qt::AlignTop, QStringLiteral("DEEPWATER SELFTEST"));
    w.newPage();
    p.drawText(QRect(0, 300, w.width(), 600), Qt::AlignHCenter | Qt::AlignTop, QStringLiteral("PAGE TWO"));
  }
  add(QStringLiteral("pdf.write"), QFileInfo(pdf).size() > 0, QString::number(QFileInfo(pdf).size()) + " bytes");

  // 2. Bank Research's PDF reader (banks/Pdf.mm on Apple, platform/WinPdf.cpp on Windows)
  const banks::pdf::Info info = banks::pdf::info(pdf);
  add(QStringLiteral("pdf.info"), info.pages == 2 && info.title == QStringLiteral("Deepwater selftest"),
      QStringLiteral("%1 pages, title \"%2\", page 1 %3 x %4 pt")
          .arg(info.pages).arg(info.title).arg(info.sizes.value(0).width()).arg(info.sizes.value(0).height()));
  // rendered off the GUI thread, as the reader does
  const QImage img = QtConcurrent::run([pdf] { return banks::pdf::render(pdf, 0, 1.5, 1.0); }).result();
  int dark = 0;
  if (!img.isNull())
    for (int y = 0; y < img.height(); y += 2)
      for (int x = 0; x < img.width(); x += 2) dark += qGray(img.pixel(x, y)) < 96;
  add(QStringLiteral("pdf.render"), !img.isNull() && dark > 200,
      img.isNull() ? QStringLiteral("no image") : QStringLiteral("%1 x %2 px, %3 dark samples").arg(img.width()).arg(img.height()).arg(dark));
  const QString text = banks::pdf::text(pdf, 0);
  add(QStringLiteral("pdf.text"), text.contains(QStringLiteral("SELFTEST")), text.simplified().left(80));

  // 3. on-device OCR (macro/Ocr.mm on Apple, platform/WinOcr.cpp on Windows) on the rendered page, from a worker thread
  // as CentralBanksMore does
  QByteArray png;
  if (!img.isNull()) {
    QBuffer b(&png);
    b.open(QIODevice::WriteOnly);
    img.save(&b, "PNG");
  }
  const QList<macro::ocr::Line> lines = QtConcurrent::run([png] { return macro::ocr::recognize(png); }).result();
  QStringList read;
  bool found = false, boxesOk = !lines.isEmpty();
  for (const auto& l : lines) {
    read << l.text;
    found = found || l.text.contains(QStringLiteral("SELFTEST"), Qt::CaseInsensitive);
    boxesOk = boxesOk && l.box.left() >= -0.01 && l.box.right() <= 1.01 && l.box.top() >= -0.01 && l.box.bottom() <= 1.01 && l.box.height() > 0;
  }
  add(QStringLiteral("ocr"), found && boxesOk, read.join(QStringLiteral(" | ")).left(120));

  const QJsonObject out{{"ok", allOk}, {"platform", QSysInfo::prettyProductName()}, {"checks", checks}};
  if (!outFile.isEmpty()) {
    QFile f(outFile);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument(out).toJson());
  }
  QFile::remove(pdf);
  return allOk ? 0 : 1;
}
}  // namespace app::selftest
