#ifndef UCX_OOB_H_
#define UCX_OOB_H_

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

struct ucx_dev_oob_server_thread {
  pthread_t server_tid;
  atomic_int request_stop;
  atomic_int is_running;
  int server_fd;
  uint16_t port;
  int max_connections;
  unsigned char * response_buffer;
  size_t response_buffer_length;
};

struct ucx_dev_oob_response {
  unsigned char * data;
  size_t length;
};

int ucx_dev_oob_server_initialize (
  struct ucx_dev_oob_server_thread * server_thread, uint16_t port, int max_connections,
  struct ucx_dev_oob_response const * response
);

void ucx_dev_oob_server_destroy (struct ucx_dev_oob_server_thread * server_thread);

int ucx_dev_oob_client_make_request (
  char const * hostip4, uint16_t port,
  struct ucx_dev_oob_response * response
);

void ucx_dev_oob_client_cleanup_response (struct ucx_dev_oob_response * response);

#endif