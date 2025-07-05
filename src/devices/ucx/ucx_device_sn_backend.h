#ifndef UCX_DEVICE_SN_BACKEND_H_
#define UCX_DEVICE_SN_BACKEND_H_

#include "GASPI_types.h"

#include "ucp/api/ucp.h"
#include <pthread.h>

#define UCX_DEVICE_SN_MAX_HEADER_LENGTH (1024)

enum ucx_device_sn_status {
  UCX_DEVICE_SN_OK = 0,
  UCX_DEVICE_SN_ERR_UNSPECIFIED = -1
};

enum ucx_device_sn_terminate {
  UCX_DEVICE_SN_DO_NOT_TERMINATE = 0,
  UCX_DEVICE_SN_KILL = 1
};

struct ucx_device_sn_ep_entry {
  ucp_ep_h ep;
  _Bool is_connected;
};

struct ucx_device_sn {
  void * gctx;
  _Atomic _Bool should_stop;
  ucp_worker_h sn_active_worker;
  _Bool response_available; /* better: running id */
  void * recv_buf;
  size_t max_recv_size;
  ucp_worker_h sn_passive_worker;
  pthread_t passive_server_tid;
  ucp_listener_h sn_listener;
  uint16_t host_port;
  struct ucx_device_sn_ep_entry * ep_entries;
  struct {
    gaspi_rank_t rank;
    gaspi_rank_t tnc;
    size_t length;
    void * _Atomic hn_poff;
  } received_topology; /* TODO: put somewhere else? */
  /* _Alignas(max_align_t) unsigned char response_header [UCX_DEVICE_SN_MAX_HEADER_LENGTH]; */
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
  void * header, size_t header_size,
  void * recv_buf, size_t recv_size
);
enum ucx_device_sn_status ucx_device_sn_send_cmd (
  struct ucx_device_sn * udsn, gaspi_rank_t target_rank,
  void * header, size_t header_size
);
void ucx_device_sn_send_cmd_response (
  struct ucx_device_sn * udsn, void * recv_param,
  void * header, size_t header_size
);
enum ucx_device_sn_status
ucx_device_sn_send_and_wait (
  struct ucx_device_sn * udsn, int n_targets,
  char const * hostip4 [static n_targets],
  uint64_t port [static n_targets],
  gaspi_rank_t target_ranks [static n_targets],
  void * headers, size_t header_size,
  void * data, size_t length
);


/* "Static" callback, to be implemented in GPI2_SN_ucx.c */
void gaspiu_sn_handle_cmd (
  void * gctx, struct ucx_device_sn * udsn, void * recv_param, void * header,
  void * data, size_t length
);

#endif