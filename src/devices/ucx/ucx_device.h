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

struct ucx_device_msg_connect_data {
  char const * host;
  uint16_t port;
};

struct ucx_device;

struct ucx_device_endpoint {
  struct ucx_device * ucx_device;
  gaspi_rank_t rank;
  ucp_ep_h ep;
};

typedef struct ucx_device {
  gaspi_rank_t rank;
  atomic_int should_stop;
  atomic_int is_running;
  pthread_t server_tid;
  ucp_worker_h ucp_worker;
  ucp_listener_h ucp_listener;
  uint16_t host_port;
  ucp_context_h ucp_ctx;
  struct mpmc_queue queue;
  struct ucx_device_endpoint endpoints[UCX_DEVICE_MAX_RANKS];
} ucx_device_t;

int ucx_dev_init_device (ucx_device_t * ucx_device, gaspi_rank_t rank, uint16_t host_port);
int ucx_dev_start_device (ucx_device_t * ucx_device);
void ucx_dev_stop_device (ucx_device_t * ucx_device);
void ucx_dev_cleanup_device(ucx_device_t * ucx_device);
int ucx_dev_connect_to (ucx_device_t * ucx_device, char const * hostip4, uint16_t port);

enum ucx_dev_am {
  UCX_DEV_HUHU = 1,
  UCX_DEV_REHU = 2
};


#endif