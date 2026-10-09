#include "../../host/src/adaptive_pickup_budget.h"
#include <cassert>
int main() {
    axrb::host::detail::AdaptivePickupBudget budget;
    assert(budget.microseconds() == 0);
    budget.observe(10'000'000, 10'000'000, 1);
    assert(budget.microseconds() == 5000);
    budget.observe(40'000'000, 10'000'000, 1'000'000'001);
    assert(budget.microseconds() == 4500);
    // No more display-time gaps; recovery is delayed until misses have expired.
    budget.observe(50'000'000, 10'000'000, 12'000'000'001);
    assert(budget.microseconds() == 5000);
    budget.observe(60'000'000, 10'000'000, 23'000'000'001);
    assert(budget.microseconds() == 5500);
    budget.observe(70'000'000, 8'000'000, 24'000'000'001);
    assert(budget.microseconds() == 4000);
}
