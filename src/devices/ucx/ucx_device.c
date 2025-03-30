#include "ucx_device.h"
#include "GPI2_UCX.h"
#include "ucp/api/ucp.h"

#include "GPI2.h"
#include "GPI2_Utility.h"

#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK_GOTO_ERROR(cond, msg, errlabel) \
  do {                                        \
    if (!(cond)) {                            \
      reterr = 1;                             \
      fprintf (stderr, (msg));                \
      perror ((msg));                         \
      goto errlabel;                          \
    }                                         \
  } while (0);                 

static
void *
run_ucx_device (void * args)
{
  struct ucx_device * myself = (struct ucx_device *) args;

  while (!myself->should_stop) {
    ucp_worker_progress(myself->ucp_worker);
  }

  pthread_exit (NULL);
}

int
ucx_dev_start_thread (ucx_device_t * ucx_device)
{
  ucx_device->should_stop = 0;
  if (pthread_create (&ucx_device->server_tid, NULL, run_ucx_device, ucx_device)) {
    perror ("Failed to create server thread");
    return 1;
  }

  return 0;
}

void
ucx_dev_stop_thread (ucx_device_t * ucx_device)
{
  /* TODO: remove from here! */
  ucx_device->should_stop = 1;
  (void) pthread_join (ucx_device->server_tid, NULL);
  for (size_t i = 0; i < UCX_DEVICE_MAX_ENDPOINTS; ++i) {
    struct ucx_device_endpoint * dev_ep = &ucx_device->endpoints[i];
    if (dev_ep->ep)
    {
      ucp_ep_close_nb (dev_ep->ep, UCP_EP_CLOSE_MODE_FLUSH);
      dev_ep->ep = 0;
    }
  }
}

static
void
ep_error_callback (void * args, ucp_ep_h ep, ucs_status_t status)
{
  struct ucx_device_endpoint * endpoint = (struct ucx_device_endpoint *) args;

  switch (status) {
  case UCS_ERR_CONNECTION_RESET:
    {
      fprintf (stderr, "Server: Closing endpoint ...\n");
      (void) ucp_ep_close_nb (endpoint->ep, UCP_EP_CLOSE_MODE_FORCE);
      * endpoint = (struct ucx_device_endpoint) {0};
      fprintf (stderr, "Endpoint closed.\n");
    } break;
  default:
    {
      fprintf (
        stderr, "error handling callback was invoked with status %d (%s)\n", status, ucs_status_string (status)
      );
    }
  }
}

static
void
handle_connection_callback (ucp_conn_request_h conn_request, void * args)
{
  ucx_device_t * ucx_device = (ucx_device_t *) args;

  fprintf (stderr, "Connection handler called\n");

  struct ucx_device_endpoint * new_endpoint = &ucx_device->endpoints[ucx_device->n_endpoints++];
  * new_endpoint = (struct ucx_device_endpoint) {0};

  ucs_status_t status = ucp_ep_create (
    ucx_device->ucp_worker,
    & (ucp_ep_params_t) {
      .field_mask = UCP_EP_PARAM_FIELD_ERR_HANDLER |
        UCP_EP_PARAM_FIELD_CONN_REQUEST,
      .conn_request = conn_request,
      .err_handler = {
        .cb = ep_error_callback,
        .arg = new_endpoint
      }
    },
    &new_endpoint->ep
  );
  if (status != UCS_OK) {
    fprintf(stderr, "failed to create an endpoint on the server: (%s)\n",
            ucs_status_string(status));
    goto err_ep_create;
  }

  fprintf(stderr, "Server endpoint created\n");

  return;

err_ep_create:
  --ucx_device->n_endpoints;
  * new_endpoint = (struct ucx_device_endpoint) {0};
  return;
}

int
ucx_dev_create_listener (ucx_device_t * ucx_device, uint16_t port)
{
  ucp_listener_h ucp_listener;
  {
    ucs_status_t status = ucp_listener_create (
      ucx_device->ucp_worker,
      & (ucp_listener_params_t) {
        .field_mask = UCP_LISTENER_PARAM_FIELD_SOCK_ADDR |
          UCP_LISTENER_PARAM_FIELD_CONN_HANDLER,
        .sockaddr = (ucs_sock_addr_t) {
          .addr = (struct sockaddr *) & (struct sockaddr_in) {
            .sin_family = AF_INET,
            .sin_addr.s_addr = INADDR_ANY,
            .sin_port = htons (port)                    
          },
          .addrlen = sizeof (struct sockaddr_in)
        },
        .conn_handler = (ucp_listener_conn_handler_t) {
          .cb = handle_connection_callback,
          .arg = ucx_device
        }
      },
      &ucp_listener
    );
    if (status != UCS_OK)
    {
      fprintf(stderr, "Failed to create listener\n");
      return 1;
    }
  }

  ucx_device->ucp_listener = ucp_listener;

  return 0;
}

