#include "pages/economy/MacroPage.h"
#include "pages/economy/AssetFile.h"
#include "pages/economy/GlobeWidget.h"
#include "pages/economy/EcoKit.h"
#include "macro/MacroStore.h"
#include "macro/Registry.h"
#include "macro/CentralBanks.h"
#include "macro/Watchlist.h"
#include "core/Settings.h"
#include <QMenu>
#include <QContextMenuEvent>
#include "app/Pages.h"
#include "ui/Theme.h"
#include "ui/Touch.h"
#include <QPainter>
#include <QPainterPath>
#include <QStackedWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QApplication>
#include <QPaintEvent>
#include <QTimer>
#include <QCoreApplication>
#include <QScroller>
#include <QScrollerProperties>
#include <QScrollPrepareEvent>
#include <functional>
#include <cmath>

namespace economy {

using namespace macro;
static QColor C(const char* h) { return theme::map(QColor(h)); }   // Deepwater colour → current theme

namespace eco_macro {
// ------------------------------------------------------------------------------------------------ top bar
class TopBar : public QWidget {
public:
  explicit TopBar(QWidget* p) : QWidget(p) { setFixedHeight(36); }
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(0, 0, 0, height());
    g.setColorAt(0, C("#150f03")); g.setColorAt(1, C("#0d0800"));
    p.fillRect(rect(), g);
    p.fillRect(QRectF(0, height() - 1, width(), 1), C("#444124"));
    // globe icon
    const QPointF ic(18, 17);
    p.setPen(QPen(C("#cdc284"), 1.1)); p.setBrush(Qt::NoBrush);
    p.drawEllipse(ic, 5.5, 5.5); p.drawEllipse(ic, 2.3, 5.5);
    p.drawLine(ic + QPointF(-5.5, 0), ic + QPointF(5.5, 0));
    const bool on = eco::Blink::instance().on();
    p.setPen(Qt::NoPen); p.setBrush(on ? C("#f5c518") : C("#736f13"));
    p.drawEllipse(QPointF(38, 17), 2.6, 2.6);
    // phones: the coverage pill alone (the title would run under it), in a smaller face so it fits 430 px
    const bool phone = ui::compact(width());
    if (!phone) {
      const QString t = QCoreApplication::translate("economy", "GLOBAL ASSET NETWORK — LIVE SURVEILLANCE");
      const qreal adv = eco::micro(p, 50, 21, t, C("#5b574e"), 11, 3.5);
      if (eco::Blink::instance().phase() % 3 != 2) p.fillRect(QRectF(50 + adv + 4, 11, 6, 11), C("#f5c518"));
    }
    // coverage pill
    const auto cov = MacroStore::instance().coverage();
    const QString pill = QCoreApplication::translate("economy", "• %1 ECONOMIES · %2 MONTHLY · %3 LIVE FEEDS")
                           .arg(cov.economies).arg(cov.monthly).arg(cov.liveFeeds);
    const QFont f = phone ? eco::mono(9, QFont::Bold, 0.3) : eco::mono(10, QFont::Bold, 1);
    const qreal pw = phone ? std::min(QFontMetricsF(f).horizontalAdvance(pill) + 14, width() - 50 - 8.0)
                           : QFontMetricsF(f).horizontalAdvance(pill) + 18;
    const QRectF pr(phone ? 50 : width() - 15 - pw, 9, pw, 18);
    p.setPen(QPen(C("#9b8720"), 1)); p.setBrush(C("#4f3f00"));
    p.drawRoundedRect(pr.adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);
    p.setFont(f); p.setPen(C("#e7d592"));
    p.drawText(pr, Qt::AlignCenter, phone ? QFontMetricsF(f).elidedText(pill, Qt::ElideRight, pr.width() - 8) : pill);
    setToolTip(QCoreApplication::translate("economy", "Economies in the registry · economies with monthly official series · economies with a daily policy-rate feed "
               "(central bank or BIS; the euro area counts once)"));
  }
};

// ------------------------------------------------------------------------------------------------ registry
class Registry : public QWidget {
public:
  std::function<void(const QString&)> onHover, onClick;
  std::function<void()> onGroup;   // the tab changed (the page tints the globe for ☆ WATCH)
  explicit Registry(QWidget* p) : QWidget(p) {
    setMouseTracking(true);
    setList(Group(std::clamp(settings::value("economy.macro.registryGroup", 0).toInt(), 0, kGroupCount - 1)));
    // a finger drags the list (QScroller: kinetic, and a tap still opens the row); the mouse keeps the wheel
    if (ui::touch::enabled()) {
#ifdef Q_OS_IOS
      QScroller::grabGesture(this, QScroller::TouchGesture);
#else
      QScroller::grabGesture(this, QScroller::LeftMouseButtonGesture);
#endif
      QScrollerProperties sp = QScroller::scroller(this)->scrollerProperties();
      sp.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy, QScrollerProperties::OvershootAlwaysOff);
      sp.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy, QScrollerProperties::OvershootAlwaysOff);
      sp.setScrollMetric(QScrollerProperties::DragStartDistance, 0.004);
      QScroller::scroller(this)->setScrollerProperties(sp);
    }
  }
  // phones: group tabs spread over the width and 30 px tall (finger targets), rows start lower to make room
  void setPhone(bool on) { if (on != m_phone) { m_phone = on; m_scroll = std::clamp(m_scroll, 0.0, maxScroll()); update(); } }
  // large windows: the whole panel is drawn at a scale (338 logical px wide) so it does not get lost
  // the page sets the geometry (338 * k wide, minus the 2 px gap): no fixed width, or the widget pokes 2 px out
  void setScale(qreal k) { m_k = k; update(); }
  qreal lw() const { return width() / m_k; }
  qreal lh() const { return height() / m_k; }
  std::function<bool(const Economy&)> filter;   // cadence filter from the bottom controls
  void setList(Group g) {
    const bool same = g == m_group;
    m_group = g; m_list.clear();
    for (const auto& e : registry(g)) if (!filter || filter(e)) m_list << e;
    if (!same) m_scroll = 0;
    m_scroll = std::clamp(m_scroll, 0.0, maxScroll());
    update();
  }
  void choose(Group g) {   // a click on a tab: remembered for the next launch
    setList(g);
    settings::setValue("economy.macro.registryGroup", int(g));
    if (onGroup) onGroup();
  }
  Group group() const { return m_group; }
  int rowTop() const { return m_phone ? 76 : 66; }
  qreal pitch() const { return 31.7; }
  // seven tabs (six groups and ☆ WATCH) sized from their labels and spread over the width: the free room is shared
  // as padding, or taken back from it when the labels are wider than the panel (French, narrow windows)
  QFont tabFont() const { return eco::mono(m_phone ? 10 : 9, QFont::Bold, m_phone ? 0.5 : 0.2); }
  QRectF tabRect(int i) const {
    const QFontMetricsF fm(tabFont());
    qreal text[kGroupCount], sum = 0;
    for (int k = 0; k < kGroupCount; ++k) { text[k] = fm.horizontalAdvance(groupLabel(Group(k))); sum += text[k]; }
    const qreal avail = lw() - 12, gap = 2;
    const qreal pad = std::clamp((avail - sum - gap * (kGroupCount - 1)) / kGroupCount, 2.0, m_phone ? 40.0 : 12.0);
    qreal x = 6;
    for (int k = 0; k < i; ++k) x += text[k] + pad + gap;
    return m_phone ? QRectF(x, 38, text[i] + pad, 30) : QRectF(x, 41, text[i] + pad, 18);
  }
  // the ☆ of a row: left of the rate badge
  QRectF starRect(int row) const {
    const qreal y = rowTop() + row * pitch() - m_scroll;
    const QString badge = rateBadge(m_list[row]);
    const qreal bw = QFontMetricsF(eco::mono(9, QFont::Bold, 0.5)).horizontalAdvance(badge) + 16;
    return QRectF(lw() - 8 - bw - 24, y + (pitch() - 20) / 2, 20, 20);
  }
  int rowAt(const QPointF& pos) const {
    if (pos.y() < rowTop()) return -1;
    const int i = int((pos.y() - rowTop() + m_scroll) / pitch());
    return i >= 0 && i < m_list.size() ? i : -1;
  }
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), C("#060401"));
    p.scale(m_k, m_k);
    // header
    p.fillRect(QRectF(0, 2, lw(), 30), C("#1b1302"));
    p.fillRect(QRectF(0, 32, lw(), 1), C("#2a2410"));
    p.setPen(Qt::NoPen); p.setBrush(eco::Blink::instance().on() ? C("#6b5f26") : C("#4a421a"));
    p.drawEllipse(QPointF(15, 17), 2.5, 2.5);
    const qreal adv = eco::micro(p, 28, 21, QCoreApplication::translate("economy", "ASSET REGISTRY"), C("#605a48"), 11, 3);
    if (eco::Blink::instance().phase() % 3 != 2) p.fillRect(QRectF(28 + adv + 3, 11, 6, 11), C("#f5c518"));
    p.setFont(eco::mono(11)); p.setPen(C("#5c5744"));
    p.drawText(QRectF(0, 2, lw() - 12, 30), Qt::AlignRight | Qt::AlignVCenter, QString::number(m_list.size()));
    // tabs
    for (int i = 0; i < kGroupCount; ++i) {
      const QRectF r = tabRect(i);
      const bool sel = int(m_group) == i, hov = r.contains(m_mouse);
      if (sel) { p.setPen(Qt::NoPen); p.setBrush(C("#ffd900")); p.drawRoundedRect(r, 2, 2); }
      p.setFont(tabFont());
      p.setPen(sel ? theme::onAccent() : hov ? C("#fcd700") : C("#848178"));
      p.drawText(r, Qt::AlignCenter, groupLabel(Group(i)));
    }
    // rows
    p.save();
    p.setClipRect(QRectF(0, rowTop(), lw(), lh() - rowTop()));
    const int hover = rowAt(m_mouse);
    for (int i = 0; i < m_list.size(); ++i) {
      const qreal y = rowTop() + i * pitch() - m_scroll;
      if (y + pitch() < rowTop() || y > lh()) continue;
      const Economy& e = m_list[i];
      const bool live = hasPolicyRate(e), hov = i == hover;
      const QRectF r(0, y, lw(), pitch() - 1);
      if (live || hov) p.fillRect(r, hov ? C("#2a2208") : C("#1e1801"));
      if (live || hov) p.fillRect(QRectF(0, y, 3, pitch() - 1), hov ? C("#fff09a") : C("#d8c463"));
      const qreal dx = hov ? 5 : 0;
      p.setFont(eco::mono(11)); p.setPen(C("#535037"));
      p.drawText(QRectF(10 + dx, y, 24, pitch()), Qt::AlignVCenter | Qt::AlignLeft, QString("%1").arg(i + 1, 2, 10, QChar('0')));
      p.drawPixmap(QPointF(33 + dx, y + (pitch() - 14) / 2), macro::flag(e.iso2, QSize(21, 14)));
      p.setFont(eco::display(13, QFont::Bold, 0.3)); p.setPen(C("#efebdf"));
      p.drawText(QRectF(62 + dx, y, 220, pitch()), Qt::AlignVCenter | Qt::AlignLeft,
                 QFontMetricsF(eco::display(13, QFont::Bold, 0.3)).elidedText(displayName(e).toUpper(), Qt::ElideRight, lw() - 62 - dx - 84));
      // ☆ watchlist: filled for a listed country, an outline on the hovered row (a click toggles it)
      const bool listed = Watchlist::instance().contains(e.iso2);
      if (listed || hov) {
        const QRectF sr = starRect(i);
        p.setFont(eco::mono(13, QFont::Bold));
        p.setPen(listed ? C("#ffd900") : sr.contains(m_mouse) ? C("#fff09a") : C("#6b6346"));
        p.drawText(sr, Qt::AlignCenter, QString::fromUtf8(listed ? "★" : "☆"));
      }
      const QString badge = rateBadge(e);
      const QFont bf = eco::mono(9, QFont::Bold, 0.5);
      const qreal bw = QFontMetricsF(bf).horizontalAdvance(badge) + 16;
      const QRectF br(lw() - 8 - bw, y + (pitch() - 15) / 2, bw, 15);
      const bool dim = badge == "WB";
      p.setPen(QPen(dim ? C("#2a2510") : C("#786504"), 1)); p.setBrush(dim ? C("#1c1805") : C("#544500"));
      p.drawRoundedRect(br.adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);
      p.setPen(Qt::NoPen); p.setBrush(dim ? C("#615e51") : C("#eadf92"));
      p.drawEllipse(QPointF(br.left() + 5, br.center().y()), 1.3, 1.3);
      p.setFont(bf); p.setPen(dim ? C("#615e51") : C("#eadf92"));
      p.drawText(br.adjusted(6, 0, 0, 0), Qt::AlignCenter, badge);
    }
    if (m_list.isEmpty() && m_group == Group::Watch) {
      p.setFont(eco::mono(10)); p.setPen(C("#848178"));
      p.drawText(QRectF(16, rowTop() + 16, lw() - 32, 120), Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap,
                 QCoreApplication::translate("economy", "Your watchlist is empty. Add a country with the ☆ of its row in the other tabs, or with a right-click on the globe."));
    }
    // the list scrolls with the wheel (small windows): a thin thumb and a fade at the cut edge say so
    if (const qreal maxS = maxScroll(); maxS > 0) {
      const qreal view = lh() - rowTop(), th = std::max(24.0, view * view / (view + maxS));
      const qreal ty = rowTop() + (view - th) * (m_scroll / maxS);
      p.setPen(Qt::NoPen); p.setBrush(C("#5c5744"));
      p.drawRoundedRect(QRectF(lw() - 3, ty + 2, 2, th - 4), 1, 1);
      if (m_scroll < maxS - 0.5) {
        QLinearGradient g(0, lh() - 22, 0, lh());
        g.setColorAt(0, QColor(6, 4, 1, 0)); g.setColorAt(1, C("#060401"));
        p.fillRect(QRectF(0, lh() - 22, lw() - 4, 22), g);
      }
    }
    p.restore();
  }
  qreal maxScroll() const { return std::max(0.0, rowTop() + m_list.size() * pitch() - lh() + 8); }
  void resizeEvent(QResizeEvent*) override { m_scroll = std::clamp(m_scroll, 0.0, maxScroll()); }
  void mouseMoveEvent(QMouseEvent* e) override {
    m_mouse = e->position() / m_k;
    const int r = rowAt(m_mouse);
    const QString iso = r >= 0 ? m_list[r].iso2 : QString();
    if (iso != m_hoverIso) { m_hoverIso = iso; if (onHover) onHover(iso); }
    bool hand = r >= 0;
    for (int i = 0; i < kGroupCount; ++i) hand |= tabRect(i).contains(m_mouse);
    setToolTip(r >= 0 && starRect(r).contains(m_mouse)
                   ? (Watchlist::instance().contains(m_list[r].iso2) ? QCoreApplication::translate("economy", "Remove from the watchlist")
                                                                       : QCoreApplication::translate("economy", "Add to the watchlist"))
                   : QString());
    setCursor(hand ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
  }
  void leaveEvent(QEvent*) override {
    m_mouse = {-1, -1};
    if (!m_hoverIso.isEmpty()) { m_hoverIso.clear(); if (onHover) onHover({}); }
    update();
  }
  void mousePressEvent(QMouseEvent* e) override {
    const QPointF pos = e->position() / m_k;
    for (int i = 0; i < kGroupCount; ++i) if (tabRect(i).contains(pos)) { choose(Group(i)); return; }
    const int r = rowAt(pos);
    if (r >= 0 && starRect(r).contains(pos)) { Watchlist::instance().toggle(m_list[r].iso2); return; }
    if (r >= 0 && onClick) onClick(m_list[r].iso2);
  }
  void wheelEvent(QWheelEvent* e) override {
    const qreal dy = !e->pixelDelta().isNull() ? e->pixelDelta().y() / m_k : e->angleDelta().y() / 120.0 * 60;
    m_scroll = std::clamp(m_scroll - dy, 0.0, maxScroll());
    update();
  }
  // finger scrolling (QScroller, touch only): the scroller moves m_scroll in widget pixels
  bool event(QEvent* e) override {
    if (e->type() == QEvent::ScrollPrepare) {
      auto* se = static_cast<QScrollPrepareEvent*>(e);
      se->setViewportSize(QSizeF(size()));
      se->setContentPosRange(QRectF(0, 0, 0, maxScroll() * m_k));
      se->setContentPos(QPointF(0, m_scroll * m_k));
      se->accept();
      return true;
    }
    if (e->type() == QEvent::Scroll) {
      m_scroll = std::clamp(static_cast<QScrollEvent*>(e)->contentPos().y() / m_k, 0.0, maxScroll());
      update();
      return true;
    }
    return QWidget::event(e);
  }
private:
  bool m_phone = false;
  Group m_group = Group::G20;
  QList<Economy> m_list;
  QPointF m_mouse{-1, -1};
  QString m_hoverIso;
  qreal m_scroll = 0;
  qreal m_k = 1;
};

