#include "const.h"

#ifdef MEM_DBG
#include <atomic>
std::atomic<size_t> heapalloc = 0;
#endif