void
ucx_dev_cleanup_listener (ucx_device_t * ucp_device)
{
  if (ucp_device->ucp_listener)
  {
    ucp_listener_destroy (ucp_device->ucp_listener);
    ucp_device->ucp_listener = 0;
  }
}

static
void
send_rehu_complete_callback (void * request, ucs_status_t status, void * user_data)
{
  ucp_request_free (request);
}

static
void
send_rehu (ucp_ep_h ep)
{
  ucs_status_ptr_t request = ucp_am_send_nbx (
    ep, UCX_DEV_REHU, & (gaspi_rank_t) { 55 }, sizeof(gaspi_rank_t), NULL, 0, & (ucp_request_param_t) {
      .op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_FLAGS,
      .cb = { .send = send_rehu_complete_callback },
      .flags = UCP_AM_SEND_FLAG_REPLY | UCP_AM_SEND_FLAG_EAGER | UCP_AM_SEND_FLAG_COPY_HEADER
    }
  );
  if (UCS_PTR_IS_ERR (request)) {
    fprintf (stderr, "Server: Error sending REHU AM.\n");
    return;
  }
  else if (request)
  {
    ucp_request_free (request);
  }
}

static
ucs_status_t
am_huhu_callback (
  void * arg, const void * header, size_t header_length, void * data, size_t length, const ucp_am_recv_param_t * param
)
{
  gaspi_rank_t * rank = (gaspi_rank_t *) header;

  fprintf (stderr, "Received a HUHU.\n");
  fprintf (stderr, "Received rank: %d\n", (int) * rank);

  send_rehu (param->reply_ep);

  return UCS_OK;
}

int
ucx_dev_init_device (ucx_device_t * ucx_device)
{
  ucp_config_t * config = NULL;
  {
    ucs_status_t status = ucp_config_read("GPI2", NULL, &config);
    if (status != UCS_OK) {
      GASPI_DEBUG_PRINT_ERROR("ucp_config_read failed: %d", status);
      goto err_config_read;
    }
  }

  ucp_context_h ucp_context = 0;
  {
    ucs_status_t status = ucp_init (
      & (ucp_params_t) {
        .field_mask = UCP_PARAM_FIELD_FEATURES,
        
        /* We do need tag matching for send/recv. */
        .features = UCP_FEATURE_AM
      },
      config,
      &ucp_context
    );
    if (status != UCS_OK) {
      GASPI_DEBUG_PRINT_ERROR("ucp_init failed: %d", status);
      ucp_config_release(config);
      goto err_init;
    }
  }

  ucp_config_release(config);

  ucp_worker_h ucp_worker = 0;
  {
    ucs_status_t status = ucp_worker_create (
      ucp_context,
      & (ucp_worker_params_t) {
        .field_mask = UCP_WORKER_PARAM_FIELD_THREAD_MODE,
        .thread_mode = UCS_THREAD_MODE_MULTI
      },
      &ucp_worker
    );
    if (status != UCS_OK) {
      GASPI_DEBUG_PRINT_ERROR("ucp_worker_create failed: %d", status);
      goto err_worker_create;
    }
  }
  
  {
    ucs_status_t status = ucp_worker_set_am_recv_handler (ucp_worker, & (ucp_am_handler_param_t) {
      .field_mask = UCP_AM_HANDLER_PARAM_FIELD_ID | UCP_AM_HANDLER_PARAM_FIELD_FLAGS | UCP_AM_HANDLER_PARAM_FIELD_CB,
      .id = UCX_DEV_HUHU,
      .flags = UCP_AM_FLAG_WHOLE_MSG,
      .cb = am_huhu_callback
    });
    if (status != UCS_OK) {
      GASPI_DEBUG_PRINT_ERROR("setting AM HUHU callback failed: %d", status);
      goto err_set_am_recv_handler;
    }
  }

  * ucx_device = (ucx_device_t) {
    .ucp_ctx = ucp_context,
    .ucp_worker = ucp_worker
  };

  return 0;

err_set_am_recv_handler:
  ucp_worker_destroy (ucp_worker);

err_worker_create:
  ucp_cleanup (ucp_context);

err_init:
err_config_read:
  return -1;
}

void
ucx_dev_cleanup_device(struct ucx_device * ucx_device)
{
//  ucp_worker_destroy(wpool->default_worker);
  ucp_cleanup(ucx_device->ucp_ctx);
}
