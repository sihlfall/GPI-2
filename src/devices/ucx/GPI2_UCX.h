#ifndef _GPI2_UCX_H_
#define _GPI2_UCX_H_

#include "GPI2.h"
#include "GPI2_Dev.h"

#include "ucx_qp.h"
#include "ucx_device.h"


typedef struct
{
  struct ucx_device ucx_device;
  struct ucx_wc wc_grp_send[64];
  struct ucx_qp **qpC[GASPI_MAX_QP];
  struct mpmc_queue scqC[GASPI_MAX_QP];
} gaspi_ucx_ctx;

#endif //_GPI2_UCX_H_
