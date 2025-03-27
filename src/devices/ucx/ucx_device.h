#ifndef UCX_DEVICE_H_
#define UCX_DEVICE_H_

#include "ucp/api/ucp.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

struct ucx_dev_oob_server_thread;

struct handle_connection_args {
  ucp_worker_h worker;
  struct ucx_dev_oob_server_thread * server_thread;
};


struct ucx_dev_oob_server_thread {
  atomic_int request_stop;
  atomic_int is_running;
  pthread_t server_tid;
  uint16_t port;
  ucp_worker_h ucp_worker;
  ucp_listener_h ucp_listener;
  struct handle_connection_args handle_connection_args;
  ucp_ep_h ep;
  ucp_context_h ucp_ctx;
  ucp_worker_h default_worker;
};


struct ucx_dev_args
{
  int peers_num;
  int id;
  int port;
  int oob_fd;
};

int ucx_dev_oob_server_initialize (
  struct ucx_dev_oob_server_thread * server_thread, ucp_worker_h ucp_server_worker, uint16_t port
);

void ucx_dev_oob_server_destroy (struct ucx_dev_oob_server_thread * server_thread);

int ucx_dev_init_device (struct ucx_dev_args * args, struct ucx_dev_oob_server_thread * wpool);
void ucx_dev_stop_device(struct ucx_dev_oob_server_thread * wpool);


#endif