#include <QNativeGestureEvent>
#include "pages/charts/plot/Chart.h"
#include "pages/charts/plot/Format.h"
#include "ui/Theme.h"
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>

namespace charts::plot {

// ================= PrimitiveContext =================

TimeScale* PrimitiveContext::timeScale() const { return chart ? chart->timeScale() : nullptr; }

std::optional<double> PrimitiveContext::priceToY(double price) const {
  if (series) return series->priceToCoordinate(price);
  if (!chart) return std::nullopt;
  PriceScale* sc = chart->priceScale("right", paneIndex);
  return sc ? sc->priceToY(price, kNaN) : std::nullopt;
}

std::optional<double> PrimitiveContext::logicalToX(double logical) const {
  return chart ? chart->timeScale()->logicalToCoordinate(logical) : std::nullopt;
}

// ================= Pane =================

int Pane::paneIndex() const {
  for (size_t i = 0; i < chart_->panes_.size(); ++i)
    if (chart_->panes_[i].get() == this) return int(i);
  return -1;
}

int Pane::getHeight() const {
  chart_->ensureLayout();
  return rect_.height();
}

void Pane::setHeight(int h) {
  chart_->ensureLayout();
  int avail = 0;
  double others = 0;
  for (const auto& p : chart_->panes_) {
    avail += p->rect_.height();
    if (p.get() != this) others += p->stretch_;
  }
  if (chart_->panes_.size() <= 1 || avail <= 0) return;
  h = qBound(2, h, avail - 2);
  stretch_ = others * h / qMax(1, avail - h);
  chart_->invalidate();
}

void Pane::setStretchFactor(double f) {
  if (f == stretch_) return;
  stretch_ = qMax(1e-6, f);
  chart_->invalidate();
}

QVector<Series*> Pane::getSeries() const { return QVector<Series*>(series_.begin(), series_.end()); }

PriceScale* Pane::priceScale(const QString& id) {
  auto it = scales_.find(id);
  if (it != scales_.end()) return it->second.get();
  const ChartOptions& o = chart_->o_;
  PriceScaleOptions def = id == "right" ? o.rightPriceScale : id == "left" ? o.leftPriceScale : o.overlayPriceScales;
  if ((id == "right" || id == "left") && !chart_->panes_.empty() && chart_->panes_[0].get() != this)
    if (auto f = chart_->panes_[0]->scales_.find(id); f != chart_->panes_[0]->scales_.end()) {
      def = f->second->o_;   // shared options (mode, colours ...) of the right / left scale
      def.autoScale = true;
    }
  auto* sc = new PriceScale(chart_, this, id, def);
  scales_.emplace(id, std::unique_ptr<PriceScale>(sc));
  return sc;
}

void Pane::attachPrimitive(Primitive* p) {
  if (!p || std::find(primitives_.begin(), primitives_.end(), p) != primitives_.end()) return;
  primitives_.push_back(p);
  Chart* c = chart_;
  p->attached({c, nullptr, [c] { c->repaintBase(); }});
  chart_->invalidate();
}

void Pane::detachPrimitive(Primitive* p) {
  auto it = std::find(primitives_.begin(), primitives_.end(), p);
  if (it == primitives_.end()) return;
  primitives_.erase(it);
  p->detached();
  chart_->invalidate();
}

void Pane::moveTo(int index) {
  auto& v = chart_->panes_;
  const int from = paneIndex();
  index = qBound(0, index, int(v.size()) - 1);
  if (from < 0 || from == index) return;
  auto keep = std::move(v[from]);
  v.erase(v.begin() + from);
  v.insert(v.begin() + index, std::move(keep));
  chart_->invalidate();
}

// ================= Chart: setup =================

Chart::Chart(QWidget* parent, const ChartOptions& options) : QWidget(parent), o_(options) {
  setMouseTracking(true);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setFocusPolicy(Qt::ClickFocus);
  ts_.o_ = o_.timeScale;
  ts_.spacing_ = o_.timeScale.barSpacing;
  ts_.rightOffset_ = o_.timeScale.rightOffset;
  ensurePane(0);
}

Chart::~Chart() {
  for (auto& p : panes_)
    for (Primitive* pr : p->primitives_) pr->detached();
  series_.clear();
}

QFont Chart::font() const {
  const int fs = o_.layout.fontSize;
  QFont f = o_.layout.fontFamily.isEmpty() ? theme::mono(fs) : QFont(o_.layout.fontFamily);
  f.setPixelSize(fs);
  return f;
}

// Copy into `target` every field that differs between the old and new chart-level defaults.
void Chart::mergeScaleOptions(const PriceScaleOptions& a, const PriceScaleOptions& b, PriceScaleOptions& t) {
#define DW_M(f) if (!(a.f == b.f)) t.f = b.f;
  DW_M(autoScale) DW_M(mode) DW_M(invertScale) DW_M(alignLabels) DW_M(borderVisible) DW_M(borderColor)
  DW_M(textColor) DW_M(entireTextOnly) DW_M(visible) DW_M(ticksVisible) DW_M(minimumWidth) DW_M(tickMarkDensity)
  DW_M(scaleMargins.top) DW_M(scaleMargins.bottom)
}

static void mergeTimeOptions(const TimeScaleOptions& a, const TimeScaleOptions& b, TimeScaleOptions& t) {
  DW_M(rightOffset) DW_M(barSpacing) DW_M(minBarSpacing) DW_M(maxBarSpacing) DW_M(minVisibleBars) DW_M(fixLeftEdge) DW_M(fixRightEdge)
  DW_M(lockVisibleTimeRangeOnResize) DW_M(rightBarStaysOnScroll) DW_M(borderVisible) DW_M(borderColor) DW_M(visible)
  DW_M(timeVisible) DW_M(secondsVisible) DW_M(shiftVisibleRangeOnNewBar) DW_M(ticksVisible) DW_M(allowBoldLabels)
  DW_M(minimumHeight)
#undef DW_M
  if (b.tickMarkFormatter || a.tickMarkFormatter) t.tickMarkFormatter = b.tickMarkFormatter;
}

void Chart::applyOptions(const ChartOptions& o) {
  const ChartOptions old = o_;
  o_ = o;
  for (auto& p : panes_)
    for (auto& [id, sc] : p->scales_) {
      if (id == "right") mergeScaleOptions(old.rightPriceScale, o.rightPriceScale, sc->o_);
      else if (id == "left") mergeScaleOptions(old.leftPriceScale, o.leftPriceScale, sc->o_);
      else mergeScaleOptions(old.overlayPriceScales, o.overlayPriceScales, sc->o_);
    }
  TimeScaleOptions t = ts_.o_;
  mergeTimeOptions(old.timeScale, o.timeScale, t);
  ts_.applyOptions(t);   // syncs o_.timeScale back
  if (!o_.handleScroll.pressedMouseMove || !o_.handleScale.axisPressedMouseMove.price) drag_ = Drag::None;
  invalidate();
}

// ================= series and panes =================

Pane* Chart::ensurePane(int index) {
  index = qMax(0, index);
  while (int(panes_.size()) <= index) {
    panes_.push_back(std::unique_ptr<Pane>(new Pane(this)));
    panes_.back()->priceScale("right");
  }
  return panes_[index].get();
}

Series* Chart::addSeries(SeriesType type, const std::function<void(SeriesOptions&)>& init, int paneIndex) {
  SeriesOptions o = seriesDefaults(type);
  if (init) init(o);
  return addSeries(type, o, paneIndex);
}

Series* Chart::addSeries(SeriesType type, const SeriesOptions& options, int paneIndex) {
  Pane* p = ensurePane(paneIndex);
  auto* s = new Series(this, type, options, p);
  series_.push_back(std::unique_ptr<Series>(s));
  p->series_.push_back(s);
  p->priceScale(options.priceScaleId);
  timesDirty_ = true;
  invalidate();
  return s;
}

void Chart::removeSeries(Series* s) {
  auto it = std::find_if(series_.begin(), series_.end(), [&](const auto& p) { return p.get() == s; });
  if (it == series_.end()) return;
  if (Pane* p = s->pane_) p->series_.erase(std::remove(p->series_.begin(), p->series_.end(), s), p->series_.end());
  series_.erase(it);   // deletes; primitives get detached()
  removeEmptyPanes();
  timesDirty_ = true;
  invalidate();
}

QVector<Series*> Chart::series() const {
  QVector<Series*> out;
  for (const auto& s : series_) out.append(s.get());
  return out;
}

QVector<Pane*> Chart::panes() const {
  QVector<Pane*> out;
  for (const auto& p : panes_) out.append(p.get());
  return out;
}

Pane* Chart::addPane(bool preserveEmptyPane) {
  Pane* p = ensurePane(int(panes_.size()));
  p->preserveEmpty_ = preserveEmptyPane;
  invalidate();
  return p;
}

// Panes left without series disappear (pane 0 stays), as in Lightweight Charts v5.
void Chart::removeEmptyPanes() {
  for (int i = int(panes_.size()) - 1; i >= 1; --i) {
    Pane* p = panes_[i].get();
    if (p->series_.empty() && p->primitives_.empty() && !p->preserveEmpty_) {
      forgetPane(p);
      panes_.erase(panes_.begin() + i);
    }
  }
}

void Chart::forgetPane(Pane* p) {
  if (!p || p != dragPaneObj_) return;
  dragPaneObj_ = nullptr;
  dragScale_ = nullptr;   // owned by the pane: gone with it
}

PriceScale* Chart::dragScaleNow() {
  if (dragScale_) return dragScale_;
  if (drag_ != Drag::Pan && drag_ != Drag::PriceScale) return nullptr;
  if (dragPane_ < 0 || dragPane_ >= int(panes_.size())) return nullptr;
  // the pane was recreated at the same place: carry on with the new one, from the press point
  dragPaneObj_ = panes_[dragPane_].get();
  dragScale_ = dragPaneObj_->priceScale("right");
  const double y = pressPos_.y() - dragPaneObj_->rect_.top();
  if (drag_ == Drag::PriceScale) dragScale_->startScale(y);
  else dragScale_->startScroll(y);
  return dragScale_;
}

void Chart::endSeparatorDrag() {
  drag_ = Drag::None;
  sepIndex_ = -1;
  if (hoverSep_ != -1) { hoverSep_ = -1; baseDirty_ = true; update(); }
}

void Chart::removePane(int index) {
  if (index < 0 || index >= int(panes_.size())) return;
  Pane* p = panes_[index].get();
  const std::vector<Series*> doomed = p->series_;
  for (Series* s : doomed) {
    p->series_.clear();
    auto it = std::find_if(series_.begin(), series_.end(), [&](const auto& q) { return q.get() == s; });
    if (it != series_.end()) series_.erase(it);
  }
  for (Primitive* pr : p->primitives_) pr->detached();
  p->primitives_.clear();
  if (panes_.size() > 1) {
    forgetPane(p);
    panes_.erase(panes_.begin() + index);
  }
  timesDirty_ = true;
  invalidate();
}

void Chart::swapPanes(int a, int b) {
  if (a < 0 || b < 0 || a >= int(panes_.size()) || b >= int(panes_.size()) || a == b) return;
  std::swap(panes_[a], panes_[b]);
  invalidate();
}

PriceScale* Chart::priceScale(const QString& id, int paneIndex) {
  if (id != "right" && id != "left") {   // an overlay id lives in the pane of the first series using it
    for (const auto& p : panes_)
      if (p->scales_.count(id)) return p->scales_.at(id).get();
  }
  return ensurePane(paneIndex)->priceScale(id);
}

QSize Chart::paneSize(int i) const {
  const_cast<Chart*>(this)->ensureLayout();
  if (i < 0 || i >= int(panes_.size())) return {};
  return panes_[i]->rect_.size();
}

// ================= invalidation =================

void Chart::invalidate() {
  layoutDirty_ = true;
  baseDirty_ = true;
  update();
}

void Chart::repaintBase() {
  baseDirty_ = true;
  update();
}

void Chart::dataChanged(bool timesChanged) {
  if (timesChanged) timesDirty_ = true;
  invalidate();
  viewChanged();
}

void Chart::viewChanged() {
  invalidate();
  if (notifyQueued_) return;
  notifyQueued_ = true;
  QTimer::singleShot(0, this, [this] {
    notifyQueued_ = false;
    ensureLayout();
    ts_.notify();
  });
}

void Chart::rebuildTimePoints() {
  timesDirty_ = false;
  // Every series' rows are strictly ascending (Series::setData / update keep them so). Indicators almost always
  // sit on the price series' times (or a part of them): start from the longest series and merge in only the series
  // that bring times of their own, instead of sorting every row of every series at each indicator render.
  QVector<Time> all;
  const Series* ref = nullptr;
  for (const auto& s : series_)
    if (!ref || s->rows_.size() > ref->rows_.size()) ref = s.get();
  if (ref) {
    all.reserve(int(ref->rows_.size()));
    for (const auto& r : ref->rows_) all.append(r.time);
  }
  QVector<Time> merged;
  for (const auto& s : series_) {
    if (s.get() == ref || s->rows_.empty()) continue;
    int k = 0;
    bool subset = true;
    for (const auto& r : s->rows_) {
      while (k < all.size() && all[k] < r.time) ++k;
      if (k == all.size() || all[k] != r.time) { subset = false; break; }
    }
    if (subset) continue;
    merged.clear();
    merged.reserve(all.size() + int(s->rows_.size()));
    int i = 0;
    for (const auto& r : s->rows_) {
      while (i < all.size() && all[i] < r.time) merged.append(all[i++]);
      if (i < all.size() && all[i] == r.time) ++i;
      merged.append(r.time);
    }
    while (i < all.size()) merged.append(all[i++]);
    all.swap(merged);
  }
  // index of every row on the new scale (rows and times are both sorted: one merge walk per series)
  for (const auto& s : series_) {
    int k = 0;
    for (auto& r : s->rows_) {
      while (k < all.size() && all[k] < r.time) ++k;
      r.idx = k;
    }
  }
  if (all != ts_.times_) ts_.setPoints(std::move(all));
}

// ================= layout =================

void Chart::ensureLayout() {
  if (timesDirty_) rebuildTimePoints();
  if (!layoutDirty_) return;
  static thread_local int depth = 0;
  if (depth > 0) return;
  ++depth;
  layoutDirty_ = false;
  baseDirty_ = true;
  const int W = width(), H = height(), fs = o_.layout.fontSize;
  timeAxisH_ = 0;
  if (ts_.o_.visible) {
    const int h = int(std::ceil(1 + 5 + fs + 3.0 * fs / 12 * 2 + 4.0 * fs / 12));
    timeAxisH_ = qMax(fmt::makeEven(h), ts_.o_.minimumHeight);
  }
  const int n = int(panes_.size());
  const int avail = qMax(0, H - timeAxisH_ - (n - 1));
  double sum = 0;
  for (const auto& p : panes_) sum += p->stretch_;
  if (sum <= 0) sum = 1;
  QVector<int> heights(n);
  int used = 0;
  for (int i = 0; i < n; ++i) {
    heights[i] = i == n - 1 ? avail - used : int(std::lround(panes_[i]->stretch_ / sum * avail));
    heights[i] = qMax(heights[i], qMin(2, avail));
    used += heights[i];
  }
  const bool rightVisible = panes_.empty() ? true : panes_[0]->priceScale("right")->o_.visible;
  int axisW = rightVisible ? (priceAxisW_ > 0 ? priceAxisW_ : 60) : 0;
  for (int iter = 0; iter < 3; ++iter) {
    const int plotW = qMax(0, W - axisW);
    ts_.setWidth(plotW, timeAxisH_);
    int y = 0;
    for (int i = 0; i < n; ++i) {
      panes_[i]->rect_ = QRect(0, y, plotW, heights[i]);
      y += heights[i] + 1;
    }
    for (auto& p : panes_) computeScales(p.get());
    const int need = rightVisible ? requiredAxisWidth() : 0;
    if (need == axisW) break;
    axisW = need;
  }
  priceAxisW_ = axisW;
  if (isVisible()) ts_.pendingRange_.reset();   // applied with the final width
  --depth;
}

void Chart::computeScales(Pane* pane) {
  int from = 0, to = -1;
  const bool vis = ts_.visibleIndexes(from, to);
  const int fs = o_.layout.fontSize;
  for (auto& [id, scp] : pane->scales_) {
    PriceScale* sc = scp.get();
    sc->height_ = pane->rect_.height();
    QVector<Series*> list;
    for (Series* s : pane->series_)
      if (s->o_.priceScaleId == id) list.append(s);
    sc->formatSource_ = nullptr;
    for (Series* s : list)
      if (s->o_.visible) { sc->formatSource_ = s; break; }
    if (!sc->formatSource_ && !list.isEmpty()) sc->formatSource_ = list.first();
    // base value of the relative modes: each series' first visible value that is not 0 (a percentage of 0 is
    // undefined: every point of a series starting at 0 — a histogram, an oscillator — sat flat on 0 %)
    for (Series* s : list) {
      s->firstValue_ = kNaN;
      if (!vis) continue;
      for (int k = s->rowAtOrAfter(from); k < int(s->rows_.size()) && s->rows_[k].idx <= to; ++k)
        if ((s->rows_[k].flags & Series::HasValue) && s->rows_[k].c != 0 && std::isfinite(s->rows_[k].c)) {
          s->firstValue_ = s->rows_[k].c;
          break;
        }
    }
    // Logarithmic is set for the "right" scale of every pane: a pane showing a value <= 0 (MACD, CCI, momentum, a
    // histogram's 0 base) cannot be drawn in log and falls back to Normal while such a value is visible
    {
      bool off = false;
      if (sc->o_.mode == PriceScaleMode::Logarithmic && vis)
        for (Series* s : list) {
          if (!s->o_.visible || s->o_.noAutoscale) continue;
          const bool bars = s->isBarType();
          bool any = false;
          for (int k = s->rowAtOrAfter(from); !off && k < int(s->rows_.size()) && s->rows_[k].idx <= to; ++k) {
            const Series::Row& r = s->rows_[k];
            if (!(r.flags & Series::HasValue)) continue;
            any = true;
            off = bars ? !(r.l > 0) || !(r.h > 0) : !(r.c > 0);
          }
          if (any && s->type_ == SeriesType::Histogram && !(s->o_.base > 0)) off = true;
          if (off) break;
        }
      if (off != sc->logOff_) {
        sc->logOff_ = off;
        sc->o_.autoScale = true;   // a manual range in the other units means nothing now (as a mode change)
      }
    }
    const bool needAuto = sc->o_.autoScale || !sc->hasRange_;
    if (needAuto && vis) {
      double lo = std::numeric_limits<double>::infinity(), hi = -lo;
      double marginAbove = 0, marginBelow = 0;
      const bool log = sc->isLog();
      for (Series* s : list) {
        if (!s->o_.visible || s->o_.noAutoscale) continue;
        const double base = s->firstValue_;
        const bool bars = s->isBarType();
        auto take = [&](double v) {
          if (log && !(v > 0)) return;
          const double u = sc->toInternal(v, base);
          if (u < lo) lo = u;
          if (u > hi) hi = u;
        };
        bool any = false;
        for (int k = s->rowAtOrAfter(from); k < int(s->rows_.size()) && s->rows_[k].idx <= to; ++k) {
          const Series::Row& r = s->rows_[k];
          if (!(r.flags & Series::HasValue)) continue;
          any = true;
          if (bars) { take(r.l); take(r.h); }
          else take(r.c);
        }
        if (any && s->type_ == SeriesType::Histogram) take(s->o_.base);
        if (!s->markers_.isEmpty()) {   // room for the markers (as the markers plugin's autoscale margins)
          const double sp = ts_.spacing_;
          auto ceilOdd = [](double v) { int c = int(std::ceil(v)); return c % 2 ? c : c + 1; };
          int size = ceilOdd(qBound(12.0, sp, 30.0));
          if (size % 2) size -= 1;
          const double margin = qMax(ceilOdd(qBound(12.0, sp, 30.0) * 0.1), 3);
          const double need = 1.5 * size + 2 * margin;
          bool ab = false, be = false, in = false;
          for (const auto& m : s->markers_) {
            ab |= m.position == MarkerPosition::AboveBar;
            be |= m.position == MarkerPosition::BelowBar;
            in |= m.position == MarkerPosition::InBar;
          }
          marginAbove = qMax(marginAbove, ab ? need : in ? std::ceil(need / 2) : 0);
          marginBelow = qMax(marginBelow, be ? need : in ? std::ceil(need / 2) : 0);
        }
      }
      if (lo <= hi) {
        if (lo == hi) {
          const double d = sc->isRelative() || log ? 0.5 : (lo != 0 ? std::fabs(lo) * 0.005 : 0.5);
          lo -= d;
          hi += d;
        }
        if (marginAbove > 0 || marginBelow > 0) {
          const double H = sc->height_;
          const double inner = H - 1 - H * (sc->o_.scaleMargins.top + sc->o_.scaleMargins.bottom);
          if (inner > marginAbove + marginBelow + 4) {
            const double perPx = (hi - lo) / (inner - marginAbove - marginBelow);
            hi += marginAbove * perPx;
            lo -= marginBelow * perPx;
          }
        }
        sc->min_ = lo;
        sc->max_ = hi;
        sc->hasRange_ = true;
      }
    }
    sc->buildTicks(fs);
  }
}

int Chart::requiredAxisWidth() {
  const QFontMetricsF fm(font());
  const int fs = o_.layout.fontSize;
  double w = 0;
  for (auto& p : panes_) {
    PriceScale* sc = p->priceScale("right");
    const auto& t = sc->ticks_;
    if (!t.isEmpty()) w = qMax(w, qMax(fm.horizontalAdvance(t.first().label), fm.horizontalAdvance(t.last().label)));
    for (Series* s : p->series_) {
      if (!s->o_.visible) continue;
      PriceScale* ssc = s->priceScale();
      if (s->o_.lastValueVisible)
        if (const Series::Row* r = s->lastValueRow()) {
          w = qMax(w, fm.horizontalAdvance(formatAxisPrice(ssc, s, r->c)));
          // the longest a countdown gets ("23:59:59"), so the axis does not widen and narrow as it ticks
          if (s->o_.lastValueSubtitle && !s->o_.lastValueSubtitle().isEmpty()) w = qMax(w, fm.horizontalAdvance(QStringLiteral("00:00:00")));
        }
      for (const auto& pl : s->priceLines_)
        if (pl->o_.axisLabelVisible) w = qMax(w, fm.horizontalAdvance(formatAxisPrice(ssc, s, pl->o_.price)));
    }
    if (o_.crosshair.mode != CrosshairMode::Hidden && o_.crosshair.horzLine.visible && o_.crosshair.horzLine.labelVisible &&
        sc->hasRange_ && sc->height_ > 4) {
      w = qMax(w, fm.horizontalAdvance(sc->formatInternal(sc->yToInternal(1))));
      w = qMax(w, fm.horizontalAdvance(sc->formatInternal(sc->yToInternal(sc->height_ - 2))));
    }
  }
  if (w <= 0) w = 34;
  const int need = fmt::makeEven(int(std::ceil(1 + 5 + 2 * (5.0 * fs / 12) + 5 + w)));
  return qMax(need, panes_.empty() ? 0 : panes_[0]->priceScale("right")->o_.minimumWidth);
}

QString Chart::formatAxisPrice(const PriceScale* sc, const Series* s, double price) const {
  if (sc && s) {
    if (sc->o_.mode == PriceScaleMode::Percentage) return fmt::percent(sc->toInternal(price, s->firstValue_));
    if (sc->o_.mode == PriceScaleMode::IndexedTo100) return fmt::fixed(sc->toInternal(price, s->firstValue_), 2);
  }
  if (!s) {
    if (o_.localization.priceFormatter) return o_.localization.priceFormatter(price);
    return fmt::fixed(price, 2);
  }
  const PriceFormat& f = s->o_.priceFormat;
  switch (f.type) {
    case PriceFormat::Volume: return fmt::volume(price, f.precision);
    case PriceFormat::Percent: return fmt::fixed(price, f.precision, f.minMove) + '%';
    case PriceFormat::Custom: if (f.formatter) return f.formatter(price); break;
    case PriceFormat::Price: break;
  }
  if (o_.localization.priceFormatter) return o_.localization.priceFormatter(price);
  return fmt::fixed(price, f.precision, f.minMove);
}

// ================= events =================

int Chart::subscribeCrosshairMove(MouseEventHandler h) { crossSubs_.insert(nextSub_, std::move(h)); return nextSub_++; }
int Chart::subscribeClick(MouseEventHandler h) { clickSubs_.insert(nextSub_, std::move(h)); return nextSub_++; }
int Chart::subscribeDblClick(MouseEventHandler h) { dblSubs_.insert(nextSub_, std::move(h)); return nextSub_++; }

void Chart::fire(const QMap<int, MouseEventHandler>& subs, const MouseEventParams& p) {
  const auto copy = subs;   // a handler may unsubscribe
  for (const auto& h : copy) h(p);
}

Chart::Zone Chart::zoneAt(const QPoint& pos, int& paneIndex) const {
  paneIndex = -1;
  const int plotW = ts_.width();
  if (timeAxisH_ > 0 && pos.y() >= height() - timeAxisH_) return pos.x() < plotW ? Zone::TimeAxis : Zone::None;
  if (o_.layout.panes.enableResize)
    for (int i = 0; i + 1 < int(panes_.size()); ++i) {
      const int sy = panes_[i]->rect_.bottom() + 1;
      if (std::abs(pos.y() - sy) <= 3) { paneIndex = i; return Zone::Separator; }
    }
  for (int i = 0; i < int(panes_.size()); ++i) {
    const QRect& r = panes_[i]->rect_;
    if (pos.y() >= r.top() && pos.y() <= r.bottom()) {
      paneIndex = i;
      return pos.x() < plotW ? Zone::Pane : Zone::PriceAxis;
    }
  }
  return Zone::None;
}

MouseEventParams Chart::paramsAt(const QPointF& pos, int paneIndex, Qt::KeyboardModifiers mods, Qt::MouseButton b) const {
  MouseEventParams p;
  p.modifiers = mods;
  p.button = b;
  if (paneIndex < 0 || paneIndex >= int(panes_.size()) || ts_.isEmpty()) return p;
  const QRect& r = panes_[paneIndex]->rect_;
  const QPointF local(pos.x() - r.left(), pos.y() - r.top());
  const double l = std::ceil(std::round(ts_.xToFloatIndex(local.x()) * 1e6) / 1e6);
  p.point = local;
  p.paneIndex = paneIndex;
  p.logical = l;
  const int idx = int(l);
  if (idx >= 0 && idx <= ts_.baseIndex()) {
    p.time = ts_.times_[idx];
    for (const auto& s : series_) {
      const Series::Row* row = s->rowAtIndex(idx);
      if (!row || !(row->flags & Series::HasValue)) continue;
      DataPoint d;
      d.time = row->time;
      if (s->isBarType()) { d.open = row->o; d.high = row->h; d.low = row->l; d.close = row->c; }
      else d.value = row->c;
      if (row->flags & Series::HasColor) d.color = QColor::fromRgba(row->color);
      p.seriesData.insert(s.get(), d);
    }
  }
  for (Primitive* pr : panes_[paneIndex]->primitives_)
    if (auto h = pr->hitTest(local.x(), local.y())) { p.hoveredObjectId = h->externalId; break; }
  if (p.hoveredObjectId.isEmpty())
    for (Series* s : panes_[paneIndex]->series_)
      for (Primitive* pr : s->primitives_)
        if (auto h = pr->hitTest(local.x(), local.y())) { p.hoveredObjectId = h->externalId; break; }
  return p;
}

void Chart::updateCrosshair(const QPointF& pos, int paneIndex, Qt::KeyboardModifiers mods) {
  if (ts_.isEmpty() || paneIndex < 0) return hideCrosshair();
  const QRect& r = panes_[paneIndex]->rect_;
  const double lx = pos.x() - r.left(), ly = pos.y() - r.top();
  const int idx = int(std::ceil(std::round(ts_.xToFloatIndex(lx) * 1e6) / 1e6));
  double y = ly;
  const CrosshairMode mode = o_.crosshair.mode;
  if ((mode == CrosshairMode::Magnet || mode == CrosshairMode::MagnetOHLC) && idx >= 0 && idx <= ts_.baseIndex()) {
    double best = 1e18;
    for (Series* s : panes_[paneIndex]->series_) {
      if (!s->o_.visible) continue;
      const Series::Row* row = s->rowAtIndex(idx);
      PriceScale* sc = s->priceScale();
      if (!row || !(row->flags & Series::HasValue) || !sc->hasRange_) continue;
      QVector<double> cands{row->c};
      if (mode == CrosshairMode::MagnetOHLC && s->isBarType()) cands = {row->o, row->h, row->l, row->c};
      for (double v : cands)
        if (auto yy = sc->priceToY(v, s->firstValue_); yy && std::fabs(*yy - ly) < best) { best = std::fabs(*yy - ly); y = *yy; }
    }
  }
  const QRegion was = crossRegion();
  cross_.visible = mode != CrosshairMode::Hidden;
  cross_.index = idx;
  cross_.y = y;
  cross_.pane = paneIndex;
  cross_.mouseX = lx;
  update(was | crossRegion());
  if (!crossSubs_.isEmpty()) fire(crossSubs_, paramsAt(pos, paneIndex, mods, Qt::NoButton));
}

void Chart::hideCrosshair() {
  if (!cross_.visible) return;
  const QRegion was = crossRegion();
  cross_.visible = false;
  update(was);
  if (!crossSubs_.isEmpty()) fire(crossSubs_, MouseEventParams{});
}

void Chart::setCrosshairPosition(double price, Time time, Series* s) {
  ensureLayout();
  if (!s || ts_.isEmpty()) return;
  const auto idx = ts_.timeToIndex(time, true);
  const auto y = s->priceToCoordinate(price);
  if (!idx || !y) return;
  int index = *idx;
  // as Lightweight Charts (model setAndSaveCurrentPosition): the index stays inside the visible strict range
  // [floor(from), ceil(to)], so a synced crosshair sits at the edge when that time is scrolled out of view
  if (const auto r = ts_.getVisibleLogicalRange()) index = qBound(int(std::floor(r->from)), index, int(std::ceil(r->to)));
  const QRegion was = crossRegion();
  cross_.visible = o_.crosshair.mode != CrosshairMode::Hidden;
  cross_.index = index;
  cross_.y = *y;
  cross_.pane = qMax(0, s->paneIndex());
  update(was | crossRegion());
}

void Chart::clearCrosshairPosition() {
  if (!cross_.visible) return;
  const QRegion was = crossRegion();
  cross_.visible = false;
  update(was);
}

// What paintOverlay draws for the crosshair as it stands: the vertical line (and the markers on line series) as a
// strip, the horizontal line with its price label across the width, and the time axis (its label). A move repaints
// these for the old and the new position instead of the whole chart.
QRegion Chart::crossRegion() const {
  if (!cross_.visible || ts_.isEmpty() || panes_.empty()) return {};
  const double x = ts_.indexToX(cross_.index);
  const QRect pr = panes_[qBound(0, cross_.pane, int(panes_.size()) - 1)]->rect_;
  const int fs = o_.layout.fontSize;
  const double lh = std::ceil(fs + 2 * 2.5 * fs / 12) + 1;
  int reach = 4;   // half the line, plus the largest marker on a line series
  for (const auto& s : series_)
    reach = qMax(reach, int(std::ceil(s->o_.crosshairMarkerRadius + s->o_.crosshairMarkerBorderWidth)) + 2);
  reach = qMax(reach, int(std::ceil(qMax(o_.crosshair.vertLine.width, o_.crosshair.horzLine.width))) + 2);
  QRegion r(QRect(int(std::floor(x)) - reach, 0, 2 * reach + 1, height()));
  const double cy = pr.top() + cross_.y;
  r += QRect(0, int(std::floor(cy - lh / 2)) - reach, width(), int(std::ceil(lh)) + 2 * reach);
  if (timeAxisH_ > 0) r += QRect(0, height() - timeAxisH_, width(), timeAxisH_);
  return r;
}

void Chart::resizeEvent(QResizeEvent*) { invalidate(); viewChanged(); }

void Chart::mousePressEvent(QMouseEvent* e) {
  ensureLayout();
  if (e->button() != Qt::LeftButton) return QWidget::mousePressEvent(e);
  int pi = -1;
  const Zone z = zoneAt(e->pos(), pi);
  pressPos_ = e->pos();
  moved_ = false;
  dblPress_ = false;
  drag_ = Drag::None;
  dragPane_ = pi;
  dragPaneObj_ = pi >= 0 && pi < int(panes_.size()) ? panes_[pi].get() : nullptr;
  dragScale_ = nullptr;
  sepIndex_ = -1;
  if (z == Zone::Pane) {
    if (o_.handleScroll.pressedMouseMove) {
      drag_ = Drag::Pan;
      ts_.startScroll(e->position().x());
      dragScale_ = panes_[pi]->priceScale("right");
      dragScale_->startScroll(e->position().y() - panes_[pi]->rect_.top());
    }
  } else if (z == Zone::PriceAxis) {
    if (o_.handleScale.axisPressedMouseMove.price) {
      drag_ = Drag::PriceScale;
      dragScale_ = panes_[pi]->priceScale("right");
      dragScale_->startScale(e->position().y() - panes_[pi]->rect_.top());
    }
  } else if (z == Zone::TimeAxis) {
    if (o_.handleScale.axisPressedMouseMove.time) {
      drag_ = Drag::TimeScale;
      ts_.startScale(e->position().x());
    }
  } else if (z == Zone::Separator) {
    drag_ = Drag::Separator;
    sepIndex_ = pi;
    sepStartA_ = panes_[pi]->rect_.height();
    sepStartB_ = panes_[pi + 1]->rect_.height();
  }
}

void Chart::mouseMoveEvent(QMouseEvent* e) {
  ensureLayout();
  const QPointF pos = e->position();
  if (drag_ != Drag::None && (e->buttons() & Qt::LeftButton)) {
    if ((e->pos() - pressPos_).manhattanLength() > 2) moved_ = true;
    switch (drag_) {
      case Drag::Pan:
        if (moved_) {
          setCursor(Qt::ClosedHandCursor);
          ts_.scrollTo(pos.x());
          if (PriceScale* sc = dragScaleNow()) sc->scrollTo(pos.y() - dragPaneObj_->rect_.top());
        }
        break;
      case Drag::PriceScale:
        if (PriceScale* sc = dragScaleNow()) sc->scaleTo(pos.y() - dragPaneObj_->rect_.top());
        break;
      case Drag::TimeScale:
        ts_.scaleTo(pos.x());
        break;
      case Drag::Separator: {
        // panes may have been removed during the drag: the two around the separator are looked up again
        if (sepIndex_ < 0 || sepIndex_ + 1 >= int(panes_.size())) { endSeparatorDrag(); break; }
        const int total = sepStartA_ + sepStartB_;
        const int a = qBound(qMin(30, total / 2), sepStartA_ + (e->pos().y() - pressPos_.y()), total - qMin(30, total / 2));
        Pane* pa = panes_[sepIndex_].get();
        Pane* pb = panes_[sepIndex_ + 1].get();
        const double st = pa->stretch_ + pb->stretch_;
        pa->stretch_ = st * a / qMax(1, total);
        pb->stretch_ = st - pa->stretch_;
        invalidate();
        break;
      }
      case Drag::None: break;
    }
  }
  int pi = -1;
  const Zone z = zoneAt(e->pos(), pi);
  // cursor and separator hover
  const int hover = (z == Zone::Separator || drag_ == Drag::Separator) ? (drag_ == Drag::Separator ? sepIndex_ : pi) : -1;
  if (hover != hoverSep_) { hoverSep_ = hover; baseDirty_ = true; update(); }
  if (drag_ == Drag::Pan && moved_) {
  } else if (z == Zone::Separator || drag_ == Drag::Separator) setCursor(Qt::SizeVerCursor);
  else if (z == Zone::PriceAxis && o_.handleScale.axisPressedMouseMove.price) setCursor(Qt::SizeVerCursor);
  else if (z == Zone::TimeAxis && o_.handleScale.axisPressedMouseMove.time) setCursor(Qt::SizeHorCursor);
  else {
    Qt::CursorShape c = Qt::ArrowCursor;
    if (z == Zone::Pane) {
      const QRect& r = panes_[pi]->rect_;
      const double lx = pos.x() - r.left(), ly = pos.y() - r.top();
      auto look = [&](Primitive* pr) { if (auto h = pr->hitTest(lx, ly)) { c = h->cursor; return true; } return false; };
      bool hit = false;
      for (Primitive* pr : panes_[pi]->primitives_) if (!hit) hit = look(pr);
      for (Series* s : panes_[pi]->series_)
        for (Primitive* pr : s->primitives_) if (!hit) hit = look(pr);
    }
    setCursor(c);
  }
  if (z == Zone::Pane && drag_ != Drag::Separator) updateCrosshair(pos, pi, e->modifiers());
  else if (drag_ != Drag::Pan) hideCrosshair();
}

void Chart::mouseReleaseEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return QWidget::mouseReleaseEvent(e);
  ensureLayout();
  const Drag was = drag_;
  ts_.endScroll();
  ts_.endScale();
  if (dragScale_) { dragScale_->endScale(); dragScale_->endScroll(); }
  drag_ = Drag::None;
  dragScale_ = nullptr;
  dragPaneObj_ = nullptr;
  sepIndex_ = -1;
  int pi = -1;
  const Zone z = zoneAt(e->pos(), pi);
  if (!moved_ && !dblPress_ && z == Zone::Pane && (was == Drag::Pan || was == Drag::None) && !clickSubs_.isEmpty())
    fire(clickSubs_, paramsAt(e->position(), pi, e->modifiers(), e->button()));
  dblPress_ = false;
  if (was == Drag::Pan) setCursor(Qt::ArrowCursor);
}

