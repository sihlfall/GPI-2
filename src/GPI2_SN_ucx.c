
#include "GASPI.h"
#include "GPI2_CommCtx.h"
#include "GPI2_Types.h"
#include "devices/ucx/GPI2_UCX.h"
#include "devices/ucx/ucx_device.h"
#include "devices/ucx/ucx_device_sn_backend.h"

#include <arpa/inet.h>
#include <pthread.h>

// TODO: Delete when we do not have SN and SN_ucx in parallel anymore

#define TEMP_PORT_OFFSET (30)
struct request {
  _Atomic int response_ready;
  pthread_mutex_t lock;
  pthread_cond_t cond;
};

static inline
void
request_init (struct request * request)
{
  *request = (struct request) {
    .response_ready = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER
  };
}

static inline
void
request_cleanup (struct request * request)
{
  pthread_mutex_destroy (&request->lock);
  pthread_cond_destroy (&request->cond);
}

#define EXPECT_ZERO(v,msg,err) \
  do { \
    if (!v) \
    { \
      fprintf (stderr, msg); \
      goto err; \
    } \
  } while (0)

/* static */
/*
gaspi_return_t
gaspiu_sn_connect_to_rank (
  gaspi_context_t * gctx,
  gaspi_rank_t rank,
  gaspi_timeout_t timeout_ms
)
{
  //struct ucx_device * ucx_device = (struct ucx_device *)gctx->device;

  struct request request;
  request_init (&request);

  EXPECT_ZERO(pthread_mutex_lock (&request.lock),
    "Could not lock mutex", err_lock
  );
  // enqueue request
  while (!atomic_load_explicit(&request.response_ready, memory_order_relaxed))
  {
    EXPECT_ZERO(pthread_cond_wait (&request.cond, &request.lock),
      "Cond_wait failed", err_wait
    );
  }
  EXPECT_ZERO(pthread_mutex_unlock (&request.lock),
    "Could not unlock mutex", err_unlock
  );

  request_cleanup (&request);

  // check for success

  return GASPI_SUCCESS;

err_unlock:
err_wait:
  (void) pthread_mutex_unlock (&request.lock);
err_lock:
  request_cleanup (&request);
  return GASPI_ERROR;
}
*/

int gaspiu_init_and_start_sn (gaspi_context_t * gctx)
{
  gaspi_ucx_ctx * ucx_ctx = (struct gaspi_utx_ctx *) gctx->device->ctx;
  
  uint16_t port = gctx->config->sn_port + gctx->local_rank + TEMP_PORT_OFFSET;  
  if (
    ucx_device_sn_init (
      &ucx_ctx->sn_device, ucx_ctx->ucp_ctx, gctx->tnc, port
    ) != UCX_DEVICE_OK
  ) {
    fprintf (stderr, "Error initializing SN device\n");
    goto err_sn_init;
  };
  if (
    ucx_device_sn_start (&ucx_ctx->sn_device)
  ) {
    fprintf (stderr, "Error initializing SN device\n");
    goto err_sn_start;
  }
  return 0;

err_sn_start:
  ucx_device_sn_cleanup (&ucx_ctx->sn_device);
err_sn_init:
  return -1;
}

int
gaspiu_stop_and_cleanup_sn (gaspi_context_t * gctx)
{
  gaspi_ucx_ctx * ucx_ctx = (gaspi_ucx_ctx *) gctx->device->ctx;
  ucx_device_sn_stop (&ucx_ctx->sn_device);
  ucx_device_sn_cleanup (&ucx_ctx->sn_device);
  return 0;
}

int
gaspiu_sn_connect_to_rank (gaspi_rank_t rank, gaspi_timeout_t timeout_ms)
{
  gaspi_context_t const *const gctx = &glb_gaspi_ctx;
  gaspi_ucx_ctx * ucx_ctx = (gaspi_ucx_ctx *) gctx->device->ctx;

  if (
    ucx_device_sn_connect_to_rank (
      &ucx_ctx->sn_device, pgaspi_gethostname (rank),
      gctx->config->sn_port + TEMP_PORT_OFFSET + gctx->poff[rank],
      rank, timeout_ms
    ) != UCX_DEVICE_SN_OK
  ) {
    fprintf (stderr, "SN: Could not connect to rank %d\n", (int) rank);
    return -1;
  }
  return 0;
}
