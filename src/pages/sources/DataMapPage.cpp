// Sources › Data: a diagram of where Deepwater's data comes from and which tabs use it.
//
// Left: every outside source declared with sources::Registrar (live status dot), grouped by the main tab it feeds most,
// plus the few local inputs that are not network sources (broker files, local models, the clock). Right: the main tabs
// and their sub-tabs in navigation order. A curve joins a source to each sub-tab it feeds. Hover a source, a sub-tab or
// a tab to light up its links; the tooltip gives the source's publisher, data, access, terms and status.
//
// Built from the registry's `pages` lists only (no second table to keep in sync): a source that feeds a new page must
// list it in its Registrar. Pages named there but absent from the navigation are reported under the diagram.
// A search field lights the sources whose name, publisher or data match (case and accents ignored) and scrolls to them;
// Enter goes to the next match.
// Test hooks: DW_DATAMAP_HOVER=<source id | tab/sub | tab> (lit as if hovered), DW_DATAMAP_DUMP=<file> (links as text),
// DW_DATAMAP_SEARCH=<text> (typed in the search field).
// Phones (page narrower than ui::kCompactWidth): the diagram keeps its two columns, fitted to the width (narrower
// boxes, short curves between them) and scrolls vertically only; a tap lights a node's links and shows its tooltip
// (no hover with a finger). The header stacks: title, search at full width, counts, legend on one line.
#include "app/Pages.h"
#include "net/Sources.h"
#include "positioning/Retail.h"
#include "ui/Fit.h"
#include "ui/Kit.h"
#include "ui/Theme.h"
#include "ui/Touch.h"
#include <QCoreApplication>
#include <QFile>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QToolTip>
#include <QScrollBar>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace srcmap {
namespace {
struct Local { const char* id; const char* name; const char* data; QStringList pages; };
// Inputs that are not network sources but still feed pages (drawn with a dashed outline).
QList<Local> locals() {
  return {
      {"local-broker", QT_TRANSLATE_NOOP("sources", "Your broker's exports (MT4, MT5, cTrader)"),
       QT_TRANSLATE_NOOP("sources", "Trade history files and the live bridge files the platforms write on this computer"),
       {"account/track-record", "account/risk-calculator"}},
      {"local-clock", QT_TRANSLATE_NOOP("sources", "This computer's clock (computed by Deepwater)"),
       QT_TRANSLATE_NOOP("sources", "Session hours and countdowns are computed from the time zones, no outside data"),
       {"global/market-hours"}},
  };
}

// Feeds of one institution declared separately (press RSS, data API, website) shown as one node.
QString nodeOf(const QString& id) {
  static const QHash<QString, QString> m{
      {"fed-press", "federalreserve"}, {"fed-rss", "federalreserve"},
      {"ecb-press", "ecb"}, {"ecb-rss", "ecb"},
      {"boe-news", "boe"}, {"boe-rss", "boe"},
      {"boc-valet", "boc"}, {"yahoo-rss", "yahoo"}};
  return m.value(id, id);
}

// One distinct hue per main tab, whatever the theme (theme colours collapse on some themes: Glacier's accent and warn
// are both blue): a light and a dark variant of the same seven hues.
// lower case without accents, for the search
QString fold(const QString& s) {
  QString d = s.normalized(QString::NormalizationForm_D).toLower();
  QString out; out.reserve(d.size());
  for (const QChar ch : d) if (ch.category() != QChar::Mark_NonSpacing) out += ch;
  return out;
}

// Source names come in English: a French word of the search also matches its English form ("banque" finds "Bank").
QStringList forms(const QString& word) {
  static const QHash<QString, QStringList> fr{
      {"banque", {"bank"}}, {"banques", {"bank"}}, {"centrale", {"central"}}, {"centrales", {"central"}},
      {"federale", {"federal"}}, {"europeenne", {"european"}}, {"europe", {"europ"}}, {"japon", {"japan"}},
      {"angleterre", {"england"}}, {"suisse", {"swiss"}}, {"canada", {"canada"}}, {"australie", {"australia"}},
      {"etats-unis", {"u.s.", "us ", "united states"}}, {"americain", {"u.s.", "us "}}, {"statistiques", {"statistic"}},
      {"statistique", {"statistic"}}, {"tresor", {"treasury"}}, {"bourse", {"exchange"}}, {"marche", {"market"}},
      {"marches", {"market"}}, {"actualites", {"news", "rss"}}, {"nouvelles", {"news"}}, {"calendrier", {"calendar"}},
      {"mondiale", {"world"}}, {"reglements", {"settlements"}}, {"internationaux", {"international"}},
      {"ministere", {"ministry"}}, {"finances", {"finance"}}, {"travail", {"labor"}}, {"emploi", {"employment", "labor"}},
      {"devises", {"currenc", "fx"}}, {"taux", {"rate"}}, {"prix", {"price"}}, {"cours", {"price", "quote"}},
      {"petrole", {"oil", "crude"}}, {"or", {"gold"}}, {"modeles", {"model"}}, {"courtier", {"broker"}},
      {"horloge", {"clock"}}, {"recherche", {"research"}}, {"prevision", {"forecast"}}, {"previsions", {"forecast"}},
      {"positions", {"position"}}, {"futures", {"futures"}}, {"obligations", {"bond"}}, {"chomage", {"unemployment"}},
      {"croissance", {"growth", "gdp"}}, {"pib", {"gdp"}}, {"video", {"youtube", "stream"}}, {"direct", {"live"}}};
  return QStringList{word} + fr.value(word);
}

QColor tabColour(const QString& tab) {
  static const QHash<QString, QPair<const char*, const char*>> hues{
      {"global", {"#e8b923", "#a16207"}}, {"news", {"#22d3ee", "#0e7490"}}, {"economy", {"#fb923c", "#c2410c"}},
      {"positioning", {"#f87171", "#be123c"}}, {"market", {"#4ade80", "#15803d"}}, {"account", {"#94a3b8", "#475569"}}};
  if (!hues.contains(tab)) return theme::pal().textDim;
  const auto h = hues.value(tab);
  return QColor(theme::isDark() ? h.first : h.second);
}

enum class St { Waiting, Active, Failing, Disabled, Local };
St statusOf(const QString& id) {
  if (id.startsWith("local-")) return St::Local;
  if (sources::isDisabled(id)) return St::Disabled;
  if (id == "myfxbook") {   // its host is shared with the calendar RSS: the account decides
    using S = positioning::Retail::State;
    switch (positioning::Retail::instance().state()) {
      case S::Connected: return St::Active;
      case S::Error: case S::Expired: return St::Failing;
      default: return St::Waiting;
    }
  }
  switch (sources::status(id)) {
    case sources::Status::Active: return St::Active;
    case sources::Status::Failing: return St::Failing;
    case sources::Status::Disabled: return St::Disabled;
    default: return St::Waiting;
  }
}
QString statusText(St s) {
  switch (s) {
    case St::Active: return QCoreApplication::translate("sources", "ACTIVE");
    case St::Failing: return QCoreApplication::translate("sources", "FAILING");
    case St::Disabled: return QCoreApplication::translate("sources", "DISABLED");
    case St::Local: return QCoreApplication::translate("sources", "LOCAL");
    default: return QCoreApplication::translate("sources", "WAITING");
  }
}
QColor statusColour(St s) {
  const auto& c = theme::pal();
  switch (s) {
    case St::Active: return c.up;
    case St::Failing: return c.down;
    case St::Disabled: return c.textMute;
    case St::Local: return c.accentText;
    default: return c.textDim;
  }
}
QString accessText(sources::Access a) {
  switch (a) {
    case sources::Access::Key: return QCoreApplication::translate("sources", "API key");
    case sources::Access::Account: return QCoreApplication::translate("sources", "your account");
    case sources::Access::Bundled: return QCoreApplication::translate("sources", "bundled with the app");
    case sources::Access::Forbidden: return QCoreApplication::translate("sources", "not used (terms)");
    default: return QCoreApplication::translate("sources", "free, no key");
  }
}
}  // namespace

