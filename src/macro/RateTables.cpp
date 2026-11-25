#include "macro/RateTables.h"
#include <QRegularExpression>
#include <QStringList>
#include <QVector>
#include <algorithm>
#include <optional>

namespace macro::ratetables {
namespace {
int monthOf(const QString& t) {
  static const QStringList m{"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
  return int(m.indexOf(t.left(3).toLower())) + 1;
}

struct Token { QString text; double x = 0, y = 0, h = 0; };   // centre and height, image units

// A recognised line can hold several cells ("Implied Move (bps) Total (bps)"): its words, placed in proportion.
QList<Token> tokens(const QList<ocr::Line>& lines) {
  QList<Token> out;
  for (const auto& l : lines) {
    const QString t = l.text.simplified();
    if (t.isEmpty()) continue;
    const double per = l.box.width() / t.size();
    for (int from = 0; from < t.size();) {
      int to = int(t.indexOf(' ', from));
      if (to < 0) to = int(t.size());
      out << Token{t.mid(from, to - from), l.box.left() + per * (from + to) / 2.0, l.box.center().y(), l.box.height()};
      from = to + 1;
    }
  }
  return out;
}

QString plain(QString t) { return t.replace(QChar(0x2212), '-').replace(QChar(0x2013), '-').replace(',', '.'); }

// "20.8", "-0.3", "(5.9)": the number, if the token is one.
std::optional<double> number(const QString& t) {
  static const QRegularExpression rx("^\\(?([-+]?\\d{1,3}(?:\\.\\d{1,3})?)\\)?$");
  const auto m = rx.match(plain(t));
  if (!m.hasMatch()) return {};
  return m.captured(1).toDouble();
}
// "2.958%", "4.7480/0" (the percent sign misread): three decimals are printed.
std::optional<double> rate(const QString& t) {
  static const QRegularExpression rx("^(-?\\d{1,2}\\.\\d{3})");
  const auto m = rx.match(plain(t));
  if (!m.hasMatch()) return {};
  return m.captured(1).toDouble();
}
bool agree(double x, double y, double tol) { return std::isfinite(x) && std::isfinite(y) && std::fabs(x - y) <= tol; }
}

Table readPicture(const QList<ocr::Line>& lines, const QString& bank, double policy) {
  Table t;
  static const QStringList banks{"FED", "ECB", "BOE", "BOJ", "BOC", "RBA", "RBNZ", "SNB"};
  static const QRegularExpression effRx("Effective\\s+Rate:?\\s*(-?\\d{1,2}[.,]\\d{1,3})", QRegularExpression::CaseInsensitiveOption);
  double header = NAN;
  for (const auto& l : lines) {
    const QString s = l.text.simplified();
    if (banks.contains(s.toUpper()) && s.toUpper() != bank.toUpper()) { t.fault = Fault::OtherBank; return t; }
    if (const auto m = effRx.match(s); m.hasMatch()) header = plain(m.captured(1)).toDouble();
  }
  const QList<Token> all = tokens(lines);
  // the rows: a month at the left edge ("Dec-26")
  static const QRegularExpression monthRx("^([A-Za-z]{3})[-.\\x{2013}\\x{2014}]?(\\d{2})$");
  struct Found { QDate month; double y, h; };
  QList<Found> rows;
  for (const Token& k : all) {
    if (k.x > 0.15) continue;
    const auto m = monthRx.match(k.text);
    if (!m.hasMatch()) continue;
    if (const int mo = monthOf(m.captured(1)); mo > 0) rows << Found{QDate(2000 + m.captured(2).toInt(), mo, 1), k.y, k.h};
  }
  if (rows.isEmpty()) return t;
  std::sort(rows.begin(), rows.end(), [](const Found& a, const Found& b) { return a.y < b.y; });
  double gap = rows.first().h;   // half the smallest distance between two rows
  for (int i = 1; i < rows.size(); ++i) gap = i == 1 ? (rows[i].y - rows[i - 1].y) / 2 : std::min(gap, (rows[i].y - rows[i - 1].y) / 2);

  double eff = header, total = 0;
  int read = 0, agreed = 0;   // implied rates read, and those that go with the totals
  QVector<double> level, printed;   // the rate each row starts from (implied less total); the implied rates as printed
  QDate last;
  for (const Found& r : rows) {
    if (last.isValid() && r.month <= last) break;   // a misread month
    QList<Token> cells;
    for (const Token& k : all) if (std::fabs(k.y - r.y) < gap && k.x > 0.55) cells << k;
    std::sort(cells.begin(), cells.end(), [](const Token& a, const Token& b) { return a.x < b.x; });
    double implied = NAN;
    QVector<double> bp;
    for (const Token& c : cells) {
      if (c.x < 0.76) { if (const auto v = rate(c.text)) implied = *v; }
      else if (const auto v = number(c.text)) bp << *v;
    }
    const double move = bp.size() == 2 ? bp[0] : NAN, sum = bp.size() == 2 ? bp[1] : NAN;
    if (!std::isfinite(eff) && t.rows.isEmpty() && std::isfinite(implied) && agree(move, sum, 0.05)) eff = implied - sum / 100.0;   // no header
    const double a = sum, b = total + move, c = (implied - eff) * 100.0;
    double kept = NAN;
    if (agree(a, b, 0.25) || agree(a, c, 0.6)) kept = a;   // the implied rate has three decimals, the effective rate two
    else if (agree(b, c, 0.6)) kept = b;
    if (!std::isfinite(kept)) break;
    Row row; row.month = r.month; row.total = kept; row.move = kept - total;
    row.implied = agree(kept, c, 0.6) ? implied : eff + kept / 100.0;
    if (std::isfinite(implied)) { ++read; agreed += agree(kept, c, 0.6); level << implied - kept / 100.0; printed << implied; }
    else printed << NAN;
    t.rows << row;
    total = kept; last = r.month;
  }
  // the rate in the title can be a day behind the table's own: when every row starts from one same rate close to it, that one
  if (read >= 2 && agreed * 2 < read && std::isfinite(eff)) {
    std::sort(level.begin(), level.end());
    const double mid = level[level.size() / 2];
    if (level.last() - level.first() <= 0.012 && std::fabs(mid - eff) <= 0.025) {
      eff = mid; agreed = read;
      for (int i = 0; i < t.rows.size(); ++i) t.rows[i].implied = std::isfinite(printed[i]) ? printed[i] : eff + t.rows[i].total / 100.0;
    }
  }
  t.effective = eff;
  if (t.rows.isEmpty() || !std::isfinite(eff) || (read >= 2 && agreed * 2 < read) || (std::isfinite(policy) && std::fabs(eff - policy) > 0.35)) {
    t.rows.clear(); t.fault = Fault::Inconsistent; return t;
  }
  t.fault = Fault::None;
  return t;
}

// ------------------------------------------------------------------------------------------------ Trading Economics
namespace {
QString cell(QString s) {
  static const QRegularExpression tag("<[^>]*>");
  s.remove(tag);
  return s.replace("&nbsp;", " ").replace("&amp;", "&").replace("&#8211;", QString(QChar(0x2013))).simplified();
}
}

Page readTradingEconomics(const QByteArray& html) {
  Page out;
  const QString s = QString::fromUtf8(html);
  // the outcomes: the table headed "Target Rate"
  const qsizetype at = s.indexOf("title=\"Target Rate");
  if (at >= 0) {
    const qsizetype end = s.indexOf("</table>", at);
    const QString tab = s.mid(at, end < 0 ? -1 : end - at);
    static const QRegularExpression row("<tr[^>]*>\\s*<td[^>]*>(.*?)</td>\\s*<td[^>]*>(.*?)</td>", QRegularExpression::DotMatchesEverythingOption);
    double sum = 0;
    for (auto it = row.globalMatch(tab); it.hasNext();) {
      const auto m = it.next();
      bool okR = false, okP = false;
      const double r = cell(m.captured(1)).remove('%').toDouble(&okR), p = cell(m.captured(2)).remove('%').toDouble(&okP);
      if (okR && okP) { out.next << Outcome{r, p / 100.0}; sum += p; }
    }
    if (std::fabs(sum - 100) > 2) out.next.clear();
    for (auto& o : out.next) o.p = o.p * 100.0 / sum;
  }
  static const QHash<QString, QString> countries{{"united-states", "FED"}, {"euro-area", "ECB"}, {"united-kingdom", "BOE"}, {"japan", "BOJ"},
                                                 {"canada", "BOC"}, {"switzerland", "SNB"}, {"australia", "RBA"}, {"new-zealand", "RBNZ"}};
  // one line of the list of countries: its link, then the rate in force, the implied rate, the change, the meeting
  static const QRegularExpression line("href=\"/([a-z-]+)/implied-rates\"((?:(?!</tr>).)*)</tr>", QRegularExpression::DotMatchesEverythingOption);
  static const QRegularExpression td("<td[^>]*>(.*?)</td>", QRegularExpression::DotMatchesEverythingOption);
  for (auto it = line.globalMatch(s); it.hasNext();) {
    const auto m = it.next();
    const QString id = countries.value(m.captured(1));
    if (id.isEmpty() || out.banks.contains(id)) continue;
    QStringList c;
    for (auto jt = td.globalMatch(m.captured(2)); jt.hasNext();) c << cell(jt.next().captured(1));
    if (c.size() < 4) continue;
    Implied k; bool okC = false, okR = false, okB = false;
    k.current = c[0].remove('%').section(QChar(0x2013), -1).trimmed().toDouble(&okC);   // a range: its upper bound
    k.rate = c[1].remove('%').toDouble(&okR);
    k.bp = plain(c[2].remove("bp")).toDouble(&okB);
    const QStringList d = c[3].split(' ', Qt::SkipEmptyParts);
    if (d.size() == 3) k.meeting = QDate(2000 + d[2].right(2).toInt(), monthOf(d[1]), d[0].toInt());
    if (okC && okR && okB) out.banks.insert(id, k);
  }
  return out;
}

double expectedBp(const QList<Outcome>& next, double rate) {
  if (next.isEmpty() || !std::isfinite(rate)) return NAN;
  double e = 0; bool held = false;
  for (const auto& o : next) { e += o.p * (o.rate - rate) * 100.0; held |= std::fabs(o.rate - rate) < 0.03; }
  return held ? e : NAN;
}

QString decisionOf(const QString& currency, const QString& title) {
  static const QHash<QString, QString> banks{{"AUD", "RBA"}, {"NZD", "RBNZ"}, {"JPY", "BOJ"}, {"CHF", "SNB"}};
  static const QRegularExpression rx("^(?:(?:RBA|RBNZ|BoJ|SNB)\\s+)?(?:(?:Official )?Cash Rate|Policy Rate|Interest Rate Decision)$",
                                     QRegularExpression::CaseInsensitiveOption);
  const QString id = banks.value(currency.toUpper());
  return !id.isEmpty() && rx.match(title.simplified()).hasMatch() ? id : QString();
}

// ------------------------------------------------------------------------------------------------ investingLive, weekly
QHash<QString, Weekly> readWeekly(const QByteArray& html) {
  QHash<QString, Weekly> out;
  QString s = QString::fromUtf8(html);
  static const QRegularExpression start("Rate\\s+(?:hikes|cuts)\\s+by\\s+year-end", QRegularExpression::CaseInsensitiveOption);
  const qsizetype from = s.indexOf(start);
  if (from < 0) return out;
  s = s.mid(from, 60000);
  static const QRegularExpression tag("<[^>]*>"), ent("&nbsp;|\\x{00A0}");
  s = s.replace(tag, " ").replace(ent, " ").simplified();
  static const QRegularExpression rx(
      "(?<head>Rate (?<dir>hikes|cuts) by year-end)|"
      "\\b(?<bank>Fed|ECB|BoE|BoJ|BoC|SNB|RBA|RBNZ)\\s*:\\s*(?<bp>\\d{1,3}(?:\\.\\d+)?)\\s*bps?\\s*\\((?<p>\\d{1,3})\\s*%\\s*probability of\\s*"
      "(?<what>rate hike|rate cut|no change)[^)]*\\)(?:\\s*20\\d\\d:\\s*(?<ny>\\d{1,3})\\s*bps?(?<nycut>\\s+of rate cuts)?)?",
      QRegularExpression::CaseInsensitiveOption);
  double sign = 1;
  for (auto it = rx.globalMatch(s); it.hasNext();) {
    const auto m = it.next();
    if (!m.captured("head").isEmpty()) { sign = m.captured("dir").compare("cuts", Qt::CaseInsensitive) == 0 ? -1 : 1; continue; }
    const QString id = m.captured("bank").toUpper();
    if (out.contains(id)) continue;      // the first statement: the page repeats the article in its data
    Weekly w;
    w.chance = m.captured("p").toInt() / 100.0;
    if (w.chance > 1) continue;
    const QString what = m.captured("what").toLower();
    w.what = what == "rate hike" ? "hike" : what == "rate cut" ? "cut" : "hold";
    w.yearEndBp = sign * m.captured("bp").toDouble();
    if (w.what == "hike") w.nextBp = 25.0 * w.chance;
    else if (w.what == "cut") w.nextBp = -25.0 * w.chance;
    else w.nextBp = sign * 25.0 * (1 - w.chance);
    if (!m.captured("ny").isEmpty()) w.nextYearBp = (m.captured("nycut").isEmpty() ? sign : -1.0) * m.captured("ny").toDouble();
    out.insert(id, w);
  }
  return out;
}
}
