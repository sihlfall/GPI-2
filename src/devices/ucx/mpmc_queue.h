#ifndef MPMC_QUEUE_H_
#define MPMC_QUEUE_H_

#include <stdatomic.h>
#include <stdint.h>

#define ALF_CACHELINESIZE 64

typedef int64_t alf_value_type;
typedef uint64_t alf_index_type;
typedef __uint128_t alf_entry_type;

#define ALF_BUFFER_SIZE ((alf_index_type) 64)

struct mpmc_queue {
  _Alignas (2 * ALF_CACHELINESIZE) _Atomic(alf_index_type) write_index;
  _Alignas (2 * ALF_CACHELINESIZE) _Atomic(alf_index_type) read_index;
  _Alignas (2 * ALF_CACHELINESIZE) _Atomic(alf_entry_type) buffer[ALF_BUFFER_SIZE];
};

int alf_enqueue (struct mpmc_queue * q, alf_value_type d);
int alf_dequeue (struct mpmc_queue * q, alf_value_type * d);
int alf_is_empty (struct mpmc_queue * q);

#endif