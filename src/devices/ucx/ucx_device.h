#ifndef UCX_DEVICE_H_
#define UCX_DEVICE_H_

#include "mpmc_queue_struct.h"

#include "GASPI.h"
#include "ucp/api/ucp.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

#define UCX_DEVICE_MAX_ENDPOINTS 1024
#define UCX_DEVICE_MAX_RANKS 128

#define UCX_DEV_MSG_CONNECT 1
#define UCX_DEV_MSG_RDMA_WRITE 2
#define UCX_DEV_MSG_QP_RDMA_WRITE 3

#define UCX_DEVICE_OK 0
#define UCX_DEVICE_ERR_UNSPECIFIED (-1)

#define UCX_WC_SUCCESS 0

typedef int ucx_device_status_t;

struct ucx_device_msg_connect_data {
  char const * host;
  uint16_t port;
  gaspi_rank_t peer_rank;
};

struct ucx_device;

enum ucx_device_endpoint_status {
  ucx_device_endpoint_not_connected = 0,
  ucx_device_endpoint_ok = 1,
  ucx_device_endpoint_connecting = 2
};

struct ucx_device_ep_entry {
  enum ucx_device_endpoint_status status;
  ucp_ep_h ep;
  void * ep_instance;
};

struct ucx_device_endpoints {
  struct ucx_device * ucx_device;
  int tnc;
  struct ucx_device_ep_entry ary[];
};

struct ucx_device {
  gaspi_rank_t rank;
  atomic_int should_stop;
  atomic_int is_running;
  pthread_t server_tid;
  ucp_worker_h ucp_worker;
  ucp_listener_h ucp_listener;
  uint16_t host_port;
  ucp_context_h ucp_ctx;
  struct mpmc_queue queue;
  struct ucx_device_endpoints * endpoints;
  struct mpmc_queue scqGroups;
};

ucx_device_status_t ucx_device_init (
  struct ucx_device * ucx_device, gaspi_rank_t rank, uint16_t host_port
);
ucx_device_status_t ucx_device_start (struct ucx_device * ucx_device);
void ucx_device_stop (struct ucx_device * ucx_device);
void ucx_device_cleanup (struct ucx_device * ucx_device);
ucx_device_status_t ucx_device_connect_to (
  struct ucx_device * ucx_device, char const * hostip4, uint16_t port,
  gaspi_rank_t peer_rank
);
ucx_device_status_t ucx_device_rdma_write (
  struct ucx_device * ucx_device, void * local_addr, int length, int dst,
  void * rkey_buffer, void * remote_addr,
  struct mpmc_queue * cq, uint64_t wr_id
);

struct ucx_qp;

ucx_device_status_t ucx_device_qp_rdma_write (
  struct ucx_device * ucx_device, struct ucx_qp * qp
);


enum ucx_dev_am {
  UCX_DEV_HUHU = 1,
  UCX_DEV_REHU = 2
};


#endif