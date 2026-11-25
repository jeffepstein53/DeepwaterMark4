#include "macro/Watchlist.h"
#include "pages/global/overview/OverviewData.h"
#include "core/I18n.h"
#include <QCoreApplication>
#include "pages/global/overview/FeedPanels.h"
#include "macro/CentralBanks.h"
#include "macro/Registry.h"
#include "market/Instruments.h"
#include "news/Translate.h"
#include "positioning/RiskIndex.h"
#include "ui/Format.h"
#include "wire/ReleaseSource.h"
#include "wire/Wire.h"
#include <QLocale>
#include <cmath>

namespace overview {

// Registry country names (macro::economies(), English) in the interface language. The Economy package's own
// translation wins when it has one (context "economy"); this table makes the Overview French on its own.
static const char* const kCountryNames[] = {
  QT_TRANSLATE_NOOP("global", "United States"),
  QT_TRANSLATE_NOOP("global", "Canada"),
  QT_TRANSLATE_NOOP("global", "Mexico"),
  QT_TRANSLATE_NOOP("global", "Brazil"),
  QT_TRANSLATE_NOOP("global", "Argentina"),
  QT_TRANSLATE_NOOP("global", "Chile"),
  QT_TRANSLATE_NOOP("global", "Colombia"),
  QT_TRANSLATE_NOOP("global", "Peru"),
  QT_TRANSLATE_NOOP("global", "Uruguay"),
  QT_TRANSLATE_NOOP("global", "Ecuador"),
  QT_TRANSLATE_NOOP("global", "Venezuela"),
  QT_TRANSLATE_NOOP("global", "Costa Rica"),
  QT_TRANSLATE_NOOP("global", "Panama"),
  QT_TRANSLATE_NOOP("global", "Guatemala"),
  QT_TRANSLATE_NOOP("global", "Dominican Rep."),
  QT_TRANSLATE_NOOP("global", "Jamaica"),
  QT_TRANSLATE_NOOP("global", "Euro Zone"),
  QT_TRANSLATE_NOOP("global", "Germany"),
  QT_TRANSLATE_NOOP("global", "France"),
  QT_TRANSLATE_NOOP("global", "Italy"),
  QT_TRANSLATE_NOOP("global", "Spain"),
  QT_TRANSLATE_NOOP("global", "Netherlands"),
  QT_TRANSLATE_NOOP("global", "Belgium"),
  QT_TRANSLATE_NOOP("global", "Austria"),
  QT_TRANSLATE_NOOP("global", "Ireland"),
  QT_TRANSLATE_NOOP("global", "Portugal"),
  QT_TRANSLATE_NOOP("global", "Greece"),
  QT_TRANSLATE_NOOP("global", "Finland"),
  QT_TRANSLATE_NOOP("global", "Slovakia"),
  QT_TRANSLATE_NOOP("global", "Slovenia"),
  QT_TRANSLATE_NOOP("global", "Lithuania"),
  QT_TRANSLATE_NOOP("global", "Latvia"),
  QT_TRANSLATE_NOOP("global", "Estonia"),
  QT_TRANSLATE_NOOP("global", "Croatia"),
  QT_TRANSLATE_NOOP("global", "Bulgaria"),
  QT_TRANSLATE_NOOP("global", "United Kingdom"),
  QT_TRANSLATE_NOOP("global", "Switzerland"),
  QT_TRANSLATE_NOOP("global", "Sweden"),
  QT_TRANSLATE_NOOP("global", "Norway"),
  QT_TRANSLATE_NOOP("global", "Denmark"),
  QT_TRANSLATE_NOOP("global", "Poland"),
  QT_TRANSLATE_NOOP("global", "Czechia"),
  QT_TRANSLATE_NOOP("global", "Hungary"),
  QT_TRANSLATE_NOOP("global", "Romania"),
  QT_TRANSLATE_NOOP("global", "Serbia"),
  QT_TRANSLATE_NOOP("global", "Iceland"),
  QT_TRANSLATE_NOOP("global", "Turkey"),
  QT_TRANSLATE_NOOP("global", "Russia"),
  QT_TRANSLATE_NOOP("global", "South Africa"),
  QT_TRANSLATE_NOOP("global", "Nigeria"),
  QT_TRANSLATE_NOOP("global", "Egypt"),
  QT_TRANSLATE_NOOP("global", "Morocco"),
  QT_TRANSLATE_NOOP("global", "Kenya"),
  QT_TRANSLATE_NOOP("global", "Ghana"),
  QT_TRANSLATE_NOOP("global", "Algeria"),
  QT_TRANSLATE_NOOP("global", "Ethiopia"),
  QT_TRANSLATE_NOOP("global", "Tunisia"),
  QT_TRANSLATE_NOOP("global", "Côte d'Ivoire"),
  QT_TRANSLATE_NOOP("global", "China"),
  QT_TRANSLATE_NOOP("global", "Japan"),
  QT_TRANSLATE_NOOP("global", "India"),
  QT_TRANSLATE_NOOP("global", "South Korea"),
  QT_TRANSLATE_NOOP("global", "Indonesia"),
  QT_TRANSLATE_NOOP("global", "Saudi Arabia"),
  QT_TRANSLATE_NOOP("global", "Singapore"),
  QT_TRANSLATE_NOOP("global", "Hong Kong"),
  QT_TRANSLATE_NOOP("global", "Malaysia"),
  QT_TRANSLATE_NOOP("global", "Thailand"),
  QT_TRANSLATE_NOOP("global", "Philippines"),
  QT_TRANSLATE_NOOP("global", "Israel"),
  QT_TRANSLATE_NOOP("global", "U.A.E."),
  QT_TRANSLATE_NOOP("global", "Vietnam"),
  QT_TRANSLATE_NOOP("global", "Pakistan"),
  QT_TRANSLATE_NOOP("global", "Kuwait"),
  QT_TRANSLATE_NOOP("global", "Qatar"),
  QT_TRANSLATE_NOOP("global", "Australia"),
  QT_TRANSLATE_NOOP("global", "Fiji"),
  QT_TRANSLATE_NOOP("global", "New Caledonia"),
  QT_TRANSLATE_NOOP("global", "New Zealand"),
  QT_TRANSLATE_NOOP("global", "Papua New Guinea"),
  QT_TRANSLATE_NOOP("global", "Solomon Is."),
  QT_TRANSLATE_NOOP("global", "Vanuatu")};
static QString countryName(const QString& en) {
  const QByteArray k = en.toUtf8();
  const QString eco = QCoreApplication::translate("economy", k.constData());
  if (eco != en) return eco;
  for (const char* n : kCountryNames) if (k == n) return QCoreApplication::translate("global", n);
  return en;
}

OverviewData::OverviewData(SquawkPanel* sq, RiskPanel* risk, CentralBanksPanel* banks, RegistryPanel* reg, QObject* parent)
    : QObject(parent), m_sq(sq), m_risk(risk), m_banks(banks), m_reg(reg) {
  auto& w = wire::Wire::instance();
  // hidden: nothing is pushed (the strip re-renders a wide image) nor translated; setVisible(true) catches up
  connect(&w, &wire::Wire::changed, this, [this] { if (!m_visible) return; pushSquawk(); if (m_sq->french()) translateMissing(); });
  connect(&wire::Hooks::instance(), &wire::Hooks::translatorChanged, this, [this] { if (m_sq->french()) translateMissing(); pushSquawk(); });
  m_sq->onTranslate = [this](bool fr) { if (fr) translateMissing(); pushSquawk(); };
  connect(&positioning::RiskIndex::instance(), &positioning::RiskIndex::updated, this, [this] { pushRisk(); });
  connect(&macro::CentralBanks::instance(), &macro::CentralBanks::updated, this, [this] { pushBanks(); });
  // RiskIndex::load() is only called on show: a page left open for hours kept the morning's reading. Ask again every
  // 5 minutes while visible; the age accepted from the cache is shorter than that (with the same 5 minutes, an answer
  // that took a few seconds to come was still "fresh" at the next tick: one reload in two was served from the cache).
  m_riskTimer.setInterval(5 * 60 * 1000);
  connect(&m_riskTimer, &QTimer::timeout, this, [] { positioning::RiskIndex::instance().load(240); });
  pushSquawk();
  pushRisk();
  pushBanks();
  pushRegistry();
  connect(&macro::Watchlist::instance(), &macro::Watchlist::changed, this, [this] { pushRegistry(); });
  if (m_sq->french()) translateMissing();
}

void OverviewData::setVisible(bool on) {
  if (on == m_visible) return;
  m_visible = on;
  wire::Wire::instance().setActive(this, on);
  macro::CentralBanks::instance().setActive(this, on);   // loads what is stale, then Fed and ECB odds every 5 min while shown
  if (on) {
    positioning::RiskIndex::instance().load(240);     // what is under 4 min old comes from the cache
    pushSquawk();
    if (m_sq->french()) translateMissing();   // wire changes while hidden were not looked at
    m_riskTimer.start();
  } else {
    m_riskTimer.stop();
  }
}

// ---------------------------------------------------------------- Squawk
void OverviewData::pushSquawk() {
  // hidden page: translations landing and wire changes are picked up by setVisible(true)
  if (!m_visible) return;
  const auto& w = wire::Wire::instance();
  const bool viaNews = !wire::translator() && m_sq->french();
  SquawkFeed f;
  for (const wire::Item& it : w.items(wire::Filter::All)) {
    SquawkItem s;
    s.id = it.id;
    s.time = it.time;
    s.text = it.text;
    switch (it.kind) {
      case wire::Kind::Headline: s.kind = SquawkItem::Headline; break;
      case wire::Kind::Mover: s.kind = SquawkItem::Mover; break;
      case wire::Kind::Release: s.kind = SquawkItem::Release; break;
    }
    if (it.kind == wire::Kind::Mover) {
      const auto* in = market::find(it.symbol);
      s.name = in ? in->name : it.symbol;
      s.changePct = it.changePct;
      s.price = it.price;
      s.priceDecimals = in ? qMin(in->decimals, 4) : 2;
    } else if (m_sq->french()) {
      s.textFr = m_fr.value(it.text);
      if (s.textFr.isEmpty() && viaNews) s.textFr = news::Translate::instance().cached(it.text);
    }
    f.items << s;
  }
  // footer "Terminal Data · HH:MM:SS" = newest headline (reference: 16:37:43 while the 17:34 movers scroll)
  for (const auto& it : f.items) if (it.kind == SquawkItem::Headline) { f.newest = it.time; break; }
  if (!f.newest.isValid() && !f.items.isEmpty()) f.newest = f.items.first().time;
  f.live = w.lastFetch().isValid();
  const QStringList errs = w.feedErrors();
  if (f.items.isEmpty()) f.status = errs.isEmpty() ? QCoreApplication::translate("global", "CONNECTING TO THE WIRE…") : QCoreApplication::translate("global", "WIRE UNREACHABLE");
  QStringList note{QCoreApplication::translate("global", "Wire: FinancialJuice, investingLive, Fed, ECB, BoE press RSS; price moves "
                                               "computed by Deepwater from Yahoo quotes; releases from the economic calendar.")};
  if (!w.releasesConnected()) note << QCoreApplication::translate("global", "Releases: calendar connecting.");
  if (w.lastFetch().isValid()) note << QCoreApplication::translate("global", "Last fetch %1.").arg(w.lastFetch().toLocalTime().toString("HH:mm:ss"));
  if (!errs.isEmpty()) note << errs.join("\n");
  f.sourceNote = note.join("\n");
  if (wire::translator()) f.translateNote = QCoreApplication::translate("global", "Translator connected.");
  else if (m_sq->french()) f.translateNote = news::Translate::instance().stateText();
  else f.translateNote = QCoreApplication::translate("global", "Click to show the headlines in French.");
  m_sq->setFeed(f);
}

void OverviewData::translateMissing() {
  const auto items = wire::Wire::instance().items(wire::Filter::All);
  QStringList texts;
  QSet<QString> current;
  for (const auto& it : items) {
    if (it.kind == wire::Kind::Mover) continue;
    current.insert(it.text);
    if (!m_fr.contains(it.text) && !m_asked.contains(it.text)) texts << it.text;
  }
  // only the wire's current headlines are ever looked up: older ones are dropped, or both sets grow for the session
  for (auto it = m_fr.begin(); it != m_fr.end();) it = current.contains(it.key()) ? std::next(it) : m_fr.erase(it);
  for (auto it = m_asked.begin(); it != m_asked.end();) it = current.contains(*it) ? std::next(it) : m_asked.erase(it);
  if (texts.isEmpty()) return;
  if (auto* t = wire::translator()) {
    for (int k = 0; k < texts.size(); k += 50) {
      const QStringList chunk = texts.mid(k, 50);
      for (const auto& x : chunk) m_asked.insert(x);
      t->translate(chunk, "fr", this, [this, chunk](const QStringList& out) {
        bool any = false;
        for (int i = 0; i < chunk.size() && i < out.size(); ++i)
          if (out[i] != chunk[i]) { m_fr.insert(chunk[i], out[i]); any = true; }
        // a failed call answers the originals: nothing of the chunk changed, so it is asked again at the next wire
        // change instead of staying in English for the session
        if (!any) for (const auto& x : chunk) m_asked.remove(x);
        pushSquawk();
      });
    }
  } else {
    auto& tr = news::Translate::instance();
    if (!m_newsHooked) {   // show translations as they arrive (throttled), and the translator's state
      m_newsHooked = true;
      m_trTimer.setSingleShot(true); m_trTimer.setInterval(400);
      connect(&m_trTimer, &QTimer::timeout, this, [this] { pushSquawk(); });
      connect(&tr, &news::Translate::progressed, this, [this] { if (!m_trTimer.isActive()) m_trTimer.start(); });
      connect(&tr, &news::Translate::stateChanged, this, [this] { pushSquawk(); });
    }
    for (const auto& x : texts) m_asked.insert(x);
    tr.request(texts, this, [this, texts] {
      // texts the helper failed on are asked again at the next wire change while it can work
      if (news::Translate::instance().usable())
        for (const auto& x : texts) if (news::Translate::instance().cached(x).isEmpty()) m_asked.remove(x);
      pushSquawk();
    });
  }
}

// ---------------------------------------------------------------- Risk
void OverviewData::pushRisk() {
  const auto& ri = positioning::RiskIndex::instance();
  const auto& d = ri.data();
  RiskReading r;
  if (d.ok()) {
    r.value = d.value;
    r.weekAgo = d.weekAgo;
    r.monthAgo = d.monthAgo;
    for (int i = 0; i < d.dates.size() && i < d.composite.size(); ++i)
      if (std::isfinite(d.composite[i])) r.history << qMakePair(d.dates[i], d.composite[i]);
    QStringList m{QCoreApplication::translate("global", "Risk appetite 0–100, computed by Deepwater from Yahoo Finance daily closes: "
                          "the average percentile (vs the previous 252 trading days) of %1 of 7 components.").arg(d.available)};
    for (const auto& c : d.components)
      // component names are declared QT_TRANSLATE_NOOP("positioning", …) in RiskIndex.cpp and stored in English
      m << QString("· %1: %2 → %3").arg(QCoreApplication::translate("positioning", c.name.toUtf8().constData()), c.rawText.isEmpty() ? fmt::dash() : c.rawText,
                                         c.ok() ? QString::number(qRound(c.percentile)) : fmt::dash());
    if (d.updated.isValid()) m << QCoreApplication::translate("global", "Inputs fetched %1.").arg(i18n::locale().toString(d.updated, "dd MMM HH:mm"));
    r.method = m.join("\n");
  } else {
    r.status = !d.error.isEmpty() && !ri.loading() ? QCoreApplication::translate("global", "NO DATA") : QCoreApplication::translate("global", "CONNECTING…");
    if (!d.error.isEmpty()) r.method = QCoreApplication::translate("global", "Risk index unavailable: %1").arg(d.error);
  }
  m_risk->setReading(r);
}

// ---------------------------------------------------------------- Central Banks
void OverviewData::pushBanks() {
  const auto& cb = macro::CentralBanks::instance();
  CentralBanks out;
  bool any = false;
  for (const char* key : {"FED", "ECB", "BOE", "BOJ", "RBA", "RBNZ", "BOC", "SNB"}) {   // reference order
    const macro::Bank* b = cb.bank(key);
    if (!b) continue;
    CentralBankRow r;
    r.name = b->code;
    r.currency = b->currency;
    r.flag = b->iso2 == "ez" ? "eu" : b->iso2;
    r.rate = b->rate;
    any |= std::isfinite(b->rate);
    r.nextMeeting = b->nextMeeting().decision;
    QStringList rs;
    if (!b->rateText.isEmpty()) rs << b->name + ": " + b->rateText;
    if (!b->rateSource.isEmpty()) rs << b->rateSource;
    if (!b->rateNote.isEmpty()) rs << b->rateNote;
    r.rateSource = rs.join("\n");
    const macro::Odds o = b->nextOdds();
    if (b->priced && o.valid()) {
      r.cut = o.cut() * 100; r.hold = o.hold * 100; r.hike = o.hike() * 100;
      r.oddsSource = QCoreApplication::translate("global", "Next meeting odds %1").arg(b->pricingSource) +
                     (b->pricedAt.isValid() ? " (" + i18n::locale().toString(b->pricedAt.toLocalTime(), "dd MMM HH:mm") + ")" : QString()) +
                     (b->pricingNote.isEmpty() ? QString() : "\n" + b->pricingNote);
    } else {
      r.oddsSource = b->pricingNote.isEmpty() ? QCoreApplication::translate("global", "No free market pricing for %1 yet.").arg(b->code) : b->pricingNote;
    }
    out.rows << r;
  }
  if (!any) out.status = QCoreApplication::translate("global", "CONNECTING…");
  if (!out.rows.isEmpty()) m_banks->setBanks(out);
}

// ---------------------------------------------------------------- Registry
void OverviewData::pushRegistry() {
  Registry reg;
  for (macro::Group g : {macro::Group::G20, macro::Group::America, macro::Group::Europe, macro::Group::Africa,
                         macro::Group::Asia, macro::Group::Oceania, macro::Group::Watch}) {
    QList<RegistryRow> rows;
    for (const macro::Economy& e : macro::registry(g)) {
      RegistryRow r;
      r.iso2 = e.iso2;
      r.name = countryName(e.name).toUpper();
      r.institution = macro::rateBadge(e);
      r.primary = macro::hasPolicyRate(e);
      r.bankName = !e.bankName.isEmpty() ? e.bankName
                   : r.primary ? QCoreApplication::translate("global", "Policy rate: BIS central bank policy-rate series")
                               : QCoreApplication::translate("global", "No policy-rate series: World Bank data only");
      rows << r;
    }
    reg.tabs << rows;
  }
  m_reg->setRegistry(reg);
}
}  // namespace overview
