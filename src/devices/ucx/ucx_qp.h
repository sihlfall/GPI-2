#ifndef UCX_QP_H_
#define UCX_QP_H_

#include "mpmc_queue_struct.h"
#include "ucp/api/ucp.h"


/* write completion */
struct ucx_wc {
  uint64_t wr_id;
  int status;
};

#define PLQUEUE_NAME cq
#define PLQUEUE_PAYLOAD_TYPE struct ucx_wc
#include "plqueue.incl.h"
#undef PLQUEUE_NAME
#undef PLQUEUE_PAYLOAD_TYPE

struct ucx_cq {
  int log2_num_entries;
  struct plqueue_cq_entry * entries;
  _Alignas(64) plqueue_s_cursor_t write_cursor;
  _Alignas(64) plqueue_m_cursor_t read_cursor;
};

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

struct ucx_qp_init_attr {
  //ucp_ep_h ep;
  int dst;
  struct ucx_cq * cq;
};

/* queue pair */
struct ucx_qp {
  //ucp_ep_h ep;
  int dst; /* TODO: Separate ep later? */
  struct ucx_cq * cq;
  struct mpmc_queue sq;
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
int ucx_init_cq (struct ucx_cq * cq, unsigned int capacity);
int ucx_poll_cq (struct ucx_cq * cq, uint32_t num_entries, struct ucx_wc * wc);


#endif