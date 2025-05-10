#ifndef PLQUEUE_PAYLOAD_TYPE
#error "PLQUEUE_PAYLOAD_TYPE not defined"
#endif
#ifndef PLQUEUE_NAME
#error "PLQUEUE_NAME not defined"
#endif

#ifdef FOR_INTELLISENSE_ONLY
#include "plqueue.incl.h"
#endif

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include <immintrin.h>

#define CONCAT(a, b) a##b
#define EXPAND_CONCAT(a, b) CONCAT(a, b)
#define CONCAT3(a, b, c) a##b##c
#define EXPAND_CONCAT3(a, b, c) CONCAT3(a, b, c)

#define STRUCT_PLQUEUE_ENTRY EXPAND_CONCAT3(struct plqueue_,PLQUEUE_NAME,_entry)
#define STRUCT_PLQUEUE EXPAND_CONCAT(struct plqueue_,PLQUEUE_NAME)
#define STRUCT_PLQUEUE_MAYBE_PAYLOAD \
  EXPAND_CONCAT(struct plqueue_maybe_payload_,PLQUEUE_NAME)
#define PLQUEUE_FN(proc) EXPAND_CONCAT3(plqueue_,proc,EXPAND_CONCAT(_,PLQUEUE_NAME))

static inline
void
cpu_pause (int * pausecnt)
{
  int cnt = *pausecnt;
  for (int i = 0; i <= cnt; ++i) _mm_pause ();
  if (cnt < 512) *pausecnt = cnt * 2;
}

static inline
plqueue_s_cursor_t
m_load_cursor_relaxed (plqueue_m_cursor_t * cursor)
{
  return atomic_load_explicit(&cursor->v, memory_order_relaxed);
}

static inline
void
m_advance_cursor_weak (plqueue_m_cursor_t * cursor, plqueue_s_cursor_t expected)
{
  plqueue_s_cursor_t c = expected;
  (void) atomic_compare_exchange_weak_explicit(
    &cursor->v, &c, expected + 4u, memory_order_relaxed, memory_order_relaxed
  );
}

static inline
plqueue_s_cursor_t
m_reload_or_advance_cursor (
  plqueue_m_cursor_t * cursor, plqueue_s_cursor_t advance_when
)
{
  plqueue_s_cursor_t a = advance_when + 4u;
  plqueue_s_cursor_t c = atomic_load_explicit(&cursor->v, memory_order_relaxed);
  if (
    c == advance_when &&
    atomic_compare_exchange_strong_explicit(
      &cursor->v, &c, a, memory_order_relaxed, memory_order_relaxed
    )
  ) {
    return a;
  } else {
    return c;
  }
}

