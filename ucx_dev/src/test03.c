#include "mpmc_queue_struct.h"
#include <stdio.h>
#include <stdlib.h>

int main () {
  struct mpmc_queue queue = {0};
  //_Static_assert(sizeof(queue.buffer[0]) == sizeof(alf_entry_type), "");

  int j = 0;
  for (int k = 0; k < 10; ++k)
  {
    for (int i = 0; i < 500; ++i, ++j) {
      while (1) {
        int ok = alf_enqueue (&queue, (struct alf_tag_payload_pair) { .tag = j, .payload = 10 * j });
        if (ok) break;

        printf( "Queue full at element %d; dequeuing\n", j);
        struct alf_tag_payload_pair v = {0};
        if (alf_dequeue (&queue, &v)) printf( "%d %ld\n", v.tag, v.payload); else printf ( "no value\n");
      }
    }

    struct alf_tag_payload_pair v = {0};
    for (int i = 0; i < 500; ++i)
    {
      if (alf_dequeue (&queue, &v)) printf( "%d %ld\n", v.tag, v.payload); else printf ( "no value\n");
    }
  }
}