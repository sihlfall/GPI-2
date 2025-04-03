#ifndef MPMC_QUEUE_H_
#define MPMC_QUEUE_H_

#include <stdatomic.h>
#include <stddef.h>

#define VALUE_TYPE int
#define INDEX_TYPE size_t
#define BUFFER_SIZE 1024

typedef __uint128_t entry_t;

struct mpmc_queue {
  _Atomic(INDEX_TYPE) write_index;
  _Atomic(INDEX_TYPE) read_index;
  _Atomic(entry_t) buffer[BUFFER_SIZE];
};

int alf_enqueue (struct mpmc_queue * q, VALUE_TYPE d);
int alf_dequeue (struct mpmc_queue * q, VALUE_TYPE * d);

#endif