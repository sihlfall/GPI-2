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
pgaspi_dev_create_endpoint (
  gaspi_context_t const * gctx , int i, void **info, void **remote_info,
  size_t * info_size
)
{
  gaspi_ucx_ctx * ucx_device_ctx = (gaspi_ucx_ctx *) gctx->device->ctx;

  for (unsigned int c = 0; c < gctx->config->queue_num; c++)
  {
    struct ucx_qp * qp = ucx_qp_create (& (struct ucx_qp_init_attr) {
      .dst = i,
      .queue_size_max = gctx->config->queue_size_max,
      .cq = &ucx_device_ctx->scqC[c]
    });
    if (!qp)
    {
      fprintf (stderr, "Creating qp failed\n");
      return -1;
    }

    ucx_device_ctx->qpC[c][i] = qp;
  }

  *info = NULL;
  *remote_info = NULL;
  *info_size = 0;

  return 0;
}

//TODO:
int
pgaspi_dev_disconnect_context (gaspi_context_t * const gctx, const int i)
{
  NOTIMPLEMENTED()
}

int
pgaspi_dev_connect_context (gaspi_context_t const *const gctx, const int i)
{
  gaspi_ucx_ctx * ucx_device_ctx = (gaspi_ucx_ctx *) gctx->device->ctx;

  ucx_device_status_t status = ucx_device_connect_to (&ucx_device_ctx->ucx_device, /*i,*/
    pgaspi_gethostname (i),
    gctx->config->dev_config.params.tcp.port + i, i
    /*gctx->poff[i]*/
  );
  fprintf (stderr, "Connect to returned status: %d\n", status);
  return status == UCX_DEVICE_OK ? 0 : -1;
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
pgaspi_dev_comm_queue_is_valid (
  gaspi_context_t const * const gctx, const unsigned int id
)
{
  gaspi_ucx_ctx const * ucx_dev_ctx = (gaspi_ucx_ctx const *) gctx->device->ctx;

  if (!ucx_dev_ctx->qpC[id])
  {
    return GASPI_ERR_INV_QUEUE;
  }

  return 0;
}

int
pgaspi_dev_init_core (gaspi_context_t * const gctx)
{
  gctx->device = calloc (1, sizeof (gctx->device));
  if (!gctx->device) goto err_alloc_gctx_device;

  gaspi_ucx_ctx * ucx_dev_ctx = calloc (1, sizeof (gaspi_ucx_ctx));
  if (!ucx_dev_ctx) goto err_alloc_gctx_device_ctx;
  gctx->device->ctx = ucx_dev_ctx;

  if (!ucx_init_cq (&ucx_dev_ctx->scqGroups, gctx->config->queue_size_max))
  {
    GASPI_DEBUG_PRINT_ERROR ("Failed to create CQ (ucx_init_cq)");
    return -1;
  }

  /* One-sided Communication */
  for (unsigned int c = 0; c < gctx->config->queue_num; c++)
  {
    /* TO DO: Calculate log2(queue size) here? */
    if (!ucx_init_cq (&ucx_dev_ctx->scqC[c], gctx->config->queue_size_max))
    {
      GASPI_DEBUG_PRINT_ERROR ("Failed to create CQ (ucx_init_cq)");
      return -1;
    }
  }


  for (unsigned int c = 0; c < gctx->config->queue_num; c++)
  {
    ucx_dev_ctx->qpC[c] =
      (struct ucx_qp **) calloc (gctx->tnc, sizeof (struct ucx_qp *));
    if (!ucx_dev_ctx->qpC[c])
    {
      GASPI_DEBUG_PRINT_ERROR ("Failed to allocate memory.");
      goto err_alloc_gctx_qpC;
    }
  }


  if (ucx_device_init (
    &ucx_dev_ctx->ucx_device, gctx->rank, gctx->config->dev_config.params.tcp.port + gctx->rank
  ) != UCX_DEVICE_OK)
  {
    GASPI_DEBUG_PRINT_ERROR ("Failed to initialize device.");
    goto err_device_init;
  }

  if (ucx_device_start (&ucx_dev_ctx->ucx_device) != UCX_DEVICE_OK)
  {
    GASPI_DEBUG_PRINT_ERROR ("Failed to start device.");
    goto err_device_start;
  }

  return 0;

err_device_start:
  ucx_device_cleanup (&ucx_dev_ctx->ucx_device);
err_device_init:
  /* TODO: should we deallocate the qpCs? */
err_alloc_gctx_qpC:
  /* TODO: should we deallocate the qpCs that were already initialized? */
  free (ucx_dev_ctx);
err_alloc_gctx_device_ctx:
  free (gctx->device); gctx->device = NULL;
err_alloc_gctx_device:
  return -1;
}

int
pgaspi_dev_cleanup_core (gaspi_context_t * const gctx)
{
  gaspi_ucx_ctx * ucx_dev_ctx = (gaspi_ucx_ctx *) gctx->device->ctx;
  ucx_device_stop (&ucx_dev_ctx->ucx_device);
  ucx_device_cleanup (&ucx_dev_ctx->ucx_device);
  free (ucx_dev_ctx);
  free (gctx->device); gctx->device = NULL;
  return 0;
}