// ------------------------------------------------------------------------------------------------ bottom controls
class Controls : public QWidget {
public:
  std::function<void()> onReset, onToggle;
  bool live = true, monthly = true, annual = true;
  explicit Controls(QWidget* p) : QWidget(p) { setMouseTracking(true); setAttribute(Qt::WA_TranslucentBackground); }
  // phones: the same controls 1.3x wider and 30 px tall, finger targets (the page gives them a 48 px band)
  bool phone = false;
  qreal k() const { return phone ? 1.3 : 1.0; }
  QRectF resetRect() const { return phone ? QRectF(8, height() - 38, 30, 30) : QRectF(8, height() - 31, 22, 22); }
  QRectF frameRect() const { return QRectF(segRect(0).left() - 4, segRect(0).top() - 4, 230 * k(), segRect(0).height() + 8); }
  QRectF segRect(int i) const {
    const qreal right = width() - 8, w0 = 230 * k();
    const qreal x0 = right - w0;
    static const qreal xs[3] = {4, 88, 164}, ws[3] = {80, 72, 62};
    if (phone) return QRectF(x0 + xs[i] * k(), height() - 37, ws[i] * k(), 28);
    return QRectF(x0 + xs[i], height() - 29, ws[i], 18);
  }
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF rr = resetRect();
    p.setPen(QPen(rr.contains(m_mouse) ? C("#f5c518") : C("#8a7a2a"), 1)); p.setBrush(C("#3f3200"));
    p.drawRect(rr.adjusted(0.5, 0.5, -0.5, -0.5));
    p.setPen(QPen(C("#cdc284"), 1.2)); p.setBrush(Qt::NoBrush);
    const QPointF c = rr.center();
    p.drawArc(QRectF(c.x() - 5, c.y() - 5, 10, 10), 40 * 16, 280 * 16);
    p.drawLine(c + QPointF(3.8, -4.6), c + QPointF(4.3, -1.6));
    const QRectF frame = frameRect();   // desktop: (left - 4, height - 33, 230, 26)
    p.setPen(QPen(C("#2a2418"), 1)); p.setBrush(Qt::NoBrush); p.drawRect(frame.adjusted(0.5, 0.5, -0.5, -0.5));
    const QStringList labs{QCoreApplication::translate("economy", "• LIVE FEED"), QCoreApplication::translate("economy", "• MONTHLY"), QCoreApplication::translate("economy", "• ANNUAL")};
    const bool st[3] = {live, monthly, annual};
    for (int i = 0; i < 3; ++i) {
      const QRectF r = segRect(i);
      QColor fill = Qt::transparent, border = C("#24211a"), text = C("#3e3c36");
      if (st[i]) {
        if (i == 0) { fill = C("#403300"); border = C("#c9a514"); text = C("#e9dca1"); }
        else if (i == 1) { border = C("#7a6614"); text = C("#b1a776"); }
        else { border = C("#3a3626"); text = C("#6e6a60"); }   // reference: ANNUAL reads dim even when on
      }
      if (r.contains(m_mouse)) text = text.lighter(130);
      p.setPen(QPen(border, 1)); p.setBrush(fill); p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
      p.setFont(eco::mono(phone ? 10 : 9, QFont::Bold, 1)); p.setPen(text);
      p.drawText(r, Qt::AlignCenter, labs[i]);
    }
  }
  bool hit(const QPointF& pos) const {
    if (resetRect().contains(pos)) return true;
    for (int i = 0; i < 3; ++i) if (segRect(i).contains(pos)) return true;
    return false;
  }
  void mouseMoveEvent(QMouseEvent* e) override { m_mouse = e->position(); setCursor(hit(m_mouse) ? Qt::PointingHandCursor : Qt::ArrowCursor); update(); }
  void leaveEvent(QEvent*) override { m_mouse = {-1, -1}; update(); }
  void mousePressEvent(QMouseEvent* e) override {
    if (resetRect().contains(e->position())) { if (onReset) onReset(); return; }
    bool* st[3] = {&live, &monthly, &annual};
    for (int i = 0; i < 3; ++i) if (segRect(i).contains(e->position())) { *st[i] = !*st[i]; if (onToggle) onToggle(); update(); return; }
    e->ignore();
  }
