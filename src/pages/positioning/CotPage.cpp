#include "pages/positioning/CotPage.h"
#include "pages/positioning/CotPanels.h"
#include "pages/positioning/PosKit.h"
#include "app/Pages.h"
#include "core/Settings.h"
#include "ui/Theme.h"
#include "ui/Kit.h"
#include "ui/Format.h"
#include "core/I18n.h"
#include "ui/Touch.h"
#include <QCoreApplication>
#include <QPainter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTimeZone>
#include <QTimer>
#include <QMap>
#include <cmath>
#include <algorithm>


using positioning::Cot;
using positioning::CotHistory;
using positioning::CotMarket;

namespace positioning {
// ------------------------------------------------------------------------------------------------ header
class CotHeader : public QWidget {
public:
  explicit CotHeader(QWidget* p) : QWidget(p) {
    setFixedHeight(37);
    cls = new pk::Segmented({QCoreApplication::translate("positioning", "CURRENCIES"), QCoreApplication::translate("positioning", "COMMODITIES"), QCoreApplication::translate("positioning", "INDICES")}, pk::Segmented::Yellow, theme::mono(10, QFont::Medium, 0.8), this, true);
    cls->setPadding(7, 18); cls->setInactiveColor(theme::map(0x80, 0x7d, 0x74));
  }
  pk::Segmented* cls;
  // phone upright: the asset classes go on a second row, finger-sized
  void setPhone(bool on) {
    if (on == m_phone) return;
    m_phone = on;
    cls->setPadding(on ? 12 : 7, on ? 28 : 18);
    setFixedHeight(on ? 37 + 40 : 37);
    resizeEvent(nullptr); update();
  }
protected:
  void resizeEvent(QResizeEvent*) override {
    cls->resize(cls->sizeHint());
    if (m_phone) cls->move(10, 37);
    else cls->move(width() - cls->width() - 11, 6);
  }
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    QLinearGradient g(0, 0, width(), 0); g.setColorAt(0, theme::map(0x0d, 0x09, 0x03)); g.setColorAt(1, theme::map(0x14, 0x10, 0x05));
    p.fillRect(rect(), g);
    p.fillRect(QRect(0, height() - 1, width(), 1), theme::map(0x27, 0x23, 0x0f));
    const QFont tf = theme::condensed(15, QFont::Bold);
    p.setFont(tf); p.setPen(theme::map(0xf8, 0xf5, 0xec));
    const qreal cy = 37 / 2.0;   // the title row (the phone layout adds a second row below it)
    const QString title = QCoreApplication::translate("positioning", "COT Advanced");
    p.drawText(QPointF(10, cy + QFontMetricsF(tf).capHeight() / 2), title);
    const qreal x = 10 + QFontMetricsF(tf).horizontalAdvance(title) + 8;
    const QFont sf = theme::mono(9, QFont::Normal, 0.8);
    p.setFont(sf); p.setPen(theme::map(0x45, 0x41, 0x38));
    const QString sub = QCoreApplication::translate("positioning", "CFTC · FREE, WEEKLY, OFFICIAL");
    p.drawText(QPointF(x, cy + QFontMetricsF(sf).capHeight() / 2), m_phone ? QFontMetricsF(sf).elidedText(sub, Qt::ElideRight, width() - x - 10) : sub);
  }
private:
  bool m_phone = false;
};

