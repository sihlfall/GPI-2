#include "ucx_qp.h"
#include "ucx_device.h"
#include <stdlib.h>
#include <string.h>

struct ucx_qp *
ucx_qp_create (struct ucx_qp_init_attr * attr) {
  struct ucx_qp * qp = calloc (1, sizeof (struct ucx_qp));
  if (!qp) return NULL;
  qp->dst = attr->dst;
  /* TODO: What about cq? */
  return qp;
}

void
ucx_qp_post_send (
  struct ucx_device * ucx_device,
  struct ucx_qp * qp, struct ucx_send_wr * wr, struct ucx_send_wr * bad_wr
)
{
  for (struct ucx_send_wr * current = wr; current; current = current->next)
  {
    uint64_t num_sge = current->num_sge;
    struct qp_queue_element * el = calloc(1,
      sizeof (struct qp_queue_element) + num_sge * sizeof (struct ucx_sge)
    );
    el->wr_id = current->wr_id;
    el->num_sge = num_sge;
    el->wr = current->wr;
    memcpy (el->sg_list, current->sg_list, num_sge * sizeof (struct ucx_sge));

    /* TODO: handle errors */
    alf_enqueue (&qp->sq, (struct alf_tag_payload_pair) {
      .tag = 0,
      .payload = (uintptr_t) el
    });
  }

  ucx_device_qp_rdma_write (ucx_device, qp);
}
