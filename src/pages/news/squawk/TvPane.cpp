#include "pages/news/squawk/TvPane.h"
#include "pages/news/squawk/SquawkLook.h"
#include "pages/news/squawk/NativeTv.h"
#include "core/Settings.h"
#include "net/Sources.h"
#include "ui/Theme.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QUrl>

namespace wire::squawk {
static const sources::Registrar kYouTube({"youtube-embed", "YouTube (channels' own live streams)",
  {"www.youtube.com", "www.youtube-nocookie.com"}, sources::Access::Keyless, 1000,
  "Official embed player of the channels' live streams (Bloomberg Television, Yahoo Finance, CNBC Television)",
  "YouTube Terms / API Services policies allow embedding public videos with the official player; nothing is "
  "downloaded or re-hosted by the app; loaded only while News › Squawk is visible",
  {"news/squawk"}});

static constexpr int kTitleH = 36, kVideoTop = 40, kCaptionH = 33;

TvPane::TvPane(QWidget* parent) : QWidget(parent) {
  m_channels = {
    {"Bloomberg Television", "Bloomberg", "B", "UCIALMKvObZNtJ6AmdCLP7Lg"},
    {"Yahoo Finance", "Yahoo", "Y", "UCEAZeUIeJs0IjQiqTCdVSIg"},
    {"CNBC Television", "CNBC", "C", "UCrp_UI8XtuYfpiqluWLD7Lw"},
  };
  m_cur = qBound(0, settings::value("news.squawk.tvChannel", 0).toInt(), int(m_channels.size()) - 1);
  setMouseTracking(true);
}

int TvPane::preferredHeight(int w, int pageH) const {
  // the reference: 310 px of video for a 665 px column; grows with the column up to 42 % of the page height
  const int video = qBound(100, int(w * 310.0 / 665.0), qMax(310, int(pageH * 0.42)));
  return kVideoTop + video + kCaptionH;
}
int TvPane::minimumHeightFor() const { return kVideoTop + 100 + kCaptionH; }
bool TvPane::idle() const {
  if (qEnvironmentVariableIsSet("DW_SQUAWK_NO_TV")) return true;
  if (m_tv && !m_tv->available()) return true;
  return m_noLive.contains(m_cur);
}
int TvPane::idleHeight() const { return kVideoTop + 76 + kCaptionH; }   // the message on up to three lines
void TvPane::setNoLive(int channel, bool on) {
  const bool was = idle();
  if (on) m_noLive.insert(channel); else m_noLive.remove(channel);
  if (idle() != was && onIdleChanged) onIdleChanged();
}
QRectF TvPane::videoArea() const { return QRectF(0, kVideoTop, width(), height() - kVideoTop - kCaptionH); }

QList<QRectF> TvPane::buttonRects(bool* compact) const {
  const QFont f = theme::condensed(12, QFont::DemiBold);
  const QFontMetricsF fm(f);
  const qreal x0 = titleRight() + 12, gap = 7, right = openRect().left() - 10;
  for (int pass = 0; pass < 2; ++pass) {
    QList<QRectF> out; qreal x = x0;
    for (const auto& c : m_channels) {
      const qreal w = fm.horizontalAdvance(pass == 0 ? c.name : c.shortName) + 30;
      out << QRectF(x, 6, w, 25); x += w + gap;
    }
    if (out.last().right() <= right || pass == 1) { if (compact) *compact = pass == 1; return out; }
  }
  return {};
}
qreal TvPane::titleRight() const { return 25 + QFontMetricsF(theme::mono(12, QFont::ExtraBold, 1.9)).horizontalAdvance(QCoreApplication::translate("wire", "TERMINAL TV")); }
QRectF TvPane::openRect() const {
  const bool narrow = width() < 620;
  const qreal w = narrow ? 34 : QFontMetricsF(theme::condensed(12, QFont::Medium)).horizontalAdvance(QCoreApplication::translate("wire", "Open on YouTube")) + 34;
  return QRectF(width() - 8 - w, 6, w, 25);
}

void TvPane::paintEvent(QPaintEvent*) {
  layoutView();   // ancestors may have moved (window layout): keep the native view on the black area
  const auto& L = look();
  QPainter p(this);
  p.fillRect(rect(), L.page);
  p.fillRect(QRect(0, 0, width(), kTitleH + 2), L.head);
  p.fillRect(QRect(0, height() - kCaptionH, width(), kCaptionH), theme::map("#0c0805"));
  p.fillRect(videoArea().toRect(), theme::map("#000000"));
  p.setRenderHint(QPainter::Antialiasing);
  // ▭ TERMINAL TV
  p.setPen(QPen(theme::map("#9a7a22"), 1.3)); p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(QRectF(7, 13, 12, 9), 1.5, 1.5);
  p.drawLine(QPointF(10, 10.5), QPointF(13, 13)); p.drawLine(QPointF(16, 10.5), QPointF(13, 13));
  p.setFont(theme::mono(12, QFont::ExtraBold, 1.9)); p.setPen(theme::map("#d5d3cd"));
  p.drawText(QPointF(25, 23), QCoreApplication::translate("wire", "TERMINAL TV"));
  // channel buttons
  bool compact = false;
  const auto rs = buttonRects(&compact);
  const QFont f = theme::condensed(12, QFont::DemiBold);
  for (int i = 0; i < rs.size(); ++i) {
    const QRectF r = rs[i];
    const bool on = i == m_cur, hov = i == m_hover;
    p.setPen(QPen(on ? theme::map("#6e5512") : hov ? theme::map("#4a4030") : theme::map("#2a2418"), 1));
    p.setBrush(on ? theme::map("#3e2e02") : theme::map("#0c0805"));
    p.drawRoundedRect(r.adjusted(.5, .5, -.5, -.5), 2, 2);
    const QRectF badge(r.left() + 9, r.center().y() - 6.5, 13, 13);
    if (on) { p.setPen(Qt::NoPen); p.setBrush(theme::map("#f2c418")); p.drawRoundedRect(badge, 2.5, 2.5); }
    p.setFont(theme::mono(9, QFont::Bold)); p.setPen(on ? (theme::isDefault() ? QColor("#3a2a00") : theme::onAccent()) : theme::map("#8d8a82"));
    p.drawText(badge, Qt::AlignCenter, m_channels[i].letter);
    p.setFont(f); p.setPen(on ? theme::map("#e1bd4a") : hov ? theme::map("#e6e4dc") : theme::map("#bfbeb7"));
    p.drawText(QRectF(r.left() + 27, r.top(), r.width() - 27, r.height()), Qt::AlignVCenter | Qt::AlignLeft,
               compact ? m_channels[i].shortName : m_channels[i].name);
  }
  // ⧉ Open on YouTube
  const QRectF o = openRect();
  p.setPen(QPen(m_hover == 99 ? theme::map("#4a4030") : theme::map("#2a2418"), 1)); p.setBrush(theme::map("#0c0805"));
  p.drawRoundedRect(o.adjusted(.5, .5, -.5, -.5), 2, 2);
  p.setPen(QPen(theme::map("#7b776f"), 1.1)); p.setBrush(Qt::NoBrush);
  const QPointF ic(o.left() + 14, o.center().y());
  p.drawRect(QRectF(ic.x() - 4.5, ic.y() - 3, 7.5, 7.5));
  p.drawLine(ic + QPointF(-0.5, 0.5), ic + QPointF(4.5, -4.5));
  p.drawLine(ic + QPointF(1.5, -4.5), ic + QPointF(4.5, -4.5)); p.drawLine(ic + QPointF(4.5, -4.5), ic + QPointF(4.5, -1.5));
  if (o.width() > 50) {
    p.setFont(theme::condensed(12, QFont::Medium)); p.setPen(m_hover == 99 ? theme::map("#b3afa5") : theme::map("#7b776f"));
    p.drawText(o.adjusted(25, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, QCoreApplication::translate("wire", "Open on YouTube"));
  }
  // state over the black area when the player is not there / not live
  if (!m_tv || !m_tv->isVisible()) {
    p.setFont(theme::mono(11, QFont::Normal, 1)); p.setPen(theme::map("#6d6a62"));
    QString msg;
    if (m_tv && !m_tv->available()) msg = QCoreApplication::translate("wire", "The live player needs the system web view, and on Windows Charts' local API (Charts settings).\nUse Open on YouTube.");
    else if (m_state.startsWith("error")) msg = QCoreApplication::translate("wire", "%1 has no live stream on YouTube right now.\nTry another channel, or Open on YouTube.").arg(m_channels[m_cur].name);
    else if (!m_playing) msg = qEnvironmentVariableIsSet("DW_SQUAWK_NO_TV") ? QStringLiteral("Player off (DW_SQUAWK_NO_TV)") : QCoreApplication::translate("wire", "Paused while the page is hidden");
    else msg = QCoreApplication::translate("wire", "Connecting to %1…").arg(m_channels[m_cur].name);
    p.drawText(videoArea().adjusted(20, 0, -20, 0), Qt::AlignCenter | Qt::TextWordWrap, msg);
  }
  // caption
  p.setFont(theme::sans(10)); p.setPen(theme::isDefault() ? theme::map("#3d382f") : theme::roles().textFaint);   // the measured caption is too faint once mapped
  p.drawText(QRectF(8, height() - kCaptionH + 10, width() - 16, 14), Qt::AlignLeft | Qt::AlignVCenter,
             QFontMetricsF(p.font()).elidedText(QCoreApplication::translate("wire", "Live when the network is broadcasting · powered by each channel's own YouTube stream, not hosted by this app."),
                                                Qt::ElideRight, width() - 16));
}

void TvPane::layoutView() {
  if (!m_tv || !m_tv->available()) return;
  const QRectF a = videoArea();
  const qreal w = qMin(a.width(), a.height() * 1.70);   // the reference's player box (≈527x310)
  const QRect local(int(a.center().x() - w / 2), int(a.top()), int(w), int(a.height()));
  const QRect g(mapTo(window(), local.topLeft()), local.size());
  m_tv->setGeometry(g);
  const bool out = viewClip && !viewClip().contains(g);
  if (out != m_clipped) {
    m_clipped = out;
    if (m_playing && !m_state.startsWith("error")) { m_tv->setVisible(!out); update(); }
  }
}
void TvPane::resizeEvent(QResizeEvent*) { layoutView(); }
void TvPane::moveEvent(QMoveEvent*) { layoutView(); }

void TvPane::setPlaying(bool on) {
  if (on == m_playing) return;
  m_playing = on;
  if (on) load();
  else if (m_tv) { m_tv->clear(); m_tv->setVisible(false); m_state.clear(); }
  update();
}

void TvPane::load() {
  if (!m_playing) return;
  if (!m_tv) {
    m_tv = new NativeTv(this);
    m_tv->onMessage = [this](const QString& t) {
      if (t.startsWith("error:")) { m_state = t; m_tv->setVisible(false); setNoLive(m_cur, true); update(); }
      else if (t.startsWith("state:")) { m_state = t == "state:1" ? "playing" : t; if (t == "state:1") setNoLive(m_cur, false); }
    };
    layoutView();
  }
  m_state.clear();
  if (!m_tv->available()) { update(); if (onIdleChanged) onIdleChanged(); return; }
  const QString id = m_channels[m_cur].id;
  const QString html = QString(R"(<!doctype html><html><head><meta charset="utf-8">
<style>html,body{margin:0;height:100%;background:#000;overflow:hidden}iframe{position:absolute;left:0;top:0;width:100%;height:100%;border:0}</style>
</head><body><iframe id="v" allow="autoplay; encrypted-media; picture-in-picture" allowfullscreen
 src="https://www.youtube.com/embed/live_stream?channel=%1&autoplay=1&mute=1&playsinline=1&rel=0&enablejsapi=1&origin=%2"></iframe>
<script>
var dwN = 0;
function send(s){ try { window.webkit.messageHandlers.dw.postMessage(s); return; } catch (e) {} document.title = 'dw' + (++dwN) + ':' + s; }
function onYouTubeIframeAPIReady(){ new YT.Player('v',{events:{
  onReady:function(e){ e.target.mute(); e.target.playVideo(); send('ready'); },
  onStateChange:function(e){ send('state:'+e.data); },
  onError:function(e){ send('error:'+e.data); } }}); }
</script>
<script src="https://www.youtube.com/iframe_api"></script></body></html>)").arg(id, QString::fromLatin1(QUrl::toPercentEncoding(m_tv->origin())));
  m_tv->loadHtml(html, QUrl("https://deepwater-terminal.com/tv/"));
  m_tv->setVisible(!m_clipped);
  layoutView();
  update();
}

void TvPane::mousePressEvent(QMouseEvent* e) {
  const auto rs = buttonRects();
  for (int i = 0; i < rs.size(); ++i)
    if (rs[i].contains(e->position()) && i != m_cur) {
      const bool was = idle();
      m_cur = i;
      settings::setValue("news.squawk.tvChannel", i);
      load();
      if (idle() != was && onIdleChanged) onIdleChanged();
      update();
      return;
    }
  if (openRect().contains(e->position()))
    QDesktopServices::openUrl(QUrl("https://www.youtube.com/channel/" + m_channels[m_cur].id + "/live"));
}
void TvPane::mouseMoveEvent(QMouseEvent* e) {
  int h = -1;
  const auto rs = buttonRects();
  for (int i = 0; i < rs.size(); ++i) if (rs[i].contains(e->position())) h = i;
  if (openRect().contains(e->position())) h = 99;
  if (h != m_hover) {
    m_hover = h; update(0, 0, width(), kTitleH);
    setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
  }
  if (h == 99) QToolTip::showText(e->globalPosition().toPoint(), QCoreApplication::translate("wire", "Open the live page of %1 in your browser").arg(m_channels[m_cur].name), this);
  else if (e->position().y() > height() - kCaptionH)   // the caption may be elided in narrow columns
    QToolTip::showText(e->globalPosition().toPoint(), QCoreApplication::translate("wire", "Live when the network is broadcasting · powered by each channel's own YouTube stream, not hosted by this app."), this);
}
void TvPane::leaveEvent(QEvent*) { if (m_hover != -1) { m_hover = -1; update(0, 0, width(), kTitleH); } }
}
