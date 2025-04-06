#ifndef MPMC_QUEUE_STRUCT_H_
#define MPMC_QUEUE_STRUCT_H_

/*
 * Separate header to improve interoperability with C++.
 */
#include "mpmc_queue.h"
#include <stdatomic.h>

#define ALF_CACHELINESIZE 64

#define ALF_BUFFER_SIZE ((alf_index_type) 64)

struct mpmc_queue {
  _Alignas (2 * ALF_CACHELINESIZE) _Atomic(alf_index_type) write_index;
  _Alignas (2 * ALF_CACHELINESIZE) _Atomic(alf_index_type) read_index;
  _Alignas (2 * ALF_CACHELINESIZE) _Atomic(alf_entry_type) buffer[ALF_BUFFER_SIZE];
};

#endif