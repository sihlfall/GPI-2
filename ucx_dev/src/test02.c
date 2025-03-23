#include "oob.h"
#include "ucp/api/ucp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int run_server(uint16_t host_port)
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

  /* initialize UCP context */
  ucp_context_h ucp_context;
  {
    ucs_status_t status = ucp_init (
      & (ucp_params_t) {
        .field_mask = UCP_PARAM_FIELD_FEATURES,
        .features = UCP_FEATURE_TAG | UCP_FEATURE_STREAM
      },
      NULL, &ucp_context
    );
    if (status != UCS_OK)
    {
        fprintf (stderr, "Initializing UCP context failed\n");
        exit (1);
    }
  }

  /* create worker */
  ucp_worker_h ucp_worker;
  {
    ucs_status_t status = ucp_worker_create (
      ucp_context,
      & (ucp_worker_params_t) {
        .field_mask = UCP_WORKER_PARAM_FIELD_THREAD_MODE,
        .thread_mode = UCS_THREAD_MODE_SINGLE
      },
      &ucp_worker
    );
    if (status != UCS_OK)
    {
      fprintf (stderr, "Creating UCP worker failed\n");
      exit (1);
    }
  }

  struct ucx_dev_oob_server_thread server_thread;

  if (ucx_dev_oob_server_initialize (&server_thread, ucp_worker, host_port, 3, &response))
  {
      printf ("Could not start server.\n");
      return 1;
  }

  printf("Server is running in a separate thread. Press any key to stop.\n");

  /* Wait for keypress */
  getc (stdin);

  printf ("Stopping server...\n");
  ucx_dev_oob_server_destroy (&server_thread);
  printf ("Server stopped.\n");

  ucp_worker_destroy (ucp_worker);
  ucp_cleanup (ucp_context);

  return 0;
}

int run_client (char const * peer_ip, uint16_t peer_port)
{
  /* initialize UCP context */
  ucp_context_h ucp_context;
  {
    ucs_status_t status = ucp_init (
      & (ucp_params_t) {
        .field_mask = UCP_PARAM_FIELD_FEATURES,
        .features = UCP_FEATURE_TAG | UCP_FEATURE_STREAM
      },
      NULL, &ucp_context
    );
    if (status != UCS_OK)
    {
        fprintf (stderr, "Initializing UCP context failed\n");
        exit (1);
    }
  }

  /* create worker */
  ucp_worker_h ucp_worker;
  {
    ucs_status_t status = ucp_worker_create (
      ucp_context,
      & (ucp_worker_params_t) {
        .field_mask = UCP_WORKER_PARAM_FIELD_THREAD_MODE,
        .thread_mode = UCS_THREAD_MODE_SINGLE
      },
      &ucp_worker
    );
    if (status != UCS_OK)
    {
      fprintf (stderr, "Creating UCP worker failed\n");
      exit (1);
    }
  }

  struct ucx_dev_oob_response response;

  if (ucx_dev_oob_client_make_request(ucp_worker, peer_ip, peer_port, &response)) {
      return 1;
  }

  char * text = calloc(response.length + 1, sizeof (char));
  memcpy (text, response.data, response.length);
  ucx_dev_oob_client_cleanup_response (&response);

  printf("Text read: %s\n", text);

  return 0;
}

static void abort_with_usage_message (void)
{
  fprintf(stderr, "Usage:\ntest02 s host_port\ntest02 c peer_ip peer_port\n");
  exit (1);
}

int main (int argc, char ** argv)
{
  int ret = 0;

  if (argc < 2) abort_with_usage_message ();

  if (!strcmp (argv[1], "s"))
  {
    if (argc != 3) abort_with_usage_message ();

    int tmp = atoi (argv[2]);
    if (tmp > 0xffff || tmp < 0) abort_with_usage_message ();
    uint16_t host_port = (uint16_t) tmp;

    ret = run_server (host_port);
  }
  else if (!strcmp (argv[1], "c"))
  {
    if (argc != 4) abort_with_usage_message ();

    char const * peer_ip = argv[2];

    int tmp = atoi (argv[3]);
    if (tmp > 0xffff || tmp < 0) return 1;
    uint16_t peer_port = (uint16_t) tmp;

    ret = run_client (peer_ip, peer_port);
  }
  else
  {
    return 1;
  }

  return ret;
}