void Chart::mouseDoubleClickEvent(QMouseEvent* e) {
  ensureLayout();
  dblPress_ = true;
  int pi = -1;
  const Zone z = zoneAt(e->pos(), pi);
  if (z == Zone::PriceAxis && o_.handleScale.axisDoubleClickReset.price) {
    panes_[pi]->priceScale("right")->setAutoScale(true);
  } else if (z == Zone::TimeAxis && o_.handleScale.axisDoubleClickReset.time) {
    ts_.resetTimeScale();
  } else if (z == Zone::Pane && !dblSubs_.isEmpty()) {
    fire(dblSubs_, paramsAt(e->position(), pi, e->modifiers(), e->button()));
  }
}

void Chart::wheelEvent(QWheelEvent* e) {
  ensureLayout();
  // browser-like deltas: pixels (trackpad) or 100 per notch (mouse), positive = down / right
  double dx, dy;
  if (!e->pixelDelta().isNull()) { dx = -e->pixelDelta().x(); dy = -e->pixelDelta().y(); }
  else { dx = -e->angleDelta().x() / 120.0 * 100; dy = -e->angleDelta().y() / 120.0 * 100; }
  if (e->modifiers() & Qt::ShiftModifier && dx == 0) std::swap(dx, dy);   // shift + wheel scrolls (as browsers do)
  const double s = -dy / 100, n = dx / 100;
  bool used = false;
  if (s != 0 && o_.handleScale.mouseWheel) {
    const double scale = (s > 0 ? 1 : -1) * qMin(1.0, std::fabs(s));
    ts_.zoom(e->position().x(), scale);
    used = true;
  }
  if (n != 0 && o_.handleScroll.mouseWheel) {
    ts_.scrollChart(-80 * n);
    used = true;
  }
  if (used) {
    e->accept();
    int pi = -1;
    if (zoneAt(e->position().toPoint(), pi) == Zone::Pane) {
      ensureLayout();
      updateCrosshair(e->position(), pi, e->modifiers());
    }
  } else {
    e->ignore();
  }
}

