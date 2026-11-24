#pragma once
#include <QWidget>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <functional>


namespace wire::squawk {
// TERMINAL TV: the channels' own YouTube live streams in the official embed player (muted by default),
// hosted in the system web view (NativeTv: WKWebView on macOS).
// The player only exists while the page is visible: hiding the page unloads it (no audio, no CPU).
class TvPane : public QWidget {
  Q_OBJECT
public:
  explicit TvPane(QWidget* parent = nullptr);
  void setPlaying(bool on);        // page shown / hidden
  int preferredHeight(int width, int pageHeight) const;
  int minimumHeightFor() const;     // smallest usable player (120 px of video)
  // No player on screen (the channel has no live stream, no web view, player off): a short message band is
  // enough, the page gives the height to the calendar. onIdleChanged asks the page to lay out again.
  bool idle() const;
  int idleHeight() const;
  std::function<void()> onIdleChanged;
  // Scroll mode (phones): the page's visible area in window coordinates; the native player hides while it is not
  // wholly inside it (it would float over the header). syncView() after the page scrolled.
  std::function<QRect()> viewClip;
  void syncView() { layoutView(); }
protected:
  void paintEvent(QPaintEvent*) override;
  void resizeEvent(QResizeEvent*) override;
  void moveEvent(QMoveEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void leaveEvent(QEvent*) override;
private:
  struct Channel { QString name, shortName, letter, id; };
  void load();
  void layoutView();
  QRectF videoArea() const;
  QList<QRectF> buttonRects(bool* compact = nullptr) const;
  QRectF openRect() const;
  QList<Channel> m_channels;
  int m_cur = 0, m_hover = -1;
  bool m_playing = false;
  QString m_state;                 // "", "playing", "error:150" …
  QSet<int> m_noLive;              // channels whose last load ended in a player error (kept across visits)
  void setNoLive(int channel, bool on);
  class NativeTv* m_tv = nullptr;
  bool m_clipped = false;          // hidden by viewClip
  // No player yet: load() tries again each second, 15 times at most (Windows: the loopback server that serves the
  // player page could not start yet, e.g. no free port for a moment)
  QTimer m_wait;
  int m_waits = 0;
  qreal titleRight() const;
};
}
