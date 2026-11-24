#pragma once
// Minimal test harness.
//   TEST("sma matches the reference") { CHECK(x); CHECK_NEAR(a, b, 1e-9); CHECK_EQ(a, b); }
// Each test binary's main: return check::runAll(argc, argv);   (argv[1] = optional substring filter)
#include <QString>
#include <QDebug>
#include <functional>
#include <vector>
#include <cmath>
#include <cstdio>

namespace check {
struct Case { const char* name; std::function<void()> fn; const char* file; int line; };
inline std::vector<Case>& cases() { static std::vector<Case> c; return c; }
inline int& failures() { static int f = 0; return f; }
inline const char*& current() { static const char* c = ""; return c; }
struct Reg { Reg(const char* n, std::function<void()> f, const char* file, int line) { cases().push_back({n, std::move(f), file, line}); } };
inline void fail(const char* file, int line, const QString& msg) {
  ++failures();
  std::fprintf(stderr, "  FAIL %s:%d [%s] %s\n", file, line, current(), msg.toUtf8().constData());
}
inline int runAll(int argc, char** argv) {
  const QString filter = argc > 1 ? QString::fromUtf8(argv[1]) : QString();
  int run = 0, failedCases = 0;
  for (auto& c : cases()) {
    if (!filter.isEmpty() && !QString::fromUtf8(c.name).contains(filter, Qt::CaseInsensitive)) continue;
    current() = c.name; const int before = failures(); ++run;
    try { c.fn(); } catch (const std::exception& e) { fail(c.file, c.line, QString("exception: ") + e.what()); }
    if (failures() != before) { ++failedCases; std::fprintf(stderr, "not ok - %s\n", c.name); }
    else std::printf("ok - %s\n", c.name);
  }
  std::printf("\n%d tests, %d failed\n", run, failedCases);
  return failedCases ? 1 : 0;
}
}

#define CHECK_CAT2(a, b) a##b
#define CHECK_CAT(a, b) CHECK_CAT2(a, b)
#define TEST(NAME) \
  static void CHECK_CAT(check_fn_, __LINE__)(); \
  static const check::Reg CHECK_CAT(check_reg_, __LINE__)(NAME, CHECK_CAT(check_fn_, __LINE__), __FILE__, __LINE__); \
  static void CHECK_CAT(check_fn_, __LINE__)()
#define CHECK(c) do { if (!(c)) check::fail(__FILE__, __LINE__, QStringLiteral("CHECK(" #c ")")); } while (0)
#define CHECK_EQ(a, b) do { const auto _a = (a); const auto _b = (b); if (!(_a == _b)) { QString _m; QDebug(&_m) << "CHECK_EQ(" #a ", " #b "):" << _a << "!=" << _b; check::fail(__FILE__, __LINE__, _m); } } while (0)
#define CHECK_NEAR(a, b, eps) do { const double _a = double(a), _b = double(b); \
  const bool _ok = (std::isnan(_a) && std::isnan(_b)) || std::fabs(_a - _b) <= double(eps); \
  if (!_ok) check::fail(__FILE__, __LINE__, QString("CHECK_NEAR(" #a ", " #b "): %1 vs %2").arg(_a, 0, 'g', 17).arg(_b, 0, 'g', 17)); } while (0)
