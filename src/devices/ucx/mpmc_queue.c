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

static inline
VALUE_TYPE
entry_get_data (entry_t e)
{
  return (VALUE_TYPE) e;
}

static inline
INDEX_TYPE
entry_get_seq (entry_t e)
{
  return e >> 64;
}

static inline
entry_t
entry_create (VALUE_TYPE data, INDEX_TYPE seq)
{
  return ((entry_t) seq << 64) | (entry_t) data;
}

static inline
INDEX_TYPE
trunc_seq (INDEX_TYPE seq)
{
  /* We need a bitmask of bitsof(INDEX_TYPE) - log2(BUFFER_SIZE) + 1 ones */
  INDEX_TYPE mask = (
    ( (INDEX_TYPE) 1 << (8 * sizeof(INDEX_TYPE) - 1) ) / ((INDEX_TYPE) BUFFER_SIZE / 2) * 4 - 1
  );
  return seq & mask;
}

static inline
entry_t
buffer_get_value (struct mpmc_queue * q, INDEX_TYPE i)
{
  return __sync_val_compare_and_swap (&q->buffer[(i) & (BUFFER_SIZE - 1)], 0, 0);
}

static inline
_Atomic(entry_t) *
buffer_get_pointer (struct mpmc_queue * q, INDEX_TYPE i)
{
  return &q->buffer[(i) & (BUFFER_SIZE - 1)];
}

int
alf_enqueue (struct mpmc_queue * q, VALUE_TYPE d)
{
  while (1)
  {
    INDEX_TYPE wr_index = __sync_val_compare_and_swap (&q->write_index, 0, 0);
    INDEX_TYPE wr_seq = trunc_seq ((wr_index / BUFFER_SIZE) * 2);
    INDEX_TYPE seq = entry_get_seq (buffer_get_value (q, wr_index));

    INDEX_TYPE delta = trunc_seq (seq - wr_seq);
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
alf_dequeue (struct mpmc_queue * q, VALUE_TYPE * d)
{
  while (1)
  {
    INDEX_TYPE rd_index = __sync_val_compare_and_swap (&q->read_index, 0, 0);
    INDEX_TYPE rd_seq = trunc_seq ((rd_index / BUFFER_SIZE) * 2);
    entry_t e = __sync_val_compare_and_swap (buffer_get_pointer (q, rd_index), 0, 0);
    INDEX_TYPE seq = entry_get_seq (e);

    INDEX_TYPE delta = trunc_seq (seq - rd_seq);
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
