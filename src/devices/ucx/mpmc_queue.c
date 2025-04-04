/*
 * C implementation of a lock-free multi-producer multi-consumer queue
 *
 * Original implementation in C++ developed and Copyright (c) 2019 by Erez Strauss
 * https://github.com/erez-strauss/lockfree_mpmc_queue
 * under the MIT License:
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "mpmc_queue.h"
#include <stdint.h>

#define NAIVE_LOG2(i) \
  ((i) == ((uint64_t) 1) << 32 ? 32 : \
   (i) == ((uint64_t) 1) << 31 ? 31 : \
   (i) == ((uint64_t) 1) << 30 ? 30 : \
   (i) == ((uint64_t) 1) << 29 ? 29 : \
   (i) == ((uint64_t) 1) << 28 ? 28 : \
   (i) == ((uint64_t) 1) << 27 ? 27 : \
   (i) == ((uint64_t) 1) << 26 ? 26 : \
   (i) == ((uint64_t) 1) << 25 ? 25 : \
   (i) == ((uint64_t) 1) << 24 ? 24 : \
   (i) == ((uint64_t) 1) << 23 ? 23 : \
   (i) == ((uint64_t) 1) << 22 ? 22 : \
   (i) == ((uint64_t) 1) << 21 ? 21 : \
   (i) == ((uint64_t) 1) << 20 ? 20 : \
   (i) == ((uint64_t) 1) << 19 ? 19 : \
   (i) == ((uint64_t) 1) << 18 ? 18 : \
   (i) == ((uint64_t) 1) << 17 ? 17 : \
   (i) == ((uint64_t) 1) << 16 ? 16 : \
   (i) == ((uint64_t) 1) << 15 ? 15 : \
   (i) == ((uint64_t) 1) << 14 ? 14 : \
   (i) == ((uint64_t) 1) << 13 ? 13 : \
   (i) == ((uint64_t) 1) << 12 ? 12 : \
   (i) == ((uint64_t) 1) << 11 ? 11 : \
   (i) == ((uint64_t) 1) << 10 ? 10 : \
   (i) == ((uint64_t) 1) <<  9 ?  9 : \
   (i) == ((uint64_t) 1) <<  8 ?  8 : \
   (i) == ((uint64_t) 1) <<  7 ?  7 : \
   (i) == ((uint64_t) 1) <<  6 ?  6 : \
   (i) == ((uint64_t) 1) <<  5 ?  5 : \
   (i) == ((uint64_t) 1) <<  4 ?  4 : \
   (i) == ((uint64_t) 1) <<  3 ?  3 : \
   (i) == ((uint64_t) 1) <<  2 ?  2 : \
   (i) == ((uint64_t) 1) <<  1 ?  1 : \
   (i) == ((uint64_t) 1) <<  0 ?  0 : \
   -1                                 \
  )

_Static_assert(NAIVE_LOG2(alf_buffer_size) != -1, "");


static inline
alf_value_type
entry_get_data (entry_t e)
{
  return (alf_value_type) e;
}

static inline
alf_index_type
entry_get_seq (entry_t e)
{
  return e >> 64;
}

static inline
entry_t
entry_create (alf_value_type data, alf_index_type seq)
{
  return ((entry_t) seq << 64) | (entry_t) data;
}

static inline
alf_index_type
trunc_seq (alf_index_type seq)
{
  /* We need a bitmask of bitsof(alf_index_type) - log2(alf_buffer_size) + 1 ones */
  alf_index_type mask = (
    ( (alf_index_type) 1 << (8 * sizeof(alf_index_type) - 1) ) / ((alf_index_type) alf_buffer_size / 2) * 4 - 1
  );
  return seq & mask;
}

static inline
entry_t
buffer_get_value (struct mpmc_queue * q, alf_index_type i)
{
  return __sync_val_compare_and_swap (&q->buffer[(i) & (alf_buffer_size - 1)], 0, 0);
}

static inline
_Atomic(entry_t) *
buffer_get_pointer (struct mpmc_queue * q, alf_index_type i)
{
  return &q->buffer[(i) & (alf_buffer_size - 1)];
}

int
alf_enqueue (struct mpmc_queue * q, alf_value_type d)
{
  while (1)
  {
    alf_index_type wr_index = __sync_val_compare_and_swap (&q->write_index, 0, 0);
    alf_index_type wr_seq = trunc_seq ((wr_index / alf_buffer_size) * 2);
    alf_index_type seq = entry_get_seq (buffer_get_value (q, wr_index));

    alf_index_type delta = trunc_seq (seq - wr_seq);
    if (delta == 0u)
    {
      entry_t e = entry_create (0, wr_seq);
      entry_t data_entry = entry_create (d, wr_seq + 1u);
      if (__sync_bool_compare_and_swap (buffer_get_pointer (q, wr_index), e, data_entry))
      {
          (void) __sync_val_compare_and_swap (&q->write_index, wr_index, wr_index + 1);
      }
      return 1;
    }
    else if (delta <= 2u)
    {
      (void) __sync_val_compare_and_swap (&q->write_index, wr_index, wr_index + 1);
    }
    else if (delta == 3u)
    {
      return 0;
    }
  }
}

int
alf_dequeue (struct mpmc_queue * q, alf_value_type * d)
{
  while (1)
  {
    alf_index_type rd_index = __sync_val_compare_and_swap (&q->read_index, 0, 0);
    alf_index_type rd_seq = trunc_seq ((rd_index / alf_buffer_size) * 2);
    entry_t e = __sync_val_compare_and_swap (buffer_get_pointer (q, rd_index), 0, 0);
    alf_index_type seq = entry_get_seq (e);

    alf_index_type delta = trunc_seq (seq - rd_seq);
    if (delta == 1u)
    {
      entry_t empty_entry = entry_create (0, trunc_seq (rd_seq + 2u));
      if (__sync_bool_compare_and_swap (buffer_get_pointer (q, rd_index), e, empty_entry))
      {
        * d = entry_get_data (e);
        (void) __sync_val_compare_and_swap (&q->read_index, rd_index, rd_index + 1);
        return 1;
      }
    }
    else if (delta == 0u)
    {
      return 0;
    }
    else if (delta <= 3u)
    {
      (void) __sync_val_compare_and_swap (&q->read_index, rd_index, rd_index + 1);
    }
  }
}
