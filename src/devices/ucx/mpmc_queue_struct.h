#ifndef MPMC_QUEUE_STRUCT_H_
#define MPMC_QUEUE_STRUCT_H_

/*
 * Separate header to improve interoperability with C++.
 */
#include "mpmc_queue.h"
#include <stdatomic.h>

#define ALF_CACHELINESIZE 64

#define ALF_BUFFER_SIZE ((alf_index_type) 64)

typedef int64_t alf_value_type;
typedef uint64_t alf_index_type;
typedef __uint128_t alf_entry_type;

struct mpmc_queue {
  _Alignas (2 * ALF_CACHELINESIZE) _Atomic(alf_index_type) write_index;
  _Alignas (2 * ALF_CACHELINESIZE) _Atomic(alf_index_type) read_index;
  _Alignas (2 * ALF_CACHELINESIZE) _Atomic(alf_entry_type) buffer[ALF_BUFFER_SIZE];
};

#endif