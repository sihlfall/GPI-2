#ifndef PLQUEUE_PAYLOAD_TYPE
#error "PLQUEUE_PAYLOAD_TYPE not defined"
#endif
#ifndef PLQUEUE_NAME
#error "PLQUEUE_NAME not defined"
#endif

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

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

#define CONCAT(a, b) a##b
#define EXPAND_CONCAT(a, b) CONCAT(a, b)
#define CONCAT3(a, b, c) a##b##c
#define EXPAND_CONCAT3(a, b, c) CONCAT3(a, b, c)

#define STRUCT_PLQUEUE_ENTRY EXPAND_CONCAT3(struct plqueue_,PLQUEUE_NAME,_entry)
#define STRUCT_PLQUEUE EXPAND_CONCAT(struct plqueue_,PLQUEUE_NAME)
#define STRUCT_MAYBE_PAYLOAD EXPAND_CONCAT(struct maybe_payload_,PLQUEUE_NAME)
#define PLQUEUE_PROC(proc) EXPAND_CONCAT3(plqueue_,proc,EXPAND_CONCAT(_,PLQUEUE_NAME))

STRUCT_PLQUEUE_ENTRY {
  _Atomic uint64_t seq_flags;
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

int PLQUEUE_PROC(sp_enqueue) (
  STRUCT_PLQUEUE * queue, s_cursor_t * write_cursor, PLQUEUE_PAYLOAD_TYPE payload
);
int PLQUEUE_PROC(mp_enqueue) (
  STRUCT_PLQUEUE * queue, m_cursor_t * write_cursor, PLQUEUE_PAYLOAD_TYPE payload
);
STRUCT_MAYBE_PAYLOAD PLQUEUE_PROC(sc_dequeue) (
  STRUCT_PLQUEUE * queue, s_cursor_t * read_cursor
);
STRUCT_MAYBE_PAYLOAD PLQUEUE_PROC(mc_dequeue) (
  STRUCT_PLQUEUE * queue, m_cursor_t * read_cursor
);
int PLQUEUE_PROC(sc_is_empty) (
  STRUCT_PLQUEUE * queue, s_cursor_t * read_cursor
);
int PLQUEUE_PROC(mc_is_empty) (
  STRUCT_PLQUEUE * queue, m_cursor_t * read_cursor
);


#undef PLQUEUE_PROC
#undef STRUCT_MAYBE_PAYLOAD
#undef STRUCT_PLQUEUE
#undef STRUCT_PLQUEUE_ENTRY
#undef EXPAND_CONCAT3
#undef CONCAT3
#undef EXPAND_CONCAT
#undef CONCAT
