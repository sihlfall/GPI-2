#ifndef _GPI2_UCX_H_
#define _GPI2_UCX_H_

#include "GPI2.h"
#include "GPI2_Dev.h"

#include "ucx_device.h"
#include "oob.h"

struct ucx_dev_address_length_pair {
  ucp_address_t * address;
  size_t address_length;
};

typedef struct
{
  struct ucx_dev_oob_server_thread oob_server;
  ucx_wpool_t * wpool;
  struct ucx_dev_address_length_pair * addresses;
  ucp_ep_h * eps;
} gaspi_ucx_ctx;


#endif //_GPI2_UCX_H_
