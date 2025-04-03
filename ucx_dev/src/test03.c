#include "mpmc_queue.h"
#include <stdio.h>
#include <stdlib.h>

int main () {
  struct mpmc_queue queue = {0};
  queue_init (&queue);

  enqueue (&queue, 10);
  enqueue (&queue, 20);
  enqueue (&queue, 30);

  int v[3];
  for (int i = 0; i < 3; ++i) dequeue (&queue, &v[i]);

  printf ("%d %d %d\n", v[0], v[1], v[2]);
}