// A trackpad pinch (macOS: ZoomNativeGesture, value = the magnification since the last event) zooms the time scale around
// the fingers like the wheel does: TimeScale::zoom multiplies the bar spacing by 1 + scale / 10, so a factor f is
// scale = 10 (f - 1). Native parity 2026-11-22 (the Charts phone pass); a finger pinch on iOS is not handled yet.
bool Chart::event(QEvent* e) {
  if (e->type() == QEvent::NativeGesture && o_.handleScale.mouseWheel) {
    auto* g = static_cast<QNativeGestureEvent*>(e);
    if (g->gestureType() == Qt::ZoomNativeGesture) {
      ensureLayout();
      ts_.zoom(g->position().x(), 10.0 * g->value());
      e->accept();
      return true;
    }
  }
  return QWidget::event(e);
}

void Chart::leaveEvent(QEvent*) {
  if (hoverSep_ != -1 && drag_ != Drag::Separator) { hoverSep_ = -1; baseDirty_ = true; update(); }
  if (drag_ == Drag::None) hideCrosshair();
}

QImage Chart::takeScreenshot(bool addTopLayer) {
  const bool keep = cross_.visible;
  if (!addTopLayer) cross_.visible = false;
  const QImage img = grab().toImage();
  cross_.visible = keep;
  return img;
}

}  // namespace charts::plot
