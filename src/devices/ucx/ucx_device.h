#ifndef UCX_DEVICE_H_
#define UCX_DEVICE_H_

#include "GPI2_UCX.h"
#include "ucp/api/ucp.h"

int ucx_dev_init_device (struct ucx_dev_args * args, gaspi_ucx_ctx * wpool);
void ucx_dev_stop_device(gaspi_ucx_ctx * wpool);


#endif