// ------------------------------------------------------------------------------------------------ market buttons
class CotMarketBar : public QWidget {
public:
  explicit CotMarketBar(QWidget* p) : QWidget(p) { setMouseTracking(true); setCursor(Qt::PointingHandCursor); setFixedHeight(38); }
  std::function<void(const QString&)> onPick;
  QWidget* rightWidget = nullptr;
  void setMarkets(const QList<CotMarket>& m, const QString& cur) { m_m = m; m_cur = cur; update(); }
  void setCurrent(const QString& id) { m_cur = id; update(); }
  // phone upright: finger-sized buttons that wrap onto as many rows as needed, the range after them (its own row when
  // it does not fit on the last one); the bar is then as tall as phoneHeight()
  void setPhone(bool on) { if (on != m_phone) { m_phone = on; update(); } }
  int phoneHeight() const { return int(std::ceil(rightRect().bottom())) + 6; }
  void setMarketsPhone(int w) { resize(w, height()); setFixedHeight(m_phone ? phoneHeight() : 38); resizeEvent(nullptr); }
protected:
  QList<QRectF> rects() const { return rects(width()); }
  QList<QRectF> rects(int W) const {
    QList<QRectF> out; qreal x = 10, y = 6;
    const QFontMetricsF fm(theme::mono(11));
    for (const auto& m : m_m) {
      qreal w = fm.horizontalAdvance(QCoreApplication::translate("positioning", m.label.toUtf8().constData())) + (m.flagCode.isEmpty() ? 16 : 33);   // USD button 54 px wide
      if (m_phone) {
        w += 8;
        if (x > 10 && x + w > W - 10) { x = 10; y += 38; }
      }
      out << QRectF(x, y, w, m_phone ? 32 : 25); x += w + 6;
    }
    return out;
  }
  // where the range selector goes on a phone
  QRectF rightRect() const {
    const QSize s = rightWidget ? rightWidget->sizeHint() : QSize(0, 0);
    const auto rs = rects();
    if (rs.isEmpty()) return QRectF(10, 6, s.width(), s.height());
    const QRectF last = rs.last();
    if (last.right() + 12 + s.width() <= width() - 10) return QRectF(width() - 10 - s.width(), last.top() + (last.height() - s.height()) / 2, s.width(), s.height());
    return QRectF(10, last.bottom() + 6, s.width(), s.height());
  }
  void resizeEvent(QResizeEvent*) override {
    if (!rightWidget) return;
    rightWidget->resize(rightWidget->sizeHint());
    if (m_phone) rightWidget->move(rightRect().topLeft().toPoint());
    else rightWidget->move(width() - rightWidget->width() - 11, 6);
  }
  void paintEvent(QPaintEvent*) override {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), pk::col::kPage());
    p.fillRect(QRect(0, height() - 1, width(), 1), theme::map(0x28, 0x28, 0x1a));
    const auto rs = rects();
    for (int i = 0; i < rs.size(); ++i) {
      const bool on = m_m[i].id == m_cur, hov = i == m_hover;
      p.setPen(QPen(on ? theme::map(0x43, 0x38, 0x1e) : hov ? theme::map(0x3d, 0x3a, 0x26) : theme::map(0x2b, 0x2a, 0x1a), 1));
      p.setBrush(on ? theme::map(0x30, 0x24, 0x04) : Qt::transparent);
      p.drawRoundedRect(rs[i].adjusted(.5, .5, -.5, -.5), 2, 2);
      qreal x = rs[i].left() + 7;
      if (!m_m[i].flagCode.isEmpty()) { p.drawPixmap(QPointF(x, rs[i].center().y() - 5.5), ui::flag(m_m[i].flagCode, QSize(16, 11))); x += 20; }
      p.setFont(theme::mono(11)); p.setPen(on ? theme::map(0xd5, 0xc7, 0x88) : theme::map(0xa0, 0x9d, 0x94));
      p.drawText(QRectF(x, rs[i].top(), rs[i].right() - x, rs[i].height()), Qt::AlignVCenter | Qt::AlignLeft, QCoreApplication::translate("positioning", m_m[i].label.toUtf8().constData()));
    }
  }
  void mouseMoveEvent(QMouseEvent* e) override {
    int h = -1; const auto rs = rects();
    for (int i = 0; i < rs.size(); ++i) if (rs[i].contains(e->position())) h = i;
    if (h != m_hover) { m_hover = h; update(); }
  }
  void leaveEvent(QEvent*) override { m_hover = -1; update(); }
  void mousePressEvent(QMouseEvent* e) override {
    const auto rs = rects();
    for (int i = 0; i < rs.size(); ++i) if (rs[i].contains(e->position()) && onPick) onPick(m_m[i].id);
  }
