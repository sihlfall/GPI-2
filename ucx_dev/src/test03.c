#include "mpmc_queue.h"
#include <stdio.h>
#include <stdlib.h>

int main () {
  struct mpmc_queue queue = {0};

  int j = 0;
  for (int k = 0; k < 10; ++k)
  {
    for (int i = 0; i < 500; ++i, ++j) alf_enqueue (&queue, 10 * j);

    int v;
    for (int i = 0; i < 500; ++i)
    {
      if (alf_dequeue (&queue, &v)) printf( "%d\n", v);
    }
  }
}