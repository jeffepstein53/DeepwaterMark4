#pragma once
#include <QString>
#include <functional>

// Windows' own notifications (app/Alerts): the message at the corner of the screen, kept in the notification centre, and
// a short sound of Deepwater's own. Nothing on the other systems.
namespace winnotify {
void show(const QString& title, const QString& text, std::function<void()> onClick);
void chime(bool warning);   // two notes up: a figure; two notes down: something is wrong with the data
}