private:
  QList<CotMarket> m_m; QString m_cur; int m_hover = -1; bool m_phone = false;
};

// ------------------------------------------------------------------------------------------------ content
class CotContent : public QWidget {
public:
  explicit CotContent(QWidget* p) : QWidget(p) {
    price = new cotui::PricePanel(this); split = new cotui::SplitPanel(this); donut = new cotui::DonutPanel(this);
    legacyNet = new cotui::NetChart(QCoreApplication::translate("positioning", "LEGACY NET"), this); secondNet = new cotui::NetChart(QCoreApplication::translate("positioning", "TRADERS IN FINANCIAL FUTURES NET"), this);
    nonrepNet = new cotui::NetChart(QCoreApplication::translate("positioning", "NONREPORTABLE (SMALL TRADERS) NET"), this);
    legacyTable = new cotui::ReportTable(this, false); secondTable = new cotui::ReportTable(this, true);
    legacySector = new cotui::SectorPanel(this, false); secondSector = new cotui::SectorPanel(this, true);
    legacyStats = new cotui::StatsPanel(this, false); secondStats = new cotui::StatsPanel(this, true);
    read = new cotui::ReadPanel(this);
    connect(read, &cotui::ReadPanel::neededHeightChanged, this, [this] { QTimer::singleShot(0, this, [this] { resizeEvent(nullptr); }); });
    setMinimumHeight(kHeight);
  }
  static constexpr int kHeight = 2108;
  QString message;
  cotui::PricePanel* price; cotui::SplitPanel* split; cotui::DonutPanel* donut;
  cotui::NetChart *legacyNet, *secondNet, *nonrepNet;
  cotui::ReportTable *legacyTable, *secondTable;
  cotui::SectorPanel *legacySector, *secondSector;
  cotui::StatsPanel *legacyStats, *secondStats;
  cotui::ReadPanel* read;
  int m_advY = 567;
  int m_top = 0;                              // phone mode: height of the title row + market buttons above the panels
  std::function<int(int width)> layoutTop;    // phone mode: lays them out at the top of the content, returns their height
protected:
  // Layout. Reference (1600 wide): summary row of three panels, then two columns (net charts | tables, donuts,
  // stats, read). Below 1320 px the report tables would be squeezed under 640 px, so the page reflows to one column
  // (price on its own row, split + donut below, then every advanced panel full width). Above 1600 px the charts grow
  // in height with the width (up to ×1.35) so wide screens keep sensible proportions.
  void resizeEvent(QResizeEvent*) override {
    const int W = width(), m = 7, g = 8, right = W - 8;
    const double f = std::clamp(W / 1600.0, 1.0, 1.35);
    const int sumH = int(495 * f), netA = int(392 * f), netB = int(382 * f);
    const int readH = std::max(277, read->neededHeight());
    const int t = m_top = layoutTop ? layoutTop(W) : 0;
    for (cotui::Panel* pn : std::initializer_list<cotui::Panel*>{price, split, donut, legacyNet, secondNet, nonrepNet, legacyTable, secondTable,
                                                              legacySector, secondSector, legacyStats, secondStats, read})
      pn->setPhone(W < 520);
    if (W < 520) {
      // phone upright: every panel full width, one under the other, at heights that suit a 390–440 px screen
      const int fw = right - m;
      int y = 49 + t;
      auto put = [&](QWidget* w, int h, int after = 6) { w->setGeometry(m, y, fw, h); y += h + after; };
      put(price, 440, g); put(split, 380, g); put(donut, 360, 0);
      m_advY = y + 23; y += 50;
      put(legacyNet, 340, 5); put(legacyTable, 66 + 4 * cotui::ReportTable::kPhonePitch + 4); put(legacySector, 331); put(legacyStats, 150);
      put(secondNet, 360, 5); put(secondTable, 66 + 6 * cotui::ReportTable::kPhonePitch + 4); put(secondSector, 472); put(secondStats, 205);
      put(nonrepNet, 320); put(read, std::max(277, read->neededHeight()), 0);
      if (minimumHeight() != y + 10) setMinimumHeight(y + 10);
      update();
      return;
    }
    if (W < 1320) {
      int y = 49 + t;
      price->setGeometry(m, y, right - m, sumH); y += sumH + g;
      const int sw = int((right - m - g) * 0.6);
      split->setGeometry(m, y, sw, 420);
      donut->setGeometry(m + sw + g, y, right - m - sw - g, 420); y += 420;
      m_advY = y + 23; y += 50;
      const int fw = right - m, hw = (fw - g) / 2 + 1;
      legacyNet->setGeometry(m, y, fw, netA); y += netA + 5;
      legacyTable->setGeometry(m, y, fw, 172); y += 172 + 6;
      legacySector->setGeometry(m, y, hw, 331); legacyStats->setGeometry(m + hw + g, y, fw - hw - g, 331); y += 331 + 6;
      secondNet->setGeometry(m, y, fw, netB); y += netB + 5;
      secondTable->setGeometry(m, y, fw, 223); y += 223 + 6;
      secondSector->setGeometry(m, y, hw, 472); secondStats->setGeometry(m + hw + g, y, fw - hw - g, 472); y += 472 + 6;
      nonrepNet->setGeometry(m, y, fw, netA); y += netA + 6;
      read->setGeometry(m, y, fw, readH); y += readH;
      if (minimumHeight() != y + 10) setMinimumHeight(y + 10);
      update();
      return;
    }
    const int avail = W - m - 3 * g - 8 + g;   // three summary panels
    const int pw = int(std::round(avail * 789.0 / 1569)), sw = int(std::round(avail * 486.0 / 1569));
    price->setGeometry(m, 49 + t, pw, sumH);
    split->setGeometry(m + pw + g, 49 + t, sw, sumH);
    donut->setGeometry(m + pw + g + sw + g, 49 + t, right - (m + pw + g + sw + g), sumH);
    const int top = t + 49 + sumH + 50;             // 594 at the reference size
    m_advY = t + 49 + sumH + 23;
    const int lw = pw, rx = m + lw + g, rw = right - rx;
    legacyNet->setGeometry(m, top, lw, netA);
    secondNet->setGeometry(m, top + netA + 5, lw, netB);   // content y measured on t002.0 / t004.5
    legacyTable->setGeometry(rx, top, rw, 172);
    const int hw = (rw - g) / 2 + 1;
    legacySector->setGeometry(rx, top + 178, hw, 331);
    legacyStats->setGeometry(rx + hw + g, top + 178, rw - hw - g, 331);
    secondTable->setGeometry(rx, top + 515, rw, 223);
    secondSector->setGeometry(rx, top + 744, hw, 472);
    secondStats->setGeometry(rx + hw + g, top + 744, rw - hw - g, 472);
    // the automated read grows with its text (French is longer); both columns end at the same y
    const int readY = top + 1227;
    const int nonY = top + netA + 5 + netB + 6;
    const int bottom = std::max(readY + readH, nonY + 300);
    nonrepNet->setGeometry(m, nonY, lw, bottom - nonY);
    read->setGeometry(rx, readY, rw, bottom - readY);
    if (minimumHeight() != bottom + 10) setMinimumHeight(bottom + 10);
    update();
  }
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.fillRect(rect(), pk::col::kPage());
    pk::drawSectionHeading(p, QRectF(16, 23 + m_top, width() - 32, 12), QCoreApplication::translate("positioning", "SUMMARY"));
    pk::drawSectionHeading(p, QRectF(16, m_advY, width() - 32, 12), QCoreApplication::translate("positioning", "ADVANCED"));
    if (!message.isEmpty()) {
      p.setFont(theme::mono(11)); p.setPen(theme::map(0xe0, 0x62, 0x60));
      if (width() < 520) {   // phone: right of the heading, elided to the room left
        const QRectF r(130, 17 + m_top, width() - 146, 14);
        p.drawText(r, Qt::AlignVCenter | Qt::AlignRight, QFontMetricsF(p.font()).elidedText(message, Qt::ElideRight, r.width()));
      } else p.drawText(QRectF(140, 17 + m_top, width() - 300, 14), Qt::AlignVCenter | Qt::AlignRight, message);
    }
  }
};

