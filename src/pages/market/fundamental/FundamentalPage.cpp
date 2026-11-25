#include "pages/market/fundamental/FundamentalPage.h"
#include "pages/market/fundamental/FundWidgets.h"
#include "pages/market/fundamental/FundStyle.h"
#include "pages/market/seasonality/SmoothScroll.h"
#include "app/Pages.h"
#include "core/Settings.h"
#include "ui/Kit.h"
#include "ui/Theme.h"
#include "ui/Touch.h"
#include <QPainter>
#include <QCoreApplication>
#include "core/I18n.h"
#include <QMouseEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QDateTime>

DW_PAGE("market/fundamental-heatmap", fundamental::FundamentalPage)

using namespace fundamental;

namespace fundamental {

static const char* kRules = QT_TRANSLATE_NOOP("seasonal",
    "Level = 100 × mean of six oriented signals (−1…+1, positive = supports the currency). Score = level − mean level "
    "of the eight currencies (FX is relative); BULLISH ≥ +8, BEARISH ≤ −8.\n"
    "RATES 0.7 × last move (hike +1, hold 0, cut −1) + 0.3 × tanh(real rate / 2).\n"
    "INFLATION 0.7 × tanh(CPI − target) + 0.3 × trend; above target supports (hawkish bank).\n"
    "GROWTH 0.7 × tanh(GDP q/q / 0.4) + 0.3 × trend.  LABOUR −0.7 × tanh(Δ unemployment / 0.15) − 0.3 × tanh(z vs 5 y).\n"
    "ACTIVITY 0.7 × tanh((PMI − 50) / 3) + 0.3 × trend.  SENTIMENT 0.6 × tanh(Δ confidence / 2 × median Δ) + 0.4 × tanh(z vs 5 y).\n"
    "Cell shade: ≥ 0.5 bright teal, ≥ 0.15 dim teal, ±0.15 grey, ≤ −0.15 dim red, ≤ −0.5 red. Computed by Deepwater.");
static QString rules() { return QCoreApplication::translate("seasonal", kRules); }

// Scrolling content of the page (everything under the sticky title strip).
class Content : public QWidget {
public:
  explicit Content(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    strip = new CardStrip(this);
    policy = new PolicyCard(this);
    read = new ReadCard(this);
    comparator = new ComparatorCard(this);
    heatmap = new HeatmapCard(this);
    QObject::connect(read, &ReadCard::heightChanged, this, [this] { place(); });
  }
  CardStrip* strip; PolicyCard* policy; ReadCard* read; ComparatorCard* comparator; HeatmapCard* heatmap;
  QString ccy, status;
  const Inputs* in = nullptr;
  bool cursorOn = true, detailOpen = false;
  // Size classes: < 1240 px stacked (read card + comparator side by side under the policy card, everything full
  // width); 1240–1728 reference layout (right column 429 px); wider: the right column grows up to 640 px and the
  // history charts get taller; beyond 2560 px the content is capped and centred (heatmap tiles and prose lines
  // would otherwise stretch across an ultrawide screen).
  int cw() const { return qMin(width(), 2560); }
  int x0() const { return (width() - cw()) / 2; }
  bool stacked() const { return cw() < 1240; }
  int rightW() const { return stacked() ? 0 : (cw() <= 1728 ? 429 : qMin(640, 429 + (cw() - 1728) / 4)); }
  int leftW() const { return stacked() ? cw() - 28 : cw() - 14 - rightW() - 12 - 14; }
  // Phone held upright (< 760 px): cards 4 x 2, policy card stacked, read card then comparator, two-line heatmap,
  // release detail rows on two lines. dy() = how much lower the macro file title sits under the taller card strip.
  bool phone() const { return ui::compact(width()); }
  int dy() const { return phone() ? CardStrip::heightFor(cw()) - 93 : 0; }
  QRectF detailButton() const {
    const double w = QFontMetricsF(mono(10, QFont::Normal, 3)).horizontalAdvance(QString::fromUtf8("\u25b8 ") + QCoreApplication::translate("seasonal", "%1 RELEASE DETAIL").arg(ccy)) + 24;
    return QRectF(x0() + 14, heatmap->geometry().bottom() + 16, w, phone() ? 34 : 22);
  }
  static QString footerText() {
    return QCoreApplication::translate("seasonal", "LATEST PUBLISHED RELEASES (OFFICIAL AGENCIES, CENTRAL BANKS, BIS) · ▲▼ COLOUR = WHETHER THE "
                             "FIGURE SUPPORTS THE CURRENCY (LOWER UNEMPLOYMENT SUPPORTS IT) · SCORE = MEAN OF SIX ORIENTED SIGNALS");
  }
  // Word-wrapped heights of the two long notes, asked by the layout and by every paint (the cursor blink too): kept
  // per width and text (the text follows the interface language).
  struct WrapH { int w = -1; QString text; int h = 0; };
  mutable WrapH m_footerH, m_methodH;
  static int wrappedH(WrapH& c, const QFont& f, int w, int maxH, const QString& text) {
    if (c.w != w || c.text != text) { c.w = w; c.text = text; c.h = QFontMetrics(f).boundingRect(QRect(0, 0, w, maxH), Qt::TextWordWrap, text).height(); }
    return c.h;
  }
  int footerH() const { return wrappedH(m_footerH, mono(9, QFont::Normal, 1.5), cw() - 28, 400, footerText()); }
  int methodH() const { return wrappedH(m_methodH, prose(12), cw() - 28, 600, rules().replace('\n', "  ")); }
  // cursorX is in the centred content's coordinates (painted after translate(x0(), 0)): the repaint rect must be too
  QRect cursorRect() const { return QRect(x0() + cursorX - 1, 118 + dy(), 10, 16); }
  int cursorX = 0;

