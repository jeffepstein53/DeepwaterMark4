#pragma once
#include <QDialog>
#include <QList>
#include <QRectF>

class QScrollArea;
namespace options { class Body; }

// Options (gear at the right of the header, Cmd+,): interface language (English / Français), zoom (app/Zoom.h) and theme
// (Deepwater and the nine themes of preview.html, in a 2-column grid). Changes apply immediately and are saved (ui.language, ui.theme).
// Its width follows the main window (640 at most; a phone: the screen's width less a margin) and its content scrolls
// when the window is shorter than it (a phone turned).
class OptionsDialog : public QDialog {
  Q_OBJECT
public:
  explicit OptionsDialog(QWidget* parent = nullptr);
  void fit();   // size from the main window and the content (on opening, a language change, the main window resized)
protected:
  void paintEvent(QPaintEvent*) override;
  void keyPressEvent(QKeyEvent*) override;
  void resizeEvent(QResizeEvent*) override;
private:
  QScrollArea* m_scroll;
  options::Body* m_body;
};
