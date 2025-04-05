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

// TODO: remove after debugging
#include <stdio.h>

/* Macros intended to be used for compile-time calculations. */
/* Not to be used at run-time. */

#define FLOOR_LOG2_16_BIT(i) \
  ((i) >= ((uint64_t) 1) << 15 ? 15 : \
   (i) >= ((uint64_t) 1) << 14 ? 14 : \
   (i) >= ((uint64_t) 1) << 13 ? 13 : \
   (i) >= ((uint64_t) 1) << 12 ? 12 : \
   (i) >= ((uint64_t) 1) << 11 ? 11 : \
   (i) >= ((uint64_t) 1) << 10 ? 10 : \
   (i) >= ((uint64_t) 1) <<  9 ?  9 : \
   (i) >= ((uint64_t) 1) <<  8 ?  8 : \
   (i) >= ((uint64_t) 1) <<  7 ?  7 : \
   (i) >= ((uint64_t) 1) <<  6 ?  6 : \
   (i) >= ((uint64_t) 1) <<  5 ?  5 : \
   (i) >= ((uint64_t) 1) <<  4 ?  4 : \
   (i) >= ((uint64_t) 1) <<  3 ?  3 : \
   (i) >= ((uint64_t) 1) <<  2 ?  2 : \
   (i) >= ((uint64_t) 1) <<  1 ?  1 : \
   (i) >= ((uint64_t) 1) <<  0 ?  0 : \
   -1                                 \
  )

#define FLOOR_LOG2(i) \
  ((uint64_t)(i) >> 48 ? 48 + (FLOOR_LOG2_16_BIT((uint64_t)(i) >> 48)) : \
   (uint64_t)(i) >> 32 ? 32 + (FLOOR_LOG2_16_BIT((uint64_t)(i) >> 32)) : \
   (uint64_t)(i) >> 16 ? 16 + (FLOOR_LOG2_16_BIT((uint64_t)(i) >> 16)) : \
   FLOOR_LOG2_16_BIT(i)                                                  \
  )

#define IS_POWER_OF_2(i) \
  ((i) == ((uint64_t)1 << FLOOR_LOG2(i)))

_Static_assert(FLOOR_LOG2(1) == 0, "");
_Static_assert(FLOOR_LOG2(7) == 2, "");
_Static_assert(FLOOR_LOG2(8) == 3, "");
_Static_assert(FLOOR_LOG2(1u << 20) == 20, "");
_Static_assert(FLOOR_LOG2((uint64_t)1 << 63) == 63, "");
_Static_assert(FLOOR_LOG2(((uint64_t)1 << 63) + 1) == 63, "");
_Static_assert(IS_POWER_OF_2(1), "");
_Static_assert(IS_POWER_OF_2(2), "");
_Static_assert(!IS_POWER_OF_2(3), "");
_Static_assert(IS_POWER_OF_2((uint64_t)1 << 63), "");
_Static_assert(!IS_POWER_OF_2(((uint64_t)1 << 63) + 1), "");

/* Check consistency of sizes and types, define corresponding constants. */

_Static_assert(IS_POWER_OF_2(ALF_BUFFER_SIZE) && ALF_BUFFER_SIZE >= 2, "");

enum constants {
  bitsof_value_type = 8 * sizeof(alf_value_type),
  bitsof_index_type = 8 * sizeof(alf_index_type),
  bitsof_entry_type = 8 * sizeof(alf_entry_type),
  bitsof_buffer_size = FLOOR_LOG2(ALF_BUFFER_SIZE),
  bitsof_seq = bitsof_index_type - bitsof_buffer_size + 1,
  seq_offset = bitsof_entry_type - bitsof_seq
};

_Static_assert(bitsof_seq + bitsof_value_type <= bitsof_entry_type, "");
_Static_assert(bitsof_seq < bitsof_index_type, "");

static inline
alf_value_type
entry_get_data (alf_entry_type e)
{
  return (alf_value_type) e;
}

static inline
alf_index_type
entry_get_seq (alf_entry_type e)
{
  return e >> seq_offset;
}

static inline
alf_entry_type
entry_create (alf_value_type data, alf_index_type seq)
{
  /*
   * The argument seq is permitted to overflow bitsof_seq. In this case,
   * the function guarantees that seq will be truncated to bitsof_seq bits,
   * so that entry_get_seq() will return a value not overflowing bitsof_seq.
   */
  _Static_assert(seq_offset + bitsof_seq == bitsof_entry_type, "");
  return ((alf_entry_type) seq << seq_offset) | (alf_entry_type) data;
}

static inline
alf_index_type
seq_from_index (alf_index_type idx)
{
  alf_index_type mask = ((alf_index_type) 1 << bitsof_seq) - (alf_index_type) 2;
  return (idx >> (bitsof_buffer_size - 1)) & mask;
}

static inline
_Atomic(alf_entry_type) *
buffer_get_entry_ptr (struct mpmc_queue * q, alf_index_type i)
{
  return &q->buffer[(i) & (ALF_BUFFER_SIZE - 1)];
}

#define load_atomic(ptr) \
  (__sync_val_compare_and_swap ((ptr), 0, 0))

static inline
alf_entry_type
buffer_get_entry (struct mpmc_queue * q, alf_index_type i)
{
  return load_atomic (buffer_get_entry_ptr (q, i));
}

int
alf_enqueue (struct mpmc_queue * q, alf_value_type d)
{
  while (1)
  {
    alf_index_type wr_index = load_atomic (&q->write_index);
    alf_index_type wr_seq = seq_from_index (wr_index);
    alf_index_type seq = entry_get_seq (buffer_get_entry (q, wr_index));

    alf_index_type delta = seq - wr_seq;
    if (delta == 0u)
    {
      alf_entry_type e = entry_create (0, wr_seq);
      alf_entry_type data_entry = entry_create (d, wr_seq + 1u);
      if (__sync_bool_compare_and_swap (buffer_get_entry_ptr (q, wr_index), e, data_entry))
      {
        (void) __sync_val_compare_and_swap (&q->write_index, wr_index, wr_index + 1);
        return 1;
      }
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
    alf_index_type rd_index = load_atomic (&q->read_index);
    alf_index_type rd_seq = seq_from_index (rd_index);
    alf_entry_type e = buffer_get_entry (q, rd_index);
    alf_index_type seq = entry_get_seq (e);

    alf_index_type delta = seq - rd_seq;
    if (delta == 1u)
    {
      alf_entry_type empty_entry = entry_create (0, rd_seq + 2u);
      if (__sync_bool_compare_and_swap (buffer_get_entry_ptr (q, rd_index), e, empty_entry))
      {
        *d = entry_get_data (e);
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

int
alf_is_empty (struct mpmc_queue * q)
{
  alf_index_type rd_index = atomic_load (&q->read_index);
  alf_entry_type e = buffer_get_entry (q, rd_index);
  return entry_get_seq (e) == seq_from_index (rd_index);
}
