#include <stdatomic.h>
#include <stdint.h>

typedef uint64_t s_cursor_t;

/* we use a struct for type safety */
typedef struct { _Atomic uint64_t v; } m_cursor_t;

static inline
s_cursor_t
s_load_cursor_relaxed (s_cursor_t * cursor)
{
  return *cursor;
}

static inline
s_cursor_t
m_load_cursor_relaxed (m_cursor_t * cursor)
{
  return (s_cursor_t) { atomic_load_explicit(&cursor->v, memory_order_relaxed) };
}

static inline
s_cursor_t
s_advance_cursor (s_cursor_t * cursor, s_cursor_t expected)
{
  return (*cursor += 4u);
}

static inline
s_cursor_t
s_advance_cursor_weak (s_cursor_t * cursor, s_cursor_t expected)
{
  return s_advance_cursor (cursor, expected);
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
s_advance_or_reload_cursor (s_cursor_t * cursor, s_cursor_t expected)
{
  return s_advance_cursor (cursor, expected);
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

#define MULTIPLE
#if defined(SINGLE) + defined(MULTIPLE) != 1
#error "Either SINGLE or MULTIPLE must be defined"
#endif

#ifdef SINGLE
#define SM_PREFIX s
#else
#define SM_PREFIX m
#endif

#define LOAD_CURSOR_RELAXED_IMPL(prefix) prefix##_load_cursor_relaxed
#define LOAD_CURSOR_RELAXED(prefix) LOAD_CURSOR_RELAXED_IMPL(prefix)
#define ADVANCE_CURSOR_WEAK_IMPL(prefix) prefix##_advance_cursor_weak
#define ADVANCE_CURSOR_WEAK(prefix) ADVANCE_CURSOR_WEAK_IMPL(prefix)
#define ADVANCE_OR_RELOAD_CURSOR_IMPL(prefix) prefix##_advance_cursor
#define ADVANCE_OR_RELOAD_CURSOR(prefix) ADVANCE_OR_RELOAD_CURSOR_IMPL(prefix)

int f() {
  m_cursor_t cursor;
  LOAD_CURSOR_RELAXED(SM_PREFIX) (&cursor);
}