// --------------------------------------------------------------------------------------------- canvas
class Canvas : public QWidget {
public:
  struct Src { QString id, name, publisher, data, terms; sources::Access access = sources::Access::Keyless; QStringList pages;
               QString group; bool local = false; QRectF r; QStringList feeds; };   // feeds: registry ids merged here
  struct Sub { QString page, tab, label; QRectF r; };
  struct Tab { QString id, label; QRectF r; };

  explicit Canvas(QWidget* p) : QWidget(p) {
    setMouseTracking(true);
    build();
    if (qEnvironmentVariableIsSet("DW_DATAMAP_HOVER")) m_hover = qEnvironmentVariable("DW_DATAMAP_HOVER");
    // every answer of every source notifies; what this canvas shows of it is one status dot per node: repaint when one changes
    connect(&sources::Notifier::instance(), &sources::Notifier::changed, this, [this] {
      QVector<int> shown; shown.reserve(m_src.size());
      for (const auto& s : std::as_const(m_src)) shown << int(nodeStatus(s));
      if (shown == m_shownStatus) return;
      m_shownStatus = shown;
      update();
    });
  }
  QStringList unknownPages() const { return m_unknown; }
  int sourceCount() const { int n = 0; for (const auto& s : m_src) if (!s.local) ++n; return n; }
  int subCount() const { return int(m_subs.size()); }
  int linkCount() const { int n = 0; for (const auto& s : m_src) for (const auto& p : s.pages) if (m_subIndex.contains(p)) ++n; return n; }
  QString dump() const {
    QString out;
    for (const auto& s : m_src) out += s.group + " | " + s.id + " | " + s.pages.join(", ") + "\n";
    return out;
  }
  QSize sizeHint() const override { return QSize(900, m_height); }

