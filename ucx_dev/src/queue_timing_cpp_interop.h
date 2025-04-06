#ifndef QUEUE_TIMING_CPP_INTEROP_H_
#define QUEUE_TIMING_CPP_INTEROP_H_

#include "mpmc_queue.h"
#include <stdint.h>

struct mpmc_queue * queue_create (void);
void queue_destroy (struct mpmc_queue * q);

#endif