/* write_cursor must be a multiple of 4 (last two bits not set) */
/* returns 1 if successful, 0 if not */
int
PLQUEUE_FN(sp_enqueue) (
  int log2_capacity,
  STRUCT_PLQUEUE_ENTRY entries [static (size_t)1 << log2_capacity],
  plqueue_s_cursor_t * write_cursor, PLQUEUE_PAYLOAD_TYPE payload
)
{
  plqueue_s_cursor_t seq_inc = (plqueue_s_cursor_t)1 << (log2_capacity + 2);
  plqueue_s_cursor_t qbi_mask = seq_inc - 4u;

  plqueue_s_cursor_t wcr = *write_cursor;
  plqueue_s_cursor_t wqbi = wcr & qbi_mask;

  STRUCT_PLQUEUE_ENTRY * e = &entries[wqbi >> 2];
  plqueue_s_cursor_t sf = atomic_load_explicit(&e->seq_flags, memory_order_acquire);
  plqueue_s_cursor_t delta = sf - (wcr - wqbi);
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
PLQUEUE_FN(mp_enqueue) (
  int log2_capacity,
  STRUCT_PLQUEUE_ENTRY entries [static (size_t)1 << log2_capacity],
  plqueue_m_cursor_t * write_cursor,
  PLQUEUE_PAYLOAD_TYPE payload
)
{
  int pausecnt = 1;
  plqueue_s_cursor_t halfway =
    (plqueue_s_cursor_t)1 << (8 * sizeof(plqueue_s_cursor_t) - 1);
  plqueue_s_cursor_t seq_inc = (plqueue_s_cursor_t)1 << (log2_capacity + 2);
  plqueue_s_cursor_t qbi_mask = seq_inc - 4u;

  plqueue_s_cursor_t wcr = m_load_cursor_relaxed (write_cursor);
  while (1) {
    plqueue_s_cursor_t wqbi = wcr & qbi_mask;

    STRUCT_PLQUEUE_ENTRY * e = &entries[wqbi >> 2];
    plqueue_s_cursor_t sf = atomic_load_explicit(&e->seq_flags, memory_order_acquire);
    plqueue_s_cursor_t delta = sf - (wcr - wqbi);
    if (delta == 0u) {
      if (atomic_compare_exchange_strong_explicit(
        &e->seq_flags, &sf, sf + 1u, memory_order_acq_rel, memory_order_relaxed
      )) {
        m_advance_cursor_weak (write_cursor, wcr);
        e->payload = payload;
        atomic_store_explicit(&e->seq_flags, sf + 2u, memory_order_release);
        return 1;
      }
      cpu_pause (&pausecnt);
      wcr = m_load_cursor_relaxed (write_cursor);
    } else if (delta <= 2u) {
      /* 1u -> currently being written
       * 2u -> already written, hence full 
       */
      cpu_pause (&pausecnt);
      wcr = m_reload_or_advance_cursor (write_cursor, wcr);
    } else if (delta & halfway) {
      return 0;
    } else {
      cpu_pause (&pausecnt);
      wcr = m_load_cursor_relaxed (write_cursor);
    }
  }
}


/* returns 1 if successful, 0 if not */
STRUCT_PLQUEUE_MAYBE_PAYLOAD
PLQUEUE_FN(sc_dequeue) (
  int log2_capacity,
  STRUCT_PLQUEUE_ENTRY entries [static (size_t)1 << log2_capacity],
  plqueue_s_cursor_t * read_cursor
)
{
  STRUCT_PLQUEUE_MAYBE_PAYLOAD result;

  plqueue_s_cursor_t seq_inc = (plqueue_s_cursor_t)1 << (log2_capacity + 2);
  plqueue_s_cursor_t qbi_mask = seq_inc - 4u;

  plqueue_s_cursor_t rcr = *read_cursor;
  plqueue_s_cursor_t rqbi = rcr & qbi_mask;

  STRUCT_PLQUEUE_ENTRY * e = &entries[rqbi >> 2];
  plqueue_s_cursor_t sf = atomic_load_explicit(&e->seq_flags, memory_order_acquire);
  plqueue_s_cursor_t delta = (rcr - rqbi + 2u) - sf;
  if (delta == 0u) {
    *read_cursor += 4u;
    result = (STRUCT_PLQUEUE_MAYBE_PAYLOAD) { .has_value = 1, .payload = e->payload };
    atomic_store_explicit(&e->seq_flags, sf - 3u + seq_inc, memory_order_release);
  } else {
    /* queue is empty */
    result = (STRUCT_PLQUEUE_MAYBE_PAYLOAD) { .has_value = 0 };
  }

  return result;
}

/* returns 1 if successful, 0 if not */
STRUCT_PLQUEUE_MAYBE_PAYLOAD
PLQUEUE_FN(mc_dequeue) (
  int log2_capacity,
  STRUCT_PLQUEUE_ENTRY entries [static (size_t)1 << log2_capacity],
  plqueue_m_cursor_t * read_cursor
)
{
  int pausecnt = 1;

  plqueue_s_cursor_t halfway =
    (plqueue_s_cursor_t)1 << (8 * sizeof(plqueue_s_cursor_t) - 1);
  plqueue_s_cursor_t seq_inc = (plqueue_s_cursor_t)1 << (log2_capacity + 2);
  plqueue_s_cursor_t qbi_mask = seq_inc - 4u;

  plqueue_s_cursor_t rcr = m_load_cursor_relaxed (read_cursor);
  while (1) {
    plqueue_s_cursor_t rqbi = rcr & qbi_mask;

    STRUCT_PLQUEUE_ENTRY * e = &entries[rqbi >> 2];
    plqueue_s_cursor_t sf = atomic_load_explicit(&e->seq_flags, memory_order_acquire);
    plqueue_s_cursor_t delta = (rcr - rqbi + 2u) - sf;
    if (delta == 0u) {
      if (atomic_compare_exchange_strong_explicit(
        &e->seq_flags, &sf, sf + 1u, memory_order_acq_rel, memory_order_relaxed
      )) {
        m_advance_cursor_weak (read_cursor, rcr);
        PLQUEUE_PAYLOAD_TYPE payload = e->payload;
        atomic_store_explicit(&e->seq_flags, sf - 2u + seq_inc, memory_order_release);
        return (STRUCT_PLQUEUE_MAYBE_PAYLOAD) { .has_value = 1, .payload = payload };
      }
      cpu_pause (&pausecnt);
      rcr = m_load_cursor_relaxed (read_cursor);
    } else if (delta <= 2u) {
      /* queue is empty */
      return (STRUCT_PLQUEUE_MAYBE_PAYLOAD) { .has_value = 0 };
    } else if (delta & halfway) {
      cpu_pause (&pausecnt);
      rcr = m_reload_or_advance_cursor (read_cursor, rcr);
    } else {
      cpu_pause (&pausecnt);
      rcr = m_load_cursor_relaxed (read_cursor);
    }
  }
}

/* returns 1 if successful, 0 if not */
STRUCT_PLQUEUE_MAYBE_PAYLOAD
PLQUEUE_FN(mc_dequeue_speculative) (
  int log2_capacity,
  STRUCT_PLQUEUE_ENTRY entries [static (size_t)1 << log2_capacity],
  plqueue_m_cursor_t * read_cursor
)
{
  int pausecnt = 1;

  plqueue_s_cursor_t halfway =
    (plqueue_s_cursor_t)1 << (8 * sizeof(plqueue_s_cursor_t) - 1);
  plqueue_s_cursor_t seq_inc = (plqueue_s_cursor_t)1 << (log2_capacity + 2);
  plqueue_s_cursor_t qbi_mask = seq_inc - 4u;

  plqueue_s_cursor_t rcr = m_load_cursor_relaxed (read_cursor);
  while (1) {
    plqueue_s_cursor_t rqbi = rcr & qbi_mask;

    STRUCT_PLQUEUE_ENTRY * e = &entries[rqbi >> 2];
    plqueue_s_cursor_t sf = atomic_load_explicit(&e->seq_flags, memory_order_acquire);
    plqueue_s_cursor_t delta = (rcr - rqbi + 2u) - sf;
    if (delta == 0u) {
      PLQUEUE_PAYLOAD_TYPE payload = e->payload;
      if (atomic_compare_exchange_strong_explicit(
        &e->seq_flags, &sf, sf - 2u + seq_inc,
        memory_order_acq_rel, memory_order_relaxed
      )) {
        m_advance_cursor_weak (read_cursor, rcr);
        return (STRUCT_PLQUEUE_MAYBE_PAYLOAD) { .has_value = 1, .payload = payload };
      }
      cpu_pause (&pausecnt);
      rcr = m_load_cursor_relaxed (read_cursor);
    } else if (delta <= 2u) {
      /* queue is empty */
      return (STRUCT_PLQUEUE_MAYBE_PAYLOAD) { .has_value = 0 };
    } else if (delta & halfway) {
      cpu_pause (&pausecnt);
      rcr = m_reload_or_advance_cursor (read_cursor, rcr);
    } else {
      cpu_pause (&pausecnt);
      rcr = m_load_cursor_relaxed (read_cursor);
    }
  }
}

int
PLQUEUE_FN(sc_is_empty) (
  int log2_capacity,
  STRUCT_PLQUEUE_ENTRY entries [static (size_t)1 << log2_capacity],
  plqueue_s_cursor_t * read_cursor
)
{
  plqueue_s_cursor_t seq_inc = (plqueue_s_cursor_t)1 << (log2_capacity + 2);
  plqueue_s_cursor_t qbi_mask = seq_inc - 4u;

  plqueue_s_cursor_t rcr = *read_cursor;
  plqueue_s_cursor_t rqbi = rcr & qbi_mask;

  STRUCT_PLQUEUE_ENTRY * e = &entries[rqbi >> 2];
  plqueue_s_cursor_t sf = atomic_load_explicit(&e->seq_flags, memory_order_acquire);
  plqueue_s_cursor_t delta = (rcr - rqbi + 2u) - sf;

  return delta != 0u;
}

int
PLQUEUE_FN(mc_is_empty) (
  int log2_capacity,
  STRUCT_PLQUEUE_ENTRY entries [static (size_t)1 << log2_capacity],
  plqueue_m_cursor_t * read_cursor
)
{
  plqueue_s_cursor_t seq_inc = (plqueue_s_cursor_t)1 << (log2_capacity + 2);
  plqueue_s_cursor_t qbi_mask = seq_inc - 4u;

  plqueue_s_cursor_t rcr = m_load_cursor_relaxed (read_cursor);
  plqueue_s_cursor_t rqbi = rcr & qbi_mask;

  STRUCT_PLQUEUE_ENTRY * e = &entries[rqbi >> 2];
  plqueue_s_cursor_t sf = atomic_load_explicit(&e->seq_flags, memory_order_acquire);
  plqueue_s_cursor_t delta = (rcr - rqbi + 2u) - sf;
  return delta != 0u;
}

#undef PLQUEUE_FN
#undef STRUCT_PLQUEUE_MAYBE_PAYLOAD
#undef STRUCT_PLQUEUE_ENTRY
#undef EXPAND_CONCAT3
#undef CONCAT3
#undef EXPAND_CONCAT
#undef CONCAT
