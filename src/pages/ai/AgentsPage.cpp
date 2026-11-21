// AI › Agents: the downloaded local models (and the Claude Code integration of AI › LLM › Integration) on the left; four isolated environments drawn as cubes on the right.
// Drag a model onto a cube to load it there: the cube lights up from its core. Drag a loaded cube onto another to
// move its model; EJECT turns the cube off. Assignments persist (ai::Slots).
// Each lit cube runs its model in its own sandboxed process (ai::Engine); CHAT opens the conversation of that environment.
// Test hooks: DW_AI_SLOTS="i:owner/repo/file;…" (load before showing), DW_AI_HOVER=<slot> (drop-target highlight),
// DW_AI_LIGHT=<0..1> (freeze every lit cube at that intensity, for screenshots), DW_AI_CHAT=<slot> (open its chat),
// DW_AI_ASK=<question> (with DW_AI_CHAT: send it once the page is shown), DW_AI_SKILLS="i:skill,…;…" (tick boxes),
// DW_AI_TOOLTEST=<file> (run every data tool once, results written to the file), DW_AI_UNTICK_ON_CALL=1,
// DW_AI_EVALDLG=<slot> (open that environment's EVALUATION dialog).
// Phones (pages/ai/PhoneScroll.h, 2026-11-21): one column — the models, then the four cubes one under the other — and
// the page scrolls; the chat fills the screen, its activity panel (or its WEB nodes) below it. Sideways (short): the
// desktop rows at a fixed height, scrolling. Models are loaded by dragging (a mouse) or from AI › LLM › Integration's
// LOAD buttons (Claude Code); on iOS neither a local model nor Claude Code can run (no child process): the cube's chat
// shows the engine's failure.
#include "app/Pages.h"
#include "ai/ModelHub.h"
#include "ai/Slots.h"
#include "ai/Engine.h"
#include "ai/Tools.h"
#include "ai/ClaudeCode.h"
#include "ai/Evaluation.h"
#include "technicaldata/TechnicalDataStore.h"
#include <QFile>
#include <QRegularExpression>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <memory>
#include "pages/ai/ChatView.h"
#include "pages/ai/PhoneScroll.h"
#include "pages/ai/SkillBox.h"
#include "pages/ai/VoxelCube.h"
#include "ui/Kit.h"
#include "ui/Theme.h"
#include <QCoreApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QScreen>
#include <QPushButton>
#include <QLabel>
#include <QPainter>
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QTimer>
#include <QElapsedTimer>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QFileInfo>
#include <QStackedWidget>
#include <QCheckBox>
#include <QRandomGenerator>
#include <QComboBox>
#include <QDateEdit>
#include <QFormLayout>
#include <QSpinBox>
#include <QAbstractSpinBox>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTimeZone>
#include <functional>
#include <cmath>

