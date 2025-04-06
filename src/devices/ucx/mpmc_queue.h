#ifndef MPMC_QUEUE_H_
#define MPMC_QUEUE_H_

#include <stdint.h>

typedef int64_t alf_value_type;
typedef uint64_t alf_index_type;
typedef __uint128_t alf_entry_type;

struct mpmc_queue;

int alf_enqueue (struct mpmc_queue * q, alf_value_type d);
int alf_dequeue (struct mpmc_queue * q, alf_value_type * d);
int alf_is_empty (struct mpmc_queue * q);

#endif