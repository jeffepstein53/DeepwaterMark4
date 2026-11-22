#pragma once
// Charts tab — the stroke icons of quant-terminal (MIT, Copyright (c) 2026 Lewis Jackson — docs/charts/NOTICE.md).
// The JS writes them as <svg viewBox="0 0 24 24">…inner…</svg> styled by app.css (fill none, stroke currentColor,
// stroke-width 1.6, round caps and joins, 18x18). Pass the inner markup exactly as in the JS: it is wrapped in that
// root, "currentColor" is replaced by the colour, rendered with QSvgRenderer and cached per markup, size, colour,
// device pixel ratio and theme revision.
#include <QPixmap>
#include <QIcon>
#include <QColor>
#include <QString>
#include <QRectF>

class QPainter;

namespace charts::ui::icons {

QPixmap pixmap(const QString& inner, const QColor& color, int px = 18, qreal dpr = 2.0);
QIcon icon(const QString& inner, const QColor& color, int px = 18);
void paint(QPainter& p, const QRectF& r, const QString& inner, const QColor& color);
// A complete SVG document (other viewBox, own colours: the brand logo), cached the same way.
QPixmap svg(const QString& document, QSize size, qreal dpr = 2.0);
// The topbar's brand square: <rect rx=7 fill=accent/> + the white chart path, at px x px.
QPixmap brand(int px, const QColor& accent, const QColor& stroke, qreal dpr = 2.0);

// Inner markup of the icons the page shell uses (index.html, app.js). Others pass their own strings.
inline constexpr const char* kIndicators = R"(<path d="M3 17l5-6 4 3 6-8 3 3" />)";
// phone layout (native parity, 2026-11-22): the topbar's "more" menu and the side panel's toggle
inline constexpr const char* kMore = R"(<circle cx="5" cy="12" r="1.3" /><circle cx="12" cy="12" r="1.3" /><circle cx="19" cy="12" r="1.3" />)";
inline constexpr const char* kSide = R"(<rect x="3" y="4" width="18" height="16" rx="2" /><path d="M15 4v16" />)";
inline constexpr const char* kCompare = R"(<circle cx="12" cy="12" r="8" /><path d="M12 6v12M6 12h12" />)";
inline constexpr const char* kReplay = R"(<path d="M4 12a8 8 0 1 0 3-6.2M4 4v4h4" />)";
inline constexpr const char* kAlert = R"(<path d="M6 16V11a6 6 0 1 1 12 0v5l2 2H4zM10 20h4" />)";
inline constexpr const char* kCamera =
    R"(<rect x="3" y="6" width="18" height="14" rx="2" /><circle cx="12" cy="13" r="3.5" /><path d="M8 6l2-3h4l2 3" />)";
inline constexpr const char* kDownload = R"(<path d="M12 4v11M7 10l5 5 5-5M5 20h14" />)";
inline constexpr const char* kGear =
    R"(<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z"/>)";
inline constexpr const char* kMoon = R"(<path d="M20 14.5A8 8 0 1 1 9.5 4a6.5 6.5 0 0 0 10.5 10.5z" />)";
inline constexpr const char* kClose = R"(<path d="M6 6l12 12M18 6L6 18"/>)";
inline constexpr const char* kTrash = R"(<path d="M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3"/>)";
inline constexpr const char* kClearAll = R"(<path d="M5 5l14 14M19 5L5 19"/>)";
inline constexpr const char* kChevronUp = R"(<path d="M6 15l6-6 6 6" />)";
inline constexpr const char* kEye = R"(<path d="M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>)";
inline constexpr const char* kSun =
    R"(<circle cx="12" cy="12" r="3"/><path d="M12 2v3M12 19v3M4.2 4.2l2.1 2.1M17.7 17.7l2.1 2.1M2 12h3M19 12h3M4.2 19.8l2.1-2.1M17.7 6.3l2.1-2.1"/>)";

}  // namespace charts::ui::icons