// ------------------------------------------------------------------------------------------------ page
CotPage::CotPage(QWidget* parent) : QWidget(parent) {
  setAutoFillBackground(true);
  QPalette pl = palette(); pl.setColor(QPalette::Window, pk::col::kPage()); setPalette(pl);
  auto* v = new QVBoxLayout(this); v->setContentsMargins(0, 0, 0, 0); v->setSpacing(0);
  m_header = new CotHeader(this);
  m_markets = new CotMarketBar(this);
  m_range = new pk::Segmented({QCoreApplication::translate("positioning", "6M"), QCoreApplication::translate("positioning", "1Y"), QCoreApplication::translate("positioning", "2Y"), QCoreApplication::translate("positioning", "3Y")}, pk::Segmented::Grey, theme::mono(11), m_markets, true);
  m_range->setPadding(7, 19); m_range->setInactiveColor(theme::map(0x79, 0x77, 0x6f));
  m_markets->rightWidget = m_range;
  m_scroll = new pk::Scroll(this);
  m_content = new CotContent(m_scroll);
  m_scroll->setWidget(m_content);
  m_footer = new pk::Footer(this, 28);
  m_footer->setBadge(QCoreApplication::translate("positioning", "OFFICIAL"), pk::Footer::Official, QCoreApplication::translate("positioning", "CFTC Commitments of Traders, futures only — official weekly report"));
  v->addWidget(m_header); v->addWidget(m_markets); v->addWidget(m_scroll, 1); v->addWidget(m_footer);

  m_class = settings::value("positioning.cot.class", "fx").toString();
  m_market = settings::value("positioning.cot.market", "USD").toString();
  m_category = settings::value("positioning.cot.category", 2).toInt();
  m_range->setCurrent(settings::value("positioning.cot.range", 1).toInt());
  // the automated read starts in the interface language; its EN | FR toggle switches the read only (not saved)
  m_content->read->setLanguage(i18n::current() == i18n::Lang::French ? 1 : 0);
  if (const QByteArray l = qgetenv("DW_COT_LANG"); !l.isEmpty()) m_content->read->setLanguage(l == "fr" ? 1 : 0);
  if (const QByteArray mk = qgetenv("DW_COT_MARKET"); !mk.isEmpty() && Cot::market(QString::fromUtf8(mk))) {
    m_market = QString::fromUtf8(mk); m_class = Cot::market(m_market)->assetClass;
  }
  if (!Cot::market(m_market)) { m_market = "USD"; m_class = "fx"; }

  connect(m_header->cls, &pk::Segmented::changed, this, [this](int i) { setAssetClass(QStringList{"fx", "commodities", "indices"}[i]); });
  m_markets->onPick = [this](const QString& id) { selectMarket(id); };
  connect(m_range, &pk::Segmented::changed, this, [this](int i) { settings::setValue("positioning.cot.range", i); apply(); });
  connect(m_content->split, &cotui::SplitPanel::categoryChanged, this, [this](int i) {
    m_category = i; settings::setValue("positioning.cot.category", i); m_content->donut->setData(m_hist, i);
  });
  connect(m_content->price, &cotui::PricePanel::modeChanged, this, [this](int) { loadPrice(); });
  m_check.setInterval(60 * 1000);
  connect(&m_check, &QTimer::timeout, this, &CotPage::checkReport);

  const QByteArray hv = qgetenv("DW_COT_HOVER"), sc = qgetenv("DW_COT_SCROLL");
  if (!hv.isEmpty() || !sc.isEmpty())
    QTimer::singleShot(4000, this, [this, hv, sc] {
      if (!sc.isEmpty()) {
        m_scroll->verticalScrollBar()->setValue(sc.toInt());
        // the automated read sizes itself on its first paint (just now, if it came into view): scroll again once the
        // page has grown, so a capture of the bottom shows it whole
        QTimer::singleShot(1500, this, [this, sc] { m_scroll->verticalScrollBar()->setValue(sc.toInt()); });
      }
      const auto xy = QString::fromUtf8(hv).split(',');
      if (xy.size() != 2) return;
      const QPoint pg(xy[0].toInt(), xy[1].toInt());
      if (auto* pp = m_content->price; pp->rect().contains(pp->mapFrom(this, pg))) pp->setHover(pp->mapFrom(this, pg));
      if (auto* sp = m_content->split; sp->rect().contains(sp->mapFrom(this, pg))) sp->setHover(sp->mapFrom(this, pg));
    });
}

