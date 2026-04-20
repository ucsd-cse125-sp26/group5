#include <iostream>

namespace shared {
void hello() { std::cout << "Hello World Shared"; }
}  // namespace shared

// TEMPORARY: intentional build break to verify CI gating. Revert before merging.
static_assert(false, "intentional build break to verify CI gating");
