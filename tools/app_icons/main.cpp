// Makes the app icons from the Deepwater logo (resources/icon/deepwater.svg), drawn on the macOS icon grid: a rounded
// 824 px tile inside 100 px margins.
//  ios <svg> <png>      the iOS icon: a full square, opaque, no rounded corners (iOS applies its own mask): the tile's
//                       square is kept (viewBox 100 100 824 824), its gradient filled to the corners, its hairline edge
//                       left out, rendered at 1024 x 1024 without alpha (ios/Assets.xcassets/AppIcon.appiconset)
//  mac <svg> <iconset>  the macOS icon: the logo as it is (the grid is Apple's), every size of an .iconset folder;
//                       then `iconutil -c icns <iconset> -o macos/Deepwater.icns`
//  win <svg> <ico>      the Windows icon (the exe in Explorer, the taskbar, the title bar, the shortcuts, the
//                       installer): the tile alone (viewBox 100 100 824 824: Windows icons fill their square), its
//                       rounded corners left transparent, at every size Windows asks for, as PNG images joined into
//                       one .ico (windows/Deepwater.ico)
//   cmake -S tools/app_icons -B <dir> -DCMAKE_PREFIX_PATH=~/Qt/6.12.0/macos && cmake --build <dir>
#include <QGuiApplication>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QRegularExpression>
#include <QSvgRenderer>
#include <cstdio>

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  if (argc != 4 || (qstrcmp(argv[1], "ios") != 0 && qstrcmp(argv[1], "mac") != 0 && qstrcmp(argv[1], "win") != 0)) {
    std::fprintf(stderr, "usage: app_icons ios <logo.svg> <out.png> | app_icons mac <logo.svg> <out.iconset>"
                         " | app_icons win <logo.svg> <out.ico>\n");
    return 2;
  }
  QFile f(QString::fromLocal8Bit(argv[2]));
  if (!f.open(QIODevice::ReadOnly)) { std::fprintf(stderr, "cannot read %s\n", argv[2]); return 1; }
  QString svg = QString::fromUtf8(f.readAll());
  if (qstrcmp(argv[1], "win") == 0) {
    svg.replace(R"re(viewBox="0 0 1024 1024")re", R"re(viewBox="100 100 824 824")re");
    QSvgRenderer r(svg.toUtf8());
    if (!r.isValid()) { std::fprintf(stderr, "invalid svg\n"); return 1; }
    const QList<int> sizes{16, 20, 24, 32, 40, 48, 64, 96, 128, 256};
    QList<QByteArray> pngs;
    for (int px : sizes) {
      QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
      img.fill(Qt::transparent);
      QPainter p(&img);
      p.setRenderHint(QPainter::Antialiasing);
      r.render(&p, QRectF(0, 0, px, px));
      p.end();
      QByteArray png;
      QBuffer b(&png);
      if (!b.open(QIODevice::WriteOnly) || !img.save(&b, "PNG")) { std::fprintf(stderr, "cannot encode %d px\n", px); return 1; }
      pngs << png;
    }
    // .ico: a 6-byte header, one 16-byte entry per image, then the PNG files themselves (read by Windows since Vista)
    QByteArray ico;
    const auto u16 = [&ico](quint32 v) { ico.append(char(v & 0xff)); ico.append(char((v >> 8) & 0xff)); };
    const auto u32 = [&](quint32 v) { u16(v & 0xffff); u16(v >> 16); };
    u16(0); u16(1); u16(quint32(sizes.size()));
    quint32 offset = quint32(6 + 16 * sizes.size());
    for (qsizetype i = 0; i < sizes.size(); ++i) {
      const char side = char(sizes[i] >= 256 ? 0 : sizes[i]);   // 0 means 256
      ico.append(side); ico.append(side);
      ico.append(char(0)); ico.append(char(0));   // no palette, reserved
      u16(1); u16(32);                            // planes, bits per pixel
      u32(quint32(pngs[i].size())); u32(offset);
      offset += quint32(pngs[i].size());
    }
    for (const QByteArray& png : pngs) ico += png;
    QFile out(QString::fromLocal8Bit(argv[3]));
    if (!out.open(QIODevice::WriteOnly) || out.write(ico) != ico.size()) { std::fprintf(stderr, "cannot write %s\n", argv[3]); return 1; }
    return 0;
  }
  if (qstrcmp(argv[1], "mac") == 0) {
    QSvgRenderer r(svg.toUtf8());
    if (!r.isValid()) { std::fprintf(stderr, "invalid svg\n"); return 1; }
    const QString dir = QString::fromLocal8Bit(argv[3]);
    QDir().mkpath(dir);
    for (int pt : {16, 32, 128, 256, 512})
      for (int scale : {1, 2}) {
        const int px = pt * scale;
        QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        r.render(&p, QRectF(0, 0, px, px));
        p.end();
        const QString name = QString("%1/icon_%2x%2%3.png").arg(dir).arg(pt).arg(scale == 2 ? "@2x" : "");
        if (!img.save(name, "PNG")) { std::fprintf(stderr, "cannot write %s\n", qPrintable(name)); return 1; }
      }
    return 0;
  }
  // the tile: square, to the corners; its hairline edge: gone
  svg.replace(QRegularExpression(R"re(<rect x="100" y="100" width="824" height="824" rx="[0-9.]+" fill="url\(#tile\)"/>)re"),
              R"re(<rect x="0" y="0" width="1024" height="1024" fill="url(#tile)"/>)re");
  svg.replace(QRegularExpression(R"re(<rect x="101.5"[^>]*stroke-opacity[^>]*/>)re"), QString());
  svg.replace(R"re(viewBox="0 0 1024 1024")re", R"re(viewBox="100 100 824 824")re");
  QSvgRenderer r(svg.toUtf8());
  if (!r.isValid()) { std::fprintf(stderr, "invalid svg\n"); return 1; }
  QImage img(1024, 1024, QImage::Format_RGB32);
  img.fill(QColor("#0a0702"));
  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing);
  r.render(&p, QRectF(0, 0, 1024, 1024));
  p.end();
  if (!img.convertToFormat(QImage::Format_RGB888).save(QString::fromLocal8Bit(argv[3]), "PNG")) { std::fprintf(stderr, "cannot write %s\n", argv[3]); return 1; }
  return 0;
}
