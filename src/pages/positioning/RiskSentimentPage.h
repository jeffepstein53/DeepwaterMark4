#pragma once
#include <QWidget>
#include <QTimer>

namespace positioning {
namespace pk { class TitleBar; class Ticker; class Scroll; }

// Positioning › Risk Sentiment: composite risk-on/off gauge (positioning::RiskIndex), its 1-year history, the seven
// component percentiles and 1-year S&P 500 / VIX charts.
// Test hooks: DW_RISK_HOVER=x,y (page coordinates: hover a component row or the history chart),
//             DW_RISK_SCROLL=px (initial scroll).
class RiskSentimentPage : public QWidget {
public:
  explicit RiskSentimentPage(QWidget* parent = nullptr);
protected:
  void showEvent(QShowEvent*) override;
  void hideEvent(QHideEvent*) override;
  void resizeEvent(QResizeEvent*) override;
private:
  void refresh();
  void applyBadge();
  QString m_badge = QStringLiteral("-");   // badge shown ("-": none set yet)
  pk::TitleBar* m_title;
  pk::Ticker* m_ticker;
  pk::Scroll* m_scroll;
  class RiskContent* m_content;
  QTimer m_reload;                 // the index's inputs, asked again every 5 min while shown
};
}  // namespace positioning