namespace aipage {
namespace {
// the desktop layout (models | four cubes) needs 1029 px (layout audit 2026-11-21): narrower windows take the phone one
constexpr int kAgentsDesktop = 1040;

const char* kMime = "application/x-deepwater-model";

QString bytes(qint64 b) {
  if (b <= 0) return QStringLiteral("—");
  const double v = double(b);
  if (v >= 1e9) return QString::number(v / 1e9, 'f', 2) + " GB";
  if (v >= 1e6) return QString::number(v / 1e6, 'f', 1) + " MB";
  return QString::number(v / 1e3, 'f', 0) + " KB";
}
QString quantOf(const QString& file) { ai::HubFile f; f.path = file; return f.quant(); }
qint64 sizeOf(const QString& repo, const QString& file) {
  for (const auto& m : ai::ModelHub::instance().installed()) if (m.repo == repo && m.file == file) return m.size;
  return 0;
}
QString displayName(const QString& repo, const QString& file) { ai::Slot s; s.repo = repo; s.file = file; return s.displayName(); }
QMimeData* mimeFor(const QString& repo, const QString& file, int fromSlot) {
  auto* m = new QMimeData;
  m->setData(kMime, (repo + "\n" + file + "\n" + QString::number(fromSlot)).toUtf8());
  return m;
}
bool readMime(const QMimeData* m, QString* repo, QString* file, int* fromSlot) {
  if (!m->hasFormat(kMime)) return false;
  const QStringList p = QString::fromUtf8(m->data(kMime)).split('\n');
  if (p.size() < 3) return false;
  *repo = p[0]; *file = p[1]; *fromSlot = p[2].toInt();
  return true;
}
QPixmap dragChip(const QString& text, qreal dpr) {
  const auto& c = theme::pal();
  const QFont f = theme::mono(12, QFont::Bold, 0.5);
  const int w = QFontMetrics(f).horizontalAdvance(text) + 34;
  QPixmap pm(QSize(w, 30) * dpr); pm.setDevicePixelRatio(dpr); pm.fill(Qt::transparent);
  QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(c.accent, 1)); p.setBrush(theme::alpha(c.panelAlt, 235)); p.drawRoundedRect(QRectF(0.5, 0.5, w - 1, 29), 3, 3);
  p.setPen(Qt::NoPen); p.setBrush(c.up); p.drawEllipse(QPointF(14, 15), 4, 4);
  p.setFont(f); p.setPen(c.text); p.drawText(QRectF(24, 0, w - 24, 30), Qt::AlignVCenter, text);
  return pm;
}

// A wrapping label that toggles a check box when tapped: on a phone, a check box's own text cannot wrap and would hold
// the dialog wider than the screen.
class TapLabel : public QLabel {
public:
  TapLabel(const QString& text, QAbstractButton* box, QWidget* parent) : QLabel(text, parent), m_box(box) { setWordWrap(true); }
protected:
  void mouseReleaseEvent(QMouseEvent* e) override { if (m_box->isEnabled() && rect().contains(e->position().toPoint())) m_box->toggle(); }
private:
  QAbstractButton* m_box;
};

// ---------------------------------------------------------------------------------------------- model list
class ModelList : public QWidget {
public:
  explicit ModelList(QWidget* p) : QWidget(p) { setMouseTracking(true); rebuild(); }
  void rebuild() {
    m_models = ai::ModelHub::instance().installed();
    ai::LocalModel cc; cc.repo = ai::claudecode::kRepo; cc.file = ai::claudecode::kFile;   // integration, after the files
    m_models << cc;
    setMinimumHeight(int(m_models.size()) * kRow + 4);
    update();
  }
  QSize sizeHint() const override { return QSize(300, int(m_models.size()) * kRow + 4); }
protected:
  static constexpr int kRow = 60;
  void paintEvent(QPaintEvent*) override {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    const auto& c = theme::pal();
    for (int i = 0; i < m_models.size(); ++i) {
      const auto& m = m_models[i];
      const QRectF r(0, i * kRow + 2, width(), kRow - 6);
      p.setPen(QPen(i == m_hover ? c.borderHi : c.border, 1));
      p.setBrush(i == m_hover ? c.panelAlt : c.panel);
      p.drawRoundedRect(r.adjusted(.5, .5, -.5, -.5), 3, 3);
      // grip dots: "drag me"
      p.setPen(Qt::NoPen); p.setBrush(c.textMute);
      for (int k = 0; k < 3; ++k) for (int j = 0; j < 2; ++j) p.drawEllipse(QPointF(r.left() + 12 + j * 5, r.center().y() - 6 + k * 6), 1.3, 1.3);
      const bool integ = ai::claudecode::isKey(m.repo, m.file);
      p.setFont(theme::mono(12, QFont::Bold)); p.setPen(c.text);
      p.drawText(QRectF(r.left() + 30, r.top() + 7, r.width() - 110, 20), Qt::AlignLeft | Qt::AlignVCenter,
                 QFontMetricsF(p.font()).elidedText(displayName(m.repo, m.file), Qt::ElideMiddle, r.width() - 120));
      p.setFont(theme::mono(10)); p.setPen(c.textMute);
      p.drawText(QRectF(r.left() + 30, r.top() + 28, r.width() - (integ ? 40 : 110), 18), Qt::AlignLeft | Qt::AlignVCenter,
                 QFontMetricsF(p.font()).elidedText(integ ? QCoreApplication::translate("ai", "integration · Anthropic's servers") : m.repo,
                                                    integ ? Qt::ElideRight : Qt::ElideMiddle, r.width() - (integ ? 44 : 120)));
      if (integ) {
        p.setFont(theme::mono(10, QFont::Bold, 1.0)); p.setPen(c.accentText);
        p.drawText(QRectF(r.right() - 110, r.top() + 7, 100, 20), Qt::AlignRight | Qt::AlignVCenter, QCoreApplication::translate("ai", "INTEGRATION"));
        continue;
      }
      p.setFont(theme::mono(11, QFont::Medium)); p.setPen(c.textDim);
      p.drawText(QRectF(r.right() - 90, r.top() + 7, 80, 20), Qt::AlignRight | Qt::AlignVCenter, bytes(m.size));
      const QString q = quantOf(m.file);
      if (!q.isEmpty()) { p.setFont(theme::mono(10, QFont::Bold, 1.0)); p.setPen(c.accentText);
        p.drawText(QRectF(r.right() - 90, r.top() + 28, 80, 18), Qt::AlignRight | Qt::AlignVCenter, q); }
    }
  }
  int rowAt(const QPointF& pos) const { const int i = int(pos.y()) / kRow; return i >= 0 && i < m_models.size() ? i : -1; }
  void mouseMoveEvent(QMouseEvent* e) override {
    if ((e->buttons() & Qt::LeftButton) && m_press >= 0 && (e->position() - m_pressPos).manhattanLength() > 6) {
      const auto m = m_models[m_press];
      m_press = -1;
      auto* drag = new QDrag(this);
      drag->setMimeData(mimeFor(m.repo, m.file, -1));
      drag->setPixmap(dragChip(displayName(m.repo, m.file), devicePixelRatioF()));
      drag->setHotSpot(QPoint(14, 15));
      drag->exec(Qt::CopyAction);
      return;
    }
    const int h = rowAt(e->position());
    if (h != m_hover) { m_hover = h; setCursor(h >= 0 ? Qt::OpenHandCursor : Qt::ArrowCursor); update(); }
  }
  void mousePressEvent(QMouseEvent* e) override { m_press = rowAt(e->position()); m_pressPos = e->position(); }
  void mouseReleaseEvent(QMouseEvent*) override { m_press = -1; }
  void leaveEvent(QEvent*) override { m_hover = -1; update(); }
private:
  QList<ai::LocalModel> m_models;
  int m_hover = -1, m_press = -1;
  QPointF m_pressPos;
};

// ---------------------------------------------------------------------------------------------- one environment
class CubeCard : public QWidget {
public:
  std::function<void(int)> onChat;
  explicit CubeCard(int index, QWidget* parent) : QWidget(parent), m_index(index), m_anim(new QVariantAnimation(this)) {
    setAcceptDrops(true);
    setMinimumSize(200, 210);   // the cube shrinks first; title, model line and EJECT always fit
    setMouseTracking(true);
    m_eject = new QPushButton(QCoreApplication::translate("ai", "EJECT"), this);
    m_eject->setCursor(Qt::PointingHandCursor);
    m_eject->setFont(theme::mono(10, QFont::Bold, 1.2));
    const auto& c = theme::pal();
    m_eject->setStyleSheet(QString("QPushButton { background: transparent; color: %1; border: 1px solid %2; border-radius: 3px; padding: 3px 10px; }"
                                   "QPushButton:hover { color: %3; border-color: %4; }")
                               .arg(c.textDim.name(), c.border.name(), c.down.name(), c.down.name()));
    connect(m_eject, &QPushButton::clicked, this, [this] { ai::Slots::instance().eject(m_index); });
    m_chat = new QPushButton(QCoreApplication::translate("ai", "CHAT"), this);
    m_chat->setCursor(Qt::PointingHandCursor);
    m_chat->setFont(theme::mono(10, QFont::Bold, 1.2));
    m_chat->setStyleSheet(QString("QPushButton { background: %1; color: %2; border: 1px solid %3; border-radius: 3px; padding: 3px 12px; }"
                                  "QPushButton:hover { background: %4; }")
                              .arg(theme::alpha(c.up, 40).name(QColor::HexArgb), c.up.name(), theme::alpha(c.up, 150).name(QColor::HexArgb),
                                   theme::alpha(c.up, 80).name(QColor::HexArgb)));
    connect(m_chat, &QPushButton::clicked, this, [this] { if (onChat) onChat(m_index); });
    // evaluation mode: the environment's tools answer as of a past moment (ai::Tools.h, Clock)
    m_eval = new QPushButton(QCoreApplication::translate("ai", "EVALUATION"), this);
    m_eval->setCursor(Qt::PointingHandCursor);
    m_eval->setFont(theme::mono(10, QFont::Bold, 1.2));
    m_eval->setToolTip(QCoreApplication::translate("ai", "Evaluation mode: the environment's tools answer as of a past date, as if it were then. "
                                                         "Nothing later can be read, and the tools that only know the present are off."));
    connect(m_eval, &QPushButton::clicked, this, [this] { editEvaluation(); });
    connect(&ai::Slots::instance(), &ai::Slots::asOfChanged, this, [this](int i) { if (i == m_index) syncLabels(); });
    connect(&ai::Evaluation::instance(), &ai::Evaluation::progress, this, [this] { syncLabels(); });
    for (const auto& sk : ai::skills()) m_skillBoxes << new SkillBox(m_index, sk, this);
    ai::Engine* eng = ai::Engines::instance().at(m_index);
    connect(eng, &ai::Engine::stateChanged, this, [this] { updatePulse(); update(); });
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { m_light = v.toReal(); update(); });
    m_pulse.setInterval(66);   // 15 fps gentle breathing while lit
    connect(&m_pulse, &QTimer::timeout, this, [this] { update(cubeRect().toAlignedRect().adjusted(-20, -20, 20, 20)); });
    m_clock.start();
    const bool on = !ai::Slots::instance().slot(m_index).isEmpty();
    m_light = on ? 1.0 : 0.0;
    if (qEnvironmentVariableIsSet("DW_AI_LIGHT") && on) m_light = qEnvironmentVariable("DW_AI_LIGHT").toDouble();
    if (qEnvironmentVariableIsSet("DW_AI_HOVER") && qEnvironmentVariableIntValue("DW_AI_HOVER") == m_index) m_dropHover = true;
    syncLabels();
  }
  // phones: finger-sized buttons; the card's height is the page's (one column, kPhoneHeight)
  static constexpr int kPhoneHeight = 270;
  void setPhone(bool on) {
    if (on == m_phone) return;
    m_phone = on;
    for (auto* b : {m_eject, m_chat, m_eval}) { b->setMinimumHeight(on ? 34 : 0); b->adjustSize(); }
    resizeEvent(nullptr);
    update();
  }
  void openEvaluation() { editEvaluation(); }
  void slotChanged() {
    const bool on = !ai::Slots::instance().slot(m_index).isEmpty();
    m_anim->stop();
    m_anim->setStartValue(m_light);
    m_anim->setEndValue(on ? 1.0 : 0.0);
    m_anim->setDuration(on ? 1400 : 600);
    m_anim->setEasingCurve(on ? QEasingCurve::OutCubic : QEasingCurve::InQuad);
    m_anim->start();
    m_lightStart = m_clock.elapsed();
    syncLabels();
    updatePulse();
  }

protected:
  // Height under the cube: drop hint, model lines or the "empty" note, skill boxes, EJECT / CHAT. An empty card has no
  // buttons (its boxes sit at the bottom) and drops its note when the cube would be small (the hint says the same).
  bool loaded() const { return !ai::Slots::instance().slot(m_index).isEmpty(); }
  static QString emptyNote() { return QCoreApplication::translate("ai", "Isolated environment, empty. Drag a downloaded model onto the cube."); }
  int emptyNoteHeight() const {   // the note's wrapped height at this card's width
    return QFontMetrics(theme::sans(11)).boundingRect(QRect(0, 0, qMax(40, width() - 32), 1000), Qt::TextWordWrap, emptyNote()).height() + 2;
  }
  bool showEmptyNote() const { return loaded() || height() - 62 - emptyNoteHeight() - 24 * m_skillBoxes.size() >= 120; }
  // loaded card too short for a cube above the model lines (1024 x 600 window, 2 x 2): cube on the left, lines beside it
  bool compact() const { return loaded() && height() - 126 - 24 * m_skillBoxes.size() < 90 && width() >= 260; }
  QRectF cubeRect() const {
    if (compact()) {
      qreal side = qMax<qreal>(0, qMin<qreal>(width() * 0.4, height() - 100 - 22 * (m_skillBoxes.size() - 1)));
      // phones: the buttons are taller (34 px), the cube stops above the first skill box
      if (m_phone && !m_skillBoxes.isEmpty()) side = qMax<qreal>(0, qMin<qreal>(side, m_skillBoxes.first()->y() - 34 - 8));
      return QRectF(14, 34, side, side);
    }
    const qreal below = loaded() ? 126 : showEmptyNote() ? 62 + emptyNoteHeight() : 60;
    const qreal side = qMax<qreal>(0, qMin<qreal>(width() - 24, height() - below - 24 * m_skillBoxes.size()));
    return QRectF((width() - side) / 2.0, 34, side, side);
  }
  void resizeEvent(QResizeEvent*) override {
    m_eject->adjustSize();
    m_eject->move(width() - m_eject->width() - 12, height() - m_eject->height() - 12);
    m_chat->adjustSize();
    m_chat->move(m_eject->x() - m_chat->width() - 8, m_eject->y());
    m_eval->adjustSize();
    m_eval->move(m_chat->x() - m_eval->width() - 8, m_eject->y());
    // skills: tick boxes just above the buttons (they belong to the environment, empty or not); at the bottom when
    // the card is empty (no buttons), so no empty band is left under them
    const int boxesBottom = loaded() ? m_eject->y() - 6 : height() - 10;
    for (int k = 0; k < m_skillBoxes.size(); ++k)
      m_skillBoxes[k]->setGeometry(14, boxesBottom - 20 - 22 * (int(m_skillBoxes.size()) - 1 - k), width() - 28, 20);
    m_cube = {};
  }
  void showEvent(QShowEvent*) override { updatePulse(); }
  void hideEvent(QHideEvent*) override { m_pulse.stop(); }

