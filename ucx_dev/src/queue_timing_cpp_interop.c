#include "queue_timing_cpp_interop.h"
#include "mpmc_queue.h"
#include <stdlib.h>

struct mpmc_queue * queue_create (void) {
  struct mpmc_queue * q = calloc (1, sizeof(*q));
  return q;
}

void queue_destroy (struct mpmc_queue * q) {
  if (q) free (q);
}

int queue_push (struct mpmc_queue * q, int64_t v) {
  return alf_enqueue (q, v);
}

int queue_pop (struct mpmc_queue * q, int64_t * v) {
  return alf_dequeue (q, v);
}

int queue_is_empty (struct mpmc_queue * q) {
  return alf_is_empty (q);
}
