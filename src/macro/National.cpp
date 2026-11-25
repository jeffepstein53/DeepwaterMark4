#include "macro/National.h"
#include <QCoreApplication>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace macro::national {

using A = official::Agency;

const QList<Def>& defs() {
  static const QList<Def> d{
    {"us", Ind::Inflation, A::Bls, "CUUR0000SA0", true, "CPI-U all items, y/y"},
    {"us", Ind::CoreInflation, A::Bls, "CUUR0000SA0L1E", true, "CPI-U less food and energy, y/y"},
    {"us", Ind::Unemployment, A::Bls, "LNS14000000", false, "unemployment rate, SA"},
    {"gb", Ind::Inflation, A::Ons, "economy/inflationandpriceindices/timeseries/d7g7/mm23", false, "CPI, y/y"},
    {"gb", Ind::CoreInflation, A::Ons, "economy/inflationandpriceindices/timeseries/dko8/mm23", false, "CPI less energy, food, alcohol and tobacco, y/y"},
    {"gb", Ind::Unemployment, A::Ons, "employmentandlabourmarket/peoplenotinwork/unemployment/timeseries/mgsx/lms", false, "unemployment rate, three-month average (middle month)"},
    {"jp", Ind::Inflation, A::EstatDashboard, "0703010601010030000;0703010501010030000", false, "CPI all items, y/y"},
    {"jp", Ind::CoreInflation, A::EstatDashboard, "0703010601010030010;0703010501010030010", false, "CPI less fresh food, y/y", true},
    {"ch", Ind::Inflation, A::SnbCube, "plkopr;VVP", false, "CPI, y/y"},
    {"ch", Ind::Unemployment, A::SnbCube, "amarbma;S1", false, "registered unemployment rate (SECO), SA"},
    {"ca", Ind::Inflation, A::StatCan, "41690973", true, "CPI all items, y/y"},
    {"ca", Ind::Unemployment, A::StatCan, "2062815", false, "unemployment rate, SA"},
    {"au", Ind::Inflation, A::Abs, "CPI/3.10001.10.50.M", false, "CPI all groups, y/y"},
    {"au", Ind::CoreInflation, A::Abs, "CPI/3.999902.20.50.M", false, "trimmed mean CPI, y/y", true},
    {"au", Ind::Unemployment, A::Abs, "LF/M13.3.1599.20.AUS.M", false, "unemployment rate, SA"},
    {"au", Ind::RetailSalesYoy, A::Abs, "HSI_M/9.TOT.CUR.20.AUS.M", false, "household spending, current prices, SA, y/y (replaces retail trade, ended 2025)", true},
  };
  return d;
}

int ttlSec(A a) { return a == A::Bls ? 12 * 3600 : 3 * 3600; }

Series build(const Def& d, const official::Answer& a) {
  Series s;
  s.ind = d.ind; s.iso2 = d.iso2; s.source = official::agencyName(d.agency); s.dataset = d.dataset;
  if (d.agency == A::SnbCube) s.source = d.ind == Ind::Unemployment ? QStringLiteral("SECO via SNB") : QStringLiteral("BFS via SNB");
  const official::Points pts = d.yoy ? official::pctChange(a.pts, 12) : a.pts;
  for (const auto& [date, v] : pts) if (std::isfinite(v)) s.pts << Point{date, v};
  s.freq = s.pts.size() >= 2 && s.pts[s.pts.size() - 2].date.addMonths(3) <= s.pts.last().date ? 'Q' : 'M';
  return s;
}

Series overlay(const Series& base, const Series& nat, bool instead) {
  if (!nat.ok()) return base;
  if (instead || !base.ok()) return nat;
  if (base.freq == nat.freq && base.last().date > nat.last().date) return base;
  Series out = nat;
  QVector<Point> head;
  if (base.freq == nat.freq)
    for (const auto& p : base.pts) if (p.date < nat.pts.first().date) head << p;
  out.pts = head + nat.pts;
  if (!head.isEmpty() && !base.dataset.contains("history: ") && base.source != nat.source)
    out.dataset = nat.dataset + " (history: " + base.source + " " + base.dataset + ")";
  else if (!head.isEmpty() && base.source == nat.source) out.dataset = base.dataset;
  return out;
}

