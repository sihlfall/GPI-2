#ifndef PLQUEUE_TIMING_CPP_INTEROP_H_
#define PLQUEUE_TIMING_CPP_INTEROP_H_

#include <stdint.h>

struct plqueue_u64_rw;
struct plqueue_u64_rw * queue_create (void);
void queue_destroy (struct plqueue_u64_rw * q);
int queue_enqueue (struct plqueue_u64_rw * q, uint64_t payload);
int queue_dequeue (struct plqueue_u64_rw * q, uint64_t * payload);
int queue_is_empty (struct plqueue_u64_rw * q);

#endif
