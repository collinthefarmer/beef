#pragma once

#include <cmath>
#include <concepts>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>

#ifndef BEEF_FIXTURES_DIR
#define BEEF_FIXTURES_DIR "tests/fixtures"
#endif
#ifndef BEEF_TEST_OUT_DIR
#define BEEF_TEST_OUT_DIR "build/native-tests"
#endif

namespace test {
inline int failures = 0;
inline int checks = 0;

inline void Fail(std::string_view a_what, const std::source_location &a_loc) {
  ++failures;
  std::printf("%s:%u: FAIL: %.*s\n", a_loc.file_name(), a_loc.line(),
              static_cast<int>(a_what.size()), a_what.data());
}

inline void
Check(bool a_ok, std::string_view a_what,
      const std::source_location &a_loc = std::source_location::current()) {
  ++checks;
  if (!a_ok) {
    Fail(a_what, a_loc);
  }
}

template <class T>
concept Printable = std::formattable<T, char>;

template <class A, class B>
  requires std::equality_comparable_with<A, B>
void Equal(
    const A &a_actual, const B &a_expected, std::string_view a_what,
    const std::source_location &a_loc = std::source_location::current()) {
  ++checks;
  if (a_actual == a_expected) {
    return;
  }
  if constexpr (Printable<A> && Printable<B>) {
    Fail(std::format("{} (actual {}, expected {})", a_what, a_actual,
                     a_expected),
         a_loc);
  } else {
    Fail(a_what, a_loc);
  }
}

[[nodiscard]] inline bool Near(float a_a, float a_b, float a_eps = 1e-4f) {
  return std::fabs(a_a - a_b) <= a_eps;
}

inline void
Near(float a_actual, float a_expected, std::string_view a_what,
     float a_eps = 1e-4f,
     const std::source_location &a_loc = std::source_location::current()) {
  ++checks;
  if (!Near(a_actual, a_expected, a_eps)) {
    Fail(std::format("{} (actual {}, expected {} within {})", a_what, a_actual,
                     a_expected, a_eps),
         a_loc);
  }
}

inline void
Skip(std::string_view a_what,
     const std::source_location &a_loc = std::source_location::current()) {
  ++checks;
  ++failures;
  std::printf("%s:%u: SKIP (counted as failure): %.*s\n", a_loc.file_name(),
              a_loc.line(), static_cast<int>(a_what.size()), a_what.data());
}

[[nodiscard]] inline std::filesystem::path Fixtures() {
  return std::filesystem::path{BEEF_FIXTURES_DIR};
}

[[nodiscard]] inline std::filesystem::path
ScratchDir(std::string_view a_suite) {
  const std::filesystem::path out =
      std::filesystem::path{BEEF_TEST_OUT_DIR} / "scratch" / a_suite;
  std::filesystem::remove_all(out);
  std::filesystem::create_directories(out);
  return out;
}

[[nodiscard]] inline std::string ReadFile(const std::filesystem::path &a_path) {
  std::ifstream in(a_path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

inline bool WriteFile(const std::filesystem::path &a_path,
                      std::string_view a_text) {
  std::ofstream out(a_path, std::ios::binary);
  out << a_text;
  return static_cast<bool>(out);
}

inline int Finish(const char *a_name) {
  if (failures == 0) {
    std::printf("all %d %s checks passed\n", checks, a_name);
  } else {
    std::printf("%d of %d %s checks failed\n", failures, checks, a_name);
  }
  return failures == 0 ? 0 : 1;
}
}