  struct DetailRow { QString name, latest, previous, period, released, source; };
  QList<DetailRow> detailRows() const {
    QList<DetailRow> rows;
    if (!in) return rows;
    auto add = [&](const QString& n, const Release& r, const QString& unit, int dec) {
      auto v = [&](double x) { return std::isfinite(x) ? QString::number(x, 'f', dec) + unit : dash(); };
      rows << DetailRow{n, v(r.value), v(r.previous), r.period.isValid() ? i18n::locale().toString(r.period, "MMM yyyy") : dash(),
                        r.released.isValid() ? r.released.toString("yyyy-MM-dd") : dash(),
                        !r.ok() && !r.note.isEmpty() ? r.note : (r.source.isEmpty() ? dash() : r.source)};
    };
    if (std::isfinite(in->policyRate))
      rows << DetailRow{QCoreApplication::translate("seasonal", "Policy rate"), QString::number(in->policyRate, 'f', 2) + "%",
                        std::isfinite(in->priorRate) ? QString::number(in->priorRate, 'f', 2) + "%" : dash(),
                        dash(), in->lastDecision.isValid() ? in->lastDecision.toString("yyyy-MM-dd") : dash(),
                        in->rateSource.isEmpty() ? in->policySource : in->rateSource};
    add(QCoreApplication::translate("seasonal", "Headline CPI y/y"), in->cpi, "%", 1);
    add(QCoreApplication::translate("seasonal", "Core CPI y/y"), in->coreCpi, "%", 1);
    add(QCoreApplication::translate("seasonal", "GDP q/q"), in->gdp, "%", 1);
    add(QCoreApplication::translate("seasonal", "Unemployment rate"), in->unemployment, "%", 1);
    add(in->activityName == "PMI" ? QCoreApplication::translate("seasonal", "PMI manufacturing") : in->activityName == "BCI" ? QCoreApplication::translate("seasonal", "Business confidence (OECD, 100 = avg)") : in->activityName, in->activity, "", 1);
    add(QCoreApplication::translate("seasonal", "Consumer confidence (OECD, 100 = avg)"), in->confidence, "", 1);
    add(QCoreApplication::translate("seasonal", "Retail sales y/y"), in->retail, "%", 2);
    return rows;
  }