  // Search: lights the matching sources; returns the y of each match (top to bottom) so the page can scroll to them.
  QList<int> setQuery(const QString& q) {
    m_query = fold(q.trimmed());
    if (m_laidW != width()) layout();
    // the matching set is computed once here: paint asked matches() ~6,000 times per frame (each folding the texts)
    m_matchIds.clear(); m_matchPages.clear();
    QList<int> ys;
    if (!m_query.isEmpty()) for (const auto& s : m_src) if (computeMatch(s)) {
      ys << int(s.r.top());
      m_matchIds.insert(s.id);
      for (const auto& pg : s.pages) m_matchPages.insert(pg);
    }
    update();
    return ys;
  }

protected:
  static constexpr int kRow = 22, kHead = 26, kGap = 10;

  void build() {
    // right column: navigation order, the Sources tab itself left out
    for (const auto& t : app::tabs()) {
      if (t.id == "sources") continue;
      m_tabs << Tab{t.id, app::label(t.label).toUpper(), {}};
      for (const auto& s : t.subs) { m_subIndex.insert(t.id + "/" + s.id, int(m_subs.size())); m_subs << Sub{t.id + "/" + s.id, t.id, app::label(s.label), {}}; }
    }
    QStringList tabOrder; for (const auto& t : m_tabs) tabOrder << t.id;
    // left column: registry + local inputs, grouped by the tab they feed most (ties: navigation order)
    QHash<QString, int> byNode;
    for (const auto& s : sources::all()) {
      if (s.access == sources::Access::Forbidden) continue;
      const QString node = nodeOf(s.id);
      if (byNode.contains(node)) {   // another feed of the same institution: union of its pages
        Src& x = m_src[byNode.value(node)];
        x.feeds << s.id;
        for (const auto& p : s.pages) if (!x.pages.contains(p)) x.pages << p;
        continue;
      }
      byNode.insert(node, int(m_src.size()));
      m_src << Src{node, s.publisher, s.publisher, s.data, s.terms, s.access, s.pages, {}, false, {}, {s.id}};
    }
    // the node keeps the institution's main declaration (the data feed) for its texts
    for (auto& x : m_src) if (const auto* main = sources::byId(x.id)) { x.publisher = main->publisher; x.name = main->publisher;
                                                                      x.data = main->data; x.terms = main->terms; x.access = main->access; }
    for (const auto& l : locals())
      m_src << Src{l.id, QCoreApplication::translate("sources", l.name), QCoreApplication::translate("sources", l.name), QCoreApplication::translate("sources", l.data), QString(), sources::Access::Bundled, l.pages, {}, true, {}, {}};
    for (auto& s : m_src) {
      QHash<QString, int> n;
      for (const auto& p : s.pages) {
        if (!m_subIndex.contains(p)) { if (!m_unknown.contains(s.id + " → " + p)) m_unknown << s.id + " → " + p; continue; }
        ++n[p.section('/', 0, 0)];
      }
      // group: the tab of the first page the source declares (its home page)
      s.group.clear();
      for (const auto& p : s.pages) if (m_subIndex.contains(p)) { s.group = p.section('/', 0, 0); break; }
      if (s.group.isEmpty()) s.group = tabOrder.value(0);
      if (s.local) s.group = "local";
      // shorter display name: the publisher without its parenthesis ("Bank of England (Interactive Database…)")
      const int par = int(s.name.indexOf(" ("));
      if (par > 8) s.name = s.name.left(par);
    }
    std::stable_sort(m_src.begin(), m_src.end(), [&](const Src& a, const Src& b) {
      const int ga = a.group == "local" ? 99 : int(tabOrder.indexOf(a.group)), gb = b.group == "local" ? 99 : int(tabOrder.indexOf(b.group));
      if (ga != gb) return ga < gb;
      // inside a group: by the first sub-tab they feed (fewer crossings), then by name
      auto first = [&](const Src& s) { int m = 1 << 20; for (const auto& p : s.pages) if (m_subIndex.contains(p)) m = qMin(m, m_subIndex.value(p)); return m; };
      if (first(a) != first(b)) return first(a) < first(b);
      return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    int groups = 0; QString g;
    for (const auto& s : m_src) if (s.group != g) { g = s.group; ++groups; }
    m_height = 20 + int(m_src.size()) * kRow + groups * (kHead + kGap) + 20;
    setMinimumHeight(m_height);
  }

  bool phone() const { return ui::compact(width()); }
  // phone: sub-tab boxes barely indented under their tab, so their labels keep room
  qreal subIndent() const { return phone() ? 6 : 14; }

  void layout() {
    const qreal W = width();
    qreal leftW = qBound<qreal>(250, W * 0.30, 400), rightW = qBound<qreal>(200, W * 0.24, 320);
    m_leftX = 8;
    // phone: the desktop's minimum widths (450 px) overlap on 400 px; the columns share the width, ~10 % left for curves
    if (phone()) { leftW = W * 0.5; rightW = W * 0.4; m_leftX = 0; }
    m_rightX = W - rightW - (phone() ? 0 : 8);
    qreal y = 20; QString g;
    for (auto& s : m_src) {
      if (s.group != g) { g = s.group; y += (g == m_src.first().group ? 0 : kGap) + kHead; }
      s.r = QRectF(m_leftX, y, leftW, kRow - 3);
      y += kRow;
    }
    const qreal leftBottom = y;
    // right column spread over the same height as the left one
    const int rows = int(m_subs.size()) + int(m_tabs.size());
    const qreal step = qMax<qreal>(kRow + 4, (leftBottom - 20 - m_tabs.size() * kGap) / qMax(1, rows));
    y = 20;
    for (auto& t : m_tabs) {
      t.r = QRectF(m_rightX, y, rightW, step - 3);
      y += step;
      for (auto& s : m_subs) if (s.tab == t.id) { s.r = QRectF(m_rightX + subIndent(), y, rightW - subIndent(), step - 5); y += step; }
      y += kGap;
    }
    m_laidW = W;
  }

  // a node of several feeds: failing if one fails, active if one answers, disabled when all are off
  static St nodeStatus(const Src& s) {
    if (s.local) return St::Local;
    bool active = false, failing = false, allOff = true;
    for (const auto& f : s.feeds) {
      const St st = statusOf(f);
      if (st != St::Disabled) allOff = false;
      if (st == St::Failing) failing = true;
      if (st == St::Active) active = true;
    }
    return allOff ? St::Disabled : failing ? St::Failing : active ? St::Active : St::Waiting;
  }

  bool matches(const Src& s) const { return !m_query.isEmpty() && m_matchIds.contains(s.id); }
  bool computeMatch(const Src& s) const {
    if (m_query.isEmpty()) return false;
    QString hay = s.name + " " + s.publisher + " " + s.id + " " + s.data;
    for (const auto& f : s.feeds) if (const auto* src = sources::byId(f)) hay += " " + src->publisher + " " + src->data + " " + f;
    // every word of the search must match, in French or in its English form
    const QString h = fold(hay);
    for (const auto& w : m_query.split(' ', Qt::SkipEmptyParts)) {
      bool any = false;
      for (const auto& f : forms(w)) if (h.contains(f)) { any = true; break; }
      if (!any) return false;
    }
    return true;
  }
  bool searching() const { return m_hover.isEmpty() && !m_query.isEmpty(); }

  bool lit(const Src& s) const {
    if (searching()) return matches(s);
    if (m_hover.isEmpty()) return true;
    if (m_hover == s.id) return true;
    for (const auto& p : s.pages) if (p == m_hover || p.section('/', 0, 0) == m_hover) return true;
    return false;
  }
  bool litSub(const Sub& sub) const {
    if (searching()) return m_matchPages.contains(sub.page);
    if (m_hover.isEmpty() || m_hover == sub.page || m_hover == sub.tab) return true;
    for (const auto& s : m_src) if (s.id == m_hover && s.pages.contains(sub.page)) return true;
    return false;
  }
  bool litLink(const Src& s, const QString& page) const {
    if (searching()) return matches(s);
    if (m_hover.isEmpty()) return true;
    return m_hover == s.id || m_hover == page || m_hover == page.section('/', 0, 0);
  }

  void paintEvent(QPaintEvent*) override {
    if (m_laidW != width()) layout();
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    const auto& c = theme::pal();
    p.fillRect(rect(), c.bg);
    const bool focus = !m_hover.isEmpty() || !m_query.isEmpty();

    // links first (under the boxes)
    for (const auto& s : m_src) {
      for (const auto& page : s.pages) {
        const int k = m_subIndex.value(page, -1);
        if (k < 0) continue;
        const auto& sub = m_subs[k];
        const QPointF a(s.r.right(), s.r.center().y()), b(sub.r.left(), sub.r.center().y());
        const qreal dx = (b.x() - a.x()) * 0.5;
        QPainterPath path(a);
        path.cubicTo(a + QPointF(dx, 0), b - QPointF(dx, 0), b);
        QColor col = tabColour(sub.tab);
        const bool on = litLink(s, page);
        col.setAlpha(focus ? (on ? 220 : 14) : 70);
        p.setPen(QPen(col, focus && on ? 1.8 : 1.1, s.local ? Qt::DashLine : Qt::SolidLine));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
      }
    }

    // left: group headers and sources
    QString g;
    for (const auto& s : m_src) {
      if (s.group != g) {
        g = s.group;
        const QColor gc = g == "local" ? c.accentText : tabColour(g);
        QString label = g == "local" ? QCoreApplication::translate("sources", "LOCAL, NOT FROM THE NETWORK") : QString();
        if (label.isEmpty()) for (const auto& t : m_tabs) if (t.id == g) label = QCoreApplication::translate("sources", "MAINLY FOR %1").arg(t.label);
        int n = 0; for (const auto& x : m_src) if (x.group == g) ++n;
        p.setFont(theme::mono(9, QFont::Bold, 1.6)); p.setPen(gc);
        const QString count = "  ·  " + QString::number(n);
        // phone: the narrow column elides the label, never the count
        if (phone()) label = QFontMetrics(p.font()).elidedText(label, Qt::ElideRight, int(s.r.width() - QFontMetrics(p.font()).horizontalAdvance(count)));
        p.drawText(QRectF(m_leftX, s.r.top() - kHead + 4, s.r.width(), kHead - 6), Qt::AlignLeft | Qt::AlignVCenter,
                   label + count);
      }
      const bool on = lit(s);
      const St st = nodeStatus(s);
      const bool found = searching() && matches(s);
      const QColor border = m_hover == s.id || found ? c.accent : c.border;
      p.setOpacity(focus && !on ? 0.35 : 1.0);
      p.setPen(QPen(border, 1, s.local ? Qt::DashLine : Qt::SolidLine)); p.setBrush(m_hover == s.id ? c.panelAlt : c.panel);
      p.drawRoundedRect(s.r.adjusted(.5, .5, -.5, -.5), 3, 3);
      const QColor sc = statusColour(st);
      p.setPen(Qt::NoPen); p.setBrush(st == St::Waiting ? Qt::transparent : sc);
      if (st == St::Waiting) p.setPen(QPen(sc, 1));
      p.drawEllipse(QPointF(s.r.left() + 10, s.r.center().y()), 3.2, 3.2);
      int fed = 0; for (const auto& pg : s.pages) if (m_subIndex.contains(pg)) ++fed;
      p.setFont(theme::mono(9)); p.setPen(c.textMute);
      const QString cnt = QString::number(fed);
      p.drawText(s.r.adjusted(0, 0, -8, 0), Qt::AlignRight | Qt::AlignVCenter, cnt);
      p.setFont(theme::sans(11)); p.setPen(on ? c.text : c.textDim);
      p.drawText(s.r.adjusted(20, 0, -26, 0), Qt::AlignLeft | Qt::AlignVCenter,
                 QFontMetrics(p.font()).elidedText(s.name, Qt::ElideRight, int(s.r.width() - 48)));
      p.setOpacity(1.0);
    }

    // right: tabs and sub-tabs
    for (const auto& t : m_tabs) {
      const QColor tc = tabColour(t.id);
      const bool on = !focus || m_hover == t.id || std::any_of(m_subs.begin(), m_subs.end(), [&](const Sub& s) { return s.tab == t.id && litSub(s); });
      p.setOpacity(on ? 1.0 : 0.4);
      p.setPen(Qt::NoPen); p.setBrush(tc); p.drawRoundedRect(QRectF(t.r.left(), t.r.top() + 3, 4, t.r.height() - 6), 2, 2);
      p.setFont(theme::mono(10, QFont::Bold, 2.0)); p.setPen(tc);
      p.drawText(t.r.adjusted(12, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, t.label);
      p.setOpacity(1.0);
    }
    for (const auto& s : m_subs) {
      const bool on = litSub(s);
      int n = 0; for (const auto& x : m_src) if (x.pages.contains(s.page)) ++n;
      p.setOpacity(focus && !on ? 0.35 : 1.0);
      p.setPen(QPen(m_hover == s.page ? c.accent : c.border, 1)); p.setBrush(m_hover == s.page ? c.panelAlt : c.panel);
      p.drawRoundedRect(s.r.adjusted(.5, .5, -.5, -.5), 3, 3);
      p.setPen(Qt::NoPen); p.setBrush(tabColour(s.tab)); p.drawEllipse(QPointF(s.r.left() + 9, s.r.center().y()), 2.6, 2.6);
      p.setFont(theme::sans(11)); p.setPen(on ? c.text : c.textDim);
      const int pad = phone() ? 20 : 30;   // phone: the count needs ~20 px, the label gets the rest
      p.drawText(s.r.adjusted(18, 0, -pad, 0), Qt::AlignLeft | Qt::AlignVCenter,
                 QFontMetrics(p.font()).elidedText(s.label, Qt::ElideRight, int(s.r.width() - 20 - pad)));
      p.setFont(theme::mono(9)); p.setPen(n == 0 ? c.warn : c.textMute);
      p.drawText(s.r.adjusted(0, 0, -8, 0), Qt::AlignRight | Qt::AlignVCenter, QString::number(n));
      p.setOpacity(1.0);
    }
  }

  QString hitTest(const QPointF& pos) const {
    for (const auto& s : m_src) if (s.r.contains(pos)) return s.id;
    for (const auto& s : m_subs) if (s.r.contains(pos)) return s.page;
    for (const auto& t : m_tabs) if (t.r.contains(pos)) return t.id;
    return {};
  }
  void mouseMoveEvent(QMouseEvent* e) override {
    if (qEnvironmentVariableIsSet("DW_DATAMAP_HOVER")) return;   // screenshot hook: keep the forced hover
    const QString h = hitTest(e->position());
    if (h != m_hover) { m_hover = h; setCursor(h.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor); update(); }
  }
  void leaveEvent(QEvent*) override { if (!qEnvironmentVariableIsSet("DW_DATAMAP_HOVER")) { m_hover.clear(); update(); } }
  // A finger does not hover: on a phone (or with touch) a tap lights the node's links (a second tap, or a tap on the
  // background, clears them) and shows its tooltip. A drag (the scroller) is not a tap.
  bool tapMode() const { return phone() || ui::touch::enabled(); }
  void mousePressEvent(QMouseEvent* e) override { m_press = e->position(); QWidget::mousePressEvent(e); }
  void mouseReleaseEvent(QMouseEvent* e) override {
    if (!tapMode() || (e->position() - m_press).manhattanLength() > 12) { QWidget::mouseReleaseEvent(e); return; }
    const QString h = hitTest(e->position());
    m_hover = h == m_hover ? QString() : h;
    update();
    const QString html = tipHtml(m_hover);
    if (html.isEmpty()) QToolTip::hideText();
    else QToolTip::showText(e->globalPosition().toPoint(), "<div style='max-width:" + QString::number(qMin(420, width() - 40)) + "px'>" + html + "</div>", this);
  }
  void resizeEvent(QResizeEvent*) override { m_laidW = -1; }

  bool event(QEvent* e) override {
    if (e->type() != QEvent::ToolTip) return QWidget::event(e);
    auto* he = static_cast<QHelpEvent*>(e);
    const QString html = tipHtml(hitTest(he->pos()));
    if (html.isEmpty()) QToolTip::hideText(); else QToolTip::showText(he->globalPos(), "<div style='max-width:420px'>" + html + "</div>", this);
    return true;
  }

  // the tooltip of a source or a sub-tab (empty for anything else)
  QString tipHtml(const QString& h) const {
    QString html;
    for (const auto& s : m_src) {
      if (s.id != h) continue;
      const St st = nodeStatus(s);
      QStringList pages;
      for (const auto& pg : s.pages) if (m_subIndex.contains(pg)) {
        const auto& sub = m_subs[m_subIndex.value(pg)];
        QString tab; for (const auto& t : m_tabs) if (t.id == sub.tab) tab = t.label;
        pages << (tab + " › " + sub.label).toHtmlEscaped();
      }
      html = "<b>" + s.publisher.toHtmlEscaped() + "</b><br>" + s.data.toHtmlEscaped() + "<br><br>" +
             QCoreApplication::translate("sources", "Access: %1").arg(s.local ? QCoreApplication::translate("sources", "on this computer") : accessText(s.access)).toHtmlEscaped() + "<br>" +
             QCoreApplication::translate("sources", "Status: %1").arg(statusText(st)).toHtmlEscaped() + "<br>" +
             [&] {   // a node of several feeds lists them with their own status
               if (s.feeds.size() < 2) return QString();
               QStringList l;
               for (const auto& f : s.feeds) if (const auto* src = sources::byId(f))
                 l << "· " + (src->data.left(90) + (src->data.size() > 90 ? "…" : "")).toHtmlEscaped() + " — " + statusText(statusOf(f)).toHtmlEscaped();
               return QCoreApplication::translate("sources", "Feeds:").toHtmlEscaped() + "<br>" + l.join("<br>") + "<br>";
             }() +
             (s.terms.isEmpty() ? QString() : QCoreApplication::translate("sources", "Terms: %1").arg(s.terms).toHtmlEscaped() + "<br>") +
             "<br><b>" + QCoreApplication::translate("sources", "Used by").toHtmlEscaped() + "</b><br>" + pages.join("<br>");
    }
    for (const auto& sub : m_subs) {
      if (sub.page != h) continue;
      QStringList names; for (const auto& s : m_src) if (s.pages.contains(sub.page)) names << s.name;
      html = "<b>" + sub.label.toHtmlEscaped() + "</b><br>" +
             (names.isEmpty() ? QCoreApplication::translate("sources", "No outside source.") : QCoreApplication::translate("sources", "%n source(s): ", nullptr, int(names.size())) + names.join(", ")).toHtmlEscaped();
    }
    return html;
  }

private:
  QList<Src> m_src;
  QList<Sub> m_subs;
  QList<Tab> m_tabs;
  QHash<QString, int> m_subIndex;
  QStringList m_unknown;
  QString m_hover, m_query;
  QSet<QString> m_matchIds, m_matchPages;   // sources matching m_query and the pages they feed (setQuery)
  QVector<int> m_shownStatus;   // node statuses when the canvas was last told to repaint
  qreal m_leftX = 0, m_rightX = 0, m_laidW = -1;
  QPointF m_press;
  int m_height = 600;
};

// --------------------------------------------------------------------------------------------- page
class DataMapPage : public QWidget {
public:
  explicit DataMapPage(QWidget* parent) : QWidget(parent) {
    const auto& c = theme::pal();
    auto* root = m_root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 10); root->setSpacing(8);
    auto* head = m_head = new QHBoxLayout; head->setSpacing(12);
    head->addWidget(ui::label(QCoreApplication::translate("sources", "SOURCES GRAPH"), theme::title(), c.accentText, this));
    // elides ("…", tooltip) rather than run under the search field on a narrow window
    head->addWidget(fit::label(QCoreApplication::translate("sources", "where Deepwater's data comes from, and which tabs use it"), theme::mono(11), c.textDim, this, 60), 1);
    m_canvas = new Canvas(this);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QCoreApplication::translate("sources", "Search a source…"));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedWidth(240);
    m_search->setStyleSheet(QString("QLineEdit { background: %1; border: 1px solid %2; border-radius: 3px; padding: 4px 8px; color: %3;"
                                    " font-family: 'JetBrains Mono'; font-size: 12px; } QLineEdit:focus { border-color: %4; }")
                                .arg(c.panelAlt.name(), c.border.name(), c.text.name(), c.accent.name()));
    m_found = ui::label(QString(), theme::mono(10), c.textDim, this);
    head->addWidget(m_found);
    head->addWidget(m_search);
    m_counts = ui::label(QCoreApplication::translate("sources", "%1 sources  ·  %2 sub-tabs  ·  %3 links").arg(m_canvas->sourceCount()).arg(m_canvas->subCount()).arg(m_canvas->linkCount()),
                         theme::mono(10), c.textDim, this);
    head->addWidget(m_counts);
    root->addLayout(head);

    // legend: status dots and tab colours
    auto* legend = m_legend = new QHBoxLayout; legend->setSpacing(16);
    auto dot = [&](const QColor& col, const QString& text, bool hollow = false) {
      auto* l = ui::label(QString("<span style='color:%1;'>%2</span>&nbsp;%3").arg(col.name(), hollow ? "○" : "●", text.toHtmlEscaped()),
                          theme::mono(10), c.textDim, this);
      l->setTextFormat(Qt::RichText);
      legend->addWidget(l);
    };
    dot(c.up, statusText(St::Active)); dot(c.down, statusText(St::Failing)); dot(c.textDim, statusText(St::Waiting), true);
    dot(c.textMute, statusText(St::Disabled)); dot(c.accentText, statusText(St::Local));
    legend->addStretch(1);
    auto* hint = fit::label(QCoreApplication::translate("sources", "Hover a source or a tab to follow its links"), theme::sans(11), c.textMute, this, 60);
    hint->setOwnToolTip(QCoreApplication::translate("sources", "The number on a source is how many sub-tabs it feeds; on a sub-tab, how many sources it reads."));
    legend->addWidget(hint);
    m_hint = hint;
    root->addLayout(legend);

    auto* scroll = new QScrollArea(this);
    m_scroll = scroll;
    scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(m_canvas);
    scroll->setStyleSheet(QString("QScrollArea { background:%1; }").arg(c.bg.name()));
    root->addWidget(scroll, 1);

    const QStringList unknown = m_canvas->unknownPages();
    if (!unknown.isEmpty()) {
      auto* warn = ui::label(QCoreApplication::translate("sources", "Declared pages not in the navigation: %1").arg(unknown.join(", ")), theme::mono(10), c.warn, this);
      warn->setWordWrap(true);
      root->addWidget(warn);
    }
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& t) {
      m_matches = m_canvas->setQuery(t);
      m_at = 0;
      const auto& pal = theme::pal();
      if (t.trimmed().isEmpty()) m_found->clear();
      else if (m_matches.isEmpty()) m_found->setText(QCoreApplication::translate("sources", "no source"));
      else m_found->setText(QCoreApplication::translate("sources", "%n source(s)", nullptr, int(m_matches.size())));
      m_found->setStyleSheet(QString("color:%1;background:transparent;").arg((m_matches.isEmpty() && !t.trimmed().isEmpty() ? pal.warn : pal.textDim).name()));
      showMatch();
    });
    connect(m_search, &QLineEdit::returnPressed, this, [this] {   // Enter: next match
      if (m_matches.isEmpty()) return;
      m_at = (m_at + 1) % int(m_matches.size());
      showMatch();
    });
    if (qEnvironmentVariableIsSet("DW_DATAMAP_SEARCH"))
      QTimer::singleShot(0, this, [this] { m_search->setText(qEnvironmentVariable("DW_DATAMAP_SEARCH")); });
    if (qEnvironmentVariableIsSet("DW_DATAMAP_DUMP")) {
      QFile f(qEnvironmentVariable("DW_DATAMAP_DUMP"));
      if (f.open(QIODevice::WriteOnly)) f.write((m_canvas->dump() + "unknown: " + unknown.join(", ") + "\n").toUtf8());
    }
  }
