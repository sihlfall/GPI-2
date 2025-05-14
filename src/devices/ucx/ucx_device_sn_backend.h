#ifndef UCX_DEVICE_SN_BACKEND_H_
#define UCX_DEVICE_SN_BACKEND_H_

#include "ucp/api/ucp.h"
#include <pthread.h>

enum ucx_device_sn_status {
  UCX_DEVICE_SN_OK = 0,
  UCX_DEVICE_SN_ERR_UNSPECIFIED = -1
};
  
struct ucx_device_sn {
  _Atomic _Bool should_stop;
  ucp_worker_h sn_active_worker;
  ucp_worker_h sn_passive_worker;
  pthread_t passive_server_tid;
  ucp_listener_h sn_listener;
  uint16_t host_port;  
};

#endif