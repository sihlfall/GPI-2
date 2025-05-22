#ifndef UCX_DEVICE_SN_BACKEND_H_
#define UCX_DEVICE_SN_BACKEND_H_

#include "GASPI_types.h"

#include "ucp/api/ucp.h"
#include <pthread.h>

enum ucx_device_sn_status {
  UCX_DEVICE_SN_OK = 0,
  UCX_DEVICE_SN_ERR_UNSPECIFIED = -1
};

struct ucx_device_sn_ep_entry {
  ucp_ep_h ep;
  _Bool is_connected;
};

struct ucx_device_sn {
  void * gctx;
  _Atomic _Bool should_stop;
  ucp_worker_h sn_active_worker;
  _Bool response_available;
  ucp_worker_h sn_passive_worker;
  pthread_t passive_server_tid;
  ucp_listener_h sn_listener;
  uint16_t host_port;
  struct ucx_device_sn_ep_entry * ep_entries;
};

enum ucx_device_sn_status
ucx_device_sn_init (
  void * gctx, struct ucx_device_sn * udsn, ucp_context_h ucp_context, int tnc, 
  uint16_t host_port
);
void ucx_device_sn_cleanup (struct ucx_device_sn * udsn);
enum ucx_device_sn_status ucx_device_sn_start (struct ucx_device_sn * udsn);
void ucx_device_sn_stop (struct ucx_device_sn * udsn);
enum ucx_device_sn_status ucx_device_sn_connect_to_rank (
  struct ucx_device_sn * udsn,
  char const * hostip4, uint64_t port,
  gaspi_rank_t rank, gaspi_timeout_t timeout_ms
);
enum ucx_device_sn_status ucx_device_sn_send_recv_cmd (
  struct ucx_device_sn * udsn, gaspi_rank_t target_rank,
  unsigned char * header, size_t header_size,
  unsigned char * recv_buf, size_t recv_size
);
void ucx_device_sn_send_cmd_response (
  struct ucx_device_sn * udsn, void * recv_param,
  void * header, size_t header_size
);



/* "Static" callback, to be implemented in GPI2_SN_ucx.c */
void gaspiu_sn_handle_cmd (
  void * gctx, struct ucx_device_sn * udsn, void * recv_param, void * header
);

#endif