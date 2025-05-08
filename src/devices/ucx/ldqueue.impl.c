#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#ifndef PLQUEUE_PAYLOAD_TYPE
#error "PLQUEUE_PAYLOAD_TYPE not defined"
#endif
#ifndef PLQUEUE_NAME
#error "PLQUEUE_NAME not defined"
#endif

#define STRUCT_PLQUEUE_ENTRY struct plqueue_##PLQUEUE_NAME##_entry
#define STRUCT_PLQUEUE struct plqueue_##PLQUEUE_NAME
#define STRUCT_MAYBE_PAYLOAD struct maybe_payload_##PLQUEUE_NAME

STRUCT_PLQUEUE_ENTRY {
  _Atomic(uint64_t) seq_flags;
  PLQUEUE_PAYLOAD_TYPE payload;
};

STRUCT_PLQUEUE {
  int log2_capacity;
  STRUCT_PLQUEUE_ENTRY * entries;
};

STRUCT_MAYBE_PAYLOAD {
  _Bool has_value;
  PLQUEUE_PAYLOAD_TYPE payload;
};

/* write_idx has the following bit layout:
 * 64 bits total
 * bits 0 and 1:                   flags
 *   bit 0 being set -> the entry is currently
 *     being written (= is locked)
 *   bit 1 being set -> the entry is full
 *   bit 0 and 1 cannot be both set at the same time
 * bits 2 to (log2_capacity + 1):  qbi
 *   = "queue buffer index"
 * bits (log2_capacity + 2) to 63: seq
 */
/* write_idx must be a multiple of 4 (last two bits not set) */
/* returns 1 if successful, 0 if not */
int
plqueue_enqueue (
  STRUCT_PLQUEUE * queue, _Atomic(size_t) * write_idx, PLQUEUE_PAYLOAD_TYPE payload
)
{
  size_t halfway = (size_t)1 << (8 * sizeof(size_t) - 1);
  int log2_capacity = queue->log2_capacity;
  size_t seq_inc = (size_t)1 << (log2_capacity + 2);
  size_t qbi_mask = seq_inc - 4u;

  while (1) {
    size_t wi = atomic_load_explicit(write_idx, memory_order_relaxed);
    size_t wqbi = wi & qbi_mask;

    STRUCT_PLQUEUE_ENTRY * e = &queue->entries[wqbi >> 2];
    size_t sf = atomic_load_explicit(&e->seq_flags, memory_order_acquire);
    size_t delta = sf - (wi - wqbi);
    if (delta == 0u) {
      if (atomic_compare_exchange_strong_explicit(
        &e->seq_flags, &sf, sf + 1u, memory_order_acq_rel, memory_order_relaxed
      )) {
        (void) atomic_compare_exchange_weak_explicit(
          write_idx, &wi, wi + 4u, memory_order_relaxed, memory_order_relaxed
        );
        e->payload = payload;
        atomic_store_explicit(&e->seq_flags, sf + 2u, memory_order_release);
        return 1;
      }
    } else if (delta <= 2u) {
      /* 1u -> currently being written
       * 2u -> already written, hence full 
       */
      (void) atomic_compare_exchange_strong_explicit(
        write_idx, &wi, wi + 4u, memory_order_relaxed, memory_order_relaxed
      );
    } else if (delta & halfway) {
      return 0;
    }
  }
}

/* returns 1 if successful, 0 if not */
STRUCT_MAYBE_PAYLOAD
plqueue_dequeue (STRUCT_PLQUEUE * queue, _Atomic(size_t) * read_idx)
{
  STRUCT_MAYBE_PAYLOAD result;

  size_t halfway = (size_t)1 << (8 * sizeof(size_t) - 1);
  int log2_capacity = queue->log2_capacity;
  size_t seq_inc = (size_t)1 << (log2_capacity + 2);
  size_t qbi_mask = seq_inc - 4u;

  while (1) {
    size_t ri = atomic_load_explicit(read_idx, memory_order_relaxed);
    size_t rqbi = ri & qbi_mask;

    STRUCT_PLQUEUE_ENTRY * e = &queue->entries[rqbi >> 2];
    size_t sf = atomic_load_explicit(&e->seq_flags, memory_order_acquire);
    size_t delta = (ri - rqbi + 2u) - sf;
    if (delta == 0u) {
      if (atomic_compare_exchange_strong_explicit(
        &e->seq_flags, &sf, sf + 1u, memory_order_acq_rel, memory_order_relaxed
      )) {
        (void) atomic_compare_exchange_weak_explicit(
          read_idx, &ri, ri + 4u, memory_order_relaxed, memory_order_relaxed
        );
        result = (STRUCT_MAYBE_PAYLOAD) { .has_value = 1, .payload = e->payload };
        atomic_store_explicit(&e->seq_flags, sf - 3u + seq_inc, memory_order_release);
        return result;
      }
    } else if (delta <= 2u) {
      /* queue is empty */
      result = (STRUCT_MAYBE_PAYLOAD) { .has_value = 0 };
      return result;
    } else if (delta & halfway) {
      (void) atomic_compare_exchange_strong_explicit(
        read_idx, &ri, ri + 4u, memory_order_relaxed, memory_order_relaxed
      );
    }
  }
}

#undef STRUCT_MAYBE_PAYLOAD
#undef STRUCT_PLQUEUE
#undef STRUCT_PLQUEUE_ENTRY
