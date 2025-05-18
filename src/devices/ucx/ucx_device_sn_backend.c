#include "ucx_device_sn_backend.h"

#include "GASPI_types.h"
#include "GPI2_Utility.h"

#include "ucp/api/ucp.h"
#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>

#define AM_CMD (999)

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

/* ************************************************************************************
 * AM handler
 * ************************************************************************************
 */

struct gaspi_cd_header_base {
  size_t op_len;
  int op;
  int rank;
};

static
ucs_status_t
on_am_cmd (
  void * arg, const void * header, size_t header_length, void * data, size_t length,
  const ucp_am_recv_param_t * param
)
{
  struct ucx_sn_device * usnd = (struct ucx_sn_device *) arg;

  fprintf (stderr, "SN: AM recv handler called with op %d\n", ((struct gaspi_cd_header_base *)header)->op);

err:
  return UCS_OK;
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
  struct ucx_device_sn * udsn, ucp_context_h ucp_context, int tnc, uint16_t host_port
)
{
  struct ucx_device_sn_ep_entry * ep_entries = calloc (tnc, sizeof (*ep_entries));
  if (!ep_entries)
  {
    fprintf (stderr, "Failed to allocate ep entries\n");
    goto err_alloc_ep_entries;
  }

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

  {
    ucs_status_t status = ucp_worker_set_am_recv_handler (passive_worker,
      & (ucp_am_handler_param_t) {
        .field_mask = UCP_AM_HANDLER_PARAM_FIELD_ID |
          UCP_AM_HANDLER_PARAM_FIELD_FLAGS |
          UCP_AM_HANDLER_PARAM_FIELD_CB | UCP_AM_HANDLER_PARAM_FIELD_ARG,
        .id = AM_CMD,
        .flags = UCP_AM_FLAG_WHOLE_MSG,
        .cb = on_am_cmd,
        .arg = (void *) udsn
      }
    );
    if (status != UCS_OK)
    {
      GASPI_DEBUG_PRINT_ERROR("SN: setting AM recv handler failed: %d", status);
      goto err_set_am_recv_handler;
    }
  }

  *udsn = (struct ucx_device_sn) {
    .sn_active_worker = active_worker,
    .sn_passive_worker = passive_worker,
    .ep_entries = ep_entries,
    .host_port = host_port
  };

  return UCX_DEVICE_SN_OK;

err_set_am_recv_handler:
  ucp_worker_destroy (passive_worker);

err_passive_worker_create:
  ucp_worker_destroy (active_worker);

err_active_worker_create:
  free (ep_entries);

err_alloc_ep_entries:
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

enum ucx_device_sn_status
ucx_device_sn_connect_to_rank (
  struct ucx_device_sn * udsn,
  char const * hostip4, uint64_t port,
  gaspi_rank_t rank, gaspi_timeout_t timeout_ms
)
{
  struct sockaddr_in serv_addr = {
    .sin_family = AF_INET,
    .sin_port = htons (port)
  };
  if (inet_pton (AF_INET, hostip4, &serv_addr.sin_addr) < 0) {
    fprintf (stderr, "Invalid address/Address not supported");
    goto err_inet_pton;
  };

  // TODO: Handle timeout.
  ucp_ep_h ep;
  {
    ucs_status_t status = ucp_ep_create (
      udsn->sn_active_worker,
      & (ucp_ep_params_t) {
        .field_mask = UCP_EP_PARAM_FIELD_FLAGS |
          UCP_EP_PARAM_FIELD_SOCK_ADDR   |
          UCP_EP_PARAM_FIELD_ERR_HANDLER |
          UCP_EP_PARAM_FIELD_ERR_HANDLING_MODE |
          UCP_EP_PARAM_FIELD_USER_DATA,
        .err_mode = UCP_ERR_HANDLING_MODE_PEER,
        .err_handler = { .cb = cb_ep_error },
        .flags = UCP_EP_PARAMS_FLAGS_CLIENT_SERVER,
        .sockaddr = {
          .addr = (struct sockaddr *) &serv_addr,
          .addrlen = sizeof (struct sockaddr_in)
        }
      },
      &ep
    ); 
    if (status != UCS_OK)
    {
      fprintf(
        stderr, "SN active: Failed to create an endpoint: (%s)\n",
        ucs_status_string(status)
      );
      goto err_ep_create;
    }
  }

  udsn->ep_entries[rank] = (struct ucx_device_sn_ep_entry) {
    .is_connected = 1,
    .ep = ep
  };

  return UCX_DEVICE_SN_OK;

err_ep_create:
err_inet_pton:
  return UCX_DEVICE_SN_ERR_UNSPECIFIED;
}

static
void
cb_just_free_request (void * request, ucs_status_t status, void * user_data)
{
  fprintf (stderr, "Freeing request, status: %d\n", status);
  ucp_request_free (request);
}

static
void
cb_set_bool_true (void * request, ucs_status_t status, void * user_data)
{
  _Bool * complete = (_Bool *) user_data;
  *complete = 1;
}

/* From ucp_client_server.c, adjusted */
static
int
request_wait_and_finalize (
  ucp_worker_h ucp_worker, ucs_status_ptr_t request, _Bool * complete
)
{
  if (!request) {
    /* operation was completed immediately */
    return UCS_OK;
  } else if (UCS_PTR_IS_ERR(request)) {
    return UCS_PTR_STATUS(request);
  }

  ucs_status_t status = UCS_OK;
  while (!*complete) {
    unsigned int ret = 0;
    do { ret = ucp_worker_progress (ucp_worker); } while (ret);
    if (*complete) break;
    status = ucp_worker_wait (ucp_worker);
    if (status != UCS_OK) goto err;
  }
  status = ucp_request_check_status (request);
  if (status != UCS_OK) goto err;

  ucp_request_free(request);
  return UCS_OK;

err:
  ucp_request_free(request);
  fprintf (stderr, "unable to send UCX message (%s)\n", ucs_status_string (status));
  return status;
}

enum ucx_device_sn_status
ucx_device_sn_send_recv_cmd (
  struct ucx_device_sn * udsn, gaspi_rank_t target_rank,
  unsigned char * header, size_t header_size,
  unsigned char * recv_buf, size_t recv_size
)
{
  struct ucx_device_sn_ep_entry * ep_entry = &udsn->ep_entries [target_rank];
  if (!ep_entry->is_connected) {
    fprintf (stderr, "Target rank not connected\n");
    goto err_not_connected;
  }

  fprintf (stderr, "Sending ...\n");

  _Bool complete = 0;
  ucs_status_ptr_t request = ucp_am_send_nbx (
    ep_entry->ep, AM_CMD, header, header_size, NULL, 0,
    & (ucp_request_param_t) {
      .op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_FLAGS |
        UCP_OP_ATTR_FIELD_USER_DATA,
      .cb = { .send = cb_set_bool_true },
      .flags = UCP_AM_SEND_FLAG_EAGER | UCP_AM_SEND_FLAG_REPLY,
      .user_data = &complete
    }
  );
  if (
    request_wait_and_finalize (udsn->sn_active_worker, request, &complete) != UCS_OK
  ) {
    goto err_am_send;
  }

  return UCX_DEVICE_SN_OK;

err_am_send:
err_not_connected:
  return UCX_DEVICE_SN_ERR_UNSPECIFIED;
}