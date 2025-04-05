#ifndef QUEUE_TIMING_CPP_INTEROP_H_
#define QUEUE_TIMING_CPP_INTEROP_H_

#include <stdint.h>

struct mpmc_queue;

typedef int64_t alf_value_type;

struct mpmc_queue * queue_create (void);
void queue_destroy (struct mpmc_queue * q);
int queue_push (struct mpmc_queue * q, int64_t v);
int queue_pop (struct mpmc_queue * q, int64_t * v);
int queue_is_empty (struct mpmc_queue * q);

#endif
