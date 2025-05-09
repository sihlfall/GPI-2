#include "plqueue_timing_cpp_interop.h"
#include "plqueue_u64.h"
#include <stdlib.h>
#include <stdalign.h>

struct plqueue_u64_rw {
  int log2_capacity;
  struct plqueue_u64_entry * entries;
  alignas(64) m_cursor_t write_cursor;
  alignas(64) m_cursor_t read_cursor;
};

struct plqueue_u64_rw * queue_create (void) {
  size_t log2_capacity = 6;
  struct plqueue_u64_rw * q = calloc (1, sizeof(*q));
  *q = (struct plqueue_u64_rw) {
    .log2_capacity = log2_capacity,
    .entries = calloc (1 << log2_capacity, sizeof(*q->entries))
  };
  return q;
}

void queue_destroy (struct plqueue_u64_rw * q) {
  if (q) { free (q->entries); free (q); }
}

int queue_enqueue (struct plqueue_u64_rw * q, uint64_t payload) {
  return plqueue_mp_enqueue_u64 (
    q->log2_capacity, q->entries, &q->write_cursor, payload
  );
}

int queue_dequeue (struct plqueue_u64_rw * q, uint64_t * payload) {
  struct maybe_payload_u64 r = plqueue_mc_dequeue_u64 (
    q->log2_capacity, q->entries, &q->read_cursor
  );
  if (r.has_value) {
    *payload = r.payload;
    return 1;
  } else {
    return 0;
  }
}

int queue_is_empty (struct plqueue_u64_rw * q) {
  return plqueue_mc_is_empty_u64 (
    q->log2_capacity, q->entries, &q->read_cursor
  );
}
