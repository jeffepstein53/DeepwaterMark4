#include "pages/economy/RatesPage.h"
#include "ui/Kit.h"
#include "pages/economy/EcoKit.h"
#include "macro/CentralBanks.h"
#include "macro/KeyFigures.h"
#include "macro/Registry.h"
#include "app/Pages.h"
#include "ui/Theme.h"
#include "ui/Touch.h"
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>
#include <QMouseEvent>
#include <QToolTip>
#include <QDesktopServices>
#include <QApplication>
#include <QUrl>
#include <QLocale>
#include <QTimer>
#include <QCoreApplication>
#include "core/I18n.h"
#include <QTimeZone>
#include <cmath>

namespace economy {
using namespace macro;
namespace {
QColor C(const char* h) { return theme::map(QColor(h)); }   // Deepwater colour → current theme
QString shortDate(const QDate& d) { return i18n::locale().toString(d, QCoreApplication::translate("economy", "MMM dd")) + " '" + QString::number(d.year() % 100); }
QString chipDate(const QDate& d) { return (i18n::locale().toString(d, QCoreApplication::translate("economy", "MMM dd")) + " '" + QString::number(d.year() % 100)).toUpper(); }
QString bucketName(int k) {
  static const char* const n[5] = {"-50+bps", "-25bps", QT_TRANSLATE_NOOP("economy", "No change"), "+25bps", "+50+bps"};
  return k == 2 ? QCoreApplication::translate("economy", n[2]) : QString(n[qBound(0, k, 4)]);
}
// texts kept in English by the services, translated at display
QString trData(const QString& s) { return QCoreApplication::translate("economy", s.toUtf8().constData()); }
[[maybe_unused]] const char* const kWords[] = {QT_TRANSLATE_NOOP("economy", "CHAIR"), QT_TRANSLATE_NOOP("economy", "PRESIDENT"),
                                               QT_TRANSLATE_NOOP("economy", "GOVERNOR"), QT_TRANSLATE_NOOP("economy", "CHAIRMAN"),
                                               QT_TRANSLATE_NOOP("economy", "HAWKISH"), QT_TRANSLATE_NOOP("economy", "DOVISH"),
                                               QT_TRANSLATE_NOOP("economy", "NEUTRAL")};
}

// ================================================================================================ body
class RatesBody : public QWidget {
public:
  explicit RatesBody(QWidget* p) : QWidget(p) {
    setMouseTracking(true);
    m_anim.setInterval(16);
    QObject::connect(&m_anim, &QTimer::timeout, this, [this] {
      bool moving = false;
      for (int k = 0; k < 2; ++k) {
        if (!std::isfinite(m_crossTarget[k])) continue;
        if (!std::isfinite(m_crossX[k])) m_crossX[k] = m_crossTarget[k];
        const double d = m_crossTarget[k] - m_crossX[k];
        if (std::fabs(d) > 0.5) { m_crossX[k] += d * 0.3; moving = true; } else m_crossX[k] = m_crossTarget[k];
      }
      if (!moving) m_anim.stop();
      update();
    });
  }
  QString selected = "FED";
  int chip = 0;
  bool keyData = false;
  QPointF mouse{-1, -1};
  // every area the last full paint drew differently under the mouse (filled by under()): a move that neither enters
  // nor leaves one of them changes nothing on screen and is not repainted
  QVector<QRectF> m_hot;
  bool under(const QRectF& r) { m_hot << r; return r.contains(mouse); }
  bool hotAt(const QPointF& pt) const { for (const auto& r : m_hot) if (r.contains(pt)) return true; return false; }

  // ---------------------------------------------------------------- geometry
  // content column: full width, capped at 2400 px and centred on ultrawide windows (a 3400 px table row is unreadable)
  qreal W() const { return std::min(width() - 20.0, 2400.0); }
  qreal L() const { return std::floor((width() - W()) / 2); }
  qreal S() const { return W() / 1558.0; }
  // phones (ui::compact): summary cards 2 x 2, table rows on two bands, committees stacked per bank; the blocks
  // below move down by what that adds. Desktop keeps the reference positions.
  bool phone() const { return ui::compact(width()); }
  qreal tableTop() const { return phone() ? 206 : 145; }
  qreal headH() const { return phone() ? 44 : 30; }
  qreal rowH() const { return phone() ? 82 : 45.4; }
  QRectF rowRect(int i) const { return QRectF(L(), tableTop() + headH() + i * rowH(), W(), rowH()); }
  qreal row1() const { return 585; }
  // chart panels: two columns from 1380 px of content (reference), one column below (charts keep 384 px of height);
  // on wide windows the panels grow taller with their width (up to 560 px)
  bool twoCol() const { return W() >= 1380; }
  qreal panelH() const { return phone() ? 330.0 : twoCol() ? std::clamp((W() - 11) / 2 * 0.497, 384.0, 560.0) : 384.0; }
  qreal termHead() const { return phone() ? rowRect(7).bottom() + 30 : 561; }
  qreal termTop() const { return termHead() + 24; }
  qreal polyHead() const { return termTop() + (twoCol() ? 2 : 4) * (panelH() + 12) + 1; }
  qreal polyTop() const { return polyHead() + 25; }
  // k: 0 curve, 1 points, 2 next meeting, 3 odds, 4 Polymarket next meeting, 5 Polymarket odds
  QRectF panel(int k) const {
    const qreal ph = panelH();
    if (twoCol()) {
      const qreal half = (W() - 11) / 2;
      const int row = k / 2, col = k % 2;
      const qreal y = k < 4 ? termTop() + row * (ph + 12) : polyTop();
      return QRectF(L() + col * (half + 11), y, half, ph);
    }
    const qreal y = k < 4 ? termTop() + k * (ph + 12) : polyTop() + (k - 4) * (ph + 12);
    return QRectF(L(), y, W(), ph);
  }
  qreal commTop() const { return panel(5).bottom() + 33; }
  qreal commTableTop() const { return commTop() + (phone() ? 70 : 40); }   // phones: the source note wraps above the table
  // one bank's block in the committees table: two 38 px lines (desktop); phones: name lines + 60 px per decision
  qreal commGroupH(const Bank& b) const {
    const int n = int(std::min<qsizetype>(2, b.decisions.size()));
    return phone() ? 50 + (n == 0 ? 30 : n * 60) : std::max(2, n) * 38;
  }
  int contentHeight() const {
    int h = int(commTableTop() + 26);
    for (const auto& b : CentralBanks::instance().banks()) h += int(commGroupH(b));
    return h + 30;
  }
  // the height depends on the width (two-column panels, phone layout)
  void resizeEvent(QResizeEvent*) override { if (minimumHeight() != contentHeight()) setMinimumHeight(contentHeight()); }

  const Bank* bank() const { return CentralBanks::instance().bank(selected); }

