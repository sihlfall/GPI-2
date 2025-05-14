#include "ucx_device.h"
#include "GPI2_UCX.h"
#include "GPI2_CommCtx.h"
#include "GASPI.h"
#include "ucp/api/ucp.h"
#include "arpa/inet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Server
 */

static
int
run_server (uint16_t host_port, gaspi_rank_t rank)
{
  int ret = 0;

  gaspi_context_t gctx;
  if (gaspiu_initialize_comm_ctx (&gctx)) {
    fprintf (stderr, "Could not initialize comm context\n");
    ret = 1;
    goto err_init_comm_ctx;
  }
  gaspi_ucx_ctx * ucx_ctx = gctx.device->ctx;

  struct ucx_device ucx_device;
  if (ucx_device_init (ucx_ctx->ucp_ctx, &ucx_device, rank, host_port) != UCX_DEVICE_OK) {
    fprintf (stderr, "Could not create ucx_device\n");
    ret = 1;
    goto err_init_device;
  }

  if (ucx_device_start (&ucx_device) != UCX_DEVICE_OK) {
    fprintf (stderr, "Could not start thread.\n");
    ret = 1;
    goto err_start_device;
  }

  fprintf(stderr, "Server is running in a separate thread. Press any key to stop.\n");

  /* Wait for keypress */
  getc (stdin);

  fprintf (stderr, "Stopping ucx device thread ...\n");
  ucx_device_stop (&ucx_device);

err_start_device:
  fprintf (stderr, "Cleanup ...\n");
  ucx_device_cleanup (&ucx_device);
  fprintf (stderr, "Server stopped.\n");

err_init_device:
  gaspiu_cleanup_comm_ctx (&gctx);
err_init_comm_ctx:
  return ret;
}

/*
 * Client
 */

static
ucs_status_t
initialize_ucp_context (ucp_context_h * ucp_context)
{
  ucs_status_t status = ucp_init (
    & (ucp_params_t) {
      .field_mask = UCP_PARAM_FIELD_FEATURES,
      .features = UCP_FEATURE_AM
    },
    NULL, ucp_context
  );
  return status;
}

static
void
cleanup_ucp_context (ucp_context_h ucp_context)
{
  ucp_cleanup (ucp_context);
}

static
ucs_status_t
create_worker (ucp_context_h ucp_context, ucp_worker_h * worker)
{
  ucs_status_t status = ucp_worker_create (
    ucp_context,
    & (ucp_worker_params_t) {
      .field_mask = UCP_WORKER_PARAM_FIELD_THREAD_MODE,
      .thread_mode = UCS_THREAD_MODE_SINGLE
    },
    worker
  );
  if (status != UCS_OK) return status;
  return UCS_OK;
}

static
void
destroy_worker (ucp_worker_h worker)
{
  ucp_worker_destroy (worker);
}

static
ucs_status_t
am_rehu_callback (
  void * arg, const void * header, size_t header_length, void * data, size_t length, const ucp_am_recv_param_t * param
)
{
  gaspi_rank_t * rank = (gaspi_rank_t *) header;

  fprintf (stderr, "Received a REHU.\n");
  fprintf (stderr, "Received rank: %d\n", (int) * rank);

  return UCS_OK;
}

static int send_complete = 0;

static void send_cb (void *request, ucs_status_t status, void *user_data)
{
  send_complete = 1;
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
client_make_request (ucp_worker_h ucp_worker, char const * hostip4, uint16_t port, gaspi_rank_t * rank)
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
      ucp_worker,
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
    send_complete = 0;
    ucs_status_ptr_t request = ucp_am_send_nbx (
      client_ep, UCX_DEV_HUHU, rank, sizeof(gaspi_rank_t), NULL, 0, & (ucp_request_param_t) {
        .op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_FLAGS,
        .cb = { .send = send_cb },
        .flags = UCP_AM_SEND_FLAG_REPLY | UCP_AM_SEND_FLAG_EAGER
      }
    );
    if (UCS_PTR_IS_ERR (request)) {
      fprintf (stderr, "Client: Error sending AM.\n");
      return 1;
    }
    if (request)
    {
      while (!send_complete) { ucp_worker_progress (ucp_worker); }
      ucp_request_free (request);
    }
  }

  fprintf (stderr, "Closing and destroying ep\n");
  /* To do: Ask for status and only close if not yet closed */
  {
    ucp_ep_flush (client_ep);
    ucs_status_ptr_t request = ucp_ep_close_nbx (client_ep, & (ucp_request_param_t) {0});
    if (request != NULL) {
      while (ucp_request_check_status (request) == UCS_INPROGRESS) ucp_worker_progress (ucp_worker);
    }
    ucp_request_free (request);
  }

  fprintf(stdout, "Finished\n");
  return 0;
} 

