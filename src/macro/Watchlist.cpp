#include "macro/Watchlist.h"
#include "macro/Registry.h"
#include "core/Settings.h"

namespace macro {
static const char* kKey = "macro.watchlist";

Watchlist& Watchlist::instance() { static Watchlist* w = new Watchlist; return *w; }

Watchlist::Watchlist() {
  for (const QString& c : settings::value(kKey).toString().split(',', Qt::SkipEmptyParts))
    if (const Economy* e = economy(c.trimmed()); e && !m_codes.contains(e->iso2)) m_codes << e->iso2;
}

void Watchlist::toggle(const QString& code) {
  const Economy* e = economy(code);
  if (!e) return;
  if (!m_codes.removeOne(e->iso2)) m_codes << e->iso2;
  settings::setValue(kKey, m_codes.join(','));
  emit changed();
}
}  // namespace macro