  // ---------------------------------------------------------------- paint
  void paintEvent(QPaintEvent* e) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), C("#040200"));
    // the 500 ms blink repaints the live dot only (RatesPage asks for 0,0 30×40): what crosses that box is the top
    // block (dot, title); the rest of the page and its hover zones stay as they are
    if (QRect(0, 0, 30, 40).contains(e->rect())) {
      const auto infos = m_infos;
      const auto hot = m_hot;
      paintTop(p);
      m_infos = infos; m_hot = hot;
      return;
    }
    m_infos.clear(); m_links.clear(); m_chipRects.clear(); m_hot.clear();
    paintTop(p);
    paintTable(p);
    eco::sectionHeading(p, L(), termHead(), L() + W(), QCoreApplication::translate("economy", "TERMINAL DATA"));
    paintCurve(p, panel(0));
    paintPoints(p, panel(1));
    paintNext(p, panel(2), 0);
    paintOdds(p, panel(3), 0);
    eco::sectionHeading(p, L(), polyHead(), L() + W(), QCoreApplication::translate("economy", "POLYMARKET"));
    paintPoly(p, panel(4), QCoreApplication::translate("economy", "NEXT MEETING PROBABILITY"));
    paintPoly(p, panel(5), QCoreApplication::translate("economy", "ODDS EVOLUTION (POLYMARKET)"));
    paintCommittees(p);
  }

  void infoIcon(QPainter& p, const QPointF& c, const QString& tip) {
    p.save();
    p.setPen(QPen(C("#4f4c45"), 1)); p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, 4.5, 4.5);
    p.setFont(eco::mono(7, QFont::Bold)); p.drawText(QRectF(c.x() - 4, c.y() - 5, 8, 10), Qt::AlignCenter, "i");
    p.restore();
    m_infos.append({QRectF(c.x() - 7, c.y() - 7, 14, 14), tip});
  }

  void paintTop(QPainter& p) {
    const bool on = eco::Blink::instance().phase() % 3 != 2;
    p.setPen(Qt::NoPen); p.setBrush(on ? C("#f3ce0b") : C("#6e6d22"));
    p.drawEllipse(QPointF(L() + 3, 22), 3, 3);
    p.setFont(eco::sans(15, QFont::DemiBold)); p.setPen(C("#eceae7"));
    p.drawText(QPointF(L() + 13, 27), QCoreApplication::translate("economy", "Interest Rate Probability"));
    eco::micro(p, L(), 47, QCoreApplication::translate("economy", "NEXT-MEETING OVERVIEW · 8 CENTRAL BANKS"), C("#75746e"), 9, 2);
    const auto& banks = CentralBanks::instance().banks();
    int hike = 0, hold = 0, cut = 0, priced = 0, week = 0;
    for (const auto& b : banks) {
      const int d = b.daysToNext();
      if (d >= 0 && d <= 7) ++week;
      const QString a = b.action();
      if (a.isEmpty()) continue;
      ++priced;
      if (a == "Hike") ++hike; else if (a == "Cut") ++cut; else ++hold;
    }
    const bool ph = phone();
    const qreal dy = ph ? 61 : 0;   // phones: the cards on two rows push the note down
    const qreal cw = ph ? (W() - 6) / 2 : (W() - 3 * 6) / 4;
    struct Cd { int n; int of; QColor c; QString lab; };
    const QList<Cd> cards{{hike, priced, eco::cyan(), QCoreApplication::translate("economy", "PRICED TO HIKE")}, {hold, priced, C("#afb0b1"), QCoreApplication::translate("economy", "PRICED TO HOLD")},
                          {cut, priced, eco::red(), QCoreApplication::translate("economy", "PRICED TO CUT")}, {week, int(banks.size()), C("#afb0b1"), QCoreApplication::translate("economy", "MEETING THIS WEEK")}};
    for (int i = 0; i < 4; ++i) {
      const QRectF r = ph ? QRectF(L() + (i % 2) * (cw + 6), 54 + (i / 2) * 61, cw, 55) : QRectF(L() + i * (cw + 6), 54, cw, 55);
      p.setPen(QPen(C("#302c22"), 1)); p.setBrush(C("#0c0702")); p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);
      const QFont big = eco::mono(20, QFont::Medium);
      p.setFont(big); p.setPen(cards[i].c);
      const QString n = QString::number(cards[i].n);
      p.drawText(QPointF(r.left() + 11, r.top() + 25), n);
      p.setFont(eco::mono(12)); p.setPen(C("#6c6761"));
      p.drawText(QPointF(r.left() + 13 + QFontMetricsF(big).horizontalAdvance(n), r.top() + 25), "/ " + QString::number(cards[i].of));
      QString lab = cards[i].lab;
      if (i < 3) m_infos.append({r, QCoreApplication::translate("economy", "Counted over the %1 of %2 banks with market pricing; hover a row in the table for its source.").arg(priced).arg(banks.size())});
      eco::micro(p, r.left() + 11, r.top() + 43, lab, C("#555149"), 9, 0.8);
    }
    p.setPen(Qt::NoPen); p.setBrush(C("#4cbcc0")); p.drawEllipse(QPointF(L() + 3, 121 + dy), 2, 2);
    p.setFont(eco::sans(9)); p.setPen(C("#4a4943"));
    const QString note = QCoreApplication::translate("economy", "Odds computed by Deepwater: Fed CME 30-day fed funds \u00b7 ECB CME 3M \u20acSTR \u00b7 BoE BoE OIS forward curve \u00b7 "
                                           "BoJ JPX 3M TONA \u00b7 SNB Eurex 3M SARON \u00b7 BoC MX 3M CORRA \u00b7 RBA/RBNZ FinancialJuice picture (OCR, validated). "
                                           "Hover a row for its source. Polymarket: see below.");
    p.drawText(QPointF(L() + 11, 124 + dy), QFontMetricsF(eco::sans(9)).elidedText(note, Qt::ElideRight, W() - 14));
    m_infos.append({QRectF(L(), 114 + dy, W(), 14), note});
  }

  // Phones: each bank on two bands — flag, code / name, action pill (+ bias pill when it fits), rate / since; then
  // next meeting / in N days and the IMPL BP · CUT · HOLD · HIKE columns (headed on the header's second line).
  void paintTablePhone(QPainter& p) {
    const QRectF box(L(), tableTop(), W(), headH() + 8 * rowH());
    p.fillRect(QRectF(box.left(), box.top(), box.width(), headH()), C("#0b0a03"));
    p.setPen(QPen(C("#38311e"), 1)); p.setBrush(Qt::NoBrush); p.drawRect(box.adjusted(0.5, 0.5, -0.5, -0.5));
    const auto& banks = CentralBanks::instance().banks();
    auto rateSub = [](const Bank& b) {
      return b.rateSince.isValid() ? QCoreApplication::translate("economy", "since %1").arg(i18n::locale().toString(b.rateSince, QCoreApplication::translate("economy", "MMM d")))
             : b.rateAsOf.isValid() ? QCoreApplication::translate("economy", "as of %1").arg(i18n::locale().toString(b.rateAsOf, QCoreApplication::translate("economy", "MMM d"))) : b.rateNote;
    };
    const qreal xR = L() + W() - 11, c0 = L() + 110, cw = (xR - c0) / 4;
    auto colR = [&](int k) { return c0 + (k + 1) * cw; };
    qreal subW = 0;
    {
      const QFontMetricsF sf(eco::mono(9)), rf(eco::mono(15, QFont::Bold));
      for (const Bank& b : banks) subW = std::max({subW, sf.horizontalAdvance(rateSub(b)), rf.horizontalAdvance("00.00%")});
    }
    const qreal rateLeft = xR - subW - 8;
    // header: BANK … RATE, then NEXT MEETING … IMPL BP CUT HOLD HIKE
    const QFont hf = eco::mono(9, QFont::Normal, 1);
    p.setFont(hf);
    const qreal h1 = box.top(), h2 = box.top() + 20;
    p.setPen(C("#6d6b62"));
    p.drawText(QRectF(L() + 11, h1, 200, 24), Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("economy", "BANK"));
    p.drawText(QRectF(xR - 200, h1, 200, 24), Qt::AlignRight | Qt::AlignVCenter, QCoreApplication::translate("economy", "RATE"));
    const QString implH = QCoreApplication::translate("economy", "IMPL BP"), meetH = QCoreApplication::translate("economy", "NEXT MEETING");
    if (L() + 11 + QFontMetricsF(hf).horizontalAdvance(meetH) < colR(0) - QFontMetricsF(hf).horizontalAdvance(implH) - 8)
      p.drawText(QRectF(L() + 11, h2, 200, 24), Qt::AlignLeft | Qt::AlignVCenter, meetH);
    const QList<QPair<QString, QColor>> cols{{implH, C("#6d6b62")}, {QCoreApplication::translate("economy", "CUT"), C("#c0504a")},
                                             {QCoreApplication::translate("economy", "HOLD"), C("#6a6861")}, {QCoreApplication::translate("economy", "HIKE"), eco::cyan()}};
    for (int k = 0; k < 4; ++k) { p.setPen(cols[k].second); p.drawText(QRectF(colR(k) - 200, h2, 200, 24), Qt::AlignRight | Qt::AlignVCenter, cols[k].first); }
    for (int i = 0; i < banks.size(); ++i) {
      const Bank& b = banks[i];
      const QRectF r = rowRect(i);
      const bool sel = b.id == selected, hov = under(r);
      if (sel) {
        QLinearGradient g(r.left(), 0, r.right(), 0); g.setColorAt(0, C("#352b04")); g.setColorAt(1, C("#1d1704"));
        p.fillRect(r.adjusted(1, 0, -1, 0), g);
        p.fillRect(QRectF(r.left() + 1, r.top(), 3, r.height()), C("#f5d648"));
      } else if (hov) p.fillRect(r.adjusted(1, 0, -1, 0), C("#201b05"));
      p.fillRect(QRectF(r.left() + 1, r.top(), r.width() - 2, 1), C("#292518"));
      // band 1
      const qreal cy = r.top() + 24;
      p.drawPixmap(QPointF(L() + 11, cy - 7), macro::flag(b.iso2, QSize(20, 14)));
      const QFont cf = eco::sans(14, QFont::Bold);
      p.setFont(cf); p.setPen(C("#e7e1cd"));
      p.drawText(QPointF(L() + 38, cy - 1), b.shownCode());
      p.setFont(eco::mono(10)); p.setPen(C("#736b4e"));
      const QString nm = b.id == "SNB" || b.id == "RBA" || b.id == "RBNZ" || b.id == "ECB" ? b.shownCode() : trData(b.name);
      p.drawText(QPointF(L() + 38, cy + 12), QFontMetricsF(eco::mono(10)).elidedText(nm, Qt::ElideRight, rateLeft - L() - 38));
      qreal px = L() + 38 + QFontMetricsF(cf).horizontalAdvance(b.shownCode()) + 8;
      const QString act = b.action();
      if (!act.isEmpty()) {
        const QString at = QCoreApplication::translate("economy", act.toUtf8().constData());   // Hike / Cut / Hold
        const QRectF pr(px, cy - 16, std::max(40.0, QFontMetricsF(eco::mono(9, QFont::Bold, 0.4)).horizontalAdvance(at) + 16), 16);
        if (act == "Hike") eco::pill(p, pr, QCoreApplication::translate("economy", "Hike"), C("#052523"), C("#6fbac2"), true, 8);
        else if (act == "Cut") eco::pill(p, pr, QCoreApplication::translate("economy", "Cut"), C("#2a0c0a"), C("#e0786f"), true, 8);
        else eco::pill(p, pr, QCoreApplication::translate("economy", "Hold"), C("#34311b"), C("#a4a29b"), true, 8);
        px = pr.right() + 6;
      }
      p.setFont(eco::mono(15, QFont::Bold)); p.setPen(eco::cyan());
      p.drawText(QRectF(xR - 200, cy - 14, 200, 18), Qt::AlignRight | Qt::AlignVCenter,
                 std::isfinite(b.rate) ? differs(b, "Policy rate") + QString::number(b.rate, 'f', 2) + "%" : QString("—"));
      p.setFont(eco::mono(9)); p.setPen(C("#4b4633"));
      p.drawText(QRectF(xR - 200, cy + 3, 200, 12), Qt::AlignRight | Qt::AlignVCenter, rateSub(b));
      // band 2
      const qreal cy2 = r.top() + 60;
      const MeetingDate nm2 = b.nextMeeting();
      p.setFont(eco::mono(13)); p.setPen(C("#c9c5b5"));
      p.drawText(QRectF(L() + 11, cy2 - 15, c0 - L() - 11, 18), Qt::AlignLeft | Qt::AlignVCenter,
                 nm2.decision.isValid() ? shortDate(nm2.decision) + (nm2.verified ? "" : "*") : QString("—"));
      if (nm2.decision.isValid()) {
        const int d = b.daysToNext();
        const QString t = d <= 0 ? QCoreApplication::translate("economy", "today") : QCoreApplication::translate("economy", "in %1d").arg(d);
        const QFont f = eco::mono(9, QFont::Bold);
        const QRectF br(L() + 12, cy2 + 4, QFontMetricsF(f).horizontalAdvance(t) + 9, 12);
        p.setPen(Qt::NoPen); p.setBrush(C("#0f3324")); p.drawRoundedRect(br, 2, 2);
        p.setFont(f); p.setPen(eco::cyan()); p.drawText(br, Qt::AlignCenter, t);
        if (!nm2.verified) m_infos.append({QRectF(L() + 11, cy2 - 15, c0 - L() - 11, 18), QCoreApplication::translate("economy", "Date entered by hand from a secondary source: the bank's site refuses programs. Verify on %1").arg(b.scheduleUrl)});
      }
      const Odds o = b.nextOdds();
      if (!b.priced || !o.valid()) {
        p.setFont(eco::mono(13)); p.setPen(C("#5e5b54"));
        for (int k = 0; k < 4; ++k) p.drawText(QRectF(colR(k) - 100, cy2 - 10, 100, 20), Qt::AlignRight | Qt::AlignVCenter, "—");
        // "NO FEED" beside the action pill when it fits
        const QRectF br(px, cy - 16, 62, 16);
        if (br.right() < rateLeft) eco::pill(p, br, QCoreApplication::translate("economy", "NO FEED"), C("#14130e"), C("#5e5b54"), false, 3);
        m_infos.append({QRectF(c0, cy2 - 20, xR - c0, 40), b.pricingNote});
        continue;
      }
      p.setFont(eco::mono(13, QFont::Bold)); p.setPen(b.impliedBp >= 0 ? eco::cyan() : eco::red());
      p.drawText(QRectF(colR(0) - 120, cy2 - 14, 120, 18), Qt::AlignRight | Qt::AlignVCenter, differs(b, "Odds") + eco::signedBp(b.impliedBp));
      const double wk = b.impliedBp - b.impliedBpWeekAgo;
      p.setFont(eco::mono(9)); p.setPen(!std::isfinite(wk) ? C("#4b4633") : wk >= 0 ? C("#3fc7b4") : eco::red());
      p.drawText(QRectF(colR(0) - 120, cy2 + 3, 120, 12), Qt::AlignRight | Qt::AlignVCenter,
                 std::isfinite(wk) ? QCoreApplication::translate("economy", "%1 1w").arg(eco::signedBp(wk))
                                    : QCoreApplication::translate("economy", "%1 1w").arg(QString::fromUtf8("\u2014")));
      p.setFont(eco::mono(13)); p.setPen(eco::red());
      p.drawText(QRectF(colR(1) - 120, cy2 - 10, 120, 20), Qt::AlignRight | Qt::AlignVCenter, QString::number(o.cut() * 100, 'f', 1) + "%");
      p.setPen(C("#c9c7c0"));
      p.drawText(QRectF(colR(2) - 120, cy2 - 10, 120, 20), Qt::AlignRight | Qt::AlignVCenter, QString::number(o.hold * 100, 'f', 1) + "%");
      p.setFont(eco::mono(13, QFont::Bold)); p.setPen(eco::cyan());
      p.drawText(QRectF(colR(3) - 120, cy2 - 10, 120, 20), Qt::AlignRight | Qt::AlignVCenter, QString::number(o.hike() * 100, 'f', 1) + "%");
      // bias pill beside the action pill when there is room before the rate
      int pct = 0; const QString bias = b.bias(&pct);
      const QString bt = trData(bias) + " " + QString::number(pct);   // HAWKISH / DOVISH / NEUTRAL
      const qreal bw = QFontMetricsF(eco::mono(9, QFont::Bold, 0.5)).horizontalAdvance(bt) + 14;
      const QRectF br(px, cy - 16, bw, 16);
      if (br.right() < rateLeft) {
        if (bias == "HAWKISH") eco::pill(p, br, bt, C("#072a28"), C("#63b3ba"), false, 3);
        else if (bias == "DOVISH") eco::pill(p, br, bt, C("#2a0d0b"), C("#e0786f"), false, 3);
        else eco::pill(p, br, bt, C("#171712"), C("#9e9f9e"), false, 3);
      }
      m_infos.append({QRectF(c0, cy2 - 20, cw, 40), b.pricingSource + (b.pricingNote.isEmpty() ? QString() : ". " + b.pricingNote) + checkedOdds(b)});
    }
  }

  // what the other sources give for the same figure (macro/KeyFigures): ≠ before ours when one differs
  static QString differs(const Bank& b, const char* figure) {
    return macro::checkDiffers(b.currency, QLatin1String(figure)) ? QStringLiteral("≠ ") : QString();
  }
  static QString checkedOdds(const Bank& b) {
    const macro::check::Item* it = macro::KeyFigures::instance().item(b.currency, QStringLiteral("Odds"));
    if (!it || it->others.isEmpty()) return {};
    const QString line = macro::checkLine(*it);
    return "\n" + (line.isEmpty() ? QString() : line + "\n") + QCoreApplication::translate("checks", "Other sources: %1").arg(macro::checkOthers(*it));
  }

  void paintTable(QPainter& p) {
    if (phone()) { paintTablePhone(p); return; }
    const qreal s = S();
    const QRectF box(L(), tableTop(), W(), 30 + 8 * rowH());
    p.fillRect(QRectF(box.left(), box.top(), box.width(), 30), C("#0b0a03"));
    p.setPen(QPen(C("#38311e"), 1)); p.setBrush(Qt::NoBrush); p.drawRect(box.adjusted(0.5, 0.5, -0.5, -0.5));
    const auto& banks = CentralBanks::instance().banks();
    auto rateSub = [](const Bank& b) {
      return b.rateSince.isValid() ? QCoreApplication::translate("economy", "since %1").arg(i18n::locale().toString(b.rateSince, QCoreApplication::translate("economy", "MMM d")))
             : b.rateAsOf.isValid() ? QCoreApplication::translate("economy", "as of %1").arg(i18n::locale().toString(b.rateAsOf, QCoreApplication::translate("economy", "MMM d"))) : b.rateNote;
    };
    // columns: the reference positions scaled to the width; on narrow windows each column is pushed right of what
    // it follows (measured texts: the action pill, the "since …" line, the meeting date), and CUT / HOLD / HIKE share
    // what is left before the BIAS pill
    qreal pillW = 40, subW = 0, meetW = 0;
    {
      const QFontMetricsF pf(eco::mono(9, QFont::Bold, 0.4)), sf(eco::mono(9)), mf(eco::mono(14)), rf(eco::mono(15, QFont::Bold));
      for (const char* a : {"Hike", "Cut", "Hold"}) pillW = std::max(pillW, pf.horizontalAdvance(QCoreApplication::translate("economy", a)) + 16);
      for (const Bank& b : banks) {
        subW = std::max({subW, sf.horizontalAdvance(rateSub(b)), rf.horizontalAdvance("00.00%")});
        const MeetingDate nm = b.nextMeeting();
        if (nm.decision.isValid()) meetW = std::max(meetW, mf.horizontalAdvance(shortDate(nm.decision) + "*"));
      }
    }
    qreal biasW = 62;
    {
      const QFontMetricsF bfm(eco::mono(9, QFont::Bold, 0.5));
      for (const char* b : {"HAWKISH", "DOVISH", "NEUTRAL"}) biasW = std::max(biasW, bfm.horizontalAdvance(trData(b) + " 100") + 14);
    }
    const qreal implW = std::max(QFontMetricsF(eco::mono(14, QFont::Bold)).horizontalAdvance("+888.8"),
                                 QFontMetricsF(eco::mono(9)).horizontalAdvance(QCoreApplication::translate("economy", "%1 1w").arg("+88.8")));
    const qreal xBias = L() + W() - 11, xPill = L() + 349 * s;
    const qreal xRate = std::max(L() + 559 * s, xPill + pillW + 10 + subW);
    const qreal xMeet = xRate + std::max(11 * s, 14.0);
    qreal xImpl = std::max(L() + 939 * s, xMeet + meetW + 18 + implW), step = 129.7 * s;
    if (xImpl > L() + 939 * s) step = std::max(0.0, (std::min(L() + 1328 * s, xBias - biasW - 18) - xImpl) / 3);
    const qreal xCut = xImpl + (xImpl > L() + 939 * s ? step : 130 * s), xHold = xImpl > L() + 939 * s ? xCut + step : L() + 1198 * s,
                xHike = xImpl > L() + 939 * s ? xHold + step : L() + 1328 * s;
    p.setFont(eco::mono(9, QFont::Normal, 1));
    auto hdr = [&](qreal x, const QString& t, const QColor& c, bool right) {
      p.setPen(c);
      if (right) p.drawText(QRectF(x - 200, box.top(), 200, 30), Qt::AlignRight | Qt::AlignVCenter, t);
      else p.drawText(QRectF(x, box.top(), 200, 30), Qt::AlignLeft | Qt::AlignVCenter, t);
    };
    hdr(L() + 11, QCoreApplication::translate("economy", "BANK"), C("#6d6b62"), false); hdr(xRate, QCoreApplication::translate("economy", "RATE"), C("#6d6b62"), true); hdr(xMeet, QCoreApplication::translate("economy", "NEXT MEETING"), C("#6d6b62"), false);
    hdr(xImpl, QCoreApplication::translate("economy", "IMPL BP"), C("#6d6b62"), true); hdr(xCut, QCoreApplication::translate("economy", "CUT"), C("#c0504a"), true); hdr(xHold, QCoreApplication::translate("economy", "HOLD"), C("#6a6861"), true);
    hdr(xHike, QCoreApplication::translate("economy", "HIKE"), eco::cyan(), true); hdr(xBias, QCoreApplication::translate("economy", "BIAS"), C("#6d6b62"), true);
    for (int i = 0; i < banks.size(); ++i) {
      const Bank& b = banks[i];
      const QRectF r = rowRect(i);
      const bool sel = b.id == selected, hov = under(r);
      if (sel) {
        QLinearGradient g(r.left(), 0, r.right(), 0); g.setColorAt(0, C("#352b04")); g.setColorAt(1, C("#1d1704"));
        p.fillRect(r.adjusted(1, 0, -1, 0), g);
        p.fillRect(QRectF(r.left() + 1, r.top(), 3, r.height()), C("#f5d648"));
      } else if (hov) p.fillRect(r.adjusted(1, 0, -1, 0), C("#201b05"));
      if (i > 0) p.fillRect(QRectF(r.left() + 1, r.top(), r.width() - 2, 1), C("#292518"));
      const qreal cy = r.center().y();
      p.drawPixmap(QPointF(L() + 11, cy - 7), macro::flag(b.iso2, QSize(20, 14)));
      p.setFont(eco::sans(14, QFont::Bold)); p.setPen(C("#e7e1cd"));
      p.drawText(QPointF(L() + 38, cy - 1), b.shownCode());
      p.setFont(eco::mono(10)); p.setPen(C("#736b4e"));
      p.drawText(QPointF(L() + 38, cy + 12), b.id == "SNB" || b.id == "RBA" || b.id == "RBNZ" || b.id == "ECB" ? b.shownCode() : trData(b.name));
      // action pill
      const QString act = b.action();
      if (!act.isEmpty()) {
        const QString at = QCoreApplication::translate("economy", act.toUtf8().constData());   // Hike / Cut / Hold
        const QRectF pr(xPill, cy - 8, std::max(40.0, QFontMetricsF(eco::mono(9, QFont::Bold, 0.4)).horizontalAdvance(at) + 16), 16);
        if (act == "Hike") eco::pill(p, pr, QCoreApplication::translate("economy", "Hike"), C("#052523"), C("#6fbac2"), true, 8);
        else if (act == "Cut") eco::pill(p, pr, QCoreApplication::translate("economy", "Cut"), C("#2a0c0a"), C("#e0786f"), true, 8);
        else eco::pill(p, pr, QCoreApplication::translate("economy", "Hold"), C("#34311b"), C("#a4a29b"), true, 8);
      }
      // rate
      p.setFont(eco::mono(15, QFont::Bold)); p.setPen(eco::cyan());
      p.drawText(QRectF(xRate - 200, cy - 14, 200, 18), Qt::AlignRight | Qt::AlignVCenter,
                 std::isfinite(b.rate) ? differs(b, "Policy rate") + QString::number(b.rate, 'f', 2) + "%" : QString("—"));
      if (const QString t = macro::checkTip(b.currency, "Policy rate"); !t.isEmpty()) m_infos.append({QRectF(xRate - 100, cy - 18, 100, 36), t});
      p.setFont(eco::mono(9)); p.setPen(C("#4b4633"));
      p.drawText(QRectF(xRate - 200, cy + 3, 200, 12), Qt::AlignRight | Qt::AlignVCenter, rateSub(b));
      // next meeting
      const MeetingDate nm = b.nextMeeting();
      p.setFont(eco::mono(14)); p.setPen(C("#c9c5b5"));
      p.drawText(QRectF(xMeet, cy - 15, 200, 18), Qt::AlignLeft | Qt::AlignVCenter,
                 nm.decision.isValid() ? differs(b, "Meeting") + shortDate(nm.decision) + (nm.verified ? "" : "*") : QString("—"));
      if (differs(b, "Meeting").size()) m_infos.append({QRectF(xMeet, cy - 15, 110, 18), macro::checkTip(b.currency, "Meeting")});
      if (nm.decision.isValid()) {
        const int d = b.daysToNext();
        const QString t = d <= 0 ? QCoreApplication::translate("economy", "today") : QCoreApplication::translate("economy", "in %1d").arg(d);
        const QFont f = eco::mono(9, QFont::Bold);
        const QRectF br(xMeet + 1, cy + 4, QFontMetricsF(f).horizontalAdvance(t) + 9, 12);
        p.setPen(Qt::NoPen); p.setBrush(C("#0f3324")); p.drawRoundedRect(br, 2, 2);
        p.setFont(f); p.setPen(eco::cyan()); p.drawText(br, Qt::AlignCenter, t);
        if (!nm.verified) m_infos.append({QRectF(xMeet, cy - 15, 110, 18), QCoreApplication::translate("economy", "Date entered by hand from a secondary source: the bank's site refuses programs. Verify on %1").arg(b.scheduleUrl)});
      }
      const Odds o = b.nextOdds();
      if (!b.priced || !o.valid()) {
        p.setFont(eco::mono(14)); p.setPen(C("#5e5b54"));
        for (qreal x : {xImpl, xCut, xHold, xHike}) p.drawText(QRectF(x - 100, cy - 10, 100, 20), Qt::AlignRight | Qt::AlignVCenter, "—");
        const QRectF br(xBias - 62, cy - 8, 62, 16);
        eco::pill(p, br, QCoreApplication::translate("economy", "NO FEED"), C("#14130e"), C("#5e5b54"), false, 3);
        m_infos.append({QRectF(xImpl - 100, r.top(), xBias - xImpl + 100, r.height()), b.pricingNote});
        continue;
      }
      p.setFont(eco::mono(14, QFont::Bold)); p.setPen(b.impliedBp >= 0 ? eco::cyan() : eco::red());
      p.drawText(QRectF(xImpl - 120, cy - 14, 120, 18), Qt::AlignRight | Qt::AlignVCenter, differs(b, "Odds") + eco::signedBp(b.impliedBp));
      const double wk = b.impliedBp - b.impliedBpWeekAgo;
      p.setFont(eco::mono(9)); p.setPen(!std::isfinite(wk) ? C("#4b4633") : wk >= 0 ? C("#3fc7b4") : eco::red());
      p.drawText(QRectF(xImpl - 120, cy + 3, 120, 12), Qt::AlignRight | Qt::AlignVCenter,
                 std::isfinite(wk) ? QCoreApplication::translate("economy", "%1 1w").arg(eco::signedBp(wk))
                                    : QCoreApplication::translate("economy", "%1 1w").arg(QString::fromUtf8("\u2014")));
      p.setFont(eco::mono(14)); p.setPen(eco::red());
      p.drawText(QRectF(xCut - 120, cy - 10, 120, 20), Qt::AlignRight | Qt::AlignVCenter, QString::number(o.cut() * 100, 'f', 1) + "%");
      p.setPen(C("#c9c7c0"));
      p.drawText(QRectF(xHold - 120, cy - 10, 120, 20), Qt::AlignRight | Qt::AlignVCenter, QString::number(o.hold * 100, 'f', 1) + "%");
      p.setFont(eco::mono(14, QFont::Bold)); p.setPen(eco::cyan());
      p.drawText(QRectF(xHike - 120, cy - 10, 120, 20), Qt::AlignRight | Qt::AlignVCenter, QString::number(o.hike() * 100, 'f', 1) + "%");
      int pct = 0; const QString bias = b.bias(&pct);
      const QString bt = trData(bias) + " " + QString::number(pct);   // HAWKISH / DOVISH / NEUTRAL
      const QFont bf = eco::mono(9, QFont::Bold, 0.5);
      const qreal bw = QFontMetricsF(bf).horizontalAdvance(bt) + 14;
      const QRectF br(xBias - bw, cy - 8, bw, 16);
      if (bias == "HAWKISH") eco::pill(p, br, bt, C("#072a28"), C("#63b3ba"), false, 3);
      else if (bias == "DOVISH") eco::pill(p, br, bt, C("#2a0d0b"), C("#e0786f"), false, 3);
      else eco::pill(p, br, bt, C("#171712"), C("#9e9f9e"), false, 3);
      m_infos.append({QRectF(xImpl - 100, r.top(), 100, r.height()), b.pricingSource +
                      (b.pricingNote.isEmpty() ? QString() : ". " + b.pricingNote) + checkedOdds(b)});
    }
  }

  void panelFrame(QPainter& p, const QRectF& r, const QString& title, const QString& info) {
    p.setPen(QPen(C("#2a2a14"), 1)); p.setBrush(C("#080603")); p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
    p.fillRect(QRectF(r.left() + 1, r.top() + 32, r.width() - 2, 1), C("#1e1c10"));
    const QFont f = eco::mono(10, QFont::Bold, 0.4);
    p.setFont(f); p.setPen(C("#a39f98"));
    p.drawText(QPointF(r.left() + 11, r.top() + 20), title);
    infoIcon(p, QPointF(r.left() + 11 + QFontMetricsF(f).horizontalAdvance(title) + 10, r.top() + 17), info);
  }

  void noData(QPainter& p, const QRectF& r, const QString& msg) {
    p.setFont(eco::mono(11)); p.setPen(C("#6a6961"));
    p.drawText(r.adjusted(40, 40, -40, -20), Qt::AlignCenter | Qt::TextWordWrap, msg);
  }

  QString pricingMsg(const Bank* b) const {
    if (!b) return "—";
    if (b->priced) return {};
    return b->pricingNote.isEmpty() ? QCoreApplication::translate("economy", "Loading futures…") : b->pricingNote;
  }

  void yGrid(QPainter& p, const QRectF& pl, double lo, double hi, double step, const std::function<QString(double)>& lab, qreal labelRight) {
    for (double t = lo; t <= hi + step * 1e-6; t += step) {
      const qreal y = std::round(pl.bottom() - (t - lo) / (hi - lo) * pl.height());
      p.fillRect(QRectF(pl.left(), y, pl.width(), 1), C("#1f2012"));
      p.setFont(eco::mono(10)); p.setPen(C("#6a6961"));
      p.drawText(QRectF(labelRight - 80, y - 7, 80, 14), Qt::AlignRight | Qt::AlignVCenter, lab(t));
    }
  }

  void paintCurve(QPainter& p, const QRectF& r) {
    panelFrame(p, r, QCoreApplication::translate("economy", "IMPLIED INTEREST RATE CURVE"),
               QCoreApplication::translate("economy", "Policy rate implied after each upcoming meeting: today's rate plus the cumulative implied change, computed by Deepwater from futures."));
    const Bank* b = bank();
    const QString msg = pricingMsg(b);
    if (!msg.isEmpty()) { noData(p, r, msg); return; }
    const QRectF pl(r.left() + 98, r.top() + 65, r.width() - 98 - 25, r.height() - 100);
    QVector<double> v{b->rate}; QStringList labs{QCoreApplication::translate("economy", "Now")};
    for (const auto& pt : b->path) { v << pt.rate; labs << shortDate(pt.meeting.decision); }
    double lo = *std::min_element(v.begin(), v.end()), hi = *std::max_element(v.begin(), v.end());
    const double step = 0.25;
    double ylo = std::floor((lo - 0.2) / step) * step, yhi = std::ceil((hi + 0.12) / step) * step;
    auto Y = [&](double x) { return pl.bottom() - (x - ylo) / (yhi - ylo) * pl.height(); };
    yGrid(p, pl, ylo, yhi, step, [](double t) { return QString::number(t, 'f', 3) + "%"; }, r.left() + 59);
    const int n = v.size();
    const qreal pitch = (pl.width() - 30) / std::max(1, n - 1);
    auto X = [&](int i) { return pl.left() + 21 + i * pitch; };
    QPainterPath line;
    QVector<QPointF> pts;
    for (int i = 0; i < n; ++i) pts << QPointF(X(i), Y(v[i]));
    line.moveTo(pts[0]);
    for (int i = 1; i < n; ++i) {   // monotone-ish cubic
      const QPointF a = pts[i - 1], c = pts[i];
      const qreal dx = (c.x() - a.x()) / 3;
      line.cubicTo(QPointF(a.x() + dx, a.y()), QPointF(c.x() - dx, c.y()), c);
    }
    QPainterPath fill = line; fill.lineTo(pts.last().x(), pl.bottom() + 16); fill.lineTo(pts[0].x(), pl.bottom() + 16); fill.closeSubpath();
    QLinearGradient g(0, pl.top(), 0, pl.bottom()); g.setColorAt(0, C("#0d3131")); g.setColorAt(1, C("#080d0b"));
    p.setPen(Qt::NoPen); p.setBrush(g); p.drawPath(fill);
    QPen dash(C("#4a5042"), 1); dash.setDashPattern({4, 3});
    p.setPen(dash); p.drawLine(QPointF(pl.left(), Y(b->rate)), QPointF(pl.right(), Y(b->rate)));
    p.setPen(QPen(C("#2cb1bf"), 1.6)); p.setBrush(Qt::NoBrush); p.drawPath(line);
    // phones: the meetings sit closer than a label; value and date labels only every `every` points (and the last value)
    const int every = phone() ? std::max(2, int(std::ceil(76 / std::max<qreal>(1, pitch)))) : 2;
    for (int i = 0; i < n; ++i) {
      p.setPen(QPen(C("#16595e"), 1.2)); p.setBrush(C("#6fd5dc")); p.drawEllipse(pts[i], 2.8, 2.8);
      p.setFont(eco::mono(10)); p.setPen(C("#8f8d87"));
      if (!phone() || i % every == 0 || i == n - 1)
        p.drawText(QRectF(pts[i].x() - 30, pts[i].y() - 22, 60, 14), Qt::AlignCenter, QString::number(v[i], 'f', 2) + "%");
      if (i % every == 0) {
        p.setPen(C("#6a6961"));
        p.drawText(QRectF(pts[i].x() - 40, r.bottom() - 26, 80, 14), Qt::AlignCenter, labs[i]);
      }
    }
    p.setFont(eco::sans(11)); p.setPen(C("#686962"));
    p.drawText(QRectF(pts[0].x() - 25, Y(b->rate) + 3, 60, 14), Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("economy", "Current"));
  }

  void paintPoints(QPainter& p, const QRectF& r) {
    panelFrame(p, r, QCoreApplication::translate("economy", "IMPLIED POINTS"), QCoreApplication::translate("economy", "Implied change at each meeting, in basis points (computed by Deepwater from futures)."));
    const Bank* b = bank();
    const QString msg = pricingMsg(b);
    if (!msg.isEmpty()) { noData(p, r, msg); return; }
    const QRectF pl(r.left() + 60, r.top() + 65, r.width() - 80, r.height() - 100);
    double lo = 0, hi = 0;
    for (const auto& pt : b->path) { lo = std::min(lo, pt.bp); hi = std::max(hi, pt.bp); }
    const double step = hi - lo > 60 ? 20 : hi - lo > 24 ? 10 : 5;
    const double ylo = std::floor((lo - 2) / step) * step, yhi = std::ceil((hi + 2) / step) * step;
    auto Y = [&](double x) { return pl.bottom() - (x - ylo) / (yhi - ylo) * pl.height(); };
    yGrid(p, pl, ylo, yhi, step, [](double t) { return QString::number(t, 'f', 0) + " bps"; }, r.left() + 51);
    p.fillRect(QRectF(pl.left(), std::round(Y(0)), pl.width(), 1), C("#31291e"));
    const int n = b->path.size();
    const qreal pitch = pl.width() / std::max(1, n);
    const int every = phone() ? std::max(2, int(std::ceil(76 / std::max<qreal>(1, pitch)))) : 2;   // phones: dates closer than a label
    for (int i = 0; i < n; ++i) {
      const double v = b->path[i].bp;
      const qreal cx = pl.left() + (i + 0.5) * pitch;
      const qreal y0 = Y(0), y1 = Y(v);
      p.fillRect(QRectF(cx - 14.5, std::min(y0, y1), 29, std::max(1.0, std::fabs(y1 - y0))), v >= 0 ? C("#19daf1") : C("#f46a5d"));
      p.setFont(eco::mono(10)); p.setPen(C("#97948c"));
      p.drawText(QRectF(cx - 30, std::min(y0, y1) - 17, 60, 14), Qt::AlignCenter, eco::signedBp(v));
      if (i % every == 0) {
        p.setPen(C("#6a6961"));
        p.drawText(QRectF(cx - 40, r.bottom() - 26, 80, 14), Qt::AlignCenter, shortDate(b->path[i].meeting.decision));
      }
    }
  }

  void paintNext(QPainter& p, const QRectF& r, int idx) {
    panelFrame(p, r, QCoreApplication::translate("economy", "NEXT MEETING PROBABILITY"),
               QCoreApplication::translate("economy", "Probability of each outcome at the next meeting versus today's rate (computed by Deepwater from futures, 25 bp steps)."));
    const Bank* b = bank();
    const QString msg = pricingMsg(b);
    if (!msg.isEmpty()) { noData(p, r, msg); return; }
    const QRectF pl(r.left() + 56, r.top() + 65, r.width() - 78, r.height() - 104);
    m_nextPlot[idx] = pl;
    yGrid(p, pl, 0, 100, 20, [](double t) { return QString::number(t, 'f', 0) + "%"; }, r.left() + 48);
    const Odds o = b->nextOdds();
    const qreal pitch = pl.width() / 5;
    int hov = -1;
    if (under(pl.adjusted(0, -10, 0, 20))) hov = std::clamp(int((mouse.x() - pl.left()) / pitch), 0, 4);
    for (int k = 0; k < 5; ++k) {
      const double v = o.at(k);
      const qreal cx = pl.left() + (k + 0.5) * pitch;
      const qreal y = pl.bottom() - v * pl.height();
      if (v < 0.01) p.fillRect(QRectF(cx - 23.5, pl.bottom() - 2, 47, 2), eco::outcome(k));
      else p.fillRect(QRectF(cx - 23.5, y, 47, pl.bottom() - y), eco::outcome(k));
      p.setFont(eco::mono(11)); p.setPen(C("#b0aea8"));
      p.drawText(QRectF(cx - 40, std::min(y, pl.bottom() - 2) - 19, 80, 14), Qt::AlignCenter, eco::probText(v));
      p.setPen(C("#7a7770"));
      p.drawText(QRectF(cx - 50, pl.bottom() + 9, 100, 14), Qt::AlignCenter, bucketName(k));
    }
    p.fillRect(QRectF(pl.left(), pl.bottom(), pl.width(), 1), C("#3a1e14"));
    if (hov >= 0) {
      m_crossTarget[idx] = pl.left() + (hov + 0.5) * pitch;
      if (!std::isfinite(m_crossX[idx]) || std::fabs(m_crossX[idx] - m_crossTarget[idx]) > 0.5) { if (!m_anim.isActive()) m_anim.start(); }
      QPen d(theme::map(255, 255, 255, 200), 1); d.setDashPattern({4, 3});
      p.setPen(d); p.drawLine(QPointF(m_crossX[idx], pl.top()), QPointF(m_crossX[idx], pl.bottom()));
      eco::tooltip(p, mouse, bucketName(hov), {{eco::outcome(hov), "", eco::probText(o.at(hov))}}, r, false, {25, 20});
    } else { m_crossTarget[idx] = NAN; m_crossX[idx] = NAN; }
    p.setFont(eco::mono(8)); p.setPen(C("#3e3b34"));
    p.drawText(QRectF(r.left(), r.bottom() - 14, r.width() - 10, 12), Qt::AlignRight | Qt::AlignVCenter,
               QFontMetricsF(eco::mono(8)).elidedText(QString("%1 · %2").arg(shortDate(b->nextMeeting().decision), b->pricingSource.toUpper()),
                                                      Qt::ElideRight, r.width() - 20));
  }

  void paintOdds(QPainter& p, const QRectF& r, int) {
    panelFrame(p, r, QCoreApplication::translate("economy", "ODDS EVOLUTION (TERMINAL)"),
               QCoreApplication::translate("economy", "Outcome odds for the chosen meeting over the last month, recomputed from each futures close (hourly for the Fed)."));
    const Bank* b = bank();
    const QString msg = pricingMsg(b);
    if (!msg.isEmpty()) { noData(p, r, msg); return; }
    // chips
    qreal x = r.left() + 11, y = r.top() + 42;
    {
      const QString t = keyData ? QCoreApplication::translate("economy", "KEY DATA ON") : QCoreApplication::translate("economy", "KEY DATA OFF");
      const QFont f = eco::mono(9, QFont::Bold, 0.4);
      const QRectF cr(x, y, QFontMetricsF(f).horizontalAdvance(t) + 32, 24);
      p.setPen(QPen(keyData ? C("#e0a050") : C("#2a2418"), 1)); p.setBrush(Qt::NoBrush); p.drawRoundedRect(cr.adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);
      p.setPen(QPen(C("#e0a050"), 1));
      for (int k = 0; k < 3; ++k) p.drawLine(QPointF(cr.left() + 10 + k * 3, cr.bottom() - 7), QPointF(cr.left() + 10 + k * 3, cr.bottom() - 10 - k * 2));
      p.setFont(f); p.drawText(cr.adjusted(18, 0, 0, 0), Qt::AlignCenter, t);
      m_keyRect = cr;
      m_infos.append({cr, QCoreApplication::translate("economy", "Marks the policy decisions of the 8 central banks inside the window.")});
      x = cr.right() + 8;
    }
    for (int i = 0; i < b->path.size(); ++i) {
      const QString t = chipDate(b->path[i].meeting.decision);
      const QFont f = eco::mono(9, QFont::Bold, 0.3);
      const qreal w = QFontMetricsF(f).horizontalAdvance(t) + 16;
      if (x + w > r.right() - 10) { x = r.left() + 11; y += 28; }
      const QRectF cr(x, y + 2, w, 20);
      const bool sel = i == chip;
      p.setPen(QPen(sel ? C("#c9a514") : C("#2a2418"), 1)); p.setBrush(sel ? C("#3a2e00") : Qt::transparent);
      p.drawRoundedRect(cr.adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
      p.setFont(f); p.setPen(sel ? C("#d1bf7b") : under(cr) ? C("#a9a7a0") : C("#6b6962"));
      p.drawText(cr, Qt::AlignCenter, t);
      m_chipRects << cr;
      x += w + 6;
    }
    const qreal top = y + 34;
    qreal legendBottom = top;
    // legend (phones: left to right, wrapping; it is wider than the panel)
    if (phone()) {
      qreal lx = r.left() + 24, ly = top + 6;
      p.setFont(eco::mono(10));
      for (int k = 0; k <= 4; ++k) {
        const qreal tw = QFontMetricsF(eco::mono(10)).horizontalAdvance(bucketName(k));
        if (lx + tw > r.right() - 10) { lx = r.left() + 24; ly += 18; }
        p.setPen(C("#928d85")); p.drawText(QPointF(lx, ly), bucketName(k));
        p.setPen(QPen(eco::outcome(k), 2)); p.setBrush(theme::map("#ffffff")); p.drawEllipse(QPointF(lx - 10, ly - 4), 3.2, 3.2);
        lx += tw + 32;
      }
      legendBottom = ly - 6;
    } else {
      qreal lx = r.right() - 14;
      p.setFont(eco::mono(10));
      for (int k = 4; k >= 0; --k) {
        const qreal tw = QFontMetricsF(eco::mono(10)).horizontalAdvance(bucketName(k));
        lx -= tw;
        p.setPen(C("#928d85")); p.drawText(QPointF(lx, top + 6), bucketName(k));
        p.setPen(QPen(eco::outcome(k), 2)); p.setBrush(theme::map("#ffffff")); p.drawEllipse(QPointF(lx - 10, top + 2), 3.2, 3.2);
        lx -= 32;
      }
    }
    const QRectF pl(r.left() + 56, legendBottom + 22, r.width() - 76, r.bottom() - 34 - (legendBottom + 22));
    m_oddsPlot = pl;
    yGrid(p, pl, 0, 100, 20, [](double t) { return QString::number(t, 'f', 0) + "%"; }, r.left() + 48);
    QPen half(C("#5a2a20"), 1); half.setDashPattern({4, 3}); p.setPen(half);
    p.drawLine(QPointF(pl.left(), pl.center().y()), QPointF(pl.right(), pl.center().y()));
    QVector<QPair<QDateTime, Odds>> h;
    for (const auto& s : b->history) if (chip < s.meetings.size()) h.append({s.t, s.meetings[chip]});
    if (h.size() < 2) {
      p.setFont(eco::mono(10)); p.setPen(C("#6a6961"));
      p.drawText(pl, Qt::AlignCenter | Qt::TextWordWrap, b->historySource.contains("recorded")
                 ? QCoreApplication::translate("economy", "History builds up from this app's own hourly snapshots (none older yet).") : QCoreApplication::translate("economy", "Not enough history yet."));
      return;
    }
    const qint64 t0 = h.first().first.toSecsSinceEpoch(), t1 = std::max(t0 + 1, h.last().first.toSecsSinceEpoch());
    auto X = [&](qint64 t) { return pl.left() + double(t - t0) / (t1 - t0) * pl.width(); };
    auto Yp = [&](double v) { return pl.bottom() - v * pl.height(); };
    // fills for the two leading outcomes, then lines
    for (int k : {2, 3}) {
      QPainterPath fp; fp.moveTo(X(t0), pl.bottom());
      double prevY = NAN;
      for (const auto& [t, o] : h) { const qreal xx = X(t.toSecsSinceEpoch()); if (std::isfinite(prevY)) fp.lineTo(xx, prevY); fp.lineTo(xx, Yp(o.at(k))); prevY = Yp(o.at(k)); }
      fp.lineTo(X(t1), pl.bottom()); fp.closeSubpath();
      p.setPen(Qt::NoPen); p.setBrush(k == 2 ? theme::map(196, 198, 199, 22) : theme::map(0, 194, 189, 20)); p.drawPath(fp);
    }
    for (int k = 0; k < 5; ++k) {
      QPainterPath lp; bool first = true;
      for (const auto& [t, o] : h) {
        const QPointF pt(X(t.toSecsSinceEpoch()), Yp(o.at(k)));
        if (first) { lp.moveTo(pt); first = false; } else { lp.lineTo(pt.x(), lp.currentPosition().y()); lp.lineTo(pt); }
      }
      p.setPen(QPen(k == 2 ? C("#c4c6c7") : k == 3 ? C("#1ab4ac") : eco::outcome(k), k == 2 || k == 3 ? 1.5 : 1.2));
      p.setBrush(Qt::NoBrush); p.drawPath(lp);
    }
    // key data: decision days of the 8 banks
    if (keyData) {
      for (const auto& bb : CentralBanks::instance().banks())
        for (const auto& m : bb.meetings) {
          const qint64 t = QDateTime(m.decision, QTime(12, 0), QTimeZone::UTC).toSecsSinceEpoch();
          if (t < t0 || t > t1) continue;
          const qreal xx = X(t);
          QPen kp(C("#e0a050"), 1); kp.setDashPattern({2, 3}); p.setPen(kp);
          p.drawLine(QPointF(xx, pl.top()), QPointF(xx, pl.bottom()));
          p.setFont(eco::mono(8, QFont::Bold)); p.setPen(C("#e0a050"));
          p.drawText(QRectF(xx - 20, pl.top() - 12, 40, 10), Qt::AlignCenter, bb.shownCode());
        }
    }
    // x ticks (days)
    {
      const QDate d0 = h.first().first.toLocalTime().date(), d1 = h.last().first.toLocalTime().date();
      const int days = std::max<qint64>(1, d0.daysTo(d1));
      const int every = days > 60 ? 14 : days > 20 ? 4 : days > 8 ? 2 : 1;
      p.setFont(eco::mono(10)); p.setPen(C("#6a6961"));
      qreal lastR = -1e9;
      for (QDate d = d0.addDays(1); d <= d1; d = d.addDays(1)) {
        if (d.day() != 1 && (d.dayOfYear() % every != 0 || d.daysInMonth() - d.day() < 2)) continue;
        const qreal xx = X(QDateTime(d, QTime(0, 0)).toSecsSinceEpoch());
        const QString lab = d.day() == 1 ? i18n::locale().toString(d, "MMM") : QString::number(d.day());
        if (xx - 15 < lastR) continue;
        p.drawText(QRectF(xx - 20, pl.bottom() + 9, 40, 14), Qt::AlignCenter, lab);
        lastR = xx + 15;
      }
    }
    // crosshair
    if (under(pl)) {
      int best = 0; qreal bd = 1e9;
      for (int i = 0; i < h.size(); ++i) { const qreal dd = std::fabs(X(h[i].first.toSecsSinceEpoch()) - mouse.x()); if (dd < bd) { bd = dd; best = i; } }
      const qreal xx = X(h[best].first.toSecsSinceEpoch());
      QPen d(theme::map(255, 255, 255, 190), 1); d.setDashPattern({4, 3}); p.setPen(d);
      p.drawLine(QPointF(xx, pl.top()), QPointF(xx, pl.bottom()));
      p.drawLine(QPointF(pl.left(), mouse.y()), QPointF(pl.right(), mouse.y()));
      for (int k = 0; k < 5; ++k) {
        p.setPen(QPen(k == 2 ? C("#c4c6c7") : eco::outcome(k), 1.5)); p.setBrush(C("#f5f2eb"));
        p.drawEllipse(QPointF(xx, Yp(h[best].second.at(k))), 3, 3);
      }
      const QString yv = QString::number((pl.bottom() - mouse.y()) / pl.height() * 100, 'f', 1) + "%";
      const QFont bf = eco::sans(13);
      const QRectF yb(pl.left() - 4 - QFontMetricsF(bf).horizontalAdvance(yv) - 12, mouse.y() - 11, QFontMetricsF(bf).horizontalAdvance(yv) + 12, 22);
      p.setPen(Qt::NoPen); p.setBrush(C("#3d3303")); p.drawRoundedRect(yb, 2, 2);
      p.setFont(bf); p.setPen(C("#ede6d4")); p.drawText(yb, Qt::AlignCenter, yv);
      const QString xv = i18n::locale().toString(h[best].first.toLocalTime(), "dd MMM yyyy, HH:mm");
      const QFont xf = eco::sans(12);
      const qreal xw = QFontMetricsF(xf).horizontalAdvance(xv) + 16;
      const QRectF xb(std::clamp(xx - xw / 2, r.left() + 2, r.right() - xw - 2), pl.bottom() + 5, xw, 21);
      p.setBrush(C("#3d3303")); p.setPen(Qt::NoPen); p.drawRoundedRect(xb, 2, 2);
      p.setFont(xf); p.setPen(C("#ede6d4")); p.drawText(xb, Qt::AlignCenter, xv);
      QList<eco::TipRow> rows;
      for (int k = 0; k < 5; ++k) rows << eco::TipRow{k == 2 ? C("#c4c6c7") : eco::outcome(k), bucketName(k), QString::number(h[best].second.at(k) * 100, 'f', 1) + "%"};
      eco::tooltip(p, QPointF(xx, mouse.y()), xv, rows, r, false, {22, 22});
    }
  }

  void paintPoly(QPainter& p, const QRectF& r, const QString& title) {
    panelFrame(p, r, title, QCoreApplication::translate("economy", "Polymarket's own odds, the reference's second source. Shown only when its API is reachable."));
    // phones: the status text gets the panel's width and the height left under the icon (it is long and wraps)
    const QRectF m = phone() ? r.adjusted(16, 70, -16, -40) : r.adjusted(60, 70, -60, -40);
    p.setPen(QPen(C("#3a3010"), 1)); p.setBrush(Qt::NoBrush);
    const QPointF c(m.center().x(), phone() ? m.top() + 24 : m.center().y() - 40);
    p.drawEllipse(c, 16, 16);
    p.drawLine(c + QPointF(-7, -7), c + QPointF(7, 7));
    p.setFont(eco::mono(11, QFont::Bold, 1)); p.setPen(C("#8a877e"));
    p.drawText(QRectF(m.left(), c.y() + 26, m.width(), 18), Qt::AlignCenter, CentralBanks::instance().polymarketReachable()
               ? QCoreApplication::translate("economy", "POLYMARKET MARKETS NOT WIRED") : QCoreApplication::translate("economy", "POLYMARKET UNREACHABLE"));
    p.setFont(eco::mono(10)); p.setPen(C("#6a6961"));
    p.drawText(QRectF(m.left(), c.y() + 48, m.width(), phone() ? r.bottom() - 10 - (c.y() + 48) : 60), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
               CentralBanks::instance().polymarketStatus());
  }

  // Phones: one block per bank — name / committee left, head right; then per decision: date, vote, official link;
  // the decision text; the dissent. The committee note is left out (too long for the width).
  void paintCommitteesPhone(QPainter& p) {
    eco::sectionHeading(p, L(), commTop(), L() + W(), QCoreApplication::translate("economy", "COMMITTEES & VOTES"));
    eco::micro(p, L(), commTop() + 27, QCoreApplication::translate("economy", "POLICY COMMITTEES — CHAIRS, DECISIONS & VOTES"), C("#726f6a"), 9, 0.3);
    p.setFont(eco::sans(9)); p.setPen(C("#4a4943"));
    p.drawText(QRectF(L(), commTop() + 34, W(), 30), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
               QCoreApplication::translate("economy", "From each bank's own official statement where it publishes one · otherwise read from its rate series"));
    const qreal top = commTableTop(), xR = L() + W() - 10;
    const auto& banks = CentralBanks::instance().banks();
    qreal y = top + 26;
    for (const auto& b : banks) y += commGroupH(b);
    const QRectF box(L(), top, W(), y - top);
    p.fillRect(QRectF(box.left(), top, box.width(), 26), C("#0b0a03"));
    p.setPen(QPen(C("#38311e"), 1)); p.setBrush(Qt::NoBrush); p.drawRect(box.adjusted(0.5, 0.5, -0.5, -0.5));
    p.setFont(eco::mono(9, QFont::Normal, 0.8)); p.setPen(C("#6d6b62"));
    p.drawText(QRectF(L() + 9, top, 300, 26), Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("economy", "BANK · COMMITTEE"));
    p.drawText(QRectF(xR - 200, top, 200, 26), Qt::AlignRight | Qt::AlignVCenter, QCoreApplication::translate("economy", "HEAD"));
    y = top + 26;
    const qreal leftW = W() * 0.56;
    for (const auto& b : banks) {
      const int n = int(std::min<qsizetype>(2, b.decisions.size()));
      const qreal gh = commGroupH(b);
      const QRectF g(L() + 1, y, W() - 2, gh);
      if (b.id == selected) p.fillRect(g, C("#332901"));
      else if (under(g)) p.fillRect(g, C("#161204"));
      p.fillRect(QRectF(g.left(), y, g.width(), 1), C("#292518"));
      p.setFont(eco::sans(12, QFont::DemiBold)); p.setPen(b.id == selected ? C("#e0ca76") : C("#e5dfcd"));
      p.drawText(QPointF(L() + 9, y + 20), QFontMetricsF(eco::sans(12, QFont::DemiBold)).elidedText(trData(b.name), Qt::ElideRight, leftW));
      p.setFont(eco::sans(9)); p.setPen(C("#645c3b"));
      p.drawText(QPointF(L() + 9, y + 37), QFontMetricsF(eco::sans(9)).elidedText(trData(b.committee), Qt::ElideRight, leftW));
      p.setFont(eco::mono(8)); p.setPen(C("#918a6f"));
      p.drawText(QRectF(L() + leftW + 16, y + 6, xR - L() - leftW - 16, 12), Qt::AlignRight | Qt::AlignVCenter,
                 QFontMetricsF(eco::mono(8)).elidedText(trData(b.headTitle), Qt::ElideRight, xR - L() - leftW - 16));
      p.setFont(eco::sans(12, QFont::DemiBold)); p.setPen(C("#e5dfcd"));
      p.drawText(QRectF(L() + leftW + 16, y + 20, xR - L() - leftW - 16, 18), Qt::AlignRight | Qt::AlignVCenter,
                 QFontMetricsF(eco::sans(12, QFont::DemiBold)).elidedText(b.head, Qt::ElideRight, xR - L() - leftW - 16));
      if (n == 0) {
        p.setFont(eco::sans(12)); p.setPen(C("#6a6961"));
        p.drawText(QRectF(L() + 9, y + 50, W() - 18, 26), Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("economy", "No decision read yet"));
      }
      for (int i = 0; i < n; ++i) {
        const Decision& d = b.decisions[i];
        const qreal ry = y + 50 + i * 60;
        p.fillRect(QRectF(L() + 9, ry - 4, W() - 18, 1), C("#292518"));
        p.setFont(eco::sans(12)); p.setPen(C("#c9c5b5"));
        const QString ds = i18n::locale().toString(d.date, QCoreApplication::translate("economy", "MMM d, yyyy"));
        p.drawText(QRectF(L() + 9, ry, 140, 22), Qt::AlignLeft | Qt::AlignVCenter, ds);
        qreal vx = L() + 9 + std::max<qreal>(110, QFontMetricsF(eco::sans(12)).horizontalAdvance(ds) + 12);
        const QString vl = QCoreApplication::translate("economy", "VOTE");
        p.setFont(eco::mono(8, QFont::Normal, 0.8)); p.setPen(C("#6d6b62"));
        p.drawText(QRectF(vx, ry, 100, 22), Qt::AlignLeft | Qt::AlignVCenter, vl);
        vx += QFontMetricsF(eco::mono(8, QFont::Normal, 0.8)).horizontalAdvance(vl) + 6;
        const QRectF lk(xR - 70, ry, 70, 22);
        QString vote = d.vote.isEmpty() ? (b.id == "ECB" ? QCoreApplication::translate("economy", "Not published") : QString("—")) : d.vote;
        p.setFont(eco::mono(12, QFont::Bold)); p.setPen(C("#f5f2eb"));
        p.drawText(QRectF(vx, ry, lk.left() - vx - 4, 22), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetricsF(eco::mono(12, QFont::Bold)).elidedText(vote, Qt::ElideRight, lk.left() - vx - 4));
        p.setFont(eco::sans(11)); p.setPen(under(lk) ? eco::amber() : C("#6e6440"));
        p.drawText(lk, Qt::AlignRight | Qt::AlignVCenter, QCoreApplication::translate("economy", "Official ↗"));
        m_links.append({lk, d.url.isEmpty() ? b.officialUrl : d.url});
        const QRectF tr(L() + 9, ry + 21, W() - 18, 18);
        p.setFont(eco::sans(12)); p.setPen(C("#9a968d"));
        p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, QFontMetricsF(eco::sans(12)).elidedText(d.text.isEmpty() ? QString("—") : d.text, Qt::ElideRight, tr.width()));
        if (!d.text.isEmpty()) m_infos.append({tr, d.text});
        const QString dl = QCoreApplication::translate("economy", "DISSENT");
        const QRectF sr(L() + 9, ry + 38, W() - 18, 18);
        p.setFont(eco::mono(8, QFont::Normal, 0.8)); p.setPen(C("#6d6b62"));
        p.drawText(sr, Qt::AlignLeft | Qt::AlignVCenter, dl);
        const qreal dx = QFontMetricsF(eco::mono(8, QFont::Normal, 0.8)).horizontalAdvance(dl) + 6;
        p.setFont(eco::sans(11)); p.setPen(C("#888165"));
        p.drawText(sr.adjusted(dx, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetricsF(eco::sans(11)).elidedText(d.dissent.isEmpty() ? QString("—") : d.dissent, Qt::ElideRight, sr.width() - dx));
        if (!d.dissent.isEmpty()) m_infos.append({sr, d.dissent});
      }
      y += gh;
    }
  }

  void paintCommittees(QPainter& p) {
    if (phone()) { paintCommitteesPhone(p); return; }
    const qreal s = S();
    eco::sectionHeading(p, L(), commTop(), L() + W(), QCoreApplication::translate("economy", "COMMITTEES & VOTES"));
    eco::micro(p, L(), commTop() + 27, QCoreApplication::translate("economy", "POLICY COMMITTEES — CHAIRS, DECISIONS & VOTES"), C("#726f6a"), 9, 3);
    p.setFont(eco::sans(9)); p.setPen(C("#4a4943"));
    p.drawText(QRectF(L(), commTop() + 18, W(), 14), Qt::AlignRight | Qt::AlignVCenter,
               QCoreApplication::translate("economy", "From each bank's own official statement where it publishes one · otherwise read from its rate series"));
    const qreal top = commTop() + 40;
    qreal xH = L() + 290 * s, xM = L() + 403 * s, xD = L() + 490 * s, xV = L() + 820 * s, xS = L() + 1025 * s;
    const qreal xR = L() + W() - 10;
    if (s < 0.95) {   // narrow windows: fixed minimum widths for bank, head and date; decision/vote/dissent share the rest
      xH = L() + std::max(290 * s, 200.0);
      xM = xH + 122;
      xD = xM + 104;
      const qreal rest = xR - 75 - xD;
      xV = xD + rest * 0.46;
      xS = xV + std::max(rest * 0.17, 80.0);
    }
    const auto& banks = CentralBanks::instance().banks();
    qreal y = top + 26;
    for (const auto& b : banks) y += std::max<qsizetype>(2, std::min<qsizetype>(2, b.decisions.size())) * 38;
    const QRectF box(L(), top, W(), y - top);
    p.fillRect(QRectF(box.left(), top, box.width(), 26), C("#0b0a03"));
    p.setPen(QPen(C("#38311e"), 1)); p.setBrush(Qt::NoBrush); p.drawRect(box.adjusted(0.5, 0.5, -0.5, -0.5));
    p.setFont(eco::mono(9, QFont::Normal, 0.8)); p.setPen(C("#6d6b62"));
    const QList<QPair<qreal, QString>> hd{{L() + 9, QCoreApplication::translate("economy", "BANK · COMMITTEE")}, {xH, QCoreApplication::translate("economy", "HEAD")}, {xM, QCoreApplication::translate("economy", "MEETING")}, {xD, QCoreApplication::translate("economy", "DECISION")}, {xV, QCoreApplication::translate("economy", "VOTE")}, {xS, QCoreApplication::translate("economy", "DISSENT")}};
    for (const auto& [x, t] : hd) p.drawText(QRectF(x, top, 300, 26), Qt::AlignLeft | Qt::AlignVCenter, t);
    p.drawText(QRectF(xR - 100, top, 100, 26), Qt::AlignRight | Qt::AlignVCenter, QCoreApplication::translate("economy", "SOURCE"));
    y = top + 26;
    for (const auto& b : banks) {
      const int n = int(std::min<qsizetype>(2, b.decisions.size()));
      const qreal gh = std::max(2, n) * 38;
      const QRectF g(L() + 1, y, W() - 2, gh);
      if (b.id == selected) p.fillRect(g, C("#332901"));
      else if (under(g)) p.fillRect(g, C("#161204"));
      p.fillRect(QRectF(g.left(), y, g.width(), 1), C("#292518"));
      p.setFont(eco::sans(12, QFont::DemiBold)); p.setPen(b.id == selected ? C("#e0ca76") : C("#e5dfcd"));
      p.drawText(QPointF(L() + 9, y + 20), trData(b.name));
      p.setFont(eco::sans(9)); p.setPen(C("#645c3b"));
      p.drawText(QPointF(L() + 9, y + 37), trData(b.committee));
      p.setPen(C("#4a4530"));
      p.drawText(QRectF(L() + 9, y + 42, xH - L() - 30, gh - 44), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, trData(b.committeeNote));
      p.setFont(eco::mono(8)); p.setPen(C("#918a6f"));
      p.drawText(QPointF(xH, y + 16), trData(b.headTitle));
      p.setFont(eco::sans(12, QFont::DemiBold)); p.setPen(C("#e5dfcd"));
      p.drawText(QPointF(xH, y + 33), b.head);
      if (n == 0) {
        p.setFont(eco::sans(12)); p.setPen(C("#6a6961"));
        p.drawText(QPointF(xM, y + 22), "—");
        p.drawText(QRectF(xD, y + 8, xV - xD - 20, 30), Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("economy", "No decision read yet"));
      }
      for (int i = 0; i < n; ++i) {
        const Decision& d = b.decisions[i];
        const qreal ry = y + i * 38;
        if (i > 0) p.fillRect(QRectF(xM - 6, ry, g.right() - xM + 6, 1), C("#292518"));
        p.setFont(eco::sans(12)); p.setPen(C("#c9c5b5"));
        p.drawText(QRectF(xM, ry, 120, 38), Qt::AlignLeft | Qt::AlignVCenter, i18n::locale().toString(d.date, QCoreApplication::translate("economy", "MMM d, yyyy")));
        p.setPen(C("#9a968d"));
        p.drawText(QRectF(xD, ry, xV - xD - 16, 38), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetricsF(eco::sans(12)).elidedText(d.text.isEmpty() ? QString("—") : d.text, Qt::ElideRight, xV - xD - 16));
        if (!d.text.isEmpty()) m_infos.append({QRectF(xD, ry, xV - xD - 16, 38), d.text});
        QString vote = d.vote.isEmpty() ? (b.id == "ECB" ? QCoreApplication::translate("economy", "Not published") : QString("—")) : d.vote;
        p.setFont(eco::mono(12, QFont::Bold)); p.setPen(C("#f5f2eb"));
        p.drawText(QRectF(xV, ry, 200, 38), Qt::AlignLeft | Qt::AlignVCenter, vote);
        if (d.unanimous && d.vote != QCoreApplication::translate("economy", "Unanimous")) {
          const qreal vw = QFontMetricsF(eco::mono(12, QFont::Bold)).horizontalAdvance(vote);
          p.setFont(eco::mono(11)); p.setPen(C("#797255")); p.drawText(QRectF(xV + vw + 6, ry, 120, 38), Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("economy", "unanimous"));
        }
        p.setFont(eco::sans(11)); p.setPen(C("#888165"));
        p.drawText(QRectF(xS, ry, xR - xS - 80, 38), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetricsF(eco::sans(11)).elidedText(d.dissent.isEmpty() ? QString("—") : d.dissent, Qt::ElideRight, xR - xS - 80));
        if (!d.dissent.isEmpty()) m_infos.append({QRectF(xS, ry, xR - xS - 80, 38), d.dissent});
        const QRectF lk(xR - 70, ry, 70, 38);
        p.setFont(eco::sans(11)); p.setPen(under(lk) ? eco::amber() : C("#6e6440"));
        p.drawText(lk, Qt::AlignRight | Qt::AlignVCenter, QCoreApplication::translate("economy", "Official ↗"));
        m_links.append({lk, d.url.isEmpty() ? b.officialUrl : d.url});
      }
      y += gh;
    }
  }

  // ---------------------------------------------------------------- input
  void mouseMoveEvent(QMouseEvent* e) override {
    const bool wasHot = hotAt(mouse);
    mouse = e->position();
    bool hand = false;
    for (int i = 0; i < 8; ++i) hand |= rowRect(i).contains(mouse);
    for (const auto& r : m_chipRects) hand |= r.contains(mouse);
    for (const auto& l : m_links) hand |= l.first.contains(mouse);
    hand |= m_keyRect.contains(mouse);
    setCursor(hand ? Qt::PointingHandCursor : Qt::ArrowCursor);
    QString tip;
    for (const auto& [r, t] : m_infos) if (r.contains(mouse)) tip = t;
    if (!tip.isEmpty()) QToolTip::showText(e->globalPosition().toPoint(), ui::plainTip(tip), this); else QToolTip::hideText();   // decision texts from feeds
    if (wasHot || hotAt(mouse)) update();
  }
  void leaveEvent(QEvent*) override { mouse = {-1, -1}; update(); }
  void mousePressEvent(QMouseEvent* e) override {
    const QPointF pos = e->position();
    const auto& banks = CentralBanks::instance().banks();
    for (int i = 0; i < banks.size(); ++i) if (rowRect(i).contains(pos)) { selected = banks[i].id; chip = 0; update(); return; }
    for (int i = 0; i < m_chipRects.size(); ++i) if (m_chipRects[i].contains(pos)) { chip = i; update(); return; }
    if (m_keyRect.contains(pos)) { keyData = !keyData; update(); return; }
    // links come from the central banks' feeds: web links only
    for (const auto& [r, u] : m_links) if (r.contains(pos) && !u.isEmpty()) { ui::openWebLink(u); return; }
  }

private:
  QTimer m_anim;
  double m_crossX[2] = {NAN, NAN}, m_crossTarget[2] = {NAN, NAN};
  QRectF m_nextPlot[2], m_oddsPlot, m_keyRect;
  QList<QRectF> m_chipRects;
  QList<QPair<QRectF, QString>> m_infos, m_links;
};

