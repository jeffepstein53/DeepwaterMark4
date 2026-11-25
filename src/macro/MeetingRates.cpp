#include "macro/CentralBanks.h"
#include <cmath>

// The meeting-by-meeting path behind the odds: pure arithmetic, tested in tests/calendar.
namespace macro {
QVector<double> solveMeetingRates(double r0, const QVector<QDate>& eff, const QVector<RateContract>& cs, const QVector<double>& hint) {
  const int M = eff.size();
  const auto hinted = [&hint](int k) { const double h = hint.value(k - 1, 0.0); return std::isfinite(h) ? h : 0.0; };   // regime k follows meeting k
  QVector<double> r(M + 1, NAN); r[0] = r0;
  int solved = 0;   // highest regime index with a known rate
  for (const auto& c : cs) {
    if (!std::isfinite(c.avg) || !c.start.isValid() || c.end <= c.start) continue;
    QVector<double> w(M + 1, 0.0);
    int kmax = 0;
    for (QDate d = c.start; d < c.end; d = d.addDays(1)) {
      int k = 0; while (k < M && eff[k] <= d) ++k;
      w[k] += 1; kmax = qMax(kmax, k);
    }
    const double T = c.end.daysTo(c.start) * -1.0;
    if (kmax <= solved) continue;   // nothing new in this contract
    double known = 0, wU = 0, wUk = 0, cum = 0, wCum = 0;
    for (int k = 0; k <= kmax; ++k) {
      if (k <= solved) known += w[k] * r[k];
      else { wU += w[k]; wUk += w[k] * (k - solved); cum += hinted(k); wCum += w[k] * cum; }
    }
    if (wU / T < 0.2 || wUk <= 0) continue;   // too little weight: let a later contract decide
    const double delta = (c.avg * T - known - r[solved] * wU - wCum) / wUk;
    for (int k = solved + 1; k <= kmax; ++k) r[k] = r[k - 1] + hinted(k) + delta;
    solved = kmax;
  }
  return r.mid(1);
}

QVector<Odds> oddsFromPath(double r0, const QVector<double>& rates) {
  QVector<Odds> out;
  // distribution over cumulative 25 bp steps, index offset 40
  const int N = 81, Z = 40;
  QVector<double> dist(N, 0.0); dist[Z] = 1;
  double prev = r0;
  for (double r : rates) {
    if (!std::isfinite(r)) break;
    const double steps = (r - prev) / 0.25;
    const int k = int(std::floor(steps));
    const double f = steps - k;
    QVector<double> nd(N, 0.0);
    for (int i = 0; i < N; ++i) {
      if (dist[i] == 0) continue;
      const int a = qBound(0, i + k, N - 1), b = qBound(0, i + k + 1, N - 1);
      nd[a] += dist[i] * (1 - f); nd[b] += dist[i] * f;
    }
    dist = nd; prev = r;
    Odds o; o.cut50 = 0; o.cut25 = dist[Z - 1]; o.hold = dist[Z]; o.hike25 = dist[Z + 1]; o.hike50 = 0;
    for (int i = 0; i < Z - 1; ++i) o.cut50 += dist[i];
    for (int i = Z + 2; i < N; ++i) o.hike50 += dist[i];
    out << o;
  }
  return out;
}
}