  void place() {
    if (phone()) { placePhone(); return; }
    const int X = x0(), lw = leftW();
    strip->setGeometry(X, 16, cw(), 93);
    // history charts: 281 px at the reference (548 px wide), taller on wide windows
    const int chartW = (lw - 34) / 2;
    const int chartH = chartW <= 700 ? 281 : qMin(440, 281 + int((chartW - 700) * 0.35));
    const int ph = 186 + chartH + 9;
    policy->setGeometry(X + 14, 200, lw, ph);
    int y = 200 + ph + 15;   // heatmap top: 691 at the reference
    if (stacked()) {
      const int half = (lw - 12) / 2;
      const int rh = read->heightFor(half);
      read->setGeometry(X + 14, y, half, rh);
      comparator->setGeometry(X + 14 + half + 12, y, lw - half - 12, ComparatorCard::kHeight);
      y += qMax(rh, ComparatorCard::kHeight) + 15;
    } else {
      const int rx = X + cw() - 14 - rightW();
      const int rh = read->heightFor(rightW());
      read->setGeometry(rx, 121, rightW(), rh);
      comparator->setGeometry(rx, 121 + rh + 11, rightW(), ComparatorCard::kHeight);
    }
    heatmap->setGeometry(X + 14, y, lw, HeatmapCard::heightForRows(8));
    const int detailH = detailOpen ? 34 + int(detailRows().size()) * 24 + 8 : 0;
    const int h = int(detailButton().bottom()) + 8 + detailH + 6 + footerH() + 6 + methodH() + 24;
    if (minimumHeight() != h) { setMinimumHeight(h); setMaximumHeight(h); }
    update();
  }
  int detailRowH() const { return phone() ? 40 : 24; }
  void placePhone() {
    const int w = width(), lw = w - 28, d = dy();
    strip->setGeometry(0, 16, w, CardStrip::heightFor(w));
    policy->setGeometry(14, 200 + d, lw, PolicyCard::phoneHeight());
    int y = 200 + d + PolicyCard::phoneHeight() + 15;
    const int rh = read->heightFor(lw);
    read->setGeometry(14, y, lw, rh); y += rh + 12;
    comparator->setGeometry(14, y, lw, ComparatorCard::kHeight); y += ComparatorCard::kHeight + 15;
    heatmap->setGeometry(14, y, lw, HeatmapCard::phoneHeightForRows(8));
    const int detailH = detailOpen ? 34 + int(detailRows().size()) * detailRowH() + 8 : 0;
    const int h = int(detailButton().bottom()) + 8 + detailH + 6 + footerH() + 6 + methodH() + 24;
    if (minimumHeight() != h) { setMinimumHeight(h); setMaximumHeight(h); }
    update();
  }
protected:
  void resizeEvent(QResizeEvent*) override { place(); }
  void mousePressEvent(QMouseEvent* e) override {
    if (detailButton().contains(e->position())) { detailOpen = !detailOpen; place(); }
  }
  void mouseMoveEvent(QMouseEvent* e) override {
    setCursor(detailButton().contains(e->position()) ? Qt::PointingHandCursor : Qt::ArrowCursor);
  }
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    const auto& c = col();
    p.fillRect(rect(), c.page);
    p.translate(x0(), 0);   // centred content on ultrawide windows (local coordinates below)
    const CurrencyMeta* cm = meta(ccy);
    // macro file title
    const QString lab = QCoreApplication::translate("seasonal", "MACRO FILE · %1").arg(cm ? tw(cm->country) : ccy);
    const QFont lf = mono(10, QFont::Normal, 3);
    const int d = dy();
    p.setFont(lf); p.setPen(c.label);
    p.drawText(QRectF(14, 118 + d, 600, 16), Qt::AlignLeft | Qt::AlignVCenter, lab);
    cursorX = 14 + int(QFontMetricsF(lf).horizontalAdvance(lab)) + 2;
    if (cursorOn) p.fillRect(QRectF(cursorX, 120 + d, 7, 12), c.gold);
    p.drawPixmap(QPointF(14, 150 + d), ui::flag(ccy, QSize(20, 14)));
    p.setFont(sans(32, QFont::ExtraBold, 3)); p.setPen(theme::map("#f3f2ef"));
    p.drawText(QRectF(44, 136 + d, 300, 42), Qt::AlignLeft | Qt::AlignVCenter, ccy);
    // gold rule fading right
    QLinearGradient g(14, 0, 14 + leftW(), 0);
    g.setColorAt(0, theme::map("#aca48f")); g.setColorAt(0.6, theme::alpha(theme::map("#aca48f"), 120)); g.setColorAt(1, theme::alpha(theme::map("#aca48f"), 0));
    p.fillRect(QRectF(14, 186 + d, leftW(), 1), g);
    // release detail
    const QRectF b = detailButton().translated(-x0(), 0);
    p.setPen(theme::map("#2a2518")); p.setBrush(Qt::NoBrush); p.drawRect(b.adjusted(0.5, 0.5, -0.5, -0.5));
    p.setFont(mono(10, QFont::Normal, 3)); p.setPen(c.sub);
    p.drawText(b.adjusted(10, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
               QString::fromUtf8(detailOpen ? "▾ " : "▸ ") + QCoreApplication::translate("seasonal", "%1 RELEASE DETAIL").arg(ccy));
    double y = b.bottom() + 8;
    if (detailOpen && phone()) {   // phone: name, latest and previous on the first line; period, release date, source under
      const auto rows = detailRows();
      const double lw = leftW(), xl = 14 + lw * 0.56, xp = 14 + lw * 0.78;
      p.setFont(mono(9, QFont::Normal, 2)); p.setPen(c.dim);
      p.drawText(QRectF(24, y, xl - 30, 20), Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("seasonal", "INDICATOR"));
      p.drawText(QRectF(xl, y, xp - xl - 4, 20), Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("seasonal", "LATEST"));
      p.drawText(QRectF(xp, y, 14 + lw - xp, 20), Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("seasonal", "PREVIOUS"));
      y += 26;
      if (rows.isEmpty()) {
        p.setFont(mono(11)); p.setPen(c.sub);
        p.drawText(QRectF(24, y, lw, 20), Qt::AlignLeft | Qt::AlignVCenter, status);
        y += 24;
      }
      for (const auto& r : rows) {
        p.fillRect(QRectF(14, y - 2, lw, 1), theme::map("#1d180d"));
        p.setFont(prose(14)); p.setPen(theme::map("#c9c6c1"));
        p.drawText(QRectF(24, y, xl - 30, 20), Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(p.font()).elidedText(r.name, Qt::ElideRight, int(xl - 30)));
        p.setFont(mono(12)); p.setPen(c.text);
        p.drawText(QRectF(xl, y, xp - xl - 4, 20), Qt::AlignLeft | Qt::AlignVCenter, r.latest);
        p.setPen(c.sub);
        p.drawText(QRectF(xp, y, 14 + lw - xp, 20), Qt::AlignLeft | Qt::AlignVCenter, r.previous);
        p.setFont(mono(10)); p.setPen(c.dim);
        const QString meta = QString::fromUtf8("%1 · %2 · %3").arg(r.period, r.released, r.source);
        p.drawText(QRectF(24, y + 19, lw - 20, 16), Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(p.font()).elidedText(meta, Qt::ElideRight, int(lw - 20)));
        y += detailRowH();
      }
      y += 8;
    } else if (detailOpen) {
      const auto rows = detailRows();
      const double lw = leftW();
      const double xs[] = {14, 14 + lw * 0.30, 14 + lw * 0.43, 14 + lw * 0.56, 14 + lw * 0.70, 14 + lw * 0.84};
      const QStringList heads{QCoreApplication::translate("seasonal", "INDICATOR"), QCoreApplication::translate("seasonal", "LATEST"), QCoreApplication::translate("seasonal", "PREVIOUS"), QCoreApplication::translate("seasonal", "PERIOD"), QCoreApplication::translate("seasonal", "RELEASED"), QCoreApplication::translate("seasonal", "SOURCE")};
      p.setFont(mono(9, QFont::Normal, 2)); p.setPen(c.dim);
      for (int i = 0; i < 6; ++i) p.drawText(QRectF(xs[i] + 10, y, 200, 20), Qt::AlignLeft | Qt::AlignVCenter, heads[i]);
      y += 26;
      if (rows.isEmpty()) {
        p.setFont(mono(11)); p.setPen(c.sub);
        p.drawText(QRectF(24, y, lw, 20), Qt::AlignLeft | Qt::AlignVCenter, status);
        y += 24;
      }
      for (const auto& r : rows) {
        p.fillRect(QRectF(14, y - 2, lw, 1), theme::map("#1d180d"));
        p.setFont(prose(14)); p.setPen(theme::map("#c9c6c1"));
        p.drawText(QRectF(xs[0] + 10, y, 300, 22), Qt::AlignLeft | Qt::AlignVCenter, r.name);
        p.setFont(mono(12)); p.setPen(c.text);
        p.drawText(QRectF(xs[1] + 10, y, 120, 22), Qt::AlignLeft | Qt::AlignVCenter, r.latest);
        p.setPen(c.sub);
        p.drawText(QRectF(xs[2] + 10, y, 120, 22), Qt::AlignLeft | Qt::AlignVCenter, r.previous);
        p.drawText(QRectF(xs[3] + 10, y, 120, 22), Qt::AlignLeft | Qt::AlignVCenter, r.period);
        p.drawText(QRectF(xs[4] + 10, y, 120, 22), Qt::AlignLeft | Qt::AlignVCenter, r.released);
        p.drawText(QRectF(xs[5] + 10, y, cw() - xs[5] - 24, 22), Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(p.font()).elidedText(r.source, Qt::ElideRight, int(cw() - xs[5] - 24)));
        y += 24;
      }
      y += 8;
    }
    // footer + method
    p.setFont(mono(9, QFont::Normal, 1.5)); p.setPen(c.dim);
    const int fh = footerH();
    p.drawText(QRectF(14, y + 6, cw() - 28, fh), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, footerText());
    p.setFont(prose(12)); p.setPen(theme::map("#57534a"));
    const QString method = rules().replace('\n', "  ");
    p.drawText(QRectF(14, y + 12 + fh, cw() - 28, methodH() + 4), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, method);
  }
};


FundamentalPage::FundamentalPage(QWidget* parent) : QWidget(parent) {
  m_source = defaultSource(this);
  connect(m_source, &Source::changed, this, [this] { refresh(true); });
  m_sel = settings::value("market.fundamental.ccy").toString();

  m_scroll = new seasonality::SmoothScroll(this);
  m_content = new Content(m_scroll);
  m_scroll->setWidget(m_content);

  // a click is the user's choice: from then on refresh() no longer replaces it with the strongest currency
  connect(m_content->strip, &CardStrip::picked, this, [this](const QString& c) { m_selAuto = false; select(c); });
  connect(m_content->heatmap, &HeatmapCard::picked, this, [this](const QString& c) { m_selAuto = false; select(c); });

  m_tick.setInterval(250);   // pulse dot + blinking cursors, small rects only (4 fps: each repaint flushes the window, 10 fps cost ~2% CPU)
  connect(&m_tick, &QTimer::timeout, this, &FundamentalPage::tick);
  m_fade.setSingleShot(true);
  m_fade.setInterval(1500);  // the "just selected" gold border fades to the idle grey
  connect(&m_fade, &QTimer::timeout, this, [this] {
    if (!qEnvironmentVariableIsSet("DW_FUND_FRESH")) m_content->strip->setFresh(false);   // hook keeps the gold border
  });
  refresh(false);
}

const Inputs* FundamentalPage::inputsOf(const QString& ccy) const {
  for (const auto& in : m_inputs) if (in.ccy == ccy) return &in;
  return nullptr;
}

void FundamentalPage::refresh(bool animate) {
  const bool ready = m_source->ready();
  m_inputs = ready ? m_source->inputs() : QList<Inputs>();
  m_scores = scoreAll(m_inputs);
  // "strongest currency by default": before the data is ready the fallback is USD, which used to stick (and was saved
  // as if chosen). Keep re-picking until a real strongest exists, and don't save a fallback.
  if (m_sel.isEmpty() || !meta(m_sel) || m_selAuto) {
    const bool scored = !m_scores.isEmpty() && std::isfinite(m_scores.first().score);
    m_sel = scored ? m_scores.first().ccy : QString("USD");
    m_selAuto = !scored;
  }
  const QString status = ready ? QString() : m_source->status();
  m_content->strip->setScores(m_scores, ready);
  m_content->heatmap->setScores(m_scores, ready);
  select(m_sel);
  m_content->status = status;
  if (ready && qEnvironmentVariableIsSet("DW_FUND_REVEAL")) {
    m_content->policy->policyChart()->replay(); m_content->policy->inflationChart()->replay();
  }
  Q_UNUSED(animate);
}

void FundamentalPage::select(const QString& ccy) {
  const bool changed = ccy != m_content->ccy;
  m_sel = ccy;
  if (!m_selAuto) settings::setValue("market.fundamental.ccy", ccy);
  const bool ready = m_source->ready();
  const Inputs* in = inputsOf(ccy);
  m_content->ccy = ccy; m_content->in = in;
  m_content->status = ready ? QCoreApplication::translate("seasonal", "No releases for %1").arg(ccy) : m_source->status();
  m_content->strip->setSelected(ccy, true);
  m_fade.start();
  m_content->heatmap->setSelected(ccy);
  m_content->policy->setData(in, ccy, ready ? QCoreApplication::translate("seasonal", "No data") : QCoreApplication::translate("seasonal", "Waiting for macro data…"), changed && isVisible());
  Score sc; for (const auto& s : m_scores) if (s.ccy == ccy) sc = s;
  if (in) { const Reading r = automatedRead(*in, sc); m_content->read->setReading(&r, {}); }
  else m_content->read->setReading(nullptr, QCoreApplication::translate("seasonal", "Reading the latest releases…"));
  m_content->comparator->setData(ccy, m_inputs, m_scores, ready);
  m_content->place();
  m_content->update();
}

// ---------------------------------------------------------------- strip (sticky)
void FundamentalPage::paintEvent(QPaintEvent*) {
  QPainter p(this);
  const auto& c = col();
  p.fillRect(rect(), c.page);
  QLinearGradient sg(0, 0, 0, 36); sg.setColorAt(0, theme::map("#110c03")); sg.setColorAt(1, theme::map("#161004"));
  p.fillRect(QRect(0, 0, width(), 36), sg);
  p.fillRect(QRect(0, 36, width(), 1), c.stripRule);
  p.setRenderHint(QPainter::Antialiasing);
  // bank glyph
  {
    const QColor g = theme::map("#d4b020");
    p.setPen(QPen(g, 1)); p.setBrush(Qt::NoBrush);
    QPolygonF roof{QPointF(13.5, 14), QPointF(19.5, 10.5), QPointF(25.5, 14)};
    p.drawPolygon(roof);
    for (double x : {15.5, 19.5, 23.5}) p.drawLine(QPointF(x, 15), QPointF(x, 20));
    p.drawLine(QPointF(13, 21.5), QPointF(26, 21.5));
  }
  // pulsing dot
  const double a = 0.55 + 0.45 * std::sin(m_pulse);
  p.setPen(Qt::NoPen);
  p.setBrush(theme::alpha(c.goldDot, int(70 * a))); p.drawEllipse(QPointF(38, 17), 5, 5);
  p.setBrush(theme::mix(theme::alpha(c.goldDot, 255), theme::map("#6a6418"), 1 - a)); p.drawEllipse(QPointF(38, 17), 2.6, 2.6);
  p.setRenderHint(QPainter::Antialiasing, false);
  QString title = QCoreApplication::translate("seasonal", "FUNDAMENTALS — MACRO FILE BY CURRENCY");
  QFont tf = mono(10, QFont::Normal, 3);
  // phone: no right badge (the cards say "8 economies"), the title tightens then elides before the cursor
  const bool phone = ui::compact(width());
  if (phone) {
    if (QFontMetricsF(tf).horizontalAdvance(title) > width() - 70) tf = mono(10, QFont::Normal, 1);
    title = QFontMetricsF(tf).elidedText(title, Qt::ElideRight, width() - 70);
  }
  p.setFont(tf); p.setPen(c.stripText);
  p.drawText(QRectF(50, 0, 700, 34), Qt::AlignLeft | Qt::AlignVCenter, title);
  const int cx = 50 + int(QFontMetricsF(tf).horizontalAdvance(title)) + 2;
  if (m_cursorOn) p.fillRect(QRectF(cx, 10, 7, 12), c.gold);
  if (phone) return;
  // right badge
  const QString badge = QCoreApplication::translate("seasonal", "• 8 ECONOMIES · LATEST PUBLISHED RELEASES");
  const QFont bf = mono(9, QFont::Bold, 1.5);
  const double bw = QFontMetricsF(bf).horizontalAdvance(badge) + 18;
  const QRectF br(width() - 15 - bw, 9, bw, 18);
  p.setPen(c.badgeBorder); p.setBrush(c.badgeBg); p.drawRect(br.adjusted(0.5, 0.5, -0.5, -0.5));
  p.setFont(bf); p.setPen(c.badgeText); p.drawText(br, Qt::AlignCenter, badge);
  // ⓘ: scoring rules
  const QRectF ir(br.left() - 22, 11, 14, 14);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(theme::map("#6a6458"), 1)); p.setBrush(Qt::NoBrush); p.drawEllipse(ir.adjusted(0.5, 0.5, -0.5, -0.5));
  p.setFont(mono(9, QFont::Bold)); p.drawText(ir.adjusted(0, 0, 0, -0.5), Qt::AlignCenter, "i");
}

