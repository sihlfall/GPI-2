#ifndef UCX_DEVICE_H_
#define UCX_DEVICE_H_

#include "ucp/api/ucp.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

#define UCX_DEVICE_MAX_ENDPOINTS 1024

struct ucx_device_endpoint {
  ucp_ep_h ep;
  char handshake_buffer;
};

typedef struct ucx_device {
  atomic_int should_stop;
  atomic_int is_running;
  pthread_t server_tid;
  ucp_worker_h ucp_worker;
  ucp_listener_h ucp_listener;
  ucp_ep_h ep;
  ucp_context_h ucp_ctx;
  size_t n_endpoints;
  struct ucx_device_endpoint endpoints [UCX_DEVICE_MAX_ENDPOINTS];
} ucx_device_t;

int ucx_dev_init_device (ucx_device_t * ucx_device);
int ucx_dev_create_listener (ucx_device_t * ucx_device, uint16_t port);
void ucx_dev_cleanup_listener (ucx_device_t * ucx_device);
void ucx_dev_cleanup_device(ucx_device_t * ucx_device);
int ucx_dev_start_thread (ucx_device_t * ucx_device);
void ucx_dev_stop_thread (ucx_device_t * ucx_device);



#endif