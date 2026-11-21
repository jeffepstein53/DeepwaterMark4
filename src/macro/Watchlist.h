#pragma once
#include <QObject>
#include <QStringList>

// The user's own list of economies (2026-11-21, user's request: "a watchlist on the macro map, to have only the
// countries we want"). One list shared by Economy › Macro (registry tab ☆ WATCH, globe tint) and Global › Overview
// (Asset Registry tab), kept in the settings (macro.watchlist, ISO-2 codes in the user's order).
//
//   auto& w = macro::Watchlist::instance();
//   w.contains("us"); w.toggle("jp");                        // the ☆ of a registry row, the globe's right-click menu
//   connect(&w, &macro::Watchlist::changed, …);             // both registries repaint
//   macro::registry(macro::Group::Watch)                      // the listed economies, in that order
namespace macro {
class Watchlist : public QObject {
  Q_OBJECT
public:
  static Watchlist& instance();
  QStringList codes() const { return m_codes; }
  bool contains(const QString& iso2) const { return m_codes.contains(iso2.toLower()); }
  void toggle(const QString& iso2);
signals:
  void changed();
private:
  Watchlist();
  QStringList m_codes;
};
}  // namespace macro