protected:
  void paintEvent(QPaintEvent*) override { QPainter p(this); p.fillRect(rect(), theme::pal().bg); }
  void resizeEvent(QResizeEvent* e) override { QWidget::resizeEvent(e); setPhone(ui::compact(width())); }
private:
  // Phone header: title line, the search at full width (finger height), then the match count and the totals; the
  // legend's five dots fit one line once the hover hint (no hover with a finger) is dropped.
  void setPhone(bool phone) {
    if (phone == m_phone) return;
    m_phone = phone;
    for (auto* w : {static_cast<QWidget*>(m_found), static_cast<QWidget*>(m_search), static_cast<QWidget*>(m_counts)}) {
      m_head->removeWidget(w); m_root->removeWidget(w);
      if (m_row2) m_row2->removeWidget(w);
    }
    if (phone) {
      m_root->insertWidget(1, m_search);
      if (!m_row2) { m_row2 = new QHBoxLayout; m_row2->setSpacing(12); }
      m_row2->addWidget(m_found); m_row2->addStretch(1); m_row2->addWidget(m_counts);
      m_root->insertLayout(2, m_row2);
      m_search->setMinimumWidth(0); m_search->setMaximumWidth(QWIDGETSIZE_MAX); m_search->setFixedHeight(36);
    } else {
      m_root->removeItem(m_row2);
      while (auto* it = m_row2->takeAt(0)) delete it;   // the stretch (the widgets were taken out above)
      delete m_row2; m_row2 = nullptr;
      // desktop order: title, subtitle, match count, search, totals
      m_head->insertWidget(2, m_found); m_head->insertWidget(3, m_search); m_head->insertWidget(4, m_counts);
      m_search->setFixedWidth(240); m_search->setMinimumHeight(0); m_search->setMaximumHeight(QWIDGETSIZE_MAX);
    }
    m_legend->setSpacing(phone ? 8 : 16);
    // the right column reaches the edge on a phone: no scroll bar over it (the finger scrolls)
    m_scroll->setVerticalScrollBarPolicy(phone ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
    m_hint->setVisible(!phone);
  }

  void showMatch() {
    if (m_matches.isEmpty()) return;
    // keep the match a third down the view, so the links leaving it stay visible
    const int y = m_matches.value(m_at);
    m_scroll->verticalScrollBar()->setValue(qMax(0, y - m_scroll->viewport()->height() / 3));
  }
  Canvas* m_canvas;
  QLineEdit* m_search;
  QLabel* m_found;
  QScrollArea* m_scroll = nullptr;
  QVBoxLayout* m_root = nullptr;
  QHBoxLayout *m_head = nullptr, *m_legend = nullptr, *m_row2 = nullptr;
  QLabel* m_counts = nullptr;
  QWidget* m_hint = nullptr;
  bool m_phone = false;
  QList<int> m_matches;
  int m_at = 0;
};
}  // namespace srcmap

using DataMapPageImpl = srcmap::DataMapPage;
DW_PAGE("sources/sources-graph", DataMapPageImpl)
