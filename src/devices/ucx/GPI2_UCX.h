#ifndef _GPI2_UCX_H_
#define _GPI2_UCX_H_

#include "GPI2.h"
#include "GPI2_Dev.h"

#include "oob.h"

struct ucx_dev_address_length_pair {
  ucp_address_t * address;
  size_t address_length;
};

struct ucx_dev_args
{
  int peers_num;
  int id;
  int port;
  int oob_fd;
};



typedef struct
{
  struct ucx_dev_oob_server_thread oob_server;
  ucp_context_h ucp_ctx;
  ucp_worker_h default_worker;
  ucp_ep_h * eps;
} gaspi_ucx_ctx;

#endif //_GPI2_UCX_H_
