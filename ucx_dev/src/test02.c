#include "oob.h"
#include "GPI2_UCX.h"
#include "ucp/api/ucp.h"

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
  char * s = "Hello Urs and Guenti ";
  size_t sl = strlen(s);
  int n = 30;

  char * response_string = (char *) calloc (n * sl + 1, 1);
  {
    char * p = response_string;
    int i = 0;
    for (; i < n; ++i, p += sl) memcpy (p, s, sl);
    * p = '\0';
  }

  struct ucx_dev_oob_response const response = {
      .data = (unsigned char *)response_string,
      .length = strlen (response_string)
  };
  printf ("Response length: %ld\n", response.length);

  /* create worker */
  ucp_worker_h ucp_server_worker;
  if (create_oob_server_worker (ucp_context, &ucp_server_worker) != UCS_OK)
  {
    fprintf (stderr, "Creating UCP worker failed\n");
    return 1;
  }

  struct ucx_dev_oob_server_thread server_thread;

  if (ucx_dev_oob_server_initialize (&server_thread, ucp_server_worker, ucp_data_worker, host_port, 3, &response))
  {
      printf ("Could not start server.\n");
      return 1;
  }


  if (pthread_create (&server_thread.server_tid, NULL, server_run, &server_thread)) {
    perror ("Failed to create server thread");
    free (server_thread.response_buffer);
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

static
int
run_client (gaspi_ucx_ctx * ucx_ctx, char const * peer_ip, uint16_t peer_port)
{
  struct ucx_dev_oob_response response;

  if (ucx_dev_oob_client_make_request(ucx_ctx->wpool->default_worker, peer_ip, peer_port, &response))
  {
      return 1;
  }
  
  char * text = calloc(response.length + 1, sizeof (char));
  memcpy (text, response.data, response.length);
  ucx_dev_oob_client_cleanup_response (&response);

  printf("Text read: %s\n", text);

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