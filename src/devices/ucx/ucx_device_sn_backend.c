#include "ucx_device_sn_backend.h"

#include "GASPI_types.h"
#include "GPI2_Utility.h"

#include "ucp/api/ucp.h"
#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>

static 
void
cb_ep_error (void * args, ucp_ep_h ep, ucs_status_t status)
{
  switch (status)
  {
  case UCS_ERR_CONNECTION_RESET:
    fprintf (stderr, "SN Server: Closing endpoint ...\n");
    ucs_status_ptr_t request = ucp_ep_close_nb (ep, UCP_EP_CLOSE_MODE_FORCE);
    if (UCS_PTR_IS_PTR(request)) ucp_request_release (request);
    fprintf (stderr, "SN: Endpoint closed.\n");
    break;
  default:
    fprintf (
      stderr, "SN: error handling callback was invoked with status %d (%s)\n",
      status, ucs_status_string (status)
    );
    break;
  }
}

static
void
cb_listener_handle_connection (ucp_conn_request_h conn_request, void * arg)
{
  struct ucx_device_sn * udsn = (struct ucx_device_sn *) arg;

  fprintf (stderr, "SN: Connection handler called\n");

  ucs_status_t status = ucp_ep_create (
    udsn->sn_passive_worker, & (ucp_ep_params_t) {
      .field_mask = UCP_EP_PARAM_FIELD_CONN_REQUEST | UCP_EP_PARAM_FIELD_ERR_HANDLER,
      .conn_request = conn_request,
      .err_handler = { .cb = cb_ep_error }
    }, & (ucp_ep_h) {0}
  );
  if (status != UCS_OK) {
    fprintf(
      stderr, "SN: Failed to create an endpoint: (%s)\n", ucs_status_string(status)
    );
  } else {
    fprintf (stderr, "SN: Endpoint created\n");
  }
}

static
int
do_create_listener (struct ucx_device_sn * udsn)
{
  ucp_listener_h sn_listener;
  {
    ucs_status_t status = ucp_listener_create (
      udsn->sn_passive_worker,
      & (ucp_listener_params_t) {
        .field_mask = UCP_LISTENER_PARAM_FIELD_SOCK_ADDR |
          UCP_LISTENER_PARAM_FIELD_CONN_HANDLER,
        .sockaddr = (ucs_sock_addr_t) {
          .addr = (struct sockaddr *) & (struct sockaddr_in) {
            .sin_family = AF_INET,
            .sin_addr.s_addr = INADDR_ANY,
            .sin_port = htons (udsn->host_port)      
          },
          .addrlen = sizeof (struct sockaddr_in)
        },
        .conn_handler = (ucp_listener_conn_handler_t) {
          .cb = cb_listener_handle_connection,
          .arg = udsn
        }
      },
      &sn_listener
    );
    if (status != UCS_OK) {
      fprintf(stderr, "Failed to create listener\n");
      return 1;
    }
  }

  udsn->sn_listener = sn_listener;

  return 0;
}

static
void
do_cleanup_listener (struct ucx_device_sn * udsn)
{
  if (udsn->sn_listener) {
    ucp_listener_destroy (udsn->sn_listener);
    udsn->sn_listener = 0;
  }
}

/* 
 * ************************************************************************************
 * Run (passive worker thread main function)
 * ************************************************************************************
 */

static
void *
do_passive_worker_run (void * args)
{
  struct ucx_device_sn * myself = (struct ucx_device_sn *) args;

  fprintf(stderr, "Passive worker running\n");

  if (do_create_listener (myself)) {
    fprintf (stderr, "SN: Creating listener failed\n");
    goto err;
  }

  fprintf(stderr, "Listener created\n");

  while (!atomic_load_explicit(&myself->should_stop, memory_order_acquire)) {
    unsigned int ret = 0;
    do {
      ret = ucp_worker_progress (myself->sn_passive_worker);
    } while (ret);
    (void) ucp_worker_wait (myself->sn_passive_worker);
  }

  do_cleanup_listener (myself);

err:
  /* TODO: Different return code in case of error. */
  (void) pthread_exit (NULL);
}

enum ucx_device_sn_status
ucx_device_sn_init (
  struct ucx_device_sn * udsn, ucp_context_h ucp_context, uint16_t host_port
)
{
  fprintf (stderr, "*** Initializing sn device with host port %d\n", (int)host_port);

  ucp_worker_h active_worker = 0;
  {
    ucs_status_t status = ucp_worker_create (
      ucp_context,
      & (ucp_worker_params_t) {
        .field_mask = UCP_WORKER_PARAM_FIELD_THREAD_MODE,
        .thread_mode = UCS_THREAD_MODE_MULTI
      },
      &active_worker
    );
    if (status != UCS_OK) {
      GASPI_DEBUG_PRINT_ERROR("SN: ucp_worker_create (active) failed: %d", status);
      goto err_active_worker_create;
    }
  }

  ucp_worker_h passive_worker = 0;
  {
    ucs_status_t status = ucp_worker_create (
      ucp_context,
      & (ucp_worker_params_t) {
        .field_mask = UCP_WORKER_PARAM_FIELD_THREAD_MODE,
        .thread_mode = UCS_THREAD_MODE_SINGLE
      },
      &passive_worker
    );
    if (status != UCS_OK) {
      GASPI_DEBUG_PRINT_ERROR("SN: ucp_worker_create (passive) failed: %d", status);
      goto err_passive_worker_create;
    }
  }

  *udsn = (struct ucx_device_sn) {
    .sn_active_worker = active_worker,
    .sn_passive_worker = passive_worker,
    .host_port = host_port
  };

  return UCX_DEVICE_SN_OK;

err_passive_worker_create:
  ucp_worker_destroy (active_worker);

err_active_worker_create:
  return UCX_DEVICE_SN_ERR_UNSPECIFIED;
}

enum ucx_device_sn_status
ucx_device_sn_start (struct ucx_device_sn * udsn)
{
  atomic_store_explicit(&udsn->should_stop, 0, memory_order_release);
  if (pthread_create (
    &udsn->passive_server_tid, NULL, do_passive_worker_run, udsn
  )) {
    perror ("Failed to create server thread");
    return UCX_DEVICE_SN_ERR_UNSPECIFIED;
  }

  return UCX_DEVICE_SN_OK;
}

void
ucx_device_sn_stop (struct ucx_device_sn * udsn)
{
  atomic_store_explicit(&udsn->should_stop, 1, memory_order_release);
  (void) ucp_worker_signal (udsn->sn_passive_worker);
  (void) pthread_join (udsn->passive_server_tid, NULL);
} 

void
ucx_device_sn_cleanup (struct ucx_device_sn * udsn)
{
  /* TODO: implement */
}

int
ucx_device_sn_connect_to_rank (
  void
)
{
  return 0;
}