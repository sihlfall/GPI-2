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
void
ep_error_callback (void * args, ucp_ep_h ep, ucs_status_t status)
{
  struct ucx_device_endpoint * endpoint_entry = (struct ucx_device_endpoint *) args;

  switch (status) {
    case UCS_ERR_CONNECTION_RESET:
      {
        if (endpoint_entry)
        {
          * endpoint_entry = (struct ucx_device_endpoint) {0};
        }

        fprintf (stderr, "Server: Closing endpoint ...\n");
        (void) ucp_ep_close_nb (ep, UCP_EP_CLOSE_MODE_FORCE);
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
int
register_ep (struct ucx_device * ucx_device, ucp_ep_h ep, gaspi_rank_t rank) {
  if (rank >= UCX_DEVICE_MAX_RANKS)
  {
    fprintf (stderr, "Invalid rank value");
    /* TODO: Send error */
    goto err;
  }

  struct ucx_device_endpoint * endpoint_entry = &ucx_device->endpoints[rank];
  if (endpoint_entry->ucx_device) {
    fprintf (stderr, "An endpoint has already been assigned to rank %d\n", (int) rank);
    goto err;
  }
  * endpoint_entry = (struct ucx_device_endpoint) {
    .ucx_device = ucx_device,
    .rank = rank,
    .ep = ep
  };

  ucp_ep_modify_nb (ep, & (ucp_ep_params_t) {
    .field_mask = UCP_EP_PARAM_FIELD_ERR_HANDLER | UCP_EP_PARAM_FIELD_USER_DATA,
    .err_handler = {
      .cb = ep_error_callback,
      .arg = (void *) endpoint_entry
    },
    .user_data = (void *) endpoint_entry
  });

  return 0;

err:
  return 1;
}

static
void
handle_connection_callback (ucp_conn_request_h conn_request, void * arg)
{
  ucx_device_t * ucx_device = (ucx_device_t *) arg;

  fprintf (stderr, "Connection handler called\n");

  ucp_ep_h new_endpoint = 0;
  ucs_status_t status = ucp_ep_create (
    ucx_device->ucp_worker,
    & (ucp_ep_params_t) {
      .field_mask = UCP_EP_PARAM_FIELD_ERR_HANDLER |
        UCP_EP_PARAM_FIELD_CONN_REQUEST,
      .conn_request = conn_request,
      .err_handler = {
        .cb = ep_error_callback,
        .arg = NULL
      }
    },
    &new_endpoint
  );
  if (status != UCS_OK) {
    fprintf(stderr, "failed to create an endpoint on the server: (%s)\n",
            ucs_status_string(status));
    goto err_ep_create;
  }

  fprintf(stderr, "Server endpoint created\n");

  return;

err_ep_create:
  return;
}

static
int
ucx_dev_create_listener (ucx_device_t * ucx_device)
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
            .sin_port = htons (ucx_device->host_port)      
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

static
void
ucx_dev_cleanup_listener (ucx_device_t * ucp_device)
{
  if (ucp_device->ucp_listener)
  {
    ucp_listener_destroy (ucp_device->ucp_listener);
    ucp_device->ucp_listener = 0;
  }
}

static int ucx_dev_do_connect_to (ucx_device_t * ucx_device, char const * hostip4, uint16_t port);

static
void *
run_ucx_device (void * args)
{
  struct ucx_device * myself = (struct ucx_device *) args;

  if (ucx_dev_create_listener (myself))
  {
    fprintf (stderr, "Creating listener failed\n");
    goto err;
  }

  while (!myself->should_stop) {
    struct alf_tag_payload_pair msg;
    if (alf_dequeue (&myself->queue, &msg)) {
      switch (msg.tag) {
      case UCX_DEV_MSG_CONNECT:
        {
          struct ucx_device_msg_connect_data * p = (struct ucx_device_msg_connect_data *) msg.payload;
          ucx_dev_do_connect_to (myself, p->host, p->port);
          free (p); /* TO DO: This is pretty bad. */
        }
        break;
      default:
        break;
      }
    }

    ucp_worker_progress(myself->ucp_worker);
  }

  ucx_dev_cleanup_listener (myself);

err:
  /* TODO: Different return code in case of error. */
  pthread_exit (NULL);
}

int
ucx_dev_start_device (ucx_device_t * ucx_device)
{
  ucx_device->should_stop = 0;
  if (pthread_create (&ucx_device->server_tid, NULL, run_ucx_device, ucx_device)) {
    perror ("Failed to create server thread");
    return 1;
  }

  return 0;
}

void
ucx_dev_stop_device (ucx_device_t * ucx_device)
{
  ucx_device->should_stop = 1;
  (void) pthread_join (ucx_device->server_tid, NULL);
}


static
void
send_rehu_complete_callback (void * request, ucs_status_t status, void * user_data)
{
  ucp_request_free (request);
}

static
void
send_rehu (ucp_ep_h ep, gaspi_rank_t * rank)
{
  ucs_status_ptr_t request = ucp_am_send_nbx (
    ep, UCX_DEV_REHU, rank, sizeof(gaspi_rank_t), NULL, 0, & (ucp_request_param_t) {
      .op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_FLAGS,
      .cb = { .send = send_rehu_complete_callback },
      .flags = UCP_AM_SEND_FLAG_REPLY | UCP_AM_SEND_FLAG_EAGER
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
  struct ucx_device * ucx_device = (struct ucx_device *) arg;
  gaspi_rank_t * rank = (gaspi_rank_t *) header;

  fprintf (stderr, "Received a HUHU.\n");
  fprintf (stderr, "Received rank: %d\n", (int) * rank);

  if (!(param->recv_attr & UCP_AM_RECV_ATTR_FIELD_REPLY_EP))
  {
    fprintf (stderr, "Endpoint missing, send with UCP_AM_SEND_FLAG_REPLY");
    goto err;
  }
  if (register_ep (ucx_device, param->reply_ep, * rank)) {
    fprintf (stderr, "Could not register endpoint for rank %d\n", * rank);
    goto err;
  }

  send_rehu (param->reply_ep, &ucx_device->rank);

err:
  return UCS_OK;
}

static
ucs_status_t
am_rehu_callback (
  void * arg, const void * header, size_t header_length, void * data, size_t length, const ucp_am_recv_param_t * param
)
{
  struct ucx_device * ucx_device = (struct ucx_device *) arg;
  gaspi_rank_t * rank = (gaspi_rank_t *) header;

  fprintf (stderr, "Received a REHU.\n");
  fprintf (stderr, "Received rank: %d\n", (int) * rank);

  if (!(param->recv_attr & UCP_AM_RECV_ATTR_FIELD_REPLY_EP))
  {
    fprintf (stderr, "Endpoint missing, send with UCP_AM_SEND_FLAG_REPLY");
    goto err;
  }
  if (register_ep (ucx_device, param->reply_ep, * rank)) {
    fprintf (stderr, "Could not register endpoint for rank %d\n", * rank);
    goto err;
  }

err:
  return UCS_OK;
}

static void send_huhu_complete_callback (void *request, ucs_status_t status, void *user_data)
{
  ucp_request_free (request);
}

static
void
err_cb (void *arg, ucp_ep_h ep, ucs_status_t status)
{
  fprintf(stderr,
    "error handling callback was invoked with status %d (%s)\n",
    status, ucs_status_string (status)
  );
}

static
int
ucx_dev_do_connect_to (ucx_device_t * ucx_device, char const * hostip4, uint16_t port)
{
  fprintf (stderr, "Creating endpoint\n");

  ucp_ep_h client_ep;
  {
    struct sockaddr_in serv_addr = {
      .sin_family = AF_INET,
      .sin_port = htons (port)
    };
    if (inet_pton (AF_INET, hostip4, &serv_addr.sin_addr) < 0) {
      fprintf (stderr, "Invalid address/Address not supported");
      return 1;
    };
    
    ucs_status_t status = ucp_ep_create (
      ucx_device->ucp_worker,
      & (ucp_ep_params_t) {
        .field_mask = UCP_EP_PARAM_FIELD_FLAGS |
          UCP_EP_PARAM_FIELD_SOCK_ADDR   |
          UCP_EP_PARAM_FIELD_ERR_HANDLER |
          UCP_EP_PARAM_FIELD_ERR_HANDLING_MODE,
        .err_mode = UCP_ERR_HANDLING_MODE_PEER,
        .err_handler = {
          .cb = err_cb,
          .arg = NULL
        },
        .flags = UCP_EP_PARAMS_FLAGS_CLIENT_SERVER,
        .sockaddr = {
          .addr = (struct sockaddr *) & serv_addr,
          .addrlen = sizeof (struct sockaddr_in)
        }
      },
      &client_ep
    );
    if (status != UCS_OK)
    {
      fprintf(stderr, "Creating client EP failed\n");
      return 1;
    }
  }

  fprintf(stderr, "Client endpoint created\n");

  {
    ucs_status_ptr_t request = ucp_am_send_nbx (
      client_ep, UCX_DEV_HUHU, &ucx_device->rank, sizeof(gaspi_rank_t), NULL, 0, & (ucp_request_param_t) {
        .op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_FLAGS,
        .cb = { .send = send_huhu_complete_callback },
        .flags = UCP_AM_SEND_FLAG_REPLY | UCP_AM_SEND_FLAG_EAGER
      }
    );
    if (UCS_PTR_IS_ERR (request)) {
      fprintf (stderr, "Client: Error sending AM.\n");
      return 1;
    }
  }

  return 0;
}

int
ucx_dev_connect_to (ucx_device_t * ucx_device, char const * hostip4, uint16_t port)
{
  struct ucx_device_msg_connect_data * d = calloc (1, sizeof(struct ucx_device_msg_connect_data));
  *d = (struct ucx_device_msg_connect_data) {
    .host = hostip4,
    .port = port
  };
  return alf_enqueue (&ucx_device->queue, (struct alf_tag_payload_pair) {
    .tag = UCX_DEV_MSG_CONNECT,
    .payload = (alf_payload_type) d
  });
}

int
ucx_dev_init_device (ucx_device_t * ucx_device, gaspi_rank_t rank, uint16_t host_port)
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
      .field_mask = UCP_AM_HANDLER_PARAM_FIELD_ID | UCP_AM_HANDLER_PARAM_FIELD_FLAGS |
        UCP_AM_HANDLER_PARAM_FIELD_CB | UCP_AM_HANDLER_PARAM_FIELD_ARG,
      .id = UCX_DEV_HUHU,
      .flags = UCP_AM_FLAG_WHOLE_MSG,
      .cb = am_huhu_callback,
      .arg = (void *) ucx_device
    });
    if (status != UCS_OK) {
      GASPI_DEBUG_PRINT_ERROR("setting AM HUHU callback failed: %d", status);
      goto err_set_am_recv_handler;
    }
  }
  {
    ucs_status_t status = ucp_worker_set_am_recv_handler (ucp_worker, & (ucp_am_handler_param_t) {
      .field_mask = UCP_AM_HANDLER_PARAM_FIELD_ID | UCP_AM_HANDLER_PARAM_FIELD_FLAGS |
        UCP_AM_HANDLER_PARAM_FIELD_CB | UCP_AM_HANDLER_PARAM_FIELD_ARG,
      .id = UCX_DEV_REHU,
      .flags = UCP_AM_FLAG_WHOLE_MSG,
      .cb = am_rehu_callback,
      .arg = (void *) ucx_device
    });
    if (status != UCS_OK) {
      GASPI_DEBUG_PRINT_ERROR("setting AM REHU callback failed: %d", status);
      goto err_set_am_recv_handler;
    }
  }


  * ucx_device = (ucx_device_t) {
    .rank = rank,
    .ucp_ctx = ucp_context,
    .ucp_worker = ucp_worker,
    .host_port = host_port,
    .queue = (struct mpmc_queue) {0}
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
  ucp_worker_destroy (ucx_device->ucp_worker);
  ucp_cleanup (ucx_device->ucp_ctx);
}