// ================================================================================================ page
RatesPage::RatesPage(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  m_scroll = new QScrollArea(this);
  eco::setupScroll(m_scroll);
  m_body = new RatesBody(nullptr);
  m_scroll->setWidget(m_body);
  auto& cb = CentralBanks::instance();
  connect(&cb, &CentralBanks::updated, this, [this] { m_body->setMinimumHeight(m_body->contentHeight()); m_body->update(); });
  macro::KeyFigures::instance().start();
  connect(&macro::KeyFigures::instance(), &macro::KeyFigures::changed, this, [this] { m_body->update(); });
  connect(&eco::Blink::instance(), &eco::Blink::tick, this, [this] { m_body->update(QRect(0, 0, 30, 40)); });
  m_body->setMinimumHeight(m_body->contentHeight());
}

QRectF RatesPage::iconRect(int i) const { return QRectF(width() - 124 + i * 19, 18, 16, 16); }

void RatesPage::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.fillRect(rect(), C("#040200"));
  const QRectF frame(10, 10, width() - 20, height() - 10);
  p.setPen(QPen(C("#241f12"), 1)); p.setBrush(Qt::NoBrush); p.drawRect(frame.adjusted(0.5, 0.5, -0.5, 0.5));
  p.fillRect(QRectF(11, 11, width() - 22, 33), C("#181205"));
  p.fillRect(QRectF(11, 43, width() - 22, 1), C("#676150"));
  // drag handle
  p.setPen(Qt::NoPen); p.setBrush(C("#c5a925"));
  for (int r = 0; r < 3; ++r) for (int c = 0; c < 2; ++c) p.drawEllipse(QPointF(21 + c * 3.5, 23 + r * 3.5), 1, 1);
  p.setFont(eco::mono(12, QFont::Bold, 1.5)); p.setPen(C("#e8c547"));
  p.drawText(QPointF(33, 31), QCoreApplication::translate("economy", "INTEREST RATE PROBABILITY"));
  // icons: settings, pin, lock, collapse, expand, close
  for (int i = 0; i < 6; ++i) {
    const QRectF r = iconRect(i);
    const QPointF c = r.center();
    p.setPen(QPen(i == m_hoverIcon ? C("#c5a925") : C("#777368"), 1.1)); p.setBrush(Qt::NoBrush);
    switch (i) {
      case 0: p.drawEllipse(c, 3.2, 3.2); for (int k = 0; k < 8; ++k) { const double a = k * M_PI / 4; p.drawLine(c + QPointF(std::cos(a) * 3.8, std::sin(a) * 3.8), c + QPointF(std::cos(a) * 5.2, std::sin(a) * 5.2)); } break;
      case 1: p.drawLine(c + QPointF(-3, -5), c + QPointF(3, -5)); p.drawLine(c + QPointF(-2, -5), c + QPointF(-2, 0)); p.drawLine(c + QPointF(2, -5), c + QPointF(2, 0));
              p.drawLine(c + QPointF(-4, 0), c + QPointF(4, 0)); p.drawLine(c + QPointF(0, 0), c + QPointF(0, 5)); break;
      case 2: p.drawRect(QRectF(c.x() - 4, c.y() - 1, 8, 6)); p.drawArc(QRectF(c.x() - 2.6, c.y() - 6, 5.2, 8), 0, 180 * 16); break;
      case 3: p.drawLine(c + QPointF(-4, m_collapsed ? 2 : -2), c + QPointF(0, m_collapsed ? -2 : 2)); p.drawLine(c + QPointF(0, m_collapsed ? -2 : 2), c + QPointF(4, m_collapsed ? 2 : -2)); break;
      case 4: p.drawLine(c + QPointF(-4, 4), c + QPointF(4, -4)); p.drawLine(c + QPointF(1, -4), c + QPointF(4, -4)); p.drawLine(c + QPointF(4, -4), c + QPointF(4, -1));
              p.drawLine(c + QPointF(-4, 1), c + QPointF(-4, 4)); p.drawLine(c + QPointF(-4, 4), c + QPointF(-1, 4)); break;
      case 5: p.drawLine(c + QPointF(-4, -4), c + QPointF(4, 4)); p.drawLine(c + QPointF(4, -4), c + QPointF(-4, 4)); break;
    }
  }
}

