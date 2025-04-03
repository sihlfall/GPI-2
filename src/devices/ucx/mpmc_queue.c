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
entry_get_seq (entry_t e) {
  return e >> 64;
}

static inline
entry_t
entry_create (VALUE_TYPE data, INDEX_TYPE seq) {
  return ((entry_t) seq << 64) | (entry_t) data;
}

static inline
entry_t
get_buffer_element (struct mpmc_queue * q, INDEX_TYPE i)
{
  return __sync_val_compare_and_swap (&q->buffer[(i) & (BUFFER_SIZE - 1)], 0, 0);
}

static inline
_Atomic(entry_t) *
get_buffer_element_ptr (struct mpmc_queue * q, INDEX_TYPE i)
{
  return &q->buffer[(i) & (BUFFER_SIZE - 1)];
}

void
queue_init (struct mpmc_queue * q)
{
  for (INDEX_TYPE i = 0; i < BUFFER_SIZE; ++i)
  {
    __sync_val_compare_and_swap(get_buffer_element_ptr (q, i), 0, entry_create (0, i << 1));
  }
}

int
enqueue (struct mpmc_queue * q, VALUE_TYPE d)
{
  while (1)
  {
    INDEX_TYPE wr_index = __sync_val_compare_and_swap (&q->write_index, 0, 0);
    INDEX_TYPE seq = entry_get_seq (get_buffer_element (q, wr_index));

    if (seq == wr_index << 1)
    {
      entry_t e = entry_create (0, wr_index << 1);
      entry_t data_entry = entry_create (d, (wr_index << 1) | 1u);
      if (__sync_bool_compare_and_swap (get_buffer_element_ptr (q, wr_index), e, data_entry))
      {
          (void) __sync_val_compare_and_swap (&q->write_index, wr_index, wr_index + 1);
      }
      return 1;
    }
    else if (
      seq == (wr_index << 1 | 1u) ||
      seq == (wr_index + BUFFER_SIZE) << 1
    )
    {
      (void) __sync_val_compare_and_swap (&q->write_index, wr_index, wr_index + 1);
    }
    else if (seq + (BUFFER_SIZE << 1) == (wr_index << 1 | 1u))
    {
      return 0;
    }
  }
}

int
dequeue (struct mpmc_queue * q, VALUE_TYPE * d)
{
  while (1)
  {
    INDEX_TYPE rd_index = __sync_val_compare_and_swap (&q->read_index, 0, 0);
    entry_t e = __sync_val_compare_and_swap (get_buffer_element_ptr (q, rd_index), 0, 0);
    if (entry_get_seq (e) == (rd_index << 1 | 1u))
    {
      entry_t empty_entry = entry_create (0, (rd_index + BUFFER_SIZE) << 1);
      if (__sync_bool_compare_and_swap (get_buffer_element_ptr (q, rd_index), e, empty_entry))
      {
        * d = entry_get_data (e);
        (void) __sync_val_compare_and_swap (&q->read_index, rd_index, rd_index + 1);
        return 1;
      }
      else if ((entry_get_seq (e) | 1u) == ((rd_index + BUFFER_SIZE) << 1 | 1u))
      {
        (void) __sync_val_compare_and_swap (&q->read_index, rd_index, rd_index + 1);
      }
      else if (entry_get_seq (e) == rd_index << 1)
      {
        return 0;
      }
    }
  }
}
