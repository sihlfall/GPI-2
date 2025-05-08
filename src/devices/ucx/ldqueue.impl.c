#define MULTIPLE
#if defined(SINGLE) + defined(MULTIPLE) != 1
#error "Either SINGLE or MULTIPLE must be defined"
#endif

#ifdef SINGLE
#define SM_PREFIX s
#else
#define SM_PREFIX m
#endif

#ifndef PLQUEUE_PAYLOAD_TYPE
#error "PLQUEUE_PAYLOAD_TYPE not defined"
#endif
#ifndef PLQUEUE_NAME
#error "PLQUEUE_NAME not defined"
#endif

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#define STRUCT_PLQUEUE_ENTRY struct plqueue_##PLQUEUE_NAME##_entry
#define STRUCT_PLQUEUE struct plqueue_##PLQUEUE_NAME
#define STRUCT_MAYBE_PAYLOAD struct maybe_payload_##PLQUEUE_NAME
#define PLQUEUE_PROC(proc) plqueue_##proc##_##PLQUEUE_NAME

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


/* A cursor has the following bit layout:
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

typedef uint64_t s_cursor_t;
typedef struct { _Atomic uint64_t v; } m_cursor_t; /* struct for type safety */

static inline
s_cursor_t
m_load_cursor_relaxed (m_cursor_t * cursor)
{
  return (s_cursor_t) { atomic_load_explicit(&cursor->v, memory_order_relaxed) };
}

static inline
void
m_advance_cursor_weak (m_cursor_t * cursor, s_cursor_t expected)
{
  (void) atomic_compare_exchange_weak_explicit(
    &cursor->v, &expected, expected + 4u, memory_order_relaxed, memory_order_relaxed
  );
}

static inline
s_cursor_t
m_advance_or_reload_cursor (m_cursor_t * cursor, s_cursor_t expected)
{
  s_cursor_t a = expected + 4u;
  if (atomic_compare_exchange_strong_explicit(
    &cursor->v, &expected, a, memory_order_relaxed, memory_order_relaxed
  )) {
    return a;
  } else {
    return m_load_cursor_relaxed (cursor);
  }
}

/* write_cursor must be a multiple of 4 (last two bits not set) */
/* returns 1 if successful, 0 if not */
int
PLQUEUE_PROC(sp_enqueue) (
  STRUCT_PLQUEUE * queue, s_cursor_t * write_cursor, PLQUEUE_PAYLOAD_TYPE payload
)
{
  int log2_capacity = queue->log2_capacity;
  s_cursor_t seq_inc = (s_cursor_t)1 << (log2_capacity + 2);
  s_cursor_t qbi_mask = seq_inc - 4u;

  s_cursor_t wcr = *write_cursor;
  s_cursor_t wqbi = wcr & qbi_mask;

  STRUCT_PLQUEUE_ENTRY * e = &queue->entries[wqbi >> 2];
  s_cursor_t sf = atomic_load_explicit(&e->seq_flags, memory_order_acquire);
  s_cursor_t delta = sf - (wcr - wqbi);
  if (delta == 0u) {
    *write_cursor += 4u;
    e->payload = payload;
    atomic_store_explicit(&e->seq_flags, sf + 2u, memory_order_release);
    return 1;
  } else {
    return 0;
  }
}

/* write_cursor must be a multiple of 4 (last two bits not set) */
/* returns 1 if successful, 0 if not */
int
PLQUEUE_PROC(mp_enqueue) (
  STRUCT_PLQUEUE * queue, m_cursor_t * write_cursor, PLQUEUE_PAYLOAD_TYPE payload
)
{
  s_cursor_t halfway = (s_cursor_t)1 << (8 * sizeof(s_cursor_t) - 1);
  int log2_capacity = queue->log2_capacity;
  s_cursor_t seq_inc = (s_cursor_t)1 << (log2_capacity + 2);
  s_cursor_t qbi_mask = seq_inc - 4u;

  s_cursor_t wcr = m_load_cursor_relaxed (write_cursor);
  while (1) {
    s_cursor_t wqbi = wcr & qbi_mask;

    STRUCT_PLQUEUE_ENTRY * e = &queue->entries[wqbi >> 2];
    s_cursor_t sf = atomic_load_explicit(&e->seq_flags, memory_order_acquire);
    s_cursor_t delta = sf - (wcr - wqbi);
    if (delta == 0u) {
      if (atomic_compare_exchange_strong_explicit(
        &e->seq_flags, &sf, sf + 1u, memory_order_acq_rel, memory_order_relaxed
      )) {
        m_advance_cursor_weak (write_cursor, wcr);
        e->payload = payload;
        atomic_store_explicit(&e->seq_flags, sf + 2u, memory_order_release);
        return 1;
      }
      wcr = m_load_cursor_relaxed (write_cursor);
    } else if (delta <= 2u) {
      /* 1u -> currently being written
       * 2u -> already written, hence full 
       */
      wcr = m_advance_or_reload_cursor (write_cursor, wcr);
    } else if (delta & halfway) {
      return 0;
    } else {
      wcr = m_load_cursor_relaxed (write_cursor);
    }
  }
}