private:
  QPointF m_mouse{-1, -1};
};

// background of the globe view
class GlobeView : public QWidget {
public:
  explicit GlobeView(QWidget* p) : QWidget(p) {}
  bool phone = false;   // registry under the globe: the gradient spans the whole width
  // The gradient is painted where exposed, not kept as a full-size 2x pixmap (~20 MB): the opaque globe covers almost
  // all of it and this view repaints only on resize or around the side widgets.
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    const QRectF area(0, 0, phone ? width() : width() - 338, height());
    QRadialGradient g(QPointF(area.width() * 0.25, area.height() * 0.85), area.width() * 0.9);
    g.setColorAt(0, C("#0f0a03")); g.setColorAt(1, C("#040301"));
    p.fillRect(area, g);
  }
};
}

using namespace eco_macro;

MacroPage::MacroPage(QWidget* parent) : QWidget(parent) {
  setAutoFillBackground(true);
  QPalette pl = palette(); pl.setColor(QPalette::Window, eco::pageBg()); setPalette(pl);
  m_top = new TopBar(this);
  m_stack = new QStackedWidget(this);

  m_globeView = new GlobeView(m_stack);
  m_globe = new GlobeWidget(m_globeView);
  m_registry = new Registry(m_globeView);
  m_controls = new Controls(m_globeView);
  m_stack->addWidget(m_globeView);

  m_scroll = new QScrollArea(m_stack);
  eco::setupScroll(m_scroll);
  m_file = new AssetFile;
  m_scroll->setWidget(m_file);
  m_stack->addWidget(m_scroll);

  m_registry->onHover = [this](const QString& iso) {
    if (iso.isEmpty()) { m_globe->setHighlight({}); return; }
    const Economy* e = macro::economy(iso);
    if (!e) return;
    QStringList isos{iso};
    if (iso == "ez") for (const auto& x : economies()) if (isEuroMember(x)) isos << x.iso2;
    m_globe->setHighlight(isos);
    m_globe->flyTo(e->lat, e->lon);
  };
  m_registry->onClick = [this](const QString& iso) { openEconomy(iso); };
  connect(m_globe, &GlobeWidget::clicked, this, [this](const QString& iso) {
    const Economy* e = macro::economy(iso);
    if (e) openEconomy(e->iso2);
  });
  m_registry->onGroup = [this] { updateTint(); };
  // the watchlist from the globe: right-click a country
  m_globe->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_globe, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
    const Economy* e = macro::economy(m_globe->countryAtPoint(pos));
    if (!e) return;
    QMenu menu(this);
    const bool listed = Watchlist::instance().contains(e->iso2);
    menu.addAction(listed ? QCoreApplication::translate("economy", "Remove %1 from the watchlist").arg(displayName(*e))
                          : QCoreApplication::translate("economy", "Add %1 to the watchlist").arg(displayName(*e)),
                   this, [iso = e->iso2] { Watchlist::instance().toggle(iso); });
    menu.addAction(QCoreApplication::translate("economy", "Open %1").arg(displayName(*e)), this, [this, iso = e->iso2] { openEconomy(iso); });
    menu.exec(m_globe->mapToGlobal(pos));
  });
  connect(&Watchlist::instance(), &Watchlist::changed, this, [this] { m_registry->setList(m_registry->group()); updateTint(); });
  m_controls->onReset = [this] { m_globe->resetView(); };
  m_controls->onToggle = [this] { updateTint(); };
  connect(m_file, &AssetFile::closeRequested, this, &MacroPage::closeFile);

  auto& ms = MacroStore::instance();
  connect(&ms, &MacroStore::updated, this, [this] { m_top->update(); m_registry->update(); });
  connect(&eco::Blink::instance(), &eco::Blink::tick, this, [this] { m_top->update(QRect(0, 0, 420, 36)); m_registry->update(QRect(0, 0, 300, 32)); });
  updateTint();   // the registry's filter, and the globe lit when the saved tab is ☆ WATCH
}

