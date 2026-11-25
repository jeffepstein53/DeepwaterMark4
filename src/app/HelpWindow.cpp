#include "app/HelpWindow.h"
#include "app/Pages.h"
#include "core/I18n.h"
#include "ui/Kit.h"
#include "ui/Theme.h"
#include "ui/Touch.h"
#include <QAbstractTextDocumentLayout>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>
#include <QUrl>
#include <QMainWindow>
#include <QVariantAnimation>
#include <QTimer>
#include <QRegularExpression>
#include <QShowEvent>
#include <functional>

namespace {
constexpr int kTitleH = 58;
constexpr int kLeftW = 264;
constexpr int kRightW = 252;
constexpr int kTextMaxW = 860;   // a readable line: ~110 characters of body text
QString htr(const char* s) { return QCoreApplication::translate("help", s); }

// Case and accent folding with one character out for one character in, so positions in the folded text are positions in
// the document ("echeance" finds "Échéance").
QString fold(const QString& s) {
  QString out(s.size(), QChar());
  for (int i = 0; i < s.size(); ++i) {
    QChar ch = s.at(i);
    if (ch.decompositionTag() == QChar::Canonical) { const QString d = ch.decomposition(); if (!d.isEmpty()) ch = d.at(0); }
    out[i] = ch.toCaseFolded();
  }
  return out;
}
}

namespace help {
// A painted list of rows: the chapters (left) or the sections of the chapter (right).
class NavList : public QWidget {
  Q_OBJECT
public:
  struct Item { QString text; int level; int key; QString badge; bool dim = false; };
  enum Kind { Chapters, Sections };
  NavList(Kind k, QWidget* parent) : QWidget(parent), m_kind(k) { setMouseTracking(true); }
  void setItems(const QList<Item>& items) { m_items = items; m_hover = -1; setFixedHeight(totalHeight()); update(); }
  void setCurrent(int key) { if (key != m_cur) { m_cur = key; update(); } }
  int current() const { return m_cur; }
  // top of the row of `key` (to keep it in view)
  int rowTop(int key) const { int y = 0; for (int i = 0; i < m_items.size(); ++i) { if (m_items[i].key == key) return y; y += rowH(i); } return -1; }
  int rowH(int i) const { return m_kind == Chapters ? 36 : m_items[i].level <= 2 ? 30 : 26; }
signals:
  void picked(int key);
protected:
  int totalHeight() const { int h = 0; for (int i = 0; i < m_items.size(); ++i) h += rowH(i); return h + 4; }
  int rowAt(qreal y) const { int top = 0; for (int i = 0; i < m_items.size(); ++i) { if (y >= top && y < top + rowH(i)) return i; top += rowH(i); } return -1; }
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto& c = theme::pal();
    int y = 0;
    for (int i = 0; i < m_items.size(); ++i) {
      const Item& it = m_items[i];
      const int h = rowH(i);
      const QRectF r(0, y, width(), h);
      const bool on = it.key == m_cur, hov = i == m_hover;
      if (on) { p.fillRect(r.adjusted(0, 1, 0, -1), m_kind == Chapters ? c.accentDim : theme::alpha(c.accentDim, 150)); p.fillRect(QRectF(0, y + 1, 2, h - 2), c.accent); }
      else if (hov) p.fillRect(r.adjusted(0, 1, 0, -1), c.panelAlt);
      qreal x = 14;
      qreal right = width() - 10;
      if (!it.badge.isEmpty()) {
        p.setFont(theme::mono(10, QFont::Medium, 0.5)); p.setPen(on ? c.accent : c.textMute);
        const qreal wb = QFontMetricsF(p.font()).horizontalAdvance(it.badge);
        p.drawText(QRectF(right - wb, y, wb, h), Qt::AlignVCenter | Qt::AlignRight, it.badge);
        right -= wb + 10;
      }
      if (m_kind == Chapters) {
        p.setFont(theme::mono(10, QFont::Medium, 1.0)); p.setPen(on ? theme::alpha(c.accent, 190) : c.textFaint);
        p.drawText(QRectF(x, y, 24, h), Qt::AlignVCenter | Qt::AlignLeft, QString::asprintf("%02d", i));
        x += 28;
        const QFont f = theme::mono(12, QFont::Bold, 1.6);
        p.setFont(f);
        p.setPen(it.dim ? c.textFaint : on ? c.accent : hov ? c.text : c.textDim);
        p.drawText(QRectF(x, y, right - x, h), Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetricsF(f).elidedText(it.text.toUpper(), Qt::ElideRight, right - x));
      } else {
        if (it.level >= 3) x += 14;
        const QFont f = it.level <= 2 ? theme::sans(13, QFont::DemiBold) : theme::sans(12);
        p.setFont(f);
        p.setPen(on ? c.accent : hov ? c.text : it.level <= 2 ? c.textDim : c.textMute);
        p.drawText(QRectF(x, y, right - x, h), Qt::AlignVCenter | Qt::AlignLeft, QFontMetricsF(f).elidedText(it.text, Qt::ElideRight, right - x));
      }
      y += h;
    }
  }
  void mouseMoveEvent(QMouseEvent* e) override {
    const int h = rowAt(e->position().y());
    if (h != m_hover) {
      m_hover = h; update();
      setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
      // elided titles: the whole title as a tooltip
      setToolTip(h >= 0 ? m_items[h].text : QString());
    }
  }
  void mousePressEvent(QMouseEvent* e) override { const int i = rowAt(e->position().y()); if (i >= 0) emit picked(m_items[i].key); }
  void leaveEvent(QEvent*) override { m_hover = -1; update(); }
private:
  Kind m_kind;
  QList<Item> m_items;
  int m_cur = -1, m_hover = -1;
};
}