bool FundamentalPage::event(QEvent* e) {
  if (e->type() == QEvent::ToolTip) {
    auto* he = static_cast<QHelpEvent*>(e);
    if (he->pos().y() < 36 && he->pos().x() > width() - 360) {
      QToolTip::showText(he->globalPos(), rules(), this);
      return true;
    }
  }
  return QWidget::event(e);
}

void FundamentalPage::resizeEvent(QResizeEvent*) { m_scroll->setGeometry(0, 37, width(), height() - 37); }

void FundamentalPage::tick() {
  const qint64 t = QDateTime::currentMSecsSinceEpoch() - m_t0;
  m_pulse = t / 1000.0 * M_PI;            // ≈2 s period
  const bool on = (t / 500) % 2 == 0;     // cursors: 1 Hz blink
  update(QRect(30, 9, 18, 18));
  if (on != m_cursorOn) {
    m_cursorOn = on;
    update(QRect(0, 0, 420, 36));
  }
  const bool on2 = ((t + 350) / 500) % 2 == 0;   // the macro-file cursor blinks on its own phase
  if (on2 != m_content->cursorOn) { m_content->cursorOn = on2; m_content->update(m_content->cursorRect()); }
}

void FundamentalPage::showEvent(QShowEvent*) {
  m_t0 = QDateTime::currentMSecsSinceEpoch();
  m_tick.start();
  m_source->activate();
  if (!m_hooksDone) { m_hooksDone = true; QTimer::singleShot(0, this, [this] { applyHooks(); }); }
}
void FundamentalPage::hideEvent(QHideEvent*) { m_tick.stop(); m_fade.stop(); }
void FundamentalPage::preload() { m_source->activate(); }

