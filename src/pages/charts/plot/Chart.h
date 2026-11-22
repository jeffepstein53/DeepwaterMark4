#pragma once
// The chart widget (JS LightweightCharts.createChart(el, options) → IChartApi).
//
//   auto* chart = new plot::Chart(parent);                       // createChart
//   chart->applyOptions([&](plot::ChartOptions& o) { o.layout.fontSize = 11; o.crosshair.mode = plot::CrosshairMode::Normal; });
//   auto* main = chart->addSeries(plot::CandlestickSeries, [&](plot::SeriesOptions& o) { o.upColor = up; });
//   auto* vol = chart->addSeries(plot::HistogramSeries, [&](auto& o) { o.priceScaleId = "vol"; o.priceFormat = plot::PriceFormat::volume(); });
//   chart->priceScale("vol")->applyOptions([](auto& o) { o.scaleMargins = {0.82, 0}; });
//   auto* rsi = chart->addSeries(plot::LineSeries, opts, 1);     // pane 1 is created on demand
//   chart->panes()[1]->setStretchFactor(0.28);
//   chart->subscribeCrosshairMove([&](const plot::MouseEventParams& p) { ... });
//   chart->timeScale()->setVisibleLogicalRange({n - 160.0, n + 8.0});
//
// Panes: pane 0 at the top, others below, separated by 1 px lines that can be dragged (layout.panes.enableResize).
// One time axis at the bottom; one right price axis per pane, all of the same width.
// Interaction (as Lightweight Charts' defaults): wheel = zoom around the cursor, horizontal wheel / trackpad = pan,
// drag in a pane = pan (and vertical scroll when that scale is not autoscaled), drag on the time axis = zoom,
// drag on the price axis = scale (turns autoscale off), double-click on the price axis = autoscale back on,
// double-click on the time axis = reset bar spacing and right offset, drag a separator = resize panes.
#include "pages/charts/plot/Plot.h"
#include "pages/charts/plot/PriceScale.h"
#include "pages/charts/plot/Primitive.h"
#include "pages/charts/plot/Series.h"
#include "pages/charts/plot/TimeScale.h"
#include <QImage>
#include <QMap>
#include <QWidget>
#include <map>
#include <memory>
#include <vector>

namespace charts::plot {

class Pane {
public:
  // JS IPaneApi
  int getHeight() const;
  void setHeight(int height);                 // sets the stretch factor so the pane gets this height
  double getStretchFactor() const { return stretch_; }
  void setStretchFactor(double f);
  int paneIndex() const;
  QVector<Series*> getSeries() const;
  PriceScale* priceScale(const QString& id = "right");
  void attachPrimitive(Primitive* p);         // pane primitive (draws with the pane's right scale), not owned
  void detachPrimitive(Primitive* p);
  void moveTo(int paneIndex);
  void setPreserveEmptyPane(bool on) { preserveEmpty_ = on; }
  bool preserveEmptyPane() const { return preserveEmpty_; }
  QRect rect() const { return rect_; }        // plot area in widget coordinates

private:
  friend class Chart;
  friend class Series;
  explicit Pane(Chart* c) : chart_(c) {}
  Chart* chart_;
  double stretch_ = 1;
  bool preserveEmpty_ = false;
  std::vector<Series*> series_;               // drawing order
  std::map<QString, std::unique_ptr<PriceScale>> scales_;
  std::vector<Primitive*> primitives_;
  QRect rect_;                                // plot area (without the price axis)
};

class Chart : public QWidget {
  Q_OBJECT
public:
  explicit Chart(QWidget* parent = nullptr, const ChartOptions& options = ChartOptions());
  ~Chart() override;

  // ---- options ----
  const ChartOptions& options() const { return o_; }
  void applyOptions(const ChartOptions& o);
  void applyOptions(const std::function<void(ChartOptions&)>& f) { ChartOptions o = o_; f(o); applyOptions(o); }

  // ---- series and panes ----
  Series* addSeries(SeriesType type, const std::function<void(SeriesOptions&)>& init = {}, int paneIndex = 0);
  Series* addSeries(SeriesType type, const SeriesOptions& options, int paneIndex = 0);
  void removeSeries(Series* s);
  QVector<Series*> series() const;
  QVector<Pane*> panes() const;
  Pane* addPane(bool preserveEmptyPane = false);
  void removePane(int index);
  void swapPanes(int first, int second);
  TimeScale* timeScale() { return &ts_; }
  const TimeScale* timeScale() const { return &ts_; }
  PriceScale* priceScale(const QString& id, int paneIndex = 0);
  void remove() { deleteLater(); }

  // ---- events (JS subscribe*/unsubscribe*; the returned id unsubscribes) ----
  int subscribeCrosshairMove(MouseEventHandler h);
  void unsubscribeCrosshairMove(int id) { crossSubs_.remove(id); }
  int subscribeClick(MouseEventHandler h);
  void unsubscribeClick(int id) { clickSubs_.remove(id); }
  int subscribeDblClick(MouseEventHandler h);
  void unsubscribeDblClick(int id) { dblSubs_.remove(id); }

  // ---- crosshair ----
  // Puts the crosshair on `time` (nearest point) at `price` of `series`, as if the mouse were there; does not fire
  // crosshair-move callbacks (so synced charts cannot ping-pong).
  void setCrosshairPosition(double price, Time time, Series* series);
  void clearCrosshairPosition();

