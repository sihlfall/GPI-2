#include "oob.h"
#include "GPI2_UCX.h"
#include "ucp/api/ucp.h"
#include "arpa/inet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static
ucs_status_t
initialize_ucp_context (ucp_context_h * ucp_context)
{
  ucs_status_t status = ucp_init (
    & (ucp_params_t) {
      .field_mask = UCP_PARAM_FIELD_FEATURES,
      .features = UCP_FEATURE_TAG | UCP_FEATURE_STREAM
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
create_oob_server_worker (ucp_context_h ucp_context, ucp_worker_h * oob_server_worker)
{
  ucs_status_t status = ucp_worker_create (
    ucp_context,
    & (ucp_worker_params_t) {
      .field_mask = UCP_WORKER_PARAM_FIELD_THREAD_MODE,
      .thread_mode = UCS_THREAD_MODE_MULTI
    },
    oob_server_worker
  );
  return status;
}

static
void
destroy_oob_server_worker (ucp_worker_h oob_server_worker)
{
  ucp_worker_destroy (oob_server_worker);
}

static
ucs_status_t
create_ucx_ctx_default_worker (gaspi_ucx_ctx * ucx_ctx)
{
  ucp_worker_h worker;
  ucs_status_t status = ucp_worker_create (
    ucx_ctx->wpool->ucp_ctx,
    & (ucp_worker_params_t) {
      .field_mask = UCP_WORKER_PARAM_FIELD_THREAD_MODE,
      .thread_mode = UCS_THREAD_MODE_SINGLE
    },
    &worker
  );
  if (status != UCS_OK) return status;

  ucx_ctx->wpool->default_worker = worker;
  return UCS_OK;
}

static
void
destroy_ucx_ctx_default_worker (gaspi_ucx_ctx * ucx_ctx)
{
  ucp_worker_destroy (ucx_ctx->wpool->default_worker);
}

static
int
initialize_stub_ucx_ctx (gaspi_ucx_ctx * ucx_ctx)
{
  * ucx_ctx = (gaspi_ucx_ctx) {
    .wpool = (ucx_wpool_t *) calloc (1, sizeof (ucx_wpool_t))
  };

  {
    ucp_context_h ucp_context;
    if (initialize_ucp_context(&ucp_context) != UCS_OK)
    {
      fprintf (stderr, "Initializing UCP context failed\n");
      return 1;
    }
    ucx_ctx->wpool->ucp_ctx = ucp_context;
  }

  if (create_ucx_ctx_default_worker (ucx_ctx) != UCS_OK) goto err_create_default_worker;
  return 0;

err_create_default_worker:
  cleanup_ucp_context (ucx_ctx->wpool->ucp_ctx);
  return 1;
}

static
void
cleanup_stub_ucx_ctx (gaspi_ucx_ctx * ucx_ctx)
{
  destroy_ucx_ctx_default_worker (ucx_ctx);
  cleanup_ucp_context (ucx_ctx->wpool->ucp_ctx);
}


static int server_received = 0;
static size_t server_received_chars = 0;

static void
stream_recv_cb (void *request, ucs_status_t status, size_t length, void *user_data)
{
  fprintf(stderr, "Server receive cb called\n");
  server_received = 1;
  server_received_chars = length;
}

static
void *
server_run (void * args)
{
  __attribute_maybe_unused__ int reterr = 0;
  struct ucx_dev_oob_server_thread * myself = (struct ucx_dev_oob_server_thread *) args;

  fprintf(stderr, "Server is listening\n");

  while (!myself->request_stop) {
    
    if (!myself->ep) { ucp_worker_progress(myself->ucp_worker); continue; }

    fprintf(stderr, "Endpoint recognized\n");

    {
      char msg = 0;
      size_t chars_received = 0;
      ucs_status_ptr_t request = ucp_stream_recv_nbx(
        myself->ep, &msg, 1, &chars_received,
        & (ucp_request_param_t) {
          .op_attr_mask = UCP_OP_ATTR_FIELD_FLAGS | UCP_OP_ATTR_FIELD_CALLBACK,
          .flags = UCP_STREAM_RECV_FLAG_WAITALL,
          .cb = { .recv_stream = stream_recv_cb }
        }
      );

      if (request != NULL)
      {
        while (!server_received) ucp_worker_progress (myself->ucp_worker);
        chars_received = server_received_chars;
      }

      fprintf(stderr, "Server received %lu characters: %d\n", chars_received, msg);
      fprintf(stderr, "Freeing request\n");

      if (request) ucp_request_free (request);
    }
    {
      {
        ucs_status_ptr_t request = 0;
        {
          char cmd = 'A';
          request = ucp_stream_send_nbx (myself->ep, &cmd, 1, & (ucp_request_param_t) {0});
        }
        ucp_ep_flush (myself->ep);
    
  //     while (!send_complete) { ucp_worker_progress (connection_args->worker); }
        fprintf(stdout, "Client send complete\n");
    
        ucp_request_free (request);
      }  
    }


    while (!myself->request_stop) {
      ucp_worker_progress(myself->ucp_worker);
    }
  }

  pthread_exit (NULL);
}


static
int
run_server (ucp_context_h ucp_context, ucp_worker_h ucp_data_worker, uint16_t host_port)
{
  /* create worker */
  ucp_worker_h ucp_server_worker;
  if (create_oob_server_worker (ucp_context, &ucp_server_worker) != UCS_OK)
  {
    fprintf (stderr, "Creating UCP worker failed\n");
    return 1;
  }

  struct ucx_dev_oob_server_thread server_thread;

  if (ucx_dev_oob_server_initialize (&server_thread, ucp_server_worker, host_port))
  {
      printf ("Could not start server.\n");
      return 1;
  }


  if (pthread_create (&server_thread.server_tid, NULL, server_run, &server_thread)) {
    perror ("Failed to create server thread");
    return 1;
  }


  printf("Server is running in a separate thread. Press any key to stop.\n");

  /* Wait for keypress */
  getc (stdin);

  printf ("Stopping server...\n");
  ucx_dev_oob_server_destroy (&server_thread);
  printf ("Server stopped.\n");

  destroy_oob_server_worker (ucp_server_worker);

  return 0;
}




/*
 * Client
 */

static int send_complete = 0;
static int recv_complete = 0;

static void send_cb (void *request, ucs_status_t status, void *user_data)
{
  send_complete = 1;
}

static void client_recv_ack_cb (void *request, ucs_status_t status, size_t length, void *user_data)
{
  fprintf(stdout, "Client recv handler called\n");
  recv_complete = 1;
}

static void
err_cb(void *arg, ucp_ep_h ep, ucs_status_t status)
{
    printf("error handling callback was invoked with status %d (%s)\n",
           status, ucs_status_string(status));
    //connection_closed = 1;
}

static int
client_make_request(
  ucp_worker_h ucp_worker,
  char const * hostip4, uint16_t port
)
{
  fprintf(stderr, "Creating endpoint\n");

  ucp_ep_h client_ep;
  {
    struct sockaddr_in serv_addr = {
      .sin_family = AF_INET,
      .sin_port = htons (8090)
    };
    if (inet_pton(AF_INET, hostip4, &serv_addr.sin_addr) < 0) {
      fprintf(stderr, "Invalid address/Address not supported");
      return 1;
    };
  
    ucs_status_t status = ucp_ep_create(
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
    ucs_status_ptr_t request = 0;
    {
      char cmd = 'x';
      request = ucp_stream_send_nbx (client_ep, &cmd, 1, & (ucp_request_param_t) {
        .op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK,
        .cb.send = send_cb
      });
    }
    //ucp_ep_flush (client_ep);
    fprintf(stderr, "Send initiated, yet not complete\n");


    while (!send_complete) { ucp_worker_progress (ucp_worker); }
    fprintf(stdout, "Client send complete\n");

    ucp_request_free (request);
  }

  {
    char msg = 0;
    size_t chars_received = 0;
    ucs_status_ptr_t request = ucp_stream_recv_nbx(
      client_ep, &msg, 1, &chars_received,
      & (ucp_request_param_t) {
        .op_attr_mask = UCP_OP_ATTR_FIELD_FLAGS | UCP_OP_ATTR_FIELD_CALLBACK,
        .flags = UCP_STREAM_RECV_FLAG_WAITALL,
        .cb = { .recv_stream = client_recv_ack_cb }
      }
    );

    while (!recv_complete) { ucp_worker_progress (ucp_worker); }
    fprintf(stdout, "Client received %lu characters: %d\n", chars_received, msg);

    ucp_request_free (request);
  }

  fprintf(stderr, "Closing and destroying ep\n");
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
run_client (gaspi_ucx_ctx * ucx_ctx, char const * peer_ip, uint16_t peer_port)
{
  if (client_make_request(ucx_ctx->wpool->default_worker, peer_ip, peer_port))
  {
      return 1;
  }

  return 0;
}

static
void
abort_with_usage_message (void)
{
  fprintf(stderr, "Usage:\ntest02 s host_port\ntest02 c peer_ip peer_port\n");
  exit (1);
}

struct config {
  int is_server;
  union {
    struct {
      uint16_t host_port;
    } server;
    struct {
      char const * peer_ip;
      uint16_t peer_port;
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

    if (argc != 3) abort_with_usage_message ();

    int tmp = atoi (argv[2]);
    if (tmp > 0xffff || tmp < 0) abort_with_usage_message ();
    config.v.server.host_port = (uint16_t) tmp;
  }
  else if (!strcmp (argv[1], "c"))
  {
    config.is_server = 0;

    if (argc != 4) abort_with_usage_message ();

    config.v.client.peer_ip = argv[2];

    int tmp = atoi (argv[3]);
    if (tmp > 0xffff || tmp < 0) abort_with_usage_message ();
    config.v.client.peer_port = (uint16_t) tmp;
  }
  else
  {
    abort_with_usage_message ();
  }

  gaspi_ucx_ctx ucx_ctx;
  if (initialize_stub_ucx_ctx (&ucx_ctx))
  {
    fprintf (stderr, "Initializing GASPI UCX context failed\n");
    exit (1);
  }

  if (config.is_server)
  {
    ret = run_server (ucx_ctx.wpool->ucp_ctx, ucx_ctx.wpool->default_worker, config.v.server.host_port);
  }
  else
  {
    ret = run_client (&ucx_ctx, config.v.client.peer_ip, config.v.client.peer_port);
  }

  cleanup_stub_ucx_ctx (&ucx_ctx);

  return ret;
}