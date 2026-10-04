#include "matching_engine.hpp"

namespace limitbook {

// The engine is a header template so the book type is resolved at compile time
// (no virtual dispatch on the hot path). Instantiating it here once keeps every
// other translation unit from recompiling it.
template class BasicMatchingEngine<MapListOrderBook>;
template class BasicMatchingEngine<PooledOrderBook>;

}  // namespace limitbook