/* returns 1 if successful, 0 if not */
STRUCT_MAYBE_PAYLOAD
PLQUEUE_PROC(sc_dequeue) (STRUCT_PLQUEUE * queue, s_cursor_t * read_cursor)
{
  STRUCT_MAYBE_PAYLOAD result;

  int log2_capacity = queue->log2_capacity;
  s_cursor_t seq_inc = (s_cursor_t)1 << (log2_capacity + 2);
  s_cursor_t qbi_mask = seq_inc - 4u;

  s_cursor_t rcr = *read_cursor;
  s_cursor_t rqbi = rcr & qbi_mask;

  STRUCT_PLQUEUE_ENTRY * e = &queue->entries[rqbi >> 2];
  s_cursor_t sf = atomic_load_explicit(&e->seq_flags, memory_order_acquire);
  s_cursor_t delta = (rcr - rqbi + 2u) - sf;
  if (delta == 0u) {
    *read_cursor += 4u;
    result = (STRUCT_MAYBE_PAYLOAD) { .has_value = 1, .payload = e->payload };
    atomic_store_explicit(&e->seq_flags, sf - 3u + seq_inc, memory_order_release);
  } else {
    /* queue is empty */
    result = (STRUCT_MAYBE_PAYLOAD) { .has_value = 0 };
  }

  return result;
}

/* returns 1 if successful, 0 if not */
STRUCT_MAYBE_PAYLOAD
PLQUEUE_PROC(mc_dequeue) (STRUCT_PLQUEUE * queue, m_cursor_t * read_cursor)
{
  STRUCT_MAYBE_PAYLOAD result;

  s_cursor_t halfway = (s_cursor_t)1 << (8 * sizeof(s_cursor_t) - 1);
  int log2_capacity = queue->log2_capacity;
  s_cursor_t seq_inc = (s_cursor_t)1 << (log2_capacity + 2);
  s_cursor_t qbi_mask = seq_inc - 4u;

  s_cursor_t rcr = m_load_cursor_relaxed (read_cursor);
  while (1) {
    s_cursor_t rqbi = rcr & qbi_mask;

    STRUCT_PLQUEUE_ENTRY * e = &queue->entries[rqbi >> 2];
    s_cursor_t sf = atomic_load_explicit(&e->seq_flags, memory_order_acquire);
    s_cursor_t delta = (rcr - rqbi + 2u) - sf;
    if (delta == 0u) {
      if (atomic_compare_exchange_strong_explicit(
        &e->seq_flags, &sf, sf + 1u, memory_order_acq_rel, memory_order_relaxed
      )) {
        m_advance_cursor_weak (read_cursor, rcr);
        result = (STRUCT_MAYBE_PAYLOAD) { .has_value = 1, .payload = e->payload };
        atomic_store_explicit(&e->seq_flags, sf - 3u + seq_inc, memory_order_release);
        return result;
      }
      rcr = m_load_cursor_relaxed (read_cursor);
    } else if (delta <= 2u) {
      /* queue is empty */
      result = (STRUCT_MAYBE_PAYLOAD) { .has_value = 0 };
      return result;
    } else if (delta & halfway) {
      rcr = m_advance_or_reload_cursor (read_cursor, rcr);
    } else {
      rcr = m_load_cursor_relaxed (read_cursor);
    }
  }
}

#undef STRUCT_MAYBE_PAYLOAD
#undef STRUCT_PLQUEUE
#undef STRUCT_PLQUEUE_ENTRY