namespace help {
// A side column: the chapters (search on top) or the sections. Beside the text on a wide window; on a narrow one, a
// panel pulled over the text from its edge (overlay: its own fill and a rule on its inner edge).
class Pane : public QWidget {
public:
  Pane(bool left, QWidget* parent) : QWidget(parent), m_left(left) {}
  QString label, footer;
  int labelY = 0;
  bool overlay = false;
protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    const auto& c = theme::pal();
    if (overlay) {
      p.fillRect(rect(), theme::map("#0b0804"));
      p.fillRect(m_left ? QRect(width() - 1, 0, 1, height()) : QRect(0, 0, 1, height()), c.borderHi);
    }
    p.setFont(theme::microLabel()); p.setPen(c.textMute);
    p.drawText(QPointF(22, labelY), label);
    if (!footer.isEmpty()) {
      p.setFont(theme::mono(10, QFont::Normal, 0.8)); p.setPen(c.textFaint);
      p.drawText(QRectF(22, height() - 50, width() - 40, 40), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, footer);
    }
  }
  void mousePressEvent(QMouseEvent* e) override { e->accept(); }   // a tap on its empty part does not reach the text
private:
  bool m_left;
};
// Dims the text while a panel is pulled over it; a tap on it closes the panel.
class Scrim : public QWidget {
public:
  using QWidget::QWidget;
  std::function<void()> onTap;
protected:
  void paintEvent(QPaintEvent*) override { QPainter p(this); p.fillRect(rect(), QColor(0, 0, 0, 120)); }
  void mousePressEvent(QMouseEvent*) override { if (onTap) onTap(); }
};
}

HelpWindow::HelpWindow(QWidget* mainWindow) : QDialog(mainWindow), m_main(mainWindow) {
  setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);
  setModal(false);
  setMouseTracking(true);

  m_text = new QTextBrowser(this);
  m_scrim = new help::Scrim(this);
  m_scrim->hide();
  m_scrim->onTap = [this] { openPane(0); };
  m_leftPane = new help::Pane(true, this);
  m_rightPane = new help::Pane(false, this);
  m_search = new QLineEdit(m_leftPane);
  m_search->setClearButtonEnabled(true);
  m_search->installEventFilter(this);
  m_count = new QLabel(m_leftPane);
  m_chapterScroll = new QScrollArea(m_leftPane);
  m_tocScroll = new QScrollArea(m_rightPane);
  m_paneAnim = new QVariantAnimation(this);
  m_paneAnim->setDuration(180);
  m_paneAnim->setEasingCurve(QEasingCurve::OutCubic);
  connect(m_paneAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { placePanes(v.toDouble()); });
  connect(m_paneAnim, &QVariantAnimation::finished, this, [this] { if (m_paneT == 0) { m_leftPaneOpen = m_rightPaneOpen = false; placePanes(0); } });
  m_chapterList = new help::NavList(help::NavList::Chapters, m_chapterScroll);
  m_toc = new help::NavList(help::NavList::Sections, m_tocScroll);
  for (QScrollArea* s : {m_chapterScroll, m_tocScroll}) {
    s->setFrameShape(QFrame::NoFrame);
    s->setWidgetResizable(true);
    s->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    s->setStyleSheet("QScrollArea { background: transparent; }");
    s->viewport()->setAutoFillBackground(false);
  }
  m_chapterScroll->setWidget(m_chapterList);
  m_tocScroll->setWidget(m_toc);
  // setWidget turns the content's background filling on: the lists are drawn on the window's own fill
  m_chapterList->setAutoFillBackground(false);
  m_toc->setAutoFillBackground(false);
  m_text->setOpenLinks(false);
  m_text->setFrameShape(QFrame::NoFrame);
  m_text->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_text->viewport()->setAutoFillBackground(false);
  m_text->setStyleSheet("QTextBrowser { background: transparent; border: 0; }");

  // a pulled-out panel closes once its choice is made
  connect(m_chapterList, &help::NavList::picked, this, [this](int i) { if (i >= 0 && i < m_chapters.size()) showChapter(m_chapters[i].id); openPane(0); });
  connect(m_toc, &help::NavList::picked, this, [this](int pos) { scrollToPos(pos); openPane(0); });
  connect(m_text->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { syncTocToScroll(); });
  connect(m_text, &QTextBrowser::anchorClicked, this, [this](const QUrl& u) {
    if (u.scheme() == "help") showChapter(u.path().isEmpty() ? u.host() : u.path(), u.fragment(QUrl::FullyDecoded));
    else if (u.scheme() == "http" || u.scheme() == "https") QDesktopServices::openUrl(u);
  });
  connect(m_search, &QLineEdit::textChanged, this, [this] { runSearch(); });
  connect(&i18n::Notifier::instance(), &i18n::Notifier::changed, this, [this] {
    const QString cur = m_current;
    load();
    showChapter(cur);
  });
  connect(&theme::Notifier::instance(), &theme::Notifier::changed, this, [this] { restyle(); update(); });
  if (m_main) m_main->installEventFilter(this);
  load();
  fitToMain();
}

