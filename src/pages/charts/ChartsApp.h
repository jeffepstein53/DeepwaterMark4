#pragma once
// Charts tab — the page and its "app" object. Port of quant-terminal public/js/app.js + index.html
// (MIT, Copyright (c) 2026 Lewis Jackson — docs/charts/NOTICE.md).
//
// CONTRACT for the UI agents: ChartPane and the panels call the App through these members, which mirror the JS
// `app` object one to one (app.state, app.settings, app.persist(), app.toast(), app.modal(), …). Add members freely;
// do not change or remove the ones below. State and settings are the same JSON objects the JS keeps
// (persisted to <data>/charts/state.json through charts::store, exactly like PUT /api/state).
#include <QWidget>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QPointer>
#include <functional>
#include <memory>
#include <optional>

namespace charts::ind {
struct Def;
using DefPtr = std::shared_ptr<const Def>;   // as in charts/core/Indicators.h
}

namespace charts::ui {

class ChartPane;
class Panels;

class ChartsApp : public QWidget {
  Q_OBJECT
public:
  explicit ChartsApp(QWidget* parent = nullptr);
  ~ChartsApp() override;

  // ---- state (JS: this.state / this.settings) ----
  // state: { layout, panes[], active, watchlists{}, activeWatchlist, drawings{}, settings{}, paper, strategies{},
  //          replayPaper, ... } — same keys and shapes as the JS, so data/state.json files are interchangeable.
  QJsonObject state;
  // settings: { theme, volume, bars, utc, stayInDrawMode, syncCrosshair, syncSymbol, syncReplay } (JS defaults).
  // Kept inside state["settings"] on persist().
  QJsonObject settings;
  QList<ChartPane*> panes;
  ChartPane* active = nullptr;
  QString tool = "cursor";            // "cursor" | drawing tool id | "replay-pick" | "anchor-pick"
  QHash<QString, double> prices;      // last price per symbol id (JS this.prices)
  struct PendingAnchor { QPointer<ChartPane> pane; QString uid, key; };
  std::optional<PendingAnchor> pendingAnchor;
  Panels* panels = nullptr;

  void persist();                                         // debounced 400 ms write of state (JS persist)
  QJsonArray getDrawings(const QString& symbol) const;    // deep copy
  void saveDrawings(const QString& symbol, const QJsonArray& items);
  charts::ind::DefPtr importCustom(const QString& file);   // load/reload custom/<file>, toast on failure

  // ---- layout / panes ----
  void setLayout(const QString& layout, bool save = true); // "1","2v","2h","3","4","6","8","16"
  void setActive(ChartPane* pane);
  void onPaneLoaded(ChartPane* pane);
  void syncToolbar();
  void setTf(const QString& tf);
  void setSymbol(const QString& sym, ChartPane* pane = nullptr);   // nullptr = active
  // Narrow windows (≤ 900 px) show the side panel as a sheet over the chart: opens / closes it (no-op when it is docked).
  void showSidePanel(bool on = true);
  void setTool(const QString& tool);

  // ---- modals (JS modal(title, bodyHtml, {foot, wide})): an overlay card inside the page ----
  // The card owns `body` and the `foot` widgets (right-aligned row; may be empty). Returns the card.
  QWidget* modal(const QString& title, QWidget* body, const QList<QWidget*>& foot = {}, bool wide = false);
  void closeModal();
  bool modalOpen() const;

  void openSymbolSearch(const QString& initial = {}, std::function<void(const QString&)> onPick = {},
                        const QString& title = {});   // empty: "Symbol search", translated
  void openIndicatorPicker();
  void pickAnchor(ChartPane* pane, const QString& uid, const QString& key);
  void openIndicatorSettings(ChartPane* pane, const QString& uid);
  void openSettings();
  void openFootprintSettings();   // the active chart's footprint (gear next to the chart type; modal:footprint)
  void syncCrosshair(ChartPane* src, double logical);
  void exportCsv();

  // ---- drawings → alerts ----
  QJsonObject lineOperandFor(const QJsonObject& drawing) const;   // {} when the drawing type can't be alerted on
  void alertOnSelectedDrawing();

  // ---- replay across charts ----
  void syncReplayFrom(ChartPane* pane, const QJsonObject& bar);
  void onReplayStart(ChartPane* pane);
  void onReplayBar(ChartPane* pane, const QJsonObject& bar);
  void onReplayStop(ChartPane* pane);

  // ---- prices, alerts, status ----
  void onPrice(const QString& symbol, double price);    // emits priceChanged
  void beep();
  // A Big Trades alert (pages/charts/flow): a toast and a macOS notification (visual), the system beep (sound).
  void bigTradeAlert(const QString& text, bool visual, bool sound);
  // A macOS notification (plain text), shown even when another page or app is in front; never from tests (DW_PROFILE).
  void systemNotify(const QString& text, const QString& title);
  void checkHealth();
  void setStatus(const QString& level, const QString& title = {});   // "ok" | "warn" | "bad"
  // kind: "" | "err" | "alert" | "ok". `html` may contain <b>, <br>, <code> like the JS toasts.
  void toast(const QString& html, const QString& kind = {}, int ms = 3500);

  // ---- ADDED by the page shell ----
  // A fired alert (charts::alerts::Alerts::fired is connected to it): toast "<b>Alert</b><br>text" for 12 s, beep,
  // macOS notification, then panels->onAlertFired(event). JS listenAlerts.
  void onAlertEvent(const QJsonObject& event);
  void flushPersist();                                    // write a pending persist() now
  QWidget* modalCard() const;                             // the open card (nullptr when none)
  void relayoutModal();                                   // re-fit the card after its body changed size
  QString theme() const;                                  // settings.theme: "dark" | "light"
  // charts::api::Server::stateChanged (PUT /api/state from outside): take the new state like the JS after a reload
  // (panes rebuilt when layout/panes/active changed; watchlists, strategies… re-rendered); not written back.
  void applyExternalState(const QJsonObject& state, const QJsonObject& patch);

signals:
  void priceChanged(const QString& symbol, double price);   // JS app.priceListeners
  void themeChanged();

protected:
  void showEvent(QShowEvent*) override;
  void hideEvent(QHideEvent*) override;
  void keyPressEvent(QKeyEvent*) override;
  bool eventFilter(QObject*, QEvent*) override;   // ADDED (shell): modal re-fit
  void resizeEvent(QResizeEvent*) override;       // ADDED (shell): overlays, narrow window
  void paintEvent(QPaintEvent*) override;         // ADDED (shell): page ground

private:
  struct Impl;
  Impl* d = nullptr;
};

// ---- UI steps for the harness and screenshots (tests/charts/app main.cpp --eval "step;step") ----
// Built-in steps: side:<tab>  bottom:<tab>  modal:symbol[:<query>]  modal:indicators  modal:indsettings[:<n>]
// modal:settings  modal:alert  layout:<id>  tool:<id>  type:<id>  tf:<tf>  theme:light|dark  symbol:<id>
// toast:<text>  close (modal).
// Other areas add theirs: registerUiStep("tester", fn) handles "tester" and "tester:<arg>" (fn gets the arg).
// A registered prefix wins over a built-in one.
using UiStep = std::function<bool(ChartsApp* app, const QString& arg)>;
bool registerUiStep(const QString& prefix, UiStep fn);
bool runUiStep(ChartsApp* app, const QString& step);

}  // namespace charts::ui
