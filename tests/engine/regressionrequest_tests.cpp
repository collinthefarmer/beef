// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/RegressionRequest.h"
#include "test_support.h"

#include <limits>

using namespace BetterEnchantmentEffects;
using Regression::NoRequest;
using Regression::Outcome;
using Regression::RequestPending;
using Regression::RequestState;

int main() {
  RegressionRequest request;
  test::Check(!BeginRequest(request, 0, RequestKind::kApply),
              "missing actor refused");
  const auto first = BeginRequest(request, 20, RequestKind::kApply);
  test::Check(first.has_value(), "valid request accepted");
  test::Check(!BeginRequest(request, 21, RequestKind::kRetire),
              "overlapping request refused");
  test::Equal(request.actor, 20u, "refused request keeps the target");
  test::Check(first &&
                  StateOf(request, *first) == RequestState{RequestPending{}},
              "acceptance is not completion");
  request.state = Outcome::kAborted;
  test::Check(first && !Pending(request, *first), "an abort ends waiting");
  const auto second = BeginRequest(request, 21, RequestKind::kRetire);
  test::Check(first && second && *second > *first,
              "identifiers are never reused");
  request.state = Outcome::kPass;
  test::Check(first &&
                  StateOf(request, *first) == RequestState{Outcome::kAborted},
              "an old request cannot observe a new success");
  test::Check(second &&
                  StateOf(request, *second) == RequestState{Outcome::kPass},
              "the current request sees its success");
  test::Check(RegressionRequest{}.state == RequestState{NoRequest{}},
              "a fresh store holds no request");
  request.id = std::numeric_limits<std::uint64_t>::max();
  test::Check(!BeginRequest(request, 20, RequestKind::kApply),
              "identifier exhaustion refuses instead of wrapping");
  return test::Finish("regression requests");
}