int HelpWindow::chapterIndex(const QString& id) const {
  for (int i = 0; i < m_chapters.size(); ++i) if (m_chapters[i].id == id) return i;
  return -1;
}

void HelpWindow::load() {
  // the view lets go of the document it shows before the documents go (a language change while open)
  m_text->setExtraSelections({});
  m_text->setDocument(nullptr);
  for (auto& ch : m_chapters) delete ch.doc;
  m_chapters.clear();
  QStringList ids{"start"};
  for (const auto& t : app::tabs()) ids << t.id;
  ids << "claude";   // the last chapter belongs to no tab: Options > Claude, what Claude can read (0.4.3)
  const QString lang = i18n::code();
  for (const QString& id : ids) {
    QFile f(QString(":/help/%1/%2.md").arg(lang, id));
    if (!f.exists()) f.setFileName(QString(":/help/en/%1.md").arg(id));
    if (!f.open(QIODevice::ReadOnly)) continue;
    QString md = QString::fromUtf8(f.readAll());
    // the chapters hold no HTML, but placeholders such as <country> do: the importer would read them as tags and lose
    // the rest of the chapter, so every "<" that could open a tag is escaped
    md.replace(QRegularExpression("(?<!\\\\)<(?=[A-Za-z/!?])"), "\\<");
    Chapter ch;
    ch.id = id;
    ch.title = md.section('\n', 0, 0).remove(QRegularExpression("^#+\\s*")).trimmed();
    ch.doc = new QTextDocument(this);
    ch.doc->setMarkdown(md, QTextDocument::MarkdownDialectGitHub);
    for (QTextBlock b = ch.doc->begin(); b.isValid(); b = b.next()) {
      const int lvl = b.blockFormat().headingLevel();
      if (lvl == 2 || lvl == 3) ch.headings << Heading{b.text().trimmed(), lvl, b.position()};
    }
    ch.folded = fold(ch.doc->toRawText());
    m_chapters << ch;
  }
  m_search->setPlaceholderText(htr("Search the documentation"));
  restyle();
  fillChapterList();
}

void HelpWindow::restyle() {
  const auto& c = theme::pal();
  for (auto& ch : m_chapters) style(ch.doc);
  m_search->setStyleSheet(QString("QLineEdit { background: %1; border: 1px solid %2; border-radius: 3px; color: %3; padding: 6px 8px;"
                                  " font-size: 13px; } QLineEdit:focus { border-color: %4; }")
                              .arg(c.panelAlt.name(), c.border.name(), c.text.name(), c.accent.name()));
  m_count->setFont(theme::mono(10, QFont::Medium, 1.0));
  m_count->setStyleSheet(QString("color: %1; background: transparent;").arg(c.textMute.name()));
  if (m_text->document() && chapterIndex(m_current) >= 0) showMatches();
}