void MacroPage::updateTint() {
  // LIVE FEED / MONTHLY / ANNUAL filter the registry by data cadence (the reference globe shows no tint):
  // live = a policy-rate feed (central bank or BIS), monthly = monthly official series, annual = World Bank only.
  auto& ms = MacroStore::instance();
  const bool live = m_controls->live, monthly = m_controls->monthly, annual = m_controls->annual;
  m_registry->filter = [&ms, live, monthly, annual](const Economy& e) {
    if (hasPolicyRate(e)) return live;
    const Series s = ms.series(e.iso2, Ind::Inflation);
    if (s.ok() && s.freq == 'M') return monthly;
    return annual;
  };
  m_registry->setList(m_registry->group());
  // ☆ WATCH: the listed countries lit on the globe (the euro area lights its members)
  QHash<QString, int> tint;
  if (m_registry->group() == Group::Watch)
    for (const QString& iso : Watchlist::instance().codes()) {
      tint.insert(iso, 90);
      if (iso == "ez") for (const auto& x : economies()) if (isEuroMember(x)) tint.insert(x.iso2, 90);
    }
  m_globe->setTint(tint);
}

void MacroPage::resizeEvent(QResizeEvent*) {
  m_top->setGeometry(0, 0, width(), 36);
  m_stack->setGeometry(0, 36, width(), height() - 36);
  // phone held upright: the globe on top at full width (its controls under it, finger-sized), the registry below with
  // the rest of the height; its list scrolls by finger (QScroller). A phone held sideways keeps the desktop layout.
  const bool phone = ui::compact(width());
  m_registry->setPhone(phone);
  m_controls->phone = phone;
  static_cast<GlobeView*>(m_globeView)->phone = phone;
  if (phone) {
    const int W = m_stack->width(), H = m_stack->height();
    const int gh = std::clamp(int(W * 0.93), 260, std::max(260, H / 2));
    m_registry->setScale(1);
    m_globe->setGeometry(0, 0, W, gh);
    m_controls->setGeometry(0, gh - 48, W, 48);
    m_registry->setGeometry(0, gh + 2, W, std::max(0, H - gh - 2));
    m_file->setViewportHeight(H);
    return;
  }
  // registry grows with the window on large screens (338 px at the reference width, up to 460 px)
  const qreal k = std::clamp(std::sqrt(width() / 1600.0), 1.0, 460.0 / 338.0);
  m_registry->setScale(k);
  const int rw = int(std::round(338 * k));
  const int gw = m_stack->width() - rw;
  m_globe->setGeometry(0, 0, gw, m_stack->height());
  m_registry->setGeometry(gw + 2, 2, rw - 2, m_stack->height() - 2);
  m_controls->setGeometry(0, m_stack->height() - 40, gw, 40);
  m_file->setViewportHeight(m_stack->height());
}

