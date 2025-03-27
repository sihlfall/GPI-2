#ifndef UCX_DEVICE_H_
#define UCX_DEVICE_H_

#include "ucp/api/ucp.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

typedef struct ucx_device {
  atomic_int request_stop;
  atomic_int is_running;
  pthread_t server_tid;
  ucp_worker_h ucp_worker;
  ucp_listener_h ucp_listener;
  ucp_ep_h ep;
  ucp_context_h ucp_ctx;
} ucx_device_t;


struct ucx_dev_args
{
  int peers_num;
  int id;
  int port;
  int oob_fd;
};

int ucx_dev_init_device (struct ucx_dev_args * args, ucx_device_t * ucx_device);
int ucx_dev_create_listener (ucx_device_t * ucx_device, uint16_t port);
void ucx_dev_cleanup_listener (ucx_device_t * ucx_device);
void ucx_dev_stop_device(ucx_device_t * ucx_device);


#endif