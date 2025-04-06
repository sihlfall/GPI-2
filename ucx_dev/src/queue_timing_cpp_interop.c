#include "queue_timing_cpp_interop.h"
#include "mpmc_queue_struct.h"
#include <stdlib.h>

struct mpmc_queue * queue_create (void) {
  struct mpmc_queue * q = calloc (1, sizeof(*q));
  return q;
}

void queue_destroy (struct mpmc_queue * q) {
  if (q) free (q);
}
