#ifndef UCX_OOB_H_
#define UCX_OOB_H_

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

#include <ucp/api/ucp.h>

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
};

int ucx_dev_oob_server_initialize (
  struct ucx_dev_oob_server_thread * server_thread, ucp_worker_h ucp_server_worker, uint16_t port
);

void ucx_dev_oob_server_destroy (struct ucx_dev_oob_server_thread * server_thread);

#endif