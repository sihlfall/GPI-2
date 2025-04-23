#ifndef _GPI2_UCX_H_
#define _GPI2_UCX_H_

#include "GPI2.h"
#include "GPI2_Dev.h"

#include "ucx_device.h"

struct ucx_wc {
  uint64_t wr_id;
  int status;
};

typedef struct
{
  struct ucx_device ucx_device;
  struct ucx_wc wc_grp_send[64];
} gaspi_ucx_ctx;

#endif //_GPI2_UCX_H_
