#pragma once
#include <QList>

class QWidget;

// Interface zoom (2026-11-21, user's choice: the whole app, applied at the next launch). 50, 75, 100, 125 or 150 %,
// saved as ui.zoom (percent). main() turns it into QT_SCALE_FACTOR before the QApplication exists, so every page,
// the 3D views and the video scale alike; Qt cannot change it in a running app, hence the restart.
//
//   zoom::installSaved();            // main(), before QApplication (needs the organisation names set)
//   zoom::levels();                  // {50, 75, 100, 125, 150}
//   zoom::saved(); zoom::active();   // the chosen level / the level this launch runs at
//   zoom::choose(window, 125);       // saves it and offers to restart now (Options, Cmd+= / Cmd+- / Cmd+0)
//   zoom::restart();                 // starts a new instance and quits this one
namespace zoom {
const QList<int>& levels();
int saved();
int active();
void installSaved();
void choose(QWidget* parent, int percent);
void step(QWidget* parent, int direction);   // +1 / -1: the next level up or down; 0: back to 100 %
void restart();
}  // namespace zoom