void HelpWindow::style(QTextDocument* doc) {
  const auto& c = theme::pal();
  const QColor body = theme::mix(c.textDim, c.text, 0.45);
  // the style sheet gives widgets a pixel font: the document's must be in pixels too (a point size would be -1)
  doc->setDefaultFont(theme::sans(14));
  doc->setDocumentMargin(2);
  QTextCursor cur(doc);
  cur.beginEditBlock();
  for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
    const int lvl = b.blockFormat().headingLevel();
    cur.setPosition(b.position());
    const bool inTable = cur.currentTable() != nullptr;
    QTextBlockFormat bf;
    if (lvl == 1) { bf.setTopMargin(0); bf.setBottomMargin(16); }
    else if (lvl == 2) { bf.setTopMargin(34); bf.setBottomMargin(10); }
    else if (lvl == 3) { bf.setTopMargin(20); bf.setBottomMargin(6); }
    else if (inTable) { bf.setTopMargin(0); bf.setBottomMargin(0); bf.setLineHeight(125, QTextBlockFormat::ProportionalHeight); }
    else { bf.setTopMargin(0); bf.setBottomMargin(b.textList() ? 4 : 9); bf.setLineHeight(145, QTextBlockFormat::ProportionalHeight); }
    cur.mergeBlockFormat(bf);
    // fragments first, formats after: setting a format can merge fragments under an iterator
    struct Frag { int pos, len; QTextCharFormat f; };
    QList<Frag> frags;
    for (auto it = b.begin(); !it.atEnd(); ++it) frags << Frag{it.fragment().position(), it.fragment().length(), it.fragment().charFormat()};
    for (const Frag& fr : frags) {
      QTextCharFormat f = fr.f;
      f.clearProperty(QTextFormat::FontSizeAdjustment);
      const bool code = f.fontFixedPitch() || f.font().styleHint() == QFont::Monospace;
      if (lvl == 1) { f.setFont(theme::mono(24, QFont::ExtraBold, 3.0)); f.setForeground(c.accent); }
      else if (lvl == 2) { f.setFont(theme::mono(17, QFont::Bold, 1.6)); f.setForeground(c.accentText); }
      else if (lvl == 3) { f.setFont(theme::sans(15, QFont::Bold)); f.setForeground(c.text); }
      else if (f.isAnchor()) { f.setFontUnderline(false); f.setForeground(c.accent); }
      else if (code) { f.setFont(theme::mono(13)); f.setForeground(c.accentText); f.setBackground(c.panelAlt); }
      else {
        const bool bold = f.fontWeight() >= QFont::DemiBold;
        const bool italic = f.fontItalic();
        f.setFont(theme::sans(inTable ? 13 : 14, bold ? QFont::DemiBold : QFont::Normal));
        f.setFontItalic(italic);
        f.setForeground(bold ? c.text : italic ? c.textDim : body);
      }
      cur.setPosition(fr.pos);
      cur.setPosition(fr.pos + fr.len, QTextCursor::KeepAnchor);
      cur.setCharFormat(f);
    }
  }
  // tables: thin collapsed borders, a filled header row
  std::function<void(QTextFrame*)> walk = [&](QTextFrame* fr) {
    for (QTextFrame* child : fr->childFrames()) {
      if (auto* t = qobject_cast<QTextTable*>(child)) {
        QTextTableFormat tf = t->format();
        tf.setBorder(1); tf.setBorderBrush(c.border); tf.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
        tf.setBorderCollapse(true); tf.setCellPadding(7); tf.setCellSpacing(0);
        tf.setTopMargin(4); tf.setBottomMargin(14);
        t->setFormat(tf);
        const int head = qMax(1, tf.headerRowCount());
        for (int r = 0; r < t->rows(); ++r)
          for (int col = 0; col < t->columns(); ++col) {
            QTextTableCell cell = t->cellAt(r, col);
            QTextTableCellFormat cf = cell.format().toTableCellFormat();
            cf.setBackground(r < head ? QBrush(c.panelAlt) : QBrush(Qt::NoBrush));
            cell.setFormat(cf);
            if (r < head) {
              QTextCursor cc = cell.firstCursorPosition();
              cc.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
              QTextCharFormat hf; hf.setFont(theme::mono(11, QFont::Bold, 1.2)); hf.setForeground(c.accentText);
              cc.mergeCharFormat(hf);
            }
          }
      }
      walk(child);
    }
  };
  walk(doc->rootFrame());
  cur.endEditBlock();
}

void HelpWindow::fillChapterList() {
  QList<help::NavList::Item> items;
  const bool searching = m_query.size() >= 2;
  for (int i = 0; i < m_chapters.size(); ++i) {
    int n = 0;
    if (searching) for (int at = m_chapters[i].folded.indexOf(m_query); at >= 0; at = m_chapters[i].folded.indexOf(m_query, at + m_query.size())) ++n;
    items << help::NavList::Item{m_chapters[i].title, 1, i, searching && n ? QString::number(n) : QString(), searching && !n};
  }
  m_chapterList->setItems(items);
  m_chapterList->setCurrent(chapterIndex(m_current));
}

void HelpWindow::fillToc() {
  QList<help::NavList::Item> items;
  const int i = chapterIndex(m_current);
  if (i >= 0) for (const Heading& h : m_chapters[i].headings) items << help::NavList::Item{h.title, h.level, h.pos, {}};
  m_toc->setItems(items);
  syncTocToScroll();
}

