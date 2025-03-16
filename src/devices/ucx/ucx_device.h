#ifndef UCX_DEVICE_H_
#define UCX_DEVICE_H_

#include "ucp/api/ucp.h"

struct ucx_dev_args
{
  int peers_num;
  int id;
  int port;
  int oob_fd;
};

typedef struct
{
  ucp_context_h ucp_ctx;
} ucx_wpool_t;

int ucx_dev_init_device (struct ucx_dev_args * args, ucx_wpool_t * wpool);

#endif