void CotPage::resizeEvent(QResizeEvent* e) {
  QWidget::resizeEvent(e);
  // phone upright, or a page this short (phone sideways: ~380 px)
  setPhoneMode(ui::compact(width()) || height() < 420);
}

void CotPage::setPhoneMode(bool on) {
  if (on == m_phone) return;
  m_phone = on;
  auto* v = static_cast<QVBoxLayout*>(layout());
  if (on) {
    v->removeWidget(m_header); v->removeWidget(m_markets);
    m_header->setParent(m_content); m_markets->setParent(m_content);
    m_markets->setPhone(true);
    m_range->setPadding(12, 28);   // finger-sized
    m_content->layoutTop = [this](int w) {
      m_header->setPhone(w < 520);   // upright: asset classes on a second row
      m_header->setGeometry(0, 0, w, m_header->height());
      m_markets->setMarketsPhone(w);
      m_markets->setGeometry(0, m_header->height(), w, m_markets->height());
      return m_header->height() + m_markets->height();
    };
    m_header->show(); m_markets->show();
  } else {
    m_content->layoutTop = nullptr;
    m_header->setPhone(false);
    m_markets->setPhone(false);
    m_range->setPadding(7, 19);
    m_markets->setFixedHeight(38);
    m_header->setParent(this); m_markets->setParent(this);
    v->insertWidget(0, m_header); v->insertWidget(1, m_markets);
    m_header->show(); m_markets->show();
  }
  QTimer::singleShot(0, this, [this] { QResizeEvent ev(m_content->size(), m_content->size()); QCoreApplication::sendEvent(m_content, &ev); });
}