void HelpWindow::showChapter(const QString& chapter, const QString& section) {
  int i = chapterIndex(chapter);
  if (i < 0) i = chapterIndex("start");
  if (i < 0) return;
  if (m_chapters[i].id != m_current || m_text->document() != m_chapters[i].doc) {
    m_current = m_chapters[i].id;
    m_text->setDocument(m_chapters[i].doc);
    m_text->verticalScrollBar()->setValue(0);
    m_chapterList->setCurrent(i);
    fillToc();
    m_match = -1;
    showMatches();
    update();
  }
  if (!section.isEmpty()) {
    const QString want = fold(section);
    // after the event loop has laid the window out (a first opening sizes the text only when shown)
    for (const Heading& h : m_chapters[i].headings)
      if (fold(h.title) == want) {
        m_pendingPos = h.pos;
        QTimer::singleShot(0, this, [this] { if (m_pendingPos >= 0) scrollToPos(m_pendingPos); m_pendingPos = -1; });
        break;
      }
  }
}

void HelpWindow::scrollToPos(int pos) {
  QTextDocument* doc = m_text->document();
  const QTextBlock b = doc->findBlock(pos);
  if (!b.isValid()) return;
  // the block's rect starts at its text (its top margin lies above): the heading lands 6 px below the view's edge
  const qreal top = doc->documentLayout()->blockBoundingRect(b).top() - 6;
  m_text->verticalScrollBar()->setValue(int(top));
  m_toc->setCurrent(pos);
}

void HelpWindow::syncTocToScroll() {
  const int i = chapterIndex(m_current);
  if (i < 0) return;
  QTextDocument* doc = m_text->document();
  QScrollBar* sb = m_text->verticalScrollBar();
  // the section being read: the last heading above a line a little below the top (at the very end: the last heading
  // in the upper half of the view, so a short last section can still light up)
  const qreal line = sb->value() >= sb->maximum() && sb->maximum() > 0 ? sb->value() + m_text->viewport()->height() * 0.5
                                                                       : sb->value() + 40;
  int key = m_chapters[i].headings.isEmpty() ? -1 : m_chapters[i].headings.first().pos;
  for (const Heading& h : m_chapters[i].headings) {
    const QTextBlock b = doc->findBlock(h.pos);
    if (doc->documentLayout()->blockBoundingRect(b).top() <= line) key = h.pos;
    else break;
  }
  m_toc->setCurrent(key);
  const int y = m_toc->rowTop(key);
  if (y >= 0) m_tocScroll->ensureVisible(0, y, 0, 60);
}

void HelpWindow::runSearch() {
  m_query = fold(m_search->text().trimmed());
  fillChapterList();
  m_match = -1;
  showMatches();
  if (!m_matches.isEmpty()) stepMatch(1);
  else if (m_query.size() >= 2) {   // nothing here: the first chapter that has it
    for (int k = 0; k < m_chapters.size(); ++k)
      if (m_chapters[k].folded.contains(m_query)) { showChapter(m_chapters[k].id); stepMatch(1); break; }
  }
}

void HelpWindow::showMatches() {
  m_matches.clear();
  const int i = chapterIndex(m_current);
  QList<QTextEdit::ExtraSelection> sel;
  if (i >= 0 && m_query.size() >= 2) {
    const auto& c = theme::pal();
    const QString& txt = m_chapters[i].folded;
    for (int at = txt.indexOf(m_query); at >= 0; at = txt.indexOf(m_query, at + m_query.size())) m_matches << at;
    for (int k = 0; k < m_matches.size(); ++k) {
      QTextEdit::ExtraSelection s;
      s.cursor = QTextCursor(m_chapters[i].doc);
      s.cursor.setPosition(m_matches[k]);
      s.cursor.setPosition(m_matches[k] + m_query.size(), QTextCursor::KeepAnchor);
      s.format.setBackground(k == m_match ? c.accent : theme::alpha(c.accent, 70));
      if (k == m_match) s.format.setForeground(theme::onAccent());
      sel << s;
    }
  }
  m_text->setExtraSelections(sel);
  int total = 0;
  if (m_query.size() >= 2) for (const auto& ch : m_chapters) total += ch.folded.count(m_query);
  m_count->setText(m_query.size() < 2 ? QString()
                   : total == 0       ? htr("NO MATCH")
                   : m_match >= 0     ? QString("%1 / %2  ·  %3 %4").arg(m_match + 1).arg(m_matches.size()).arg(total).arg(htr("IN ALL"))
                                      : QString("%1 %2").arg(total).arg(htr("IN ALL")));
}

