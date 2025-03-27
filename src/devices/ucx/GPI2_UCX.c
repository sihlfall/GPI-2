#include "GPI2_UCX.h"
#include "ucx_device.h"
#include "GASPI.h"
#include "GPI2.h"
#include "GPI2_Dev.h"
#include "GPI2_SN.h"
#include "GPI2_Utility.h"
#include "ucp/api/ucp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOTIMPLEMENTED() \
  do {                                                                \
    fprintf(stderr, "Not implemented [%s:%i]\n", __FILE__, __LINE__); \
    exit(1);                                                          \
  } while (0);

int
pgaspi_dev_create_endpoint (gaspi_context_t const *const GASPI_UNUSED (gctx),
                            const int GASPI_UNUSED (i),
                            void **info,
                            void **remote_info,
                            size_t * info_size)
{
  *info = NULL;
  *remote_info = NULL;
  *info_size = 0;

  return 0;
}

//TODO:
int
pgaspi_dev_disconnect_context (gaspi_context_t * const gctx,
                               const int i)
{
  gaspi_ucx_ctx * ucx_device_ctx = (gaspi_ucx_ctx *) gctx->device->ctx;

  if (ucx_device_ctx->eps[i]) {
    ucp_ep_close_nb (ucx_device_ctx->eps[i], UCP_EP_CLOSE_MODE_FLUSH);
    ucx_device_ctx->eps[i] = 0;
  }
  
  return 0;
}

int
pgaspi_dev_connect_context (gaspi_context_t const *const gctx,
                            const int i)
{
  /* gaspi_ucx_ctx * ucx_device_ctx = (gaspi_ucx_ctx *) gctx->device->ctx; */

  NOTIMPLEMENTED()
}

int
pgaspi_dev_comm_queue_connect (gaspi_context_t const *const GASPI_UNUSED (gctx),
                               const unsigned short GASPI_UNUSED (q),
                               const int GASPI_UNUSED (i))
{
  NOTIMPLEMENTED()
}

int
pgaspi_dev_comm_queue_delete (gaspi_context_t const *const gctx,
                              const unsigned int id)
{
  NOTIMPLEMENTED()
}

int
pgaspi_dev_comm_queue_create (gaspi_context_t const *const gctx,
                              const unsigned int id,
                              const unsigned short GASPI_UNUSED (remote_node))
{
  NOTIMPLEMENTED()
}

int
pgaspi_dev_comm_queue_is_valid (gaspi_context_t const *const gctx,
                                const unsigned int id)
{
  NOTIMPLEMENTED()
}

int
pgaspi_dev_init_core (gaspi_context_t * const gctx)
{
  int ret = 0;

  gctx->device = calloc (1, sizeof (gctx->device));
  if (NULL == gctx->device)
  {
    return -1;
  }

  gctx->device->ctx = calloc (1, sizeof (gaspi_ucx_ctx));
  if (NULL == gctx->device->ctx)
  {
    free (gctx->device);
    return -1;
  }

  gaspi_ucx_ctx *const ucx_dev_ctx = (gaspi_ucx_ctx *) gctx->device->ctx;

  ucx_dev_ctx->eps = calloc(gctx->tnc, sizeof (ucp_ep_h));

  struct ucx_dev_args *dev_args = malloc (sizeof (struct ucx_dev_args));

  if (NULL == dev_args)
  {
    GASPI_DEBUG_PRINT_ERROR ("Failed to allocate memory.");
    return -1;
  }

  dev_args->peers_num = gctx->tnc;
  dev_args->id = gctx->rank;
  dev_args->port =
    gctx->config->dev_config.params.tcp.port + gctx->local_rank;


  if ( ucx_dev_init_device (dev_args, &ucx_dev_ctx->oob_server) != 0)
  {
    GASPI_DEBUG_PRINT_ERROR ("Failed to initialize device.");
    return -1;
  }

  if (ucx_dev_create_listener (&ucx_dev_ctx->oob_server, gctx->config->dev_config.params.tcp.port) != 0)
  {
    GASPI_DEBUG_PRINT_ERROR ("Failed to create listener.");
    return -1;
  }

  return ret;
}

int
pgaspi_dev_cleanup_core (gaspi_context_t * const gctx)
{
  gaspi_ucx_ctx *const ucx_dev_ctx = (gaspi_ucx_ctx *) gctx->device->ctx;
  if (ucx_dev_ctx->oob_server.ucp_listener != 0) ucx_dev_cleanup_listener (&ucx_dev_ctx->oob_server);
  for (int i = 0; i < gctx->tnc; ++i) {
    if (ucx_dev_ctx->eps[i]) {
      ucp_ep_close_nb (ucx_dev_ctx->eps[i], UCP_EP_CLOSE_MODE_FORCE);
    }
  }
  ucx_dev_stop_device (&ucx_dev_ctx->oob_server);
  free (ucx_dev_ctx->eps);
  free (gctx->device->ctx);
  gctx->device->ctx = NULL;
  return 0;
}
