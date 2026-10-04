#include "event_queue.hpp"

#include "pipeline.hpp"

namespace aquila {

// Both queues are header templates; instantiate them for the pipeline's element type
// here so template errors surface when this library builds, and check the contract.
template class MutexQueue<TimedEvent>;
template class SpscRingBuffer<TimedEvent>;

static_assert(BlockingQueue<MutexQueue<TimedEvent>, TimedEvent>);
static_assert(BlockingQueue<SpscRingBuffer<TimedEvent>, TimedEvent>);

}  // namespace aquila