void HelpWindow::stepMatch(int dir) {
  if (m_query.size() < 2) return;
  int next = m_match + dir;
  if (m_match < 0) next = dir > 0 ? 0 : m_matches.size() - 1;
  if (next < 0 || next >= m_matches.size()) {
    // on to the next (or previous) chapter that has the query, round the list
    const int i = chapterIndex(m_current);
    for (int k = 1; k <= m_chapters.size(); ++k) {
      const int j = ((i + dir * k) % m_chapters.size() + m_chapters.size()) % m_chapters.size();
      if (!m_chapters[j].folded.contains(m_query)) continue;
      showChapter(m_chapters[j].id);
      next = dir > 0 ? 0 : m_matches.size() - 1;
      break;
    }
  }
  if (next < 0 || next >= m_matches.size()) return;
  m_match = next;
  showMatches();
  // the match a third down the view
  QTextDocument* doc = m_text->document();
  const QTextBlock b = doc->findBlock(m_matches[m_match]);
  const QTextLine line = b.layout()->lineForTextPosition(m_matches[m_match] - b.position());
  const qreal y = doc->documentLayout()->blockBoundingRect(b).top() + (line.isValid() ? line.y() : 0);
  m_text->verticalScrollBar()->setValue(int(y - m_text->viewport()->height() / 3.0));
  // the section that holds the match (the scroll lit the one at the top of the view)
  int key = -1;
  for (const Heading& h : m_chapters[chapterIndex(m_current)].headings) { if (h.pos <= m_matches[m_match]) key = h.pos; else break; }
  if (key >= 0) { m_toc->setCurrent(key); m_tocScroll->ensureVisible(0, m_toc->rowTop(key), 0, 60); }
}

void HelpWindow::fitToMain() {
  if (!m_main) return;
  // the main window's content, in screen coordinates: its central widget, which keeps clear of a phone's status bar
  // and home bar (a top-level window there spans the whole screen, the safe area is left to its layout)
  QRect g = m_main->geometry();
  if (auto* mw = qobject_cast<QMainWindow*>(m_main.data()); mw && mw->centralWidget())
    g = QRect(mw->centralWidget()->mapToGlobal(QPoint(0, 0)), mw->centralWidget()->size());
  const int m = ui::compact(g.width()) ? 0 : 28;
  setGeometry(g.adjusted(m, m, -m, -m));
}

// Wide: chapters | text | sections. Narrower than 980: the sections become a panel pulled from the right; on a phone
// (ui::compact) the chapters too, from the left, each opened by its button in the title row.
bool HelpWindow::leftIsPanel() const { return ui::compact(width()); }
bool HelpWindow::rightIsPanel() const { return width() < 980; }
int HelpWindow::panelWidth() const { return std::min(300, width() - 56); }

void HelpWindow::relayout() {
  const bool lp = leftIsPanel(), rp = rightIsPanel();
  const int H = height() - kTitleH - 1;
  // the panes' insides, whatever their place
  const int lw = lp ? panelWidth() : kLeftW, rw = rp ? panelWidth() : kRightW;
  m_leftPane->resize(lw, H);
  m_leftPane->labelY = 90;
  m_leftPane->label = htr("CHAPTERS");
  m_leftPane->footer = lp ? QString() : htr("F1 opens the page shown · Enter: next match · Esc closes");
  m_search->setGeometry(20, 16, lw - 36, 32);
  m_count->setGeometry(22, 52, lw - 40, 16);
  m_chapterScroll->setGeometry(6, 98, lw - 12, H - 98 - (lp ? 12 : 52));
  m_rightPane->resize(rw, H);
  m_rightPane->labelY = 34;
  m_rightPane->label = htr("IN THIS CHAPTER");
  m_tocScroll->setGeometry(6, 48, rw - 16, H - 48 - 16);
  m_leftPane->overlay = lp; m_rightPane->overlay = rp;
  if (!lp) m_leftPaneOpen = false;
  if (!rp) m_rightPaneOpen = false;
  placePanes(m_paneT);
  // the text: between the columns that are columns
  const int pad = ui::compact(width()) ? 16 : 36;
  // x1: where the text (and its 12 px scroll bar) ends; a phone keeps the same 16 px margin on both sides
  const int x0 = (lp ? 0 : kLeftW) + pad, x1 = width() - (rp ? 0 : kRightW) - (ui::compact(width()) ? 16 + 12 : 20);
  const int w = qMin(kTextMaxW, x1 - x0);
  m_text->setGeometry(x0 + (x1 - x0 - w) / 2, kTitleH + (ui::compact(width()) ? 10 : 22), w + 12,
                      height() - kTitleH - (ui::compact(width()) ? 10 : 22) - 12);   // +12: the scroll bar
  m_scrim->setGeometry(0, kTitleH + 1, width(), H);
  update();
}

