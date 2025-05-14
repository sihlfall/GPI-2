
#include "GASPI.h"
#include "GPI2_Types.h"
#include "devices/ucx/ucx_device.h"

#include <pthread.h>

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

int gaspiu_start_sn (gaspi_context_t * ctx)
{
  return 0;
}

int gaspiu_stop_sn (gaspi_context_t * ctx)
{
  return 0;
}
