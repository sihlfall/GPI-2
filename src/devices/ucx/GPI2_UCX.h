#ifndef _GPI2_UCX_H_
#define _GPI2_UCX_H_

#include "GPI2.h"
#include "GPI2_Dev.h"

#include "ucx_device.h"

struct ucx_sge {
	uint64_t addr;
	uint64_t length;
	uint64_t mem_h;
};

struct ucx_send_wr {
  uint64_t wr_id;
  struct ucx_send_wr * next;
  struct ucx_sge * sg_list;
  uint64_t num_sge;
  union {
    struct {
      uint64_t remote_addr;
      uint64_t rkey_buffer_size;
      void * rkey_buffer;
    } rdma;
  } wr;
};

struct ucx_wc {
  uint64_t wr_id;
  int status;
};

struct ucx_qp {
  int dummy;
};

typedef struct
{
  struct ucx_device ucx_device;
  struct ucx_wc wc_grp_send[64];
  struct ucx_qp **qpC[GASPI_MAX_QP];
} gaspi_ucx_ctx;

#endif //_GPI2_UCX_H_