// t: 0 closed … 1 open, for the panel that is opening or closing (the columns stay in place)
void HelpWindow::placePanes(double t) {
  m_paneT = t;
  const int y = kTitleH + 1;
  if (leftIsPanel()) {
    const int w = m_leftPane->width();
    m_leftPane->move(m_leftPaneOpen ? int(-w + w * t) : -w, y);
    m_leftPane->setVisible(m_leftPaneOpen && t > 0);
  } else { m_leftPane->move(0, y); m_leftPane->show(); }
  if (rightIsPanel()) {
    const int w = m_rightPane->width();
    m_rightPane->move(m_rightPaneOpen ? int(width() - w * t) : width(), y);
    m_rightPane->setVisible(m_rightPaneOpen && t > 0);
  } else { m_rightPane->move(width() - m_rightPane->width(), y); m_rightPane->show(); }
  m_scrim->setVisible((m_leftPaneOpen || m_rightPaneOpen) && t > 0);
  m_scrim->raise(); m_leftPane->raise(); m_rightPane->raise();
}

// 1: the chapters, 2: the sections, 0: close the one open
void HelpWindow::openPane(int which) {
  if (which == 1 && !leftIsPanel()) return;
  if (which == 2 && !rightIsPanel()) return;
  const bool wasOpen = m_leftPaneOpen || m_rightPaneOpen;
  if (which == 0) {
    if (!wasOpen) return;
    m_paneAnim->stop(); m_paneAnim->setStartValue(m_paneT); m_paneAnim->setEndValue(0.0); m_paneAnim->start();
    return;
  }
  m_paneAnim->stop();
  m_leftPaneOpen = which == 1; m_rightPaneOpen = which == 2;
  if (which == 2) syncTocToScroll();
  m_paneAnim->setStartValue(wasOpen ? m_paneT : 0.0); m_paneAnim->setEndValue(1.0); m_paneAnim->start();
}

QRectF HelpWindow::chaptersButton() const { return leftIsPanel() ? QRectF(10, 13, 34, 32) : QRectF(); }
QRectF HelpWindow::sectionsButton() const { return rightIsPanel() ? QRectF(closeRect().left() - 42, 13, 34, 32) : QRectF(); }

void HelpWindow::resizeEvent(QResizeEvent*) {
  // the text re-wraps at the new width: the block at the top of the view (or a section asked for) stays there
  const int keep = m_pendingPos >= 0 ? m_pendingPos : m_text->cursorForPosition(QPoint(4, 14)).block().position();   // 14: inside the first line, below a heading's margin
  const bool atTop = m_pendingPos < 0 && m_text->verticalScrollBar()->value() == 0;
  relayout();
  if (!atTop) scrollToPos(keep);
  syncTocToScroll();
}
void HelpWindow::showEvent(QShowEvent* e) { fitToMain(); QDialog::showEvent(e); }

QRectF HelpWindow::closeRect() const { return ui::compact(width()) ? QRectF(width() - 46, 11, 36, 36) : QRectF(width() - 44, 16, 26, 26); }

