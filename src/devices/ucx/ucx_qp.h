#ifndef UCX_QP_H_
#define UCX_QP_H_

#include "mpmc_queue_struct.h"
#include "ucp/api/ucp.h"

struct ucx_sge {
  uint64_t addr;
  uint64_t length;
  uint64_t mem_h;
};

union ucx_rdma_union {
  struct {
    uint64_t remote_addr;
    uint64_t rkey_buffer_size;
    void * rkey_buffer;
  } rdma;
};

/* write request */
struct ucx_send_wr {
  uint64_t wr_id;
  struct ucx_send_wr * next;
  struct ucx_sge * sg_list;
  uint64_t num_sge;
  union ucx_rdma_union wr;
};

/* write completion */
struct ucx_wc {
  uint64_t wr_id;
  int status;
};

struct ucx_qp_init_attr {
  //ucp_ep_h ep;
  int dst;
};

/* queue pair */
struct ucx_qp {
  //ucp_ep_h ep;
  int dst; /* TODO: Separate ep later? */
  struct mpmc_queue sq;
  //struct ucx_wc * cq; /* ? */
};

/* TODO: internal, move out of here */
struct qp_queue_element {
  uint64_t wr_id;
  uint64_t num_sge;
  union ucx_rdma_union wr;
  struct ucx_sge sg_list [];
};

struct ucx_device;

struct ucx_qp * ucx_qp_create (struct ucx_qp_init_attr * attr);
void ucx_qp_post_send (
  struct ucx_device * ucx_device, struct ucx_qp * qp, struct ucx_send_wr * wr,
  struct ucx_send_wr * bad_wr
);

#endif