  // ---- misc ----
  QImage takeScreenshot(bool addTopLayer = false);   // the whole chart, at the device pixel ratio
  QSize paneSize(int paneIndex = 0) const;
  int priceAxisWidth() const { return priceAxisW_; }
  QFont font() const;                                // layout font
  // Bring layout and scales up to date now (coordinates are valid after this; called by the coordinate getters).
  void ensureLayout();
  QRegion crossRegion() const;   // what the crosshair covers on screen (strips, labels)

  // perf counters (demo)
  qint64 paintCount() const { return paints_; }
  // Repaints the last-value label's second line (SeriesOptions::lastValueSubtitle) and nothing else.
  void refreshCountdown();

protected:
  void paintEvent(QPaintEvent*) override;
  void resizeEvent(QResizeEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void mouseDoubleClickEvent(QMouseEvent*) override;
  void wheelEvent(QWheelEvent*) override;
  bool event(QEvent*) override;   // a trackpad pinch zooms the time scale (QNativeGestureEvent)
  void leaveEvent(QEvent*) override;

private:
  friend class TimeScale;
  friend class Series;
  friend class PriceScale;
  friend class Pane;
  friend class SeriesMarkers;
  friend class PriceLine;
  friend struct PrimitiveContext;

  static void mergeScaleOptions(const PriceScaleOptions& oldDefaults, const PriceScaleOptions& newDefaults, PriceScaleOptions& target);
  void viewChanged();                 // time scale moved / zoomed
  void dataChanged(bool timesChanged);
  void invalidate();                  // repaint everything (layout + base layer)
  // A primitive asked to be drawn again: its content changed, never the series data or the sizes, so the layout and
  // the price scales stand (invalidate() would rerun ensureLayout: up to 3 scale passes per pane, per depth column).
  void repaintBase();
  void rebuildTimePoints();
  Pane* ensurePane(int index);
  void removeEmptyPanes();
  void forgetPane(Pane* p);           // a pane is about to be deleted: drop what the current drag holds of it
  PriceScale* dragScaleNow();         // the dragged pane's scale, found again if that pane was recreated
  void endSeparatorDrag();
  void computeScales(Pane* pane);
  int requiredAxisWidth();
  void paintBase(QImage& img);
  void paintOverlay(QPainter& p);
  void paintPane(QPainter& p, Pane* pane, double dpr);
  void paintSeries(QPainter& p, Pane* pane, Series* s, double dpr, int from, int to);
  void paintMarkers(QPainter& p, Series* s, int from, int to);
  void paintPriceAxis(QPainter& p, Pane* pane, double dpr);
  void paintTimeAxis(QPainter& p, double dpr);
  QVector<TimeScale::Mark> marks_;   // tick marks of the frame being painted
  int marksMaxWeight_ = 0;
  void drawPrimitives(QPainter& p, Pane* pane, PrimitiveZOrder z, double dpr);
  QString formatAxisPrice(const PriceScale* sc, const Series* s, double price) const;

  // hit testing
  enum class Zone { None, Pane, PriceAxis, TimeAxis, Separator };
  Zone zoneAt(const QPoint& pos, int& paneIndex) const;
  MouseEventParams paramsAt(const QPointF& pos, int paneIndex, Qt::KeyboardModifiers mods, Qt::MouseButton b) const;
  void updateCrosshair(const QPointF& pos, int paneIndex, Qt::KeyboardModifiers mods);
  void hideCrosshair();
  void fire(const QMap<int, MouseEventHandler>& subs, const MouseEventParams& p);

  ChartOptions o_;
  TimeScale ts_{this};
  std::vector<std::unique_ptr<Pane>> panes_;
  std::vector<std::unique_ptr<Series>> series_;   // creation order
  bool layoutDirty_ = true, baseDirty_ = true, timesDirty_ = false;
  int priceAxisW_ = 0, timeAxisH_ = 0;
  QImage base_;

  // crosshair
  struct Crosshair {
    bool visible = false;
    int index = 0;                    // logical index (snapped)
    double y = 0;                     // pane coordinate
    int pane = 0;
    double mouseX = 0;
  } cross_;

  // interaction
  enum class Drag { None, Pan, PriceScale, TimeScale, Separator } drag_ = Drag::None;
  QPoint pressPos_;
  bool moved_ = false, dblPress_ = false;
  // The dragged pane is held by pointer (indexes move with swapPanes / moveTo); panes can be removed and recreated
  // during a drag (ChartPane re-renders its indicator panes on live ticks and replay steps): forgetPane() clears
  // these, and dragScaleNow() picks up the pane now at dragPane_ (index at press time).
  int dragPane_ = -1;
  Pane* dragPaneObj_ = nullptr;
  PriceScale* dragScale_ = nullptr;
  int sepIndex_ = -1, hoverSep_ = -1;
  int sepStartA_ = 0, sepStartB_ = 0;

  int nextSub_ = 1;
  QMap<int, MouseEventHandler> crossSubs_, clickSubs_, dblSubs_;
  bool notifyQueued_ = false;
  qint64 paints_ = 0;
  // where paintPriceAxis put the last-value label of the series with a subtitle (widget coordinates), for the overlay
  struct Subtitle { bool on = false; QRectF label; double textX = 0; QColor bg, fg; std::function<QString()> text; };
  Subtitle subtitle_;
  QRect subtitleRect() const;   // the area the second line may cover (the axis, one label high, under the label)
  void paintSubtitle(QPainter& p);
};

}  // namespace charts::plot
