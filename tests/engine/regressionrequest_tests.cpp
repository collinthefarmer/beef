// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/RegressionRequest.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;

int main() {
  RegressionRequest request;
  test::Equal(request.Begin(0, false), 0, "missing actor rejected");
  const auto first = request.Begin(20, false);
  test::Check(first > 0, "valid request accepted");
  test::Equal(request.Begin(21, true), 0, "overlapping request rejected");
  test::Equal(request.actor, 20u, "rejected request preserves target");
  test::Equal(request.Result(first), std::string{"WAITING"},
              "acceptance is not completion");
  request.Cancel();
  test::Check(!request.Pending(first), "cancel prevents dispatch");
  test::Equal(request.Result(first), std::string{"ABORTED"},
              "cancel is observable");
  const auto second = request.Begin(21, true);
  test::Check(second > first, "identifiers are never reused");
  request.result = "PASS";
  test::Equal(request.Result(first), std::string{"ABORTED"},
              "old request cannot observe new success");
  test::Equal(request.Result(second), std::string{"PASS"},
              "current request sees success");
  request.id = std::numeric_limits<std::int32_t>::max();
  test::Equal(request.Begin(20, false), 0,
              "identifier exhaustion refuses instead of wrapping");
  return test::Finish("regression requests");
}
