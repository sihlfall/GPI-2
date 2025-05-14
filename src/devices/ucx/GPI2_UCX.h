#ifndef _GPI2_UCX_H_
#define _GPI2_UCX_H_

#include "GPI2.h"
#include "GPI2_Dev.h"

#include "ucx_qp.h"
#include "ucx_device.h"

#include "ucp/api/ucp.h"


typedef struct
{
  ucp_context_h ucp_ctx;

  struct ucx_device_sn sn_device;

  struct ucx_device ucx_device;
  struct ucx_wc wc_grp_send[64];
  struct ucx_qp **qpC[GASPI_MAX_QP];
  struct ucx_cq scqC[GASPI_MAX_QP];
  struct ucx_cq scqGroups;
} gaspi_ucx_ctx;

ucx_device_status_t ucx_device_comm_ctx_init (gaspi_ucx_ctx * ucx_ctx);
void ucx_device_comm_ctx_cleanup (gaspi_ucx_ctx * ucx_ctx);

#endif //_GPI2_UCX_H_