int CotPage::rangeWeeks() const { static const int w[4] = {26, 52, 104, 156}; return w[qBound(0, m_range->current(), 3)]; }

void CotPage::showEvent(QShowEvent*) {
  m_check.start();
  if (m_loaded) {
    // coming back: ask again when the last answer failed, was partial or is older than its pace allows
    if (!m_market.isEmpty() && (!m_hist.ok() || !m_hist.warning.isEmpty() || Cot::instance().due(m_market))) selectMarket(m_market);
    return;
  }
  m_loaded = true;
  setAssetClass(m_class, true);
}
void CotPage::hideEvent(QHideEvent*) { m_check.stop(); }

void CotPage::checkReport() {
  if (m_market.isEmpty() || !m_hist.ok() || !Cot::instance().due(m_market)) return;
  const QDateTime now = QDateTime::currentDateTimeUtc();
  if (m_checkedAt.isValid() && m_checkedAt.secsTo(now) < 300) return;   // an answer that does not come is not asked every minute
  m_checkedAt = now;
  Cot::instance().history(m_market, this, [this, id = m_market](const CotHistory& h) {
    // what is on screen stays unless something newer came: a failure or the same report changes nothing
    if (id != m_market || !h.ok() || h.stale || !h.warning.isEmpty()) return;
    const bool same = m_hist.ok() && m_hist.weeks.last().date == h.weeks.last().date && m_hist.warning.isEmpty() && !m_hist.stale;
    if (same) return;
    m_hist = h;
    m_content->message.clear();
    m_content->update();
    apply();
  });
}