void HelpWindow::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const auto& c = theme::pal();
  const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
  p.setPen(QPen(c.borderHi, 1)); p.setBrush(theme::map("#0b0804"));
  p.drawRoundedRect(box, 4, 4);
  ui::drawBrackets(p, QRectF(rect()).adjusted(4, 4, -4, -4), c.accent, 12);

  // title: ● DOCUMENTATION  ›  CHAPTER (a phone: the chapter's name only, between the two panel buttons)
  const int i = chapterIndex(m_current);
  const QRectF cb = chaptersButton(), sb = sectionsButton();
  auto drawButton = [&](const QRectF& r, bool open, bool lines) {
    if (r.isNull()) return;
    if (open) { p.setPen(Qt::NoPen); p.setBrush(c.accentDim); p.drawRoundedRect(r, 3, 3); }
    p.setPen(QPen(open ? c.accent : c.textDim, 1.8, Qt::SolidLine, Qt::RoundCap));
    const QPointF m = r.center();
    if (lines) for (int k = -1; k <= 1; ++k) p.drawLine(m + QPointF(-8, k * 5.5), m + QPointF(8, k * 5.5));   // ☰ chapters
    else for (int k = -1; k <= 1; ++k) {                                                                         // sections: a list
      p.drawPoint(m + QPointF(-7, k * 5.5));
      p.drawLine(m + QPointF(-3, k * 5.5), m + QPointF(k == 0 ? 6 : 8, k * 5.5));
    }
  };
  drawButton(cb, m_leftPaneOpen, true);
  drawButton(sb, m_rightPaneOpen, false);
  if (ui::compact(width())) {
    if (i >= 0) {
      const qreal x = cb.right() + 10, room = (sb.isNull() ? closeRect().left() : sb.left()) - 10 - x;
      p.setFont(theme::mono(13, QFont::Bold, 2.0)); p.setPen(c.accentText);
      p.drawText(QRectF(x, 0, room, kTitleH), Qt::AlignVCenter | Qt::AlignLeft,
                 QFontMetricsF(p.font()).elidedText(m_chapters[i].title.toUpper(), Qt::ElideRight, room));
    }
  } else {
    const qreal x0 = cb.isNull() ? 26 : cb.right() + 10;
    p.setPen(Qt::NoPen); p.setBrush(c.accent); p.drawEllipse(QPointF(x0 + 3, 31), 3, 3);
    const QFont tf = theme::mono(13, QFont::Bold, 3.0);
    p.setFont(tf); p.setPen(c.accentText);
    const QString title = htr("DOCUMENTATION");
    p.drawText(QPointF(x0 + 14, 36), title);
    if (i >= 0) {
      qreal x = x0 + 14 + QFontMetricsF(tf).horizontalAdvance(title) + 14;
      p.setPen(c.textMute); p.drawText(QPointF(x, 36), "›");
      x += 22;
      p.setFont(theme::mono(12, QFont::Bold, 2.0)); p.setPen(c.textDim);
      const qreal right = (sb.isNull() ? closeRect().left() : sb.left()) - 12;
      p.drawText(QPointF(x, 36), QFontMetricsF(p.font()).elidedText(m_chapters[i].title.toUpper(), Qt::ElideRight, right - x));
    }
  }
  const QRectF xr = closeRect();
  p.setPen(QPen(m_hoverClose ? c.text : c.textDim, 1.6, Qt::SolidLine, Qt::RoundCap));
  p.drawLine(xr.center() + QPointF(-6, -6), xr.center() + QPointF(6, 6));
  p.drawLine(xr.center() + QPointF(6, -6), xr.center() + QPointF(-6, 6));
  p.fillRect(QRectF(16, kTitleH, width() - 32, 1), c.divider);

  // the rules beside the columns (a pulled panel draws its own edge)
  if (!leftIsPanel()) p.fillRect(QRectF(kLeftW, kTitleH + 1, 1, height() - kTitleH - 16), c.divider);
  if (!rightIsPanel()) p.fillRect(QRectF(width() - kRightW, kTitleH + 1, 1, height() - kTitleH - 16), c.divider);
}

void HelpWindow::mousePressEvent(QMouseEvent* e) {
  if (closeRect().contains(e->position())) { hide(); return; }
  if (chaptersButton().contains(e->position())) { openPane(m_leftPaneOpen ? 0 : 1); update(); return; }
  if (sectionsButton().contains(e->position())) { openPane(m_rightPaneOpen ? 0 : 2); update(); return; }
  if (ui::touch::enabled()) return;   // a phone: the window fills the screen, nothing to drag
  if (e->position().y() < kTitleH) { m_dragging = true; m_dragFrom = e->globalPosition(); }
}
void HelpWindow::mouseMoveEvent(QMouseEvent* e) {
  if (m_dragging) { move(pos() + (e->globalPosition() - m_dragFrom).toPoint()); m_dragFrom = e->globalPosition(); return; }
  const bool h = closeRect().contains(e->position());
  if (h != m_hoverClose) { m_hoverClose = h; setCursor(h ? Qt::PointingHandCursor : Qt::ArrowCursor); update(closeRect().adjusted(-4, -4, 4, 4).toAlignedRect()); }
}
void HelpWindow::mouseReleaseEvent(QMouseEvent*) { m_dragging = false; }
void HelpWindow::leaveEvent(QEvent*) { if (m_hoverClose) { m_hoverClose = false; update(); } }

void HelpWindow::keyPressEvent(QKeyEvent* e) {
  if (e->matches(QKeySequence::Find)) { m_search->setFocus(); m_search->selectAll(); return; }
  if (e->key() == Qt::Key_Escape) { if (m_leftPaneOpen || m_rightPaneOpen) openPane(0); else hide(); return; }
  QDialog::keyPressEvent(e);
}

bool HelpWindow::eventFilter(QObject* o, QEvent* e) {
  if (o == m_main && (e->type() == QEvent::Move || e->type() == QEvent::Resize) && isVisible()) fitToMain();
  if (o == m_search && e->type() == QEvent::KeyPress) {
    auto* k = static_cast<QKeyEvent*>(e);
    if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) { stepMatch(k->modifiers() & Qt::ShiftModifier ? -1 : 1); return true; }
    if (k->key() == Qt::Key_Escape && !m_search->text().isEmpty()) { m_search->clear(); return true; }
    if (k->key() == Qt::Key_Down) { stepMatch(1); return true; }
    if (k->key() == Qt::Key_Up) { stepMatch(-1); return true; }
  }
  return QDialog::eventFilter(o, e);
}

#include "HelpWindow.moc"