  void paintEvent(QPaintEvent*) override {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    const auto& c = theme::pal();
    const auto& s = ai::Slots::instance().slot(m_index);
    const bool on = !s.isEmpty();
    // card
    p.setPen(QPen(m_dropHover ? c.accent : on ? theme::mix(c.border, c.up, 0.35 * m_light) : c.border, m_dropHover ? 2 : 1));
    p.setBrush(c.panel);
    p.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 4, 4);
    ui::drawBrackets(p, QRectF(rect()).adjusted(3, 3, -3, -3), m_dropHover ? c.accent : c.bracket, 10);
    // title row
    p.setFont(theme::mono(11, QFont::Bold, 2.4)); p.setPen(c.accentText);
    p.drawText(QPointF(16, 24), QCoreApplication::translate("ai", "ENVIRONMENT %1").arg(m_index + 1));
    const ai::Engine::State es = ai::Engines::instance().at(m_index)->state();
    const QString state = on ? ai::stateLabel(es) : QCoreApplication::translate("ai", "OFF");
    const QFont sf = theme::mono(9, QFont::Bold, 1.4);
    const qreal sw = QFontMetricsF(sf).horizontalAdvance(state) + 22;
    const QRectF pill(width() - 14 - sw, 12, sw, 18);
    const QColor sc = !on ? c.textMute : es == ai::Engine::State::Failed ? c.down : es == ai::Engine::State::Starting ? c.warn
                    : es == ai::Engine::State::Off ? c.textDim : c.up;
    p.setPen(QPen(theme::alpha(sc, 130), 1)); p.setBrush(theme::alpha(sc, on ? 40 : 18)); p.drawRoundedRect(pill, 3, 3);
    p.setPen(Qt::NoPen); p.setBrush(sc); p.drawEllipse(QPointF(pill.left() + 9, pill.center().y()), 3, 3);
    p.setFont(sf); p.setPen(sc); p.drawText(pill.adjusted(16, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, state);
    const auto& evr = ai::Evaluation::instance();
    const bool protocol = evr.running() && evr.environment() == m_index;
    if (const qint64 asOf = ai::Slots::instance().asOf(m_index); asOf > 0 || protocol) {   // evaluation mode: the date its tools answer as of
      const QString ev = protocol ? (evr.probing() ? QCoreApplication::translate("ai", "MEMORY PROBE")
                                                   : QCoreApplication::translate("ai", "EVALUATION %1 / %2").arg(evr.done() + 1).arg(evr.total()))
                                  : QCoreApplication::translate("ai", "EVALUATION · %1 UTC")
                                        .arg(QDateTime::fromSecsSinceEpoch(asOf, QTimeZone::UTC).toString("yyyy-MM-dd HH:mm"));
      const qreal ew = QFontMetricsF(sf).horizontalAdvance(ev) + 16;
      const QRectF eb(pill.left() - 8 - ew, 12, ew, 18);
      if (eb.left() > 16 + QFontMetricsF(theme::mono(11, QFont::Bold, 2.4)).horizontalAdvance(QCoreApplication::translate("ai", "ENVIRONMENT %1").arg(m_index + 1)) + 8) {
        p.setPen(QPen(theme::alpha(c.warn, 150), 1)); p.setBrush(theme::alpha(c.warn, 36)); p.drawRoundedRect(eb, 3, 3);
        p.setPen(c.warn); p.drawText(eb, Qt::AlignCenter, ev);
      }
    }

    // cube
    const QRectF cr = cubeRect();
    const qreal dpr = devicePixelRatioF();
    if (m_cube.size() != cr.size().toSize() || m_cubeDpr != dpr || m_cubeRev != theme::revision()) {
      m_cube.render(cr.size().toSize(), dpr, c.up);
      m_cubeDpr = dpr; m_cubeRev = theme::revision();
    }
    p.drawPixmap(cr.topLeft(), m_cube.metal());
    double light = m_light;
    if (on && light > 0.999 && !qEnvironmentVariableIsSet("DW_AI_LIGHT")) {
      if (es == ai::Engine::State::Busy)            // answering: the core works, quick strong pulse
        light = 0.70 + 0.30 * (0.5 + 0.5 * std::sin(m_clock.elapsed() / 160.0));
      else if (es == ai::Engine::State::Starting)   // loading the weights: slow rise and fall
        light = 0.45 + 0.40 * (0.5 + 0.5 * std::sin(m_clock.elapsed() / 380.0));
      else if (es == ai::Engine::State::Ready)      // in memory: gentle breathing
        light = 0.86 + 0.14 * (0.5 + 0.5 * std::sin(m_clock.elapsed() / 900.0));
      else if (es == ai::Engine::State::Failed)
        light = 0.25;
      // standby (not in memory yet): steady light, no animation
    }
    if (light > 0.001) {
      p.save();
      p.setCompositionMode(QPainter::CompositionMode_Plus);
      p.setOpacity(qBound(0.0, light, 1.0));
      p.drawPixmap(cr.topLeft(), m_cube.glow());
      p.restore();
    }
    if (!on) {   // hint on an empty cube
      p.setFont(theme::mono(10, QFont::Medium, 1.6)); p.setPen(m_dropHover ? c.accent : c.textMute);
      p.drawText(QRectF(0, cr.bottom() - 4, width(), 18), Qt::AlignCenter,
                 m_dropHover ? QCoreApplication::translate("ai", "RELEASE TO LOAD") : QCoreApplication::translate("ai", "DROP A MODEL HERE"));
    }

    // model line(s)
    const bool side = compact();
    const qreal x0 = side ? cr.right() + 12 : 16, lw = width() - x0 - 16;
    const qreal y0 = side ? cr.center().y() - 29 : cr.bottom() + 18;
    if (on) {
      p.setFont(theme::mono(12, QFont::Bold)); p.setPen(c.text);
      p.drawText(QRectF(x0, y0, lw, 20), Qt::AlignLeft | Qt::AlignVCenter,
                 QFontMetricsF(p.font()).elidedText(s.displayName(), Qt::ElideMiddle, lw));
      const qint64 sz = sizeOf(s.repo, s.file);
      QStringList meta;
      if (s.isIntegration()) meta << QCoreApplication::translate("ai", "INTEGRATION") << QCoreApplication::translate("ai", "runs on Anthropic's servers");
      else {
        const QString q = quantOf(s.file);
        if (!q.isEmpty()) meta << q;
        meta << bytes(sz);
        const qint64 ram = ai::Slots::instance().memoryTotal();
        if (ram > 0) {
          const double pct = 100.0 * sz / ram;
          meta << (pct < 1 ? QCoreApplication::translate("ai", "< 1% of RAM") : QCoreApplication::translate("ai", "≈ %1% of RAM").arg(qRound(pct)));
        }
      }
      p.setFont(theme::mono(10)); p.setPen(c.textDim);
      if (side)   // beside the cube: the details may take two lines (three on a narrow card: phone turned sideways)
        p.drawText(QRectF(x0, y0 + 22, lw, 54), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, meta.join("  ·  "));
      else
        p.drawText(QRectF(16, y0 + 20, width() - 32, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetricsF(p.font()).elidedText(meta.join("  ·  "), Qt::ElideRight, width() - 32));
    } else if (showEmptyNote()) {
      p.setFont(theme::sans(11)); p.setPen(c.textMute);
      const qreal textBottom = m_skillBoxes.isEmpty() ? height() - 10 : m_skillBoxes.first()->y() - 4;
      p.drawText(QRectF(16, y0, width() - 32, qMax<qreal>(20, textBottom - y0)), Qt::AlignLeft | Qt::TextWordWrap, emptyNote());
    }
  }

  void dragEnterEvent(QDragEnterEvent* e) override {
    QString r, f; int from;
    if (readMime(e->mimeData(), &r, &f, &from) && from != m_index) { e->acceptProposedAction(); m_dropHover = true; update(); }
  }
  void dragLeaveEvent(QDragLeaveEvent*) override { m_dropHover = false; update(); }
  void dropEvent(QDropEvent* e) override {
    m_dropHover = false;
    QString r, f; int from;
    if (!readMime(e->mimeData(), &r, &f, &from)) return;
    e->acceptProposedAction();
    if (from >= 0) ai::Slots::instance().eject(from);   // moved from another cube
    ai::Slots::instance().load(m_index, r, f);
  }
  // drag a loaded cube onto another one to move its model
  void mousePressEvent(QMouseEvent* e) override { m_pressPos = e->position(); }
  void mouseDoubleClickEvent(QMouseEvent* e) override {
    if (!ai::Slots::instance().slot(m_index).isEmpty() && cubeRect().contains(e->position()) && onChat) onChat(m_index);
  }
  void mouseMoveEvent(QMouseEvent* e) override {
    const auto& s = ai::Slots::instance().slot(m_index);
    const bool overCube = cubeRect().contains(e->position());
    setCursor(!s.isEmpty() && overCube ? Qt::OpenHandCursor : Qt::ArrowCursor);
    if (!(e->buttons() & Qt::LeftButton) || s.isEmpty() || !cubeRect().contains(m_pressPos)) return;
    if ((e->position() - m_pressPos).manhattanLength() < 8) return;
    auto* drag = new QDrag(this);
    drag->setMimeData(mimeFor(s.repo, s.file, m_index));
    drag->setPixmap(dragChip(s.displayName(), devicePixelRatioF()));
    drag->setHotSpot(QPoint(14, 15));
    drag->exec(Qt::MoveAction);
  }

private:
  void syncLabels() {
    const bool on = !ai::Slots::instance().slot(m_index).isEmpty();
    m_eject->setVisible(on);
    m_chat->setVisible(on);
    m_eval->setVisible(on);
    const bool eval = ai::Slots::instance().asOf(m_index) > 0;
    const auto& c = theme::pal();
    const QColor ec = eval ? c.warn : c.textDim;
    m_eval->setStyleSheet(QString("QPushButton { background: %1; color: %2; border: 1px solid %3; border-radius: 3px; padding: 3px 10px; }"
                                  "QPushButton:hover { color: %4; border-color: %4; }")
                              .arg(eval ? theme::alpha(c.warn, 40).name(QColor::HexArgb) : QStringLiteral("transparent"), ec.name(),
                                   eval ? theme::alpha(c.warn, 150).name(QColor::HexArgb) : c.border.name(), c.warn.name()));
    resizeEvent(nullptr);   // the skill boxes move down when the card empties, up when it loads
    update();
  }
  void updatePulse() {
    const bool on = !ai::Slots::instance().slot(m_index).isEmpty();
    const bool steady = ai::Engines::instance().at(m_index)->state() == ai::Engine::State::Off ||
                        ai::Engines::instance().at(m_index)->state() == ai::Engine::State::Failed;
    if (on && !steady && isVisible() && !qEnvironmentVariableIsSet("DW_AI_LIGHT")) m_pulse.start(); else m_pulse.stop();
    update();
  }
  int m_index;
  VoxelCube m_cube; qreal m_cubeDpr = 0; int m_cubeRev = -1;
  QVariantAnimation* m_anim;
  QTimer m_pulse;
  QElapsedTimer m_clock;
  qint64 m_lightStart = 0;
  double m_light = 0;
  bool m_dropHover = false;
  QPointF m_pressPos;
  QPushButton* m_eject;
  QPushButton* m_chat;
  QPushButton* m_eval = nullptr;
  bool m_phone = false;
  // the evaluation mode's dialog: on or off, and the moment (UTC)
  void editEvaluation() {
    // phones: the dialog as wide as the window, its text and fields scrolling above OK / Cancel, one column of
    // finger-sized controls
    const bool phone = ui::compact(window()->width(), kAgentsDesktop);
    QDialog d(window());
    d.setWindowTitle(QCoreApplication::translate("ai", "Evaluation mode · environment %1").arg(m_index + 1));
    QVBoxLayout* lay = nullptr;
    QVBoxLayout* outer = nullptr;
    // the text and fields scroll above OK / Cancel at every size: on a laptop the dialog is taller than the screen
    // and macOS squeezed its rows on top of each other (seen 2026-11-21 at 1600x1042)
    QScrollArea* phoneScroll = nullptr;
    {
      outer = new QVBoxLayout(&d); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(0);
      phoneScroll = new QScrollArea(&d);
      phoneScroll->setWidgetResizable(true); phoneScroll->setFrameShape(QFrame::NoFrame);
      phoneScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
      auto* inner = new QWidget;
      phoneScroll->setWidget(inner);
      lay = new QVBoxLayout(inner);
      if (phone) lay->setContentsMargins(16, 16, 16, 8);
      outer->addWidget(phoneScroll, 1);
    }
    auto* note = new QLabel(QCoreApplication::translate("ai",
        "The environment's tools answer as of the date below, as if it were then: the bars closed by that time, the order flow "
        "stored up to it, the macro facts published by then, backtests on the history before it. A later date asked is refused, "
        "and the tools that only know the present (prices, news, the live book, the pages) are off. The model is told the date; "
        "the tools enforce it. Its own memory of later events is not erased: compare with dates after its training."), &d);
    note->setWordWrap(true);
    note->setMinimumWidth(phone ? 0 : 420);
    auto* on = new QCheckBox(QCoreApplication::translate("ai", "Evaluation mode on"), &d);
    auto* when = new QDateTimeEdit(&d);
    when->setTimeZone(QTimeZone::UTC);
    when->setDisplayFormat("yyyy-MM-dd HH:mm 'UTC'");
    when->setCalendarPopup(true);
    const qint64 cur = ai::Slots::instance().asOf(m_index);
    on->setChecked(cur > 0);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    when->setMaximumDateTime(now);
    when->setDateTime(cur > 0 ? QDateTime::fromSecsSinceEpoch(cur, QTimeZone::UTC)
                              : QDateTime(now.date().addDays(-7), QTime(0, 0), QTimeZone::UTC));
    when->setEnabled(on->isChecked());
    connect(on, &QCheckBox::toggled, when, &QWidget::setEnabled);
    auto* blindBox = new QCheckBox(QCoreApplication::translate("ai", "Blind: dates shifted, the market's name and price level masked"), &d);
    blindBox->setChecked(ai::Slots::instance().blindSeed(m_index) != 0);
    blindBox->setEnabled(on->isChecked());
    connect(on, &QCheckBox::toggled, blindBox, &QWidget::setEnabled);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    lay->addWidget(note);
    lay->addWidget(on);
    lay->addWidget(when);
    if (phone) {   // the long option's text wraps beside its box
      auto* row = new QHBoxLayout; row->setSpacing(6);
      auto* text = new TapLabel(blindBox->text(), blindBox, &d);
      blindBox->setText(QString());
      text->setEnabled(blindBox->isEnabled());
      connect(on, &QCheckBox::toggled, text, &QWidget::setEnabled);
      row->addWidget(blindBox, 0, Qt::AlignTop);
      row->addWidget(text, 1);
      lay->addLayout(row);
    } else lay->addWidget(blindBox);
    // the evaluation protocol (ai::Evaluation): the model decides at past moments, Deepwater scores the decisions
    auto& evr = ai::Evaluation::instance();
    auto* head = new QLabel("<b>" + QCoreApplication::translate("ai", "Evaluation protocol").toHtmlEscaped() + "</b>", &d);
    auto* pnote = new QLabel(QCoreApplication::translate("ai",
        "At each moment of the period, the clock is set there (a fresh conversation) and the model decides LONG, SHORT or FLAT "
        "for the next hours with its tools; Deepwater scores each decision on the stored 1-hour bars after costs, against buy & "
        "hold, the trend rule, flat and an ML model without LLM. Probe the model's memory first: only the decisions after the "
        "last month it remembers count (tier A). Each decision is a full answer of the model: a dozen take several minutes."), &d);
    pnote->setWordWrap(true);
    const QString market = "BINANCE:BTCUSDT";
    const auto st = technicaldata::Store::instance().state(market, 3600);
    const QDateTime lastBar = QDateTime::fromSecsSinceEpoch(st.count ? st.last + 3600 : QDateTime::currentSecsSinceEpoch(), QTimeZone::UTC);
    auto* from = new QDateEdit(lastBar.date().addDays(-15), &d);
    auto* to = new QDateEdit(lastBar.date().addDays(-2), &d);
    for (auto* de : {from, to}) { de->setDisplayFormat("yyyy-MM-dd"); de->setCalendarPopup(true); }
    auto* horizon = new QComboBox(&d);
    for (int h : {1, 4, 12, 24}) horizon->addItem(QCoreApplication::translate("ai", "%1 h").arg(h), h);
    horizon->setCurrentIndex(1);
    auto* count = new QSpinBox(&d);
    count->setRange(2, 60); count->setValue(10);
    auto* repeats = new QSpinBox(&d);
    repeats->setRange(1, 5); repeats->setValue(1);
    auto* trials = new QComboBox(&d);
    trials->addItem(QCoreApplication::translate("ai", "named"), 0);
    trials->addItem(QCoreApplication::translate("ai", "blind"), 1);
    trials->addItem(QCoreApplication::translate("ai", "both, at each moment"), 2);
    auto* form = new QFormLayout;
    auto* marketLine = new QLabel(market + QCoreApplication::translate("ai", " (1-hour bars, 15 bp a side)"), &d);
    if (phone) form->setRowWrapPolicy(QFormLayout::WrapAllRows);   // each label above its field, the field full width
    form->addRow(QCoreApplication::translate("ai", "Market"), marketLine);
    form->addRow(QCoreApplication::translate("ai", "From"), from);
    form->addRow(QCoreApplication::translate("ai", "To"), to);
    form->addRow(QCoreApplication::translate("ai", "Horizon"), horizon);
    form->addRow(QCoreApplication::translate("ai", "Decisions"), count);
    form->addRow(QCoreApplication::translate("ai", "Repeats"), repeats);
    form->addRow(QCoreApplication::translate("ai", "Trials"), trials);
    QString lastKnown;
    const qint64 frontier = ai::Evaluation::frontierOf(m_index, &lastKnown);
    auto* probeLine = new QLabel(frontier > 0 ? QCoreApplication::translate("ai", "Memory probed: last month it remembers: %1.").arg(lastKnown)
                                              : QCoreApplication::translate("ai", "Memory not probed yet for this environment."), &d);
    auto* err = new QLabel(&d);
    err->setWordWrap(true);
    err->setStyleSheet(QString("color:%1;").arg(theme::pal().down.name()));
    auto* probeBtn = new QPushButton(QCoreApplication::translate("ai", "Probe its memory"), &d);
    auto* runBtn = new QPushButton(evr.running() ? QCoreApplication::translate("ai", "Stop the evaluation") : QCoreApplication::translate("ai", "Run the evaluation"), &d);
    probeBtn->setEnabled(!evr.running());
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(probeBtn);
    btnRow->addWidget(runBtn);
    btnRow->addStretch(1);
    connect(probeBtn, &QPushButton::clicked, &d, [&d, err, this] {
      const QString why = ai::Evaluation::instance().startProbe(m_index);
      if (!why.isEmpty()) { err->setText(why); return; }
      d.reject();
      if (onChat) onChat(m_index);
    });
    connect(runBtn, &QPushButton::clicked, &d, [&d, err, from, to, horizon, count, repeats, trials, market, this] {
      auto& e = ai::Evaluation::instance();
      if (e.running()) { e.stop(); d.reject(); return; }
      ai::EvalSpec spec;
      spec.env = m_index;
      spec.market = market;
      spec.horizonHours = horizon->currentData().toInt();
      spec.from = QDateTime(from->date(), QTime(0, 0), QTimeZone::UTC).toSecsSinceEpoch();
      spec.to = QDateTime(to->date(), QTime(0, 0), QTimeZone::UTC).toSecsSinceEpoch();
      spec.decisions = count->value();
      spec.repeats = repeats->value();
      spec.mode = trials->currentData().toInt();
      const QString why = e.startProtocol(spec);
      if (!why.isEmpty()) { err->setText(why); return; }
      d.reject();
      if (onChat) onChat(m_index);
    });
    // the forward test (tier C): asked in the present at scheduled times, scored once the horizon has passed
    auto* fhead = new QLabel("<b>" + QCoreApplication::translate("ai", "Forward test (tier C)").toHtmlEscaped() + "</b>", &d);
    auto* fnote = new QLabel(QCoreApplication::translate("ai",
        "The same question asked in the present, now and then every few hours while Deepwater runs (the horizon and the number of "
        "decisions above): no memory of the model can know what follows. Each decision is scored once its horizon has passed; "
        "a time missed (Deepwater closed, the environment busy or in evaluation mode) counts as missed, never asked late. Keep "
        "the same model and skills throughout: a change is reported."), &d);
    fnote->setWordWrap(true);
    auto* step = new QSpinBox(&d);
    step->setRange(1, 72); step->setValue(6); step->setSuffix(QCoreApplication::translate("ai", " h between decisions"));
    const QString fst = evr.forwardStatus(m_index);
    auto* fline = new QLabel(fst.isEmpty() ? QCoreApplication::translate("ai", "No forward test in this environment.") : fst, &d);
    fline->setWordWrap(true);
    const bool fRunning = fst.contains(QCoreApplication::translate("ai", "running"));
    auto* fBtn = new QPushButton(fRunning ? QCoreApplication::translate("ai", "Stop the forward test") : QCoreApplication::translate("ai", "Start the forward test"), &d);
    connect(fBtn, &QPushButton::clicked, &d, [&d, err, horizon, count, step, market, fRunning, this] {
      auto& e = ai::Evaluation::instance();
      if (fRunning) { e.stopForward(m_index); d.reject(); return; }
      ai::EvalSpec spec;
      spec.env = m_index;
      spec.market = market;
      spec.horizonHours = horizon->currentData().toInt();
      spec.decisions = count->value();
      const QString why = e.startForward(spec, step->value());
      if (!why.isEmpty()) { err->setText(why); return; }
      d.reject();
      if (onChat) onChat(m_index);
    });
    auto* fRow = new QHBoxLayout;
    fRow->addWidget(step);
    fRow->addWidget(fBtn);
    fRow->addStretch(1);
    lay->addSpacing(10);
    lay->addWidget(head);
    lay->addWidget(pnote);
    lay->addLayout(form);
    lay->addWidget(probeLine);
    lay->addLayout(btnRow);
    lay->addSpacing(8);
    lay->addWidget(fhead);
    lay->addWidget(fnote);
    lay->addWidget(fline);
    lay->addLayout(fRow);
    lay->addWidget(err);
    if (phone) {
      probeLine->setWordWrap(true);
      for (QBoxLayout* r : {static_cast<QBoxLayout*>(btnRow), static_cast<QBoxLayout*>(fRow)}) r->setDirection(QBoxLayout::TopToBottom);
      // the app's colours and 36 px controls (a taller native macOS button turns into a white bevel)
      const auto& pc = theme::pal();
      d.setStyleSheet(QString("QPushButton { background:%1; color:%2; border:1px solid %3; border-radius:3px; padding:6px 12px; min-height:22px; }"
                              "QPushButton:hover { border-color:%4; } QPushButton:disabled { color:%5; }"
                              "QAbstractSpinBox, QComboBox { background:%1; color:%2; border:1px solid %3; border-radius:3px; padding:4px 8px; min-height:26px; }"
                              "QCheckBox { min-height:30px; } QCheckBox::indicator { width:18px; height:18px; border:1px solid %4; border-radius:3px; background:%1; }"
                              "QCheckBox::indicator:checked { background:%6; border-color:%6; } QCheckBox::indicator:disabled { border-color:%3; }")
                          .arg(pc.panelAlt.name(), pc.text.name(), pc.border.name(), pc.borderHi.name(), pc.textMute.name(), pc.accent.name()));
      auto* foot = new QVBoxLayout; foot->setContentsMargins(16, 8, 16, 16);
      foot->addWidget(bb);
      outer->addLayout(foot);
      d.setMinimumWidth(0);
      const QSize cap = window()->size() - QSize(24, 24);
      phoneScroll->widget()->adjustSize();
      d.resize(cap.width(), qMin(cap.height(), phoneScroll->widget()->sizeHint().height() + foot->sizeHint().height() + 4));
    } else {
      auto* foot = new QVBoxLayout; foot->setContentsMargins(11, 6, 11, 11);
      foot->addWidget(bb);
      outer->addLayout(foot);
      phoneScroll->widget()->adjustSize();
      const QRect avail = window()->screen() ? window()->screen()->availableGeometry() : QRect(0, 0, 1280, 800);
      const QSize hint = phoneScroll->widget()->sizeHint();
      d.resize(hint.width() + 24, qMin(avail.height() - 80, hint.height() + foot->sizeHint().height() + 4));
    }
    if (d.exec() != QDialog::Accepted) return;
    const quint32 seed = ai::Slots::instance().blindSeed(m_index);
    ai::Slots::instance().setBlind(m_index, on->isChecked() && blindBox->isChecked() ? (seed ? seed : QRandomGenerator::global()->generate() | 1u) : 0);
    ai::Slots::instance().setAsOf(m_index, on->isChecked() ? when->dateTime().toSecsSinceEpoch() : 0);
  }
  QList<SkillBox*> m_skillBoxes;
};

// ---------------------------------------------------------------------------------------------- page
class AgentsPage : public QWidget {
public:
  explicit AgentsPage(QWidget* parent) : QWidget(parent), m_scroll(this) {
    m_scroll.desktopMin = kAgentsDesktop;
    const auto& c = theme::pal();
    applyHookSlots();
    auto* root = new QVBoxLayout(this);
    m_root = root;
    root->setContentsMargins(16, 12, 16, 12); root->setSpacing(10);

    auto* head = new QHBoxLayout; head->setSpacing(12);
    head->addWidget(ui::label(QCoreApplication::translate("ai", "AGENTS"), theme::title(), c.accentText, this));
    head->addWidget(ui::label(QCoreApplication::translate("ai", "four isolated environments"), theme::mono(11), c.textDim, this));
    head->addStretch(1);
    m_mem = ui::label(QString(), theme::mono(10), c.textDim, this);
    head->addWidget(m_mem);
    root->addLayout(head);
    m_head = head;

    auto* body = new QHBoxLayout; body->setSpacing(12);
    root->addLayout(body, 1);
    m_body = body;

    auto* left = new ui::Panel(QCoreApplication::translate("ai", "Models & integrations"), this);
    m_list = new ModelList(left);
    auto* scroll = new QScrollArea(left);
    scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setWidget(m_list);
    left->body()->addWidget(scroll, 1);
    m_listScroll = scroll;
    m_empty = ui::label(QCoreApplication::translate("ai", "No model downloaded yet: get one in AI › LLM › Local models, then drag it onto a cube."),
                        theme::sans(12), c.textMute, left);
    m_empty->setWordWrap(true);
    left->body()->addWidget(m_empty);
    auto* hint = ui::label(QCoreApplication::translate("ai", "Drag a model onto a cube to load it. Drag a lit cube onto another to move its model."),
                           theme::sans(11), c.textMute, left);
    hint->setWordWrap(true);
    left->body()->addWidget(hint);
    left->setMinimumWidth(240);   // 1024 px window: 240 + the 2 × 2 cards (stretch 3 : 7 keeps the wide look)
    body->addWidget(left, 3);
    m_left = left;

    auto* gridHost = new QWidget(this);
    m_gridHost = gridHost;
    gridHost->installEventFilter(this);
    m_grid = new QGridLayout(gridHost);
    m_grid->setContentsMargins(0, 0, 0, 0); m_grid->setSpacing(12);
    for (int i = 0; i < ai::Slots::kCount; ++i) {
      m_cards << new CubeCard(i, gridHost);
      m_cards.last()->onChat = [this](int k) { openChat(k); };
    }
    m_stack = new QStackedWidget(this);
    m_stack->addWidget(gridHost);
    m_chatView = new ChatView(m_stack);
    m_chatView->onBack = [this] { closeChat(); };
    m_chatView->onLayoutChanged = [this] { m_scroll.relayout(); };   // WEB ticked or not: what stands under the chat
    m_stack->addWidget(m_chatView);
    body->addWidget(m_stack, 7);
    layoutCards();

    auto* foot = ui::label(QCoreApplication::translate("ai",
        "Each cube is an isolated environment: its own folder and its own sandboxed process, with no internet, no access to "
        "your files, nothing shared with the other cubes. It reads Deepwater's data only when its box is ticked. "
        "A cube holding Claude Code runs it on Anthropic's servers: what it reads through its ticked boxes is sent there."),
        theme::sans(11), c.textMute, this);
    foot->setWordWrap(true);
    root->addWidget(foot);

    m_scroll.height = [this](int w, int vh) { return scrollHeight(w, vh); };
    connect(&ai::ModelHub::instance(), &ai::ModelHub::installedChanged, this, [this] { refresh(); });
    connect(&ai::Slots::instance(), &ai::Slots::changed, this, [this](int i) { if (i >= 0 && i < m_cards.size()) m_cards[i]->slotChanged(); refresh(); });
    refresh();
    m_foot = foot;
    // DW_AI_TOOLTEST=<out file>: runs every read_data tool with sample arguments and writes the results there
    if (qEnvironmentVariableIsSet("DW_AI_TOOLTEST")) runToolTest(qEnvironmentVariable("DW_AI_TOOLTEST"));
    // DW_AI_MLTEST=<out file>: calls the "ml" skill's tools one after the other (valid and invented arguments) as environment 1
    if (qEnvironmentVariableIsSet("DW_AI_MLTEST")) runMlTest(qEnvironmentVariable("DW_AI_MLTEST"));
    // DW_AI_EVAL="probe" or "run:2026-11-05,2026-11-17,4,6,1" (from, to, horizon h, decisions, repeats): starts the
    // evaluation protocol of environment 1 after launch; DW_AI_EVAL_QUIT=1 quits when it ends (tests)
    if (qEnvironmentVariableIsSet("DW_AI_EVAL")) {
      QTimer::singleShot(3000, this, [] {
        const QString v = qEnvironmentVariable("DW_AI_EVAL");
        QString why;
        if (v == "probe") why = ai::Evaluation::instance().startProbe(0);
        else if (v.startsWith("forward:")) {   // forward:horizon h,step h,decisions
          const QStringList a = v.mid(8).split(',');
          ai::EvalSpec spec;
          if (a.size() == 3) { spec.horizonHours = a[0].toInt(); spec.decisions = a[2].toInt(); }
          why = ai::Evaluation::instance().startForward(spec, a.value(1).toInt());
        } else {
          const QStringList a = v.mid(4).split(',');
          ai::EvalSpec spec;
          auto at = [](const QString& d) { return QDateTime(QDate::fromString(d, "yyyy-MM-dd"), QTime(0, 0), QTimeZone::UTC).toSecsSinceEpoch(); };
          if (a.size() >= 5) { spec.from = at(a[0]); spec.to = at(a[1]); spec.horizonHours = a[2].toInt(); spec.decisions = a[3].toInt(); spec.repeats = a[4].toInt(); }
          if (a.size() >= 6) spec.mode = a[5] == "blind" ? 1 : a[5] == "both" ? 2 : 0;
          why = ai::Evaluation::instance().startProtocol(spec);
        }
        if (!why.isEmpty()) qWarning("evaluation not started: %s", qPrintable(why));
        if (qEnvironmentVariableIsSet("DW_AI_EVAL_QUIT"))
          connect(&ai::Evaluation::instance(), &ai::Evaluation::finished, qApp, [] { QTimer::singleShot(2000, qApp, &QCoreApplication::quit); });
      });
    }
    if (qEnvironmentVariableIsSet("DW_AI_EVALDLG")) {
      const int k = qEnvironmentVariableIntValue("DW_AI_EVALDLG");
      QTimer::singleShot(1500, this, [this, k] { if (k >= 0 && k < m_cards.size()) m_cards[k]->openEvaluation(); });
    }
    if (qEnvironmentVariableIsSet("DW_AI_CHAT")) {
      const int k = qEnvironmentVariableIntValue("DW_AI_CHAT");
      QTimer::singleShot(0, this, [this, k] {
        openChat(k);
        if (qEnvironmentVariableIsSet("DW_AI_ASK")) ai::Engines::instance().at(k)->ask(qEnvironmentVariable("DW_AI_ASK"));
        // DW_AI_UNTICK_ON_CALL=1: untick every skill of that environment as soon as the model starts writing a tool call
        // (the call then arrives without its skill: it must be refused)
        if (qEnvironmentVariableIsSet("DW_AI_UNTICK_ON_CALL")) {
          ai::Engine* e = ai::Engines::instance().at(k);
          connect(e, &ai::Engine::textArrived, this, [e, k] {
            if (e->messages().isEmpty() || !e->messages().last().text.contains("<tool_call>")) return;
            for (const auto& sk : ai::skills()) ai::Slots::instance().setSkill(k, sk.id, false);
          });
        }
      });
    }
  }
protected:
  void paintEvent(QPaintEvent*) override { QPainter p(this); p.fillRect(rect(), theme::pal().bg); }
  bool eventFilter(QObject* o, QEvent* e) override {
    if (o == m_gridHost && e->type() == QEvent::Resize) layoutCards();
    return QWidget::eventFilter(o, e);
  }
  // A page built hidden gets Qt's default 100 x 30 first: no phone arrangement for that (it would leave the desktop
  // layouts measured differently), the real size arrives with the show.
  void showEvent(QShowEvent*) override { resizeEvent(nullptr); }
  void resizeEvent(QResizeEvent*) override {
    if (!isVisible()) return;
    setPhone(ui::compact(width(), kAgentsDesktop));
    if (!PhoneScroll::wanted(width(), height(), kAgentsDesktop))   // a desktop window: the heights come from the window again
      for (QWidget* w : {static_cast<QWidget*>(m_left), static_cast<QWidget*>(m_stack), static_cast<QWidget*>(m_listScroll)}) phoneHeight(w, 0);
    m_scroll.resize();
  }
private:
  // Phones: the memory line under the title (its own row), the models above the cubes, the cubes in one column.
  void setPhone(bool on) {
    if (on == m_phone) return;
    m_phone = on;
    if (on) { m_head->removeWidget(m_mem); m_root->insertWidget(1, m_mem); }
    else { m_root->removeWidget(m_mem); m_head->addWidget(m_mem); }
    m_mem->setWordWrap(on);
    m_body->setDirection(on ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    for (auto* card : m_cards) card->setPhone(on);
    layoutCards();
  }
  // The content's height when the page scrolls (phones, or short windows), after giving the body's parts theirs.
  int scrollHeight(int w, int vh) {
    const bool phone = ui::compact(w, kAgentsDesktop);
    const int inner = w - 32;   // the root's side margins
    const int top = 12 + m_head->sizeHint().height() + 10 + (phone ? m_mem->heightForWidth(inner) + 10 : 0);
    if (m_stack->currentWidget() == m_chatView) {
      // the conversation fills what the screen leaves under the title (at least 460 px sideways); below it on a phone,
      // its activity panel or its nodes
      const int screen = phone ? qMax(320, vh - top - 12) : qMax(460, vh - top - 12);
      phoneHeight(m_stack, screen + m_chatView->phoneExtra(inner));
    } else if (phone) {
      phoneHeight(m_listScroll, m_list->sizeHint().height());
      phoneHeight(m_left, layoutHeight(m_left->layout(), inner));
      phoneHeight(m_stack, CubeCard::kPhoneHeight * 4 + 12 * 3);
    } else {
      phoneHeight(m_listScroll, 0);
      phoneHeight(m_left, CubeCard::kPhoneHeight * 2 + 12);
      phoneHeight(m_stack, CubeCard::kPhoneHeight * 2 + 12);
    }
    return layoutHeight(m_root, w);
  }
  void openChat(int i) {
    if (i < 0 || i >= ai::Slots::kCount || ai::Slots::instance().slot(i).isEmpty()) return;
    m_chatView->setEnvironment(i);
    m_left->hide(); m_foot->hide();
    m_stack->setCurrentWidget(m_chatView);
    m_scroll.relayout();
    m_chatView->focusInput();
    ai::Engines::instance().at(i)->start();   // load the model while the user types
  }
  void closeChat() {
    m_stack->setCurrentIndex(0);
    m_left->show(); m_foot->show();
    m_scroll.relayout();
  }
  // four in a row or 2 × 2: whichever gives the bigger cube in the space the grid really has (card: cube side = min(w-24, h-120));
  // phones: one column (two cards side by side would cut their titles)
  void layoutCards() {
    const qreal w = m_gridHost->width(), h = m_gridHost->height(), gap = m_grid->spacing();
    const qreal rowSide = qMin((w - 3 * gap) / 4 - 24, h - 120);
    const qreal gridSide = qMin((w - gap) / 2 - 24, (h - gap) / 2 - 120);
    const bool row = rowSide > gridSide && (w - 3 * gap) / 4 >= 290;   // a row card must still fit title + state pill
    const int mode = m_phone ? 2 : row ? 1 : 0;
    if (mode == m_cardMode && m_grid->count() == m_cards.size()) return;
    m_cardMode = mode;
    const int rows = mode == 2 ? 4 : mode == 1 ? 1 : 2, cols = mode == 2 ? 1 : mode == 1 ? 4 : 2;
    for (int i = 0; i < m_cards.size(); ++i) { m_grid->removeWidget(m_cards[i]); m_grid->addWidget(m_cards[i], i / cols, i % cols); }
    for (int i = 0; i < 4; ++i) { m_grid->setRowStretch(i, 0); m_grid->setColumnStretch(i, 0); }
    for (int i = 0; i < rows; ++i) m_grid->setRowStretch(i, 1);
    for (int i = 0; i < cols; ++i) m_grid->setColumnStretch(i, 1);
  }
  void refresh() {
    m_list->rebuild();
    m_empty->setVisible(ai::ModelHub::instance().installed().isEmpty());
    const auto& s = ai::Slots::instance();
    int loaded = 0; for (int i = 0; i < ai::Slots::kCount; ++i) if (!s.slot(i).isEmpty()) ++loaded;
    QString t = QCoreApplication::translate("ai", "%1 / 4 environments loaded").arg(loaded);
    if (s.memoryTotal() > 0)
      t += "  ·  " + QCoreApplication::translate("ai", "models ≈ %1 of %2 RAM").arg(bytes(s.memoryLoaded()), bytes(s.memoryTotal()));
    m_mem->setText(t);
    const bool over = s.memoryTotal() > 0 && s.memoryLoaded() > s.memoryTotal() * 0.75;
    m_mem->setStyleSheet(QString("color:%1;background:transparent;").arg((over ? theme::pal().warn : theme::pal().textDim).name()));
    m_mem->setToolTip(over ? QCoreApplication::translate("ai", "The loaded models together need more than 3/4 of this computer's memory.") : QString());
    m_scroll.relayout();   // the list's length (phones: the models panel's height)
  }
  static void runToolTest(const QString& outFile) {
    const QList<QPair<QString, QJsonObject>> calls{
        {"get_quotes", QJsonObject{{"symbols", QJsonArray{"EURUSD", "gold", "SPX", "nonsense"}}}},
        {"get_price_history", QJsonObject{{"symbol", "EURUSD"}, {"bars", 5}}},
        {"get_economic_calendar", QJsonObject{{"days_ahead", 3}, {"min_impact", "high"}}},
        {"get_news", QJsonObject{{"max", 3}}},
        {"get_squawk", QJsonObject{{"max", 5}}},
        {"get_macro_data", QJsonObject{{"country", "France"}}},
        {"get_macro_data", QJsonObject{{"country", "USD"}, {"indicator", "inflation"}}},
        {"get_central_banks", QJsonObject{}},
        {"get_central_banks", QJsonObject{{"bank", "ECB"}}},
        {"get_cot", QJsonObject{{"market", "EUR"}, {"weeks", 3}}},
        {"get_retail_sentiment", QJsonObject{{"symbol", "EURUSD"}}},
        {"get_risk_sentiment", QJsonObject{}},
        {"get_bank_forecasts", QJsonObject{{"market", "EURUSD"}}},
        {"get_bank_research", QJsonObject{{"max", 3}}},
        {"get_market_hours", QJsonObject{}},
        {"get_technical_read", QJsonObject{{"symbol", "XAUUSD"}}},
        {"get_liquidity_pools", QJsonObject{{"symbol", "EURUSD"}}},
        {"get_seasonality", QJsonObject{{"symbol", "XAUUSD"}}},
        {"get_fundamental_scores", QJsonObject{}},
        {"get_track_record", QJsonObject{{"trades", 3}}},
        {"get_technical_coverage", QJsonObject{}},
        {"get_technical_data", QJsonObject{}},
        {"get_technical_data", QJsonObject{{"market", "BTCUSDT"}, {"timeframe", "1D"}, {"bars", 5}, {"end", "2020-03-13"}, {"columns", QJsonArray{"rsi14", "sma200"}}}},
        {"get_technical_data", QJsonObject{{"timeframe", "1m"}, {"bars", 200}, {"columns", QJsonArray{"rsi14", "ema20", "ema50", "ema200", "bb_upper", "bb_lower", "macd", "macd_signal", "obv", "vwap"}}}},
        {"get_technical_data", QJsonObject{{"timeframe", "1h"}, {"bars", 3}, {"end", "2018-02-09 12:00"}}},
        {"get_technical_data", QJsonObject{{"timeframe", "3h"}}},
        {"get_technical_data", QJsonObject{{"columns", QJsonArray{"nonsense"}}}},
        {"get_technical_data", QJsonObject{{"market", "ETHUSDT"}}},
        {"get_footprint", QJsonObject{{"timeframe", "15m"}, {"bars", 6}, {"ladder", "last"}}},
        {"get_footprint", QJsonObject{{"venue", "perp"}, {"timeframe", "1h"}, {"end", "2026-11-18 14:00"}, {"bars", 3}}},
        {"get_order_book", QJsonObject{}},
        {"get_order_book", QJsonObject{{"end", "2026-11-18 14:00"}}},
        {"get_open_interest", QJsonObject{{"rows", 6}}},
        {"get_macro_chains", QJsonObject{}},
        {"get_macro_chains", QJsonObject{{"query", "united states inflation"}}},
        {"get_macro_chains", QJsonObject{{"query", "fed"}, {"kind", "implied"}}},
        {"get_macro_facts", QJsonObject{{"chain", "rate:us"}, {"last", 6}}},
        {"get_macro_facts", QJsonObject{{"chain", "nonsense"}}},
    };
    auto results = std::make_shared<QMap<int, QString>>();
    auto left = std::make_shared<int>(int(calls.size()));
    for (int k = 0; k < calls.size(); ++k) {
      const auto* t = ai::Tools::instance().find(calls[k].first);
      const QString head = "### " + calls[k].first + " " + QString::fromUtf8(QJsonDocument(calls[k].second).toJson(QJsonDocument::Compact));
      auto write = [results, left, k, head, outFile](const ai::ToolResult& r) {
        if (results->contains(k)) return;
        results->insert(k, head + (r.ok ? "  [ok]\n" : "  [FAILED]\n") + r.text + "\n");
        if (--*left > 0) return;
        QFile f(outFile);
        if (f.open(QIODevice::WriteOnly)) for (const auto& s : *results) f.write(s.toUtf8() + "\n");
      };
      if (!t) { write({false, "no such tool"}); continue; }
      QTimer::singleShot(40000, qApp, [write] { write({false, "timeout 40 s"}); });
      t->run(calls[k].second, write);
    }
  }
  static void runMlTest(const QString& outFile) {
    // DW_AI_MLTEST_CALLS=<json file>: [["tool", {args}], …] instead of the list below
    QList<QPair<QString, QJsonObject>> custom;
    if (qEnvironmentVariableIsSet("DW_AI_MLTEST_CALLS")) {
      QFile cf(qEnvironmentVariable("DW_AI_MLTEST_CALLS"));
      if (cf.open(QIODevice::ReadOnly))
        for (const auto& v : QJsonDocument::fromJson(cf.readAll()).array()) custom << qMakePair(v.toArray().at(0).toString(), v.toArray().at(1).toObject());
    }
    const QList<QPair<QString, QJsonObject>> calls = !custom.isEmpty() ? custom : QList<QPair<QString, QJsonObject>>{
        {"ml_status", {}},
        {"ml_methods", {}},
        {"ml_methods", QJsonObject{{"family", "reinforcement"}}},
        {"ml_load_dataset", QJsonObject{{"source", "prices"}, {"key", "EURUSD"}, {"interval", "1d"}}},
        {"ml_datasets", {}},
        {"ml_read_dataset", QJsonObject{{"dataset_id", "prices-eurusd-1d"}, {"columns", QJsonArray{"close", "rsi_14"}}, {"last_rows", 3}}},
        {"ml_read_dataset", QJsonObject{{"dataset_id", "../../Preferences/x"}}},
        {"ml_train", QJsonObject{{"dataset_id", "prices-eurusd-1d"}, {"family", "supervised"}, {"algorithm", "kmeans"}, {"target", "close"}}},
        {"ml_train", QJsonObject{{"dataset_id", "prices-eurusd-1d"}, {"family", "supervised"}, {"algorithm", "hist_gb"}, {"target", "closing_price"}}},
        {"ml_train", QJsonObject{{"dataset_id", "prices-eurusd-1d"}, {"family", "supervised"}, {"algorithm", "hist_gb"}, {"target", "close"},
                                 {"features", QJsonArray{"rsi_14", "sentiment"}}}},
        {"ml_train", QJsonObject{{"dataset_id", "prices-eurusd-1d"}, {"family", "supervised"}, {"algorithm", "hist_gb"}, {"target", "close"},
                                 {"params", QJsonObject{{"n_trees", 50}}}}},
        {"ml_train", QJsonObject{{"dataset_id", "prices-eurusd-1d"}, {"family", "supervised"}, {"algorithm", "hist_gb"}, {"target", "close"},
                                 {"target_transform", "direction"}, {"horizon", 5}, {"params", QJsonObject{{"n_estimators", 99999}, {"learning_rate", 0.1}}}}},
        {"ml_train", QJsonObject{{"dataset_id", "prices-eurusd-1d"}, {"family", "unsupervised"}, {"algorithm", "kmeans"}}},
        {"ml_run", QJsonObject{{"run_id", "@last"}, {"wait_seconds", 25}}},
        {"ml_run_rows", QJsonObject{{"run_id", "@last"}, {"last_rows", 3}}},
        {"ml_train", QJsonObject{{"dataset_id", "prices-eurusd-1d"}, {"family", "reinforcement"}, {"algorithm", "q_learning"}, {"target", "close"},
                                 {"params", QJsonObject{{"episodes", 20}}}}},
        {"ml_run", QJsonObject{{"run_id", "@last"}, {"wait_seconds", 25}}},
        {"ml_runs", QJsonObject{{"last", 3}}},
    };
    auto out = std::make_shared<QString>();
    auto lastRun = std::make_shared<QString>();
    // DW_AI_MLTEST_SKIP=<n>: starts at call n (11 = straight to a valid ml_train, before anything checked the environment)
    auto idx = std::make_shared<int>(qEnvironmentVariableIntValue("DW_AI_MLTEST_SKIP"));
    auto next = std::make_shared<std::function<void()>>();
    *next = [=] {
      if (*idx >= calls.size()) {
        QFile f(outFile);
        if (f.open(QIODevice::WriteOnly)) f.write(out->toUtf8());
        return;
      }
      auto [name, args] = calls[(*idx)++];
      if (args.value("run_id").toString() == "@last") args["run_id"] = *lastRun;
      const auto* t = ai::Tools::instance().find(name);
      *out += "### " + name + " " + QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)) + "\n";
      if (!t) { *out += "no such tool\n\n"; QTimer::singleShot(0, qApp, *next); return; }
      ai::currentCaller() = ai::ToolCaller{0, "test"};
      t->run(args, [=](const ai::ToolResult& r) {
        *out += (r.ok ? "[ok] " : "[REFUSED/FAILED] ") + r.text + "\n\n";
        const auto m = QRegularExpression("run (\\d{8}-\\d{6}-[0-9a-f]+)").match(r.text);
        if (m.hasMatch() && name == "ml_train") *lastRun = m.captured(1);
        QTimer::singleShot(0, qApp, *next);
      });
    };
    QTimer::singleShot(qEnvironmentVariableIsSet("DW_AI_MLTEST_SKIP") ? 0 : 3000, qApp, *next);
  }
  static void applyHookSlots() {
    // DW_AI_BLIND="0:12345" makes environment 1 blind with that seed (with evaluation mode; ai/Blind.h), "0:" not
    for (const QString& part : qEnvironmentVariable("DW_AI_BLIND").split(';', Qt::SkipEmptyParts)) {
      const int colon = int(part.indexOf(':'));
      if (colon > 0) ai::Slots::instance().setBlind(part.left(colon).toInt(), part.mid(colon + 1).toUInt());
    }
    // DW_AI_ASOF="0:2026-11-10 00:00;1:" puts environments in evaluation mode at that UTC moment (nothing after the colon: off)
    for (const QString& part : qEnvironmentVariable("DW_AI_ASOF").split(';', Qt::SkipEmptyParts)) {
      const int colon = int(part.indexOf(':'));
      if (colon <= 0) continue;
      QDateTime t = QDateTime::fromString(part.mid(colon + 1).trimmed(), "yyyy-MM-dd HH:mm");
      if (t.isValid()) t.setTimeZone(QTimeZone::UTC);
      ai::Slots::instance().setAsOf(part.left(colon).toInt(), t.isValid() ? t.toSecsSinceEpoch() : 0);
    }
    // DW_AI_SKILLS="2:read_data;3:" grants (or, with nothing after the colon, removes) skills before showing
    for (const QString& part : qEnvironmentVariable("DW_AI_SKILLS").split(';', Qt::SkipEmptyParts)) {
      const int colon = int(part.indexOf(':'));
      if (colon <= 0) continue;
      const int env = part.left(colon).toInt();
      const QStringList want = part.mid(colon + 1).split(',', Qt::SkipEmptyParts);
      for (const auto& sk : ai::skills()) ai::Slots::instance().setSkill(env, sk.id, want.contains(sk.id));
    }
    if (!qEnvironmentVariableIsSet("DW_AI_SLOTS")) return;
    for (const QString& part : qEnvironmentVariable("DW_AI_SLOTS").split(';', Qt::SkipEmptyParts)) {
      const int colon = int(part.indexOf(':'));
      const QString key = part.mid(colon + 1);
      if (colon > 0 && key == QString(ai::claudecode::kRepo) + "/" + ai::claudecode::kFile) {   // "0:integration/claude-code"
        ai::Slots::instance().load(part.left(colon).toInt(), ai::claudecode::kRepo, ai::claudecode::kFile);
        continue;
      }
      const int cut = int(key.indexOf('/', key.indexOf('/') + 1));
      if (colon > 0 && cut > 0) ai::Slots::instance().load(part.left(colon).toInt(), key.left(cut), key.mid(cut + 1));
    }
  }
  QLabel *m_mem, *m_empty, *m_foot = nullptr;
  QWidget* m_left = nullptr;
  QStackedWidget* m_stack = nullptr;
  ChatView* m_chatView = nullptr;
  ModelList* m_list;
  QGridLayout* m_grid;
  QWidget* m_gridHost = nullptr;
  int m_cardMode = 0;   // 0: 2 × 2, 1: four in a row, 2: one column (phones)
  QList<CubeCard*> m_cards;
  PhoneScroll m_scroll;
  QVBoxLayout* m_root;
  QHBoxLayout *m_head, *m_body;
  QScrollArea* m_listScroll;
  bool m_phone = false;
};
}  // namespace
}  // namespace aipage

using AgentsPageImpl = aipage::AgentsPage;
DW_PAGE("ai/agents", AgentsPageImpl)