static
int
client_register_am_callback (ucp_worker_h ucp_worker)
{
  ucs_status_t status = ucp_worker_set_am_recv_handler (ucp_worker, & (ucp_am_handler_param_t) {
    .field_mask = UCP_AM_HANDLER_PARAM_FIELD_ID | UCP_AM_HANDLER_PARAM_FIELD_FLAGS |
      UCP_AM_HANDLER_PARAM_FIELD_CB,
    .id = UCX_DEV_REHU,
    .flags = UCP_AM_FLAG_WHOLE_MSG,
    .cb = am_rehu_callback
  });
  if (status != UCS_OK) {
    fprintf (stderr, "Client: Setting AM REHU callback failed: %d", status);
    return 1;
  }
  return 0;
}

static
int
run_client (ucp_worker_h worker, char const * peer_ip, uint16_t peer_port, gaspi_rank_t * rank)
{
  if (client_register_am_callback (worker)) {
    return 1;
  }

  if (client_make_request(worker, peer_ip, peer_port, rank))
  {
      return 1;
  }

  return 0;
}

static
void
abort_with_usage_message (void)
{
  fprintf(stderr, "Usage:\ntest02 s host_port rank\ntest02 c peer_ip peer_port rank\n");
  exit (1);
}

struct config {
  int is_server;
  union {
    struct {
      uint16_t host_port;
      gaspi_rank_t rank;
    } server;
    struct {
      char const * peer_ip;
      uint16_t peer_port;
      gaspi_rank_t rank;
    } client;
  } v;
};

int
main (int argc, char ** argv)
{
  struct config config = {0};

  int ret = 0;

  if (argc < 2) abort_with_usage_message ();

  if (!strcmp (argv[1], "s"))
  {
    config.is_server = 1;

    if (argc != 4) abort_with_usage_message ();

    {
      int tmp = atoi (argv[2]);
      if (tmp > 0xffff || tmp < 0) abort_with_usage_message ();
      config.v.server.host_port = (uint16_t) tmp;
    }
    {
      int tmp = atoi (argv[3]);
      if (tmp < 0 || tmp >= UCX_DEVICE_MAX_RANKS) abort_with_usage_message ();
      config.v.server.rank = (gaspi_rank_t) tmp;
    }
  }
  else if (!strcmp (argv[1], "c"))
  {
    config.is_server = 0;

    if (argc != 5) abort_with_usage_message ();

    config.v.client.peer_ip = argv[2];
    {
      int tmp = atoi (argv[3]);
      if (tmp > 0xffff || tmp < 0) abort_with_usage_message ();
      config.v.client.peer_port = (uint16_t) tmp;
    }
    {
      int tmp = atoi (argv[4]);
      if (tmp < 0 || tmp >= UCX_DEVICE_MAX_RANKS) abort_with_usage_message ();
      config.v.client.rank = (gaspi_rank_t) tmp;
    }
  }
  else
  {
    abort_with_usage_message ();
  }

  if (config.is_server)
  {
    ret = run_server (config.v.server.host_port, config.v.server.rank);
  }
  else
  {
    ucp_context_h ucp_context = 0;
    if (initialize_ucp_context (&ucp_context))
    {
      fprintf (stderr, "Initializing UCP context failed\n");
      exit (1);
    }
    ucp_worker_h ucp_worker = 0;
    if (create_worker (ucp_context, &ucp_worker))
    {
      fprintf (stderr, "Initializing UCP worker failed\n");
      exit (1);
    }
  
    ret = run_client (ucp_worker, config.v.client.peer_ip, config.v.client.peer_port, &config.v.client.rank);

    destroy_worker (ucp_worker);
    cleanup_ucp_context (ucp_context);
  }

  return ret;
}