void RatesPage::resizeEvent(QResizeEvent*) {
  m_scroll->setGeometry(11, 44, width() - 22, height() - 44);
  m_scroll->setVisible(!m_collapsed);
}

void RatesPage::mouseMoveEvent(QMouseEvent* e) {
  int h = -1;
  for (int i = 0; i < 6; ++i) if (iconRect(i).adjusted(-2, -2, 2, 2).contains(e->position())) h = i;
  if (h != m_hoverIcon) {
    m_hoverIcon = h; update();
    const QStringList tips{QCoreApplication::translate("economy", "Sources: NY Fed, ECB, BoE, BoC, SNB, BIS, CME via Yahoo, Montréal Exchange, JPX, Eurex, Fed/BoE statements"),
                           QCoreApplication::translate("economy", "Pinning panels is not available on this page"),
                           QCoreApplication::translate("economy", "Layout lock is not available on this page"),
                           QCoreApplication::translate("economy", "Collapse / expand the panel"),
                           QCoreApplication::translate("economy", "Floating panels are not available on this page"),
                           QCoreApplication::translate("economy", "Closing the panel is not available on this page")};
    if (h >= 0) QToolTip::showText(e->globalPosition().toPoint(), tips[h], this); else QToolTip::hideText();
  }
  setCursor(h == 3 ? Qt::PointingHandCursor : Qt::ArrowCursor);
}
void RatesPage::mousePressEvent(QMouseEvent* e) {
  if (iconRect(3).adjusted(-2, -2, 2, 2).contains(e->position())) { m_collapsed = !m_collapsed; m_scroll->setVisible(!m_collapsed); update(); }
}

void RatesPage::showEvent(QShowEvent*) {
  CentralBanks::instance().setActive(this, true);   // loads what is stale, then keeps it fresh while shown
  eco::Blink::instance().hold(this, true);
  if (m_hooksDone) return;
  m_hooksDone = true;
  const QByteArray bk = qgetenv("DW_RATES_BANK");
  if (!bk.isEmpty()) m_body->selected = QString::fromLatin1(bk).toUpper();
  const int sc = qEnvironmentVariableIntValue("DW_RATES_SCROLL");
  const QByteArray hv = qgetenv("DW_RATES_HOVER");
  if (sc > 0 || !hv.isEmpty())
    QTimer::singleShot(qEnvironmentVariableIsSet("DW_RATES_HOOK_DELAY") ? qEnvironmentVariableIntValue("DW_RATES_HOOK_DELAY") : 5000, this, [this, sc, hv] {
      m_body->setMinimumHeight(m_body->contentHeight());
      if (sc > 0) m_scroll->verticalScrollBar()->setValue(sc);
      const auto parts = hv.split(',');
      if (parts.size() == 2) {
        const QPointF local = m_body->mapFrom(this, QPointF(parts[0].toInt(), parts[1].toInt()));
        QMouseEvent ev(QEvent::MouseMove, local, m_body->mapToGlobal(local), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(m_body, &ev);
      }
    });
}
void RatesPage::hideEvent(QHideEvent*) {
  CentralBanks::instance().setActive(this, false);
  eco::Blink::instance().hold(this, false);
}
void RatesPage::preload() { CentralBanks::instance().ensureLoaded(); }

DW_PAGE("economy/rates", RatesPage)
}  // namespace economy
