#pragma once

#include "mutex_deque_mpmc_queue_v2.hpp"

namespace yutil::queue {

template <typename T>
using MutexDequeMpmcQueueBaseline = MutexDequeMpmcQueueV2<T>;

}  // namespace yutil::queue