void MacroPage::openEconomy(const QString& iso2) {
  m_file->setEconomy(iso2);
  m_scroll->verticalScrollBar()->setValue(0);
  m_stack->setCurrentWidget(m_scroll);
  m_file->setFocus();
}
void MacroPage::closeFile() {
  m_stack->setCurrentWidget(m_globeView);
  m_globe->setHighlight({});
}

void MacroPage::showEvent(QShowEvent*) {
  MacroStore::instance().ensureLoaded();
  eco::Blink::instance().hold(this, true);
  if (!m_hooksDone) { m_hooksDone = true; QTimer::singleShot(0, this, &MacroPage::applyHooks); }
}
void MacroPage::hideEvent(QHideEvent*) { eco::Blink::instance().hold(this, false); }
void MacroPage::preload() { MacroStore::instance().ensureLoaded(); }

void MacroPage::applyHooks() {
  const QByteArray grp = qgetenv("DW_MACRO_GROUP");
  if (!grp.isEmpty()) { m_registry->setList(Group(std::clamp(grp.toInt(), 0, kGroupCount - 1))); updateTint(); }
  const QByteArray c = qgetenv("DW_MACRO_COUNTRY");
  if (!c.isEmpty()) openEconomy(QString::fromLatin1(c));
  if (qEnvironmentVariableIsSet("DW_MACRO_TAB")) m_file->setTab(qEnvironmentVariableIntValue("DW_MACRO_TAB"));
  const QByteArray d = qgetenv("DW_MACRO_DETAIL");
  if (!d.isEmpty()) {
    static const QHash<QByteArray, Ind> m{{"inflation", Ind::Inflation}, {"rate", Ind::PolicyRate}, {"gdp", Ind::GdpQoq},
                                          {"unemployment", Ind::Unemployment}, {"confidence", Ind::BusinessConf}};
    m_file->showDetail(m.value(d, Ind::Inflation));
  }
  const int sc = qEnvironmentVariableIntValue("DW_MACRO_SCROLL");
  const QByteArray hv = qgetenv("DW_MACRO_HOVER");
  // let data arrive, then scroll / hover
  QTimer::singleShot(qEnvironmentVariableIsSet("DW_MACRO_HOOK_DELAY") ? qEnvironmentVariableIntValue("DW_MACRO_HOOK_DELAY") : 3000, this, [this, sc, hv] {
    if (sc > 0) m_scroll->verticalScrollBar()->setValue(sc);
    if (!hv.isEmpty()) {
      const auto parts = hv.split(',');
      if (parts.size() == 2) {
        const QPoint pt(parts[0].toInt(), parts[1].toInt());
        QWidget* w = childAt(pt);
        if (w) {
          const QPointF local = w->mapFrom(this, QPointF(pt));
          if (w == m_file) m_file->setHoverPoint(local);
          else {
            QMouseEvent ev(QEvent::MouseMove, local, w->mapToGlobal(local), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(w, &ev);
          }
        }
      }
    }
  });
}

DW_PAGE("economy/macro", MacroPage)
}  // namespace economy
