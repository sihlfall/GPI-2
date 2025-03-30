#ifndef UCX_DEVICE_H_
#define UCX_DEVICE_H_

#include "GASPI.h"
#include "ucp/api/ucp.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

#define UCX_DEVICE_MAX_ENDPOINTS 1024
#define UCX_DEVICE_MAX_RANKS 128

typedef struct ucx_device {
  gaspi_rank_t rank;
  atomic_int should_stop;
  atomic_int is_running;
  pthread_t server_tid;
  ucp_worker_h ucp_worker;
  ucp_listener_h ucp_listener;
  ucp_context_h ucp_ctx;
} ucx_device_t;

int ucx_dev_init_device (ucx_device_t * ucx_device, gaspi_rank_t rank);
int ucx_dev_create_listener (ucx_device_t * ucx_device, uint16_t port);
void ucx_dev_cleanup_listener (ucx_device_t * ucx_device);
void ucx_dev_cleanup_device(ucx_device_t * ucx_device);
int ucx_dev_start_thread (ucx_device_t * ucx_device);
void ucx_dev_stop_thread (ucx_device_t * ucx_device);

enum ucx_dev_am {
  UCX_DEV_HUHU = 1,
  UCX_DEV_REHU = 2
};


#endif