// ------------------------------------------------------------------------------------------------ the calendar's figure
namespace {
struct Rule {
  const char* ccy; const char* iso2; QRegularExpression rx; Ind ind; char freq;
  Rule(const char* c, const char* i, const char* re, Ind n, char f) : ccy(c), iso2(i), rx(QString::fromLatin1(re)), ind(n), freq(f) {}
};
const QList<Rule>& rules() {
  static const QList<Rule> r{
    {"USD", "us", "^cpi y/y$", Ind::Inflation, 'M'},
    {"USD", "us", "^core cpi y/y$", Ind::CoreInflation, 'M'},
    {"USD", "us", "^unemployment rate$", Ind::Unemployment, 'M'},
    {"USD", "us", "^(advance|prelim|final) gdp q/q$", Ind::GdpQoq, 'Q'},
    {"EUR", "ez", "^(cpi flash estimate|final cpi) y/y$", Ind::Inflation, 'M'},
    {"EUR", "ez", "^(core cpi flash estimate|final core cpi) y/y$", Ind::CoreInflation, 'M'},
    {"EUR", "ez", "^unemployment rate$", Ind::Unemployment, 'M'},
    {"EUR", "ez", "^((prelim |flash |prelim flash |revised |final )?gdp) q/q$", Ind::GdpQoq, 'Q'},
    {"GBP", "gb", "^cpi y/y$", Ind::Inflation, 'M'},
    {"GBP", "gb", "^core cpi y/y$", Ind::CoreInflation, 'M'},
    {"GBP", "gb", "^unemployment rate$", Ind::Unemployment, 'M'},
    {"GBP", "gb", "^(prelim |final |revised )?gdp q/q$", Ind::GdpQoq, 'Q'},
    {"JPY", "jp", "^national core cpi y/y$", Ind::CoreInflation, 'M'},
    {"JPY", "jp", "^unemployment rate$", Ind::Unemployment, 'M'},
    {"JPY", "jp", "^(prelim |final |revised )?gdp q/q$", Ind::GdpQoq, 'Q'},
    {"CHF", "ch", "^unemployment rate$", Ind::Unemployment, 'M'},
    {"CHF", "ch", "^(prelim |final |flash )?gdp q/q$", Ind::GdpQoq, 'Q'},
    {"CAD", "ca", "^cpi y/y$", Ind::Inflation, 'M'},
    {"CAD", "ca", "^unemployment rate$", Ind::Unemployment, 'M'},
    {"AUD", "au", "^cpi y/y$", Ind::Inflation, 'M'},
    {"AUD", "au", "^trimmed mean cpi y/y$", Ind::CoreInflation, 'M'},
    {"AUD", "au", "^unemployment rate$", Ind::Unemployment, 'M'},
    {"AUD", "au", "^gdp q/q$", Ind::GdpQoq, 'Q'},
    {"NZD", "nz", "^unemployment rate$", Ind::Unemployment, 'Q'},
    {"NZD", "nz", "^gdp q/q$", Ind::GdpQoq, 'Q'},
  };
  return r;
}
QDate localDate(const QString& ccy, const QDateTime& utc) {
  // the calendar day where it is published (Tokyo, Sydney and Wellington are a day ahead late in the UTC day)
  const bool east = ccy == "JPY" || ccy == "AUD" || ccy == "NZD";
  return utc.toUTC().addSecs(east ? 9 * 3600 : 0).date();
}
}

QDate periodOf(const QString& ccy, const QString& title, char freq, const QDateTime& utc) {
  const QDate d = localDate(ccy, utc);
  if (!d.isValid()) return {};
  if (freq == 'Q') return QDate(d.year(), ((d.month() - 1) / 3) * 3 + 1, 1).addMonths(-3);
  const QDate first(d.year(), d.month(), 1);
  const QString t = title.toLower();
  int lag = 1;
  if (t.contains("flash estimate") && d.day() >= 25) lag = 0;                       // euro-area flash, out before the month ends
  else if (ccy == "GBP" && t == "unemployment rate") lag = 3;                       // three months, named by the middle one
  else if ((ccy == "EUR" || ccy == "JPY") && t == "unemployment rate" && d.day() <= 12) lag = 2;
  return first.addMonths(-lag);
}

CalendarFigure flashOf(const QString& ccy, const QString& title, double value, const QDateTime& utc, const QString& source) {
  CalendarFigure f;
  if (!std::isfinite(value) || !utc.isValid()) return f;
  const QString t = title.trimmed().toLower();
  for (const Rule& r : rules()) {
    if (ccy != QLatin1String(r.ccy)) continue;
    if (!r.rx.match(t).hasMatch()) continue;
    f.iso2 = r.iso2; f.ind = r.ind; f.freq = r.freq;
    f.period = periodOf(ccy, t, r.freq, utc);
    f.value = ccy == "USD" && r.ind == Ind::GdpQoq ? (std::pow(1 + value / 100, 0.25) - 1) * 100 : value;
    f.source = source; f.published = utc;
    break;
  }
  return f;
}

CalendarFigure flashOfDetail(const QString& ccy, const QString& name, double value, const QDateTime& utc, const QString& source) {
  CalendarFigure f;
  static const QRegularExpression nz(R"(^(new zealand|nz) retail sales (yoy|y/y)$)", QRegularExpression::CaseInsensitiveOption);
  if (ccy == "NZD" && std::isfinite(value) && nz.match(name.trimmed()).hasMatch()) {
    f.iso2 = "nz"; f.ind = Ind::RetailSalesYoy; f.freq = 'Q'; f.period = periodOf(ccy, name, 'Q', utc);
    f.value = value; f.source = source; f.published = utc;
  }
  return f;
}

Series withFlash(const Series& s, const CalendarFigure& f, const QDateTime& now) {
  if (!f.valid()) return s;
  Series out = s;
  const QString source = QCoreApplication::translate("economy", "%1 (calendar)").arg(f.source);
  const auto fresh = [&] {
    out.ind = f.ind; out.iso2 = f.iso2; out.freq = f.freq; out.source = source; out.dataset = QStringLiteral("release calendar");
    out.pts = {Point{f.period, f.value}};
  };
  if (!s.ok()) { fresh(); return out; }
  if (s.freq != f.freq) return s;
  if (f.period > s.last().date) {
    if (s.last().date.addMonths(13) < f.period) { fresh(); return out; }   // the series stopped long ago
    out.pts << Point{f.period, f.value};
    out.source = source;
    return out;
  }
  // a newer estimate of the same quarter, before the compilation has it
  if (f.ind == Ind::GdpQoq && f.period == s.last().date && f.published.daysTo(now) <= 25 && std::abs(f.value - s.last().value) >= 0.1) {
    out.pts.last().value = f.value;
    out.source = source;
  }
  return out;
}
}