void CotPage::setAssetClass(const QString& cls, bool keep) {
  m_class = cls;
  settings::setValue("positioning.cot.class", cls);
  m_header->cls->setCurrent(QStringList{"fx", "commodities", "indices"}.indexOf(cls));
  const auto ms = Cot::markets(cls);
  QString id = m_market;
  if (!keep || !Cot::market(id) || Cot::market(id)->assetClass != cls) id = ms.isEmpty() ? QString() : ms.first().id;
  m_markets->setMarkets(ms, id);
  if (m_phone) { QResizeEvent ev(m_content->size(), m_content->size()); QCoreApplication::sendEvent(m_content, &ev); }   // more or fewer button rows
  selectMarket(id);
}

void CotPage::selectMarket(const QString& id) {
  const CotMarket* mk = Cot::market(id);
  if (!mk) return;
  m_market = id;
  settings::setValue("positioning.cot.market", id);
  m_markets->setCurrent(id);
  m_content->price->setMarket(*mk);
  m_content->secondNet->setTitle(mk->second == positioning::CotReport::Tff ? QCoreApplication::translate("positioning", "TRADERS IN FINANCIAL FUTURES NET") : QCoreApplication::translate("positioning", "DISAGGREGATED NET"));
  loadPrice();
  Cot::instance().history(id, this, [this, id](const CotHistory& h) {
    if (id != m_market) return;
    m_hist = h;
    m_content->message = !h.ok() ? QCoreApplication::translate("positioning", "CFTC: %1").arg(h.error)
                         : !h.warning.isEmpty() ? QCoreApplication::translate("positioning", "CFTC: one report could not be loaded (%1) — some panels are empty").arg(h.warning)
                         : h.stale ? QCoreApplication::translate("positioning", "CFTC unreachable — showing the cached report") : QString();
    m_content->update();
    apply();
  });
}

void CotPage::apply() {
  const int w = rangeWeeks();
  const auto& h = m_hist;
  auto* c = m_content;
  c->price->setHistory(h);
  c->split->setData(h, w, m_category);
  c->donut->setData(h, m_category);
  c->legacyNet->setData(h, w, 0); c->secondNet->setData(h, w, 1); c->nonrepNet->setData(h, w, 2);
  c->legacyTable->setData(h); c->secondTable->setData(h);
  c->legacySector->setData(h); c->secondSector->setData(h);
  c->legacyStats->setData(h); c->secondStats->setData(h);
  c->read->setData(h);
  if (!h.weeks.isEmpty()) {
    // the CFTC's own publication time of the row; else the usual Friday 15:30 Eastern after the Tuesday report date
    // (holiday weeks move it to Monday, which only the publication time shows)
    const QDateTime et = h.weeks.last().released.isValid() ? h.weeks.last().released
                         : QDateTime(h.weeks.last().date.addDays(3), QTime(15, 30), QTimeZone("America/New_York"));
    m_footer->setTime(et.toLocalTime().toString("HH:mm:ss"));
    // the reference footer shows only the time; the report / release dates go to the badge's tooltip
    m_footer->setBadge(QCoreApplication::translate("positioning", "OFFICIAL"), pk::Footer::Official,
                       QCoreApplication::translate("positioning", "CFTC Commitments of Traders, futures only — report of %1, released %2 local time").arg(h.weeks.last().date.toString("yyyy-MM-dd"), i18n::locale().toString(et.toLocalTime(), "ddd d MMM HH:mm")));
  }
}

