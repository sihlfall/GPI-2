#include "mpmc_queue_struct.h"
#include <stdio.h>
#include <stdlib.h>

int main () {
  struct mpmc_queue queue = {0};
  //_Static_assert(sizeof(queue.buffer[0]) == sizeof(alf_entry_type), "");

  int j = 0;
  for (int k = 0; k < 10; ++k)
  {
    for (int i = 0; i < 500; ++i, ++j) alf_enqueue (&queue, 10 * j);

    int64_t v;
    for (int i = 0; i < 500; ++i)
    {
      if (alf_dequeue (&queue, &v)) printf( "%ld\n", v);
    }
  }
}