#include "ucx_qp.h"
#include "ucx_device.h"
#include <stdlib.h>
#include <string.h>

#define PLQUEUE_NAME cq
#define PLQUEUE_PAYLOAD_TYPE struct ucx_wc
#include "plqueue.incl.c"
#undef PLQUEUE_NAME
#undef PLQUEUE_PAYLOAD_TYPE

#define PLQUEUE_NAME sq
#define PLQUEUE_PAYLOAD_TYPE struct ucx_send_wr
#include "plqueue.incl.c"
#undef PLQUEUE_NAME
#undef PLQUEUE_PAYLOAD_TYPE


struct ucx_qp *
ucx_qp_create (struct ucx_qp_init_attr * attr) {
  struct ucx_qp * qp = calloc (1, sizeof (struct ucx_qp));
  if (!qp) return NULL;
  if (!ucx_init_sq (&qp->sq, attr->queue_size_max))
  {
    free (qp);
    return NULL;
  }
  qp->dst = attr->dst;
  qp->cq = attr->cq;
  return qp;
}

void
ucx_qp_post_send (
  struct ucx_device * ucx_device,
  struct ucx_qp * qp, struct ucx_send_wr * wr, struct ucx_send_wr * bad_wr
)
{
  struct ucx_sq * sq = &qp->sq;
  for (struct ucx_send_wr * current = wr; current; current = current->next)
  {
    /* TODO: handle errors */
    plqueue_mp_enqueue_sq (
      sq->log2_num_entries, sq->entries, &sq->write_cursor, *current
    );
  }

  ucx_device_qp_rdma_write (ucx_device, qp);
}

static inline
int
ceil_log2(unsigned int x) {
  return x == 0 ? 0 : 32 - __builtin_clz(x - 1);
}

int
ucx_init_cq (struct ucx_cq * cq, unsigned int capacity)
{
  int log2_actual_capacity = ceil_log2 (capacity);
  if (log2_actual_capacity >= 32) return 0;
  size_t actual_capacity = (size_t)1 << log2_actual_capacity;
  struct plqueue_cq_entry * entries = calloc(actual_capacity, sizeof (cq->entries[0]));
  if (!entries) return 0;
  *cq = (struct ucx_cq) {
    .log2_num_entries = log2_actual_capacity,
    .entries = entries
  };
  return 1;
}

int
ucx_poll_cq (struct ucx_cq * cq, uint32_t num_entries, struct ucx_wc * wc)
{
  uint32_t i = 0;
  while (1) {
    if (i >= num_entries) break;
    fprintf (stderr, "Calling dequeue\n");
    fprintf (stderr, "cq: %p\n", cq);
    fprintf (stderr, "%u %p\n", cq->log2_num_entries, cq->entries);
    struct plqueue_maybe_payload_cq mp = plqueue_mc_dequeue_cq (
      cq->log2_num_entries, cq->entries, &cq->read_cursor
    );
    if (!mp.has_value) return 0;
    wc[i++] = mp.payload;
  }
  return i;
}

int
ucx_init_sq (struct ucx_sq * sq, unsigned int capacity)
{
  int log2_actual_capacity = ceil_log2 (capacity);
  if (log2_actual_capacity >= 32) return 0;
  size_t actual_capacity = (size_t)1 << log2_actual_capacity;
  struct plqueue_sq_entry * entries = calloc(actual_capacity, sizeof (sq->entries[0]));
  if (!entries) return 0;
  *sq = (struct ucx_sq) {
    .log2_num_entries = log2_actual_capacity,
    .entries = entries
  };
  return 1;
}