void FundamentalPage::applyHooks() {
  const QString ccy = qEnvironmentVariable("DW_FUND_CCY");
  if (!ccy.isEmpty() && meta(ccy.toUpper())) { m_selAuto = false; select(ccy.toUpper()); }
  const QString other = qEnvironmentVariable("DW_FUND_OTHER");
  if (!other.isEmpty()) m_content->comparator->setOther(other.toUpper());
  if (qEnvironmentVariable("DW_FUND_DETAIL") == "1") { m_content->detailOpen = true; m_content->place(); }
  const QString scroll = qEnvironmentVariable("DW_FUND_SCROLL");
  if (!scroll.isEmpty())   // again once the releases have arrived (the content grows with the read card)
    for (int ms : {200, 3000}) QTimer::singleShot(ms, this, [this, scroll] { m_scroll->scrollTo(scroll.toInt(), false); });
  const QString hover = qEnvironmentVariable("DW_FUND_HOVER");
  if (!hover.isEmpty()) {
    const QPoint pg(hover.section(',', 0, 0).toInt(), hover.section(',', 1, 1).toInt());
    QTimer::singleShot(900, this, [this, pg] {
      for (HistoryChart* ch : {m_content->policy->policyChart(), m_content->policy->inflationChart()}) {
        const QPoint lp = ch->mapFrom(this, pg);
        if (ch->rect().contains(lp)) ch->setHover(lp);
      }
      const QPoint hp = m_content->heatmap->mapFrom(this, pg);
      if (m_content->heatmap->rect().contains(hp)) {
        // find the cell under the point through a synthetic move
        QMouseEvent me(QEvent::MouseMove, QPointF(hp), m_content->heatmap->mapToGlobal(QPointF(hp)), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(m_content->heatmap, &me);
      }
    });
  }
}
}  // namespace fundamental