void CotPage::loadPrice() {
  const CotMarket* mk = Cot::market(m_market);
  if (!mk) return;
  const int req = ++m_priceReq;
  auto* pp = m_content->price;
  pp->setLoading();
  const bool index = mk->assetClass == "fx" && !pp->weighted();
  if (!index) {
    const QString note = QCoreApplication::translate("positioning", "%1 · continuous future · daily candles (COT measures %2) · Yahoo Finance").arg(mk->futures, mk->contract);
    market::candles(mk->futures, "1d", "1y", this, [this, req, note](const market::Series& s) {
      if (req != m_priceReq) return;
      int dec = 2;
      if (!s.bars.isEmpty()) { const double v = s.bars.last().c; dec = v < 10 ? 4 : v < 1000 ? 2 : 1; if (v < 0.1) dec = 6; }
      m_content->price->setBars(s.bars, dec, note, s.timezone);
    });
    return;
  }
  // Unweighted currency index: geometric mean of the currency against the seven other majors (from the USD pairs),
  // rebased to 100 at the first day of the one-year window. Computed by Deepwater from Yahoo daily candles.
  const QStringList pairs{"EURUSD", "GBPUSD", "USDJPY", "USDCHF", "USDCAD", "AUDUSD", "NZDUSD"};
  auto acc = std::make_shared<QHash<QString, market::Series>>();
  const QString ccy = mk->id;
  for (const auto& pr : pairs) {
    market::candles(pr, "1d", "1y", this, [this, req, acc, pr, pairs, ccy, mk](const market::Series& s) {
      if (req != m_priceReq) return;
      acc->insert(pr, s);
      if (acc->size() < pairs.size()) return;
      // USD value of one unit of each currency, per date, for O/H/L/C
      struct V { double o, h, l, c; };
      QMap<QDate, QHash<QString, V>> byDay;
      for (const auto& p2 : pairs) {
        const auto& ser = acc->value(p2);
        const bool inv = p2.startsWith("USD");
        const QString cur = inv ? p2.mid(3) : p2.left(3);
        // Yahoo's daily FX bars carry open ≈ close; a 24 h market opens at the previous close, so use that
        for (int bi = 0; bi < ser.bars.size(); ++bi) {
          market::Bar b = ser.bars[bi];
          if (bi > 0) { b.o = ser.bars[bi - 1].c; b.h = std::max(b.h, b.o); b.l = std::min(b.l, b.o); }
          const QDate d = market::sessionDay(ser, b.t);   // exchange-local session day (not +N h: the open bar is stamped now)
          byDay[d][cur] = inv ? V{1 / b.o, 1 / b.l, 1 / b.h, 1 / b.c} : V{b.o, b.h, b.l, b.c};
        }
      }
      const QStringList all{"USD", "EUR", "GBP", "JPY", "CHF", "CAD", "AUD", "NZD"};
      QVector<market::Bar> bars;
      double base = NAN;
      for (auto it = byDay.cbegin(); it != byDay.cend(); ++it) {
        if (it->size() < 7) continue;
        auto val = [&](const QString& c, int k) {
          if (c == "USD") return 1.0;
          const V& v = (*it)[c];
          return k == 0 ? v.o : k == 1 ? v.h : k == 2 ? v.l : v.c;
        };
        // open / close exact; high / low from the legs' highs and lows taken together (an approximation: the legs'
        // intraday extremes are not simultaneous), then widened to cover open and close
        double out[4] = {0, 0, 0, 0};
        for (const auto& x : all) {
          if (x == ccy) continue;
          double r[4];
          for (int k = 0; k < 4; ++k) r[k] = std::log(val(ccy, k) / val(x, k));
          for (int k = 0; k < 4; ++k) out[k] += r[k] / 7;
        }
        for (double& o : out) o = std::exp(o);
        if (!std::isfinite(base)) base = out[0];
        market::Bar b{it.key().startOfDay(QTimeZone::UTC).toSecsSinceEpoch(), out[0] / base * 100, out[1] / base * 100, out[2] / base * 100, out[3] / base * 100, 0};
        const double h0 = b.h, l0 = b.l;
        b.h = std::max({h0, l0, b.o, b.c}); b.l = std::min({h0, l0, b.o, b.c});
        bars << b;
      }
      const QString first = byDay.isEmpty() ? QString() : byDay.firstKey().toString("yyyy-MM-dd");
      const QString note = QCoreApplication::translate("positioning", "%1 index · unweighted · daily candles (COT measures %2) · computed by Deepwater from Yahoo FX, 100 = %3")
                               .arg(ccy, mk->contract, first);
      m_content->price->setBars(bars, 2, note);
    });
  }
}
}  // namespace positioning

DW_PAGE("positioning/cot", positioning::CotPage)
