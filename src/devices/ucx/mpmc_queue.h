#ifndef MPMC_QUEUE_H_
#define MPMC_QUEUE_H_

#include <stdatomic.h>
#include <stddef.h>

typedef int alf_value_type;
typedef size_t alf_index_type;
typedef ptrdiff_t alf_index_diff_type;

#define alf_buffer_size ((alf_index_type) 1024)

typedef __uint128_t entry_t;

struct mpmc_queue {
  _Atomic(alf_index_type) write_index;
  _Atomic(alf_index_type) read_index;
  _Atomic(entry_t) buffer[alf_buffer_size];
};

int alf_enqueue (struct mpmc_queue * q, alf_value_type d);
int alf_dequeue (struct mpmc_queue * q, alf_value_type * d);

#endif