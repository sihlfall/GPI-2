#include "oob.h"

#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK_GOTO_ERROR(cond, msg, errlabel) \
  do {                                        \
    if (!(cond)) {                            \
      reterr = 1;                             \
      perror ((msg));                         \
      printf ((msg));                         \
      goto errlabel;                          \
    }                                         \
  } while (0);                 

static inline
void
to_big_endian_64 (unsigned char * buffer, size_t x) {
  unsigned char * p = &buffer[7];
  * (p--) = x & 0xff; x >>= 8;
  * (p--) = x & 0xff; x >>= 8;
  * (p--) = x & 0xff; x >>= 8;
  * (p--) = x & 0xff; x >>= 8;
  * (p--) = x & 0xff; x >>= 8;
  * (p--) = x & 0xff; x >>= 8;
  * (p--) = x & 0xff; x >>= 8;
  * p = x & 0xff;
}

static inline
size_t
from_big_endian_64 (unsigned char const * buffer) {
  unsigned char const * p = buffer;
  size_t x;
  x = * (p++); x <<= 8;
  x |= * (p++); x <<= 8;
  x |= * (p++); x <<= 8;
  x |= * (p++); x <<= 8;
  x |= * (p++); x <<= 8;
  x |= * (p++); x <<= 8;
  x |= * (p++); x <<= 8;
  x |= * p;
  return x;
}

static inline void print_hex(unsigned char * s) {
  fprintf(stderr, "\n");
  char hexdigits [] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
  for (int i = 0; i < 9; ++i) {
  fprintf(stderr, "%d %d| ", (s[i] >> 4) & 0xf, s[i] & 0xf);
  }
  fprintf(stderr, "\n");
  for (int i = 0; i < 9; ++i) {
  fprintf(stderr, "%c%c| ", hexdigits[(s[i] >> 4) & 0xf], hexdigits[s[i] & 0xf]);
  }
  fprintf(stderr, "\n");
}

/**
 * Error handling callback.
 */
static void
err_cb(void *arg, ucp_ep_h ep, ucs_status_t status)
{
    printf("error handling callback was invoked with status %d (%s)\n",
           status, ucs_status_string(status));
    //connection_closed = 1;
}


static void
handle_connection (ucp_conn_request_h conn_request, void *args)
{
  struct handle_connection_args * connection_args = (struct handle_connection_args *) args;

  fprintf(stdout, "Connection handler called\n");

  ucp_ep_h server_ep;
  {
    ucs_status_t status = ucp_ep_create(
      connection_args->worker,
      & (ucp_ep_params_t) {
        .field_mask = UCP_EP_PARAM_FIELD_ERR_HANDLER |
          UCP_EP_PARAM_FIELD_CONN_REQUEST,
        .conn_request = conn_request,
        .err_handler = {
          .cb = err_cb,
          .arg = NULL
        }
      },
      &server_ep
    );
    if (status != UCS_OK) {
      fprintf(stderr, "failed to create an endpoint on the server: (%s)\n",
              ucs_status_string(status));
      return;
    }
  }
  connection_args->server_thread->ep = server_ep;
  fprintf(stderr, "Server endpoint created\n");
}

int
ucx_dev_oob_server_initialize (
  struct ucx_dev_oob_server_thread * server_thread,
  ucp_worker_h ucp_server_worker,
  uint16_t port
)
{
  memset (server_thread, 0, sizeof (struct ucx_dev_oob_server_thread));
  server_thread->port = port;
  server_thread->ucp_worker = ucp_server_worker;

  server_thread->handle_connection_args = (struct handle_connection_args) {
    .worker = server_thread->ucp_worker,
    .server_thread = server_thread
  };

  ucp_listener_h ucp_listener;
  {
    ucs_status_t status = ucp_listener_create (
      server_thread->ucp_worker,
      & (ucp_listener_params_t) {
        .field_mask = UCP_LISTENER_PARAM_FIELD_SOCK_ADDR |
          UCP_LISTENER_PARAM_FIELD_CONN_HANDLER,
        .sockaddr = (ucs_sock_addr_t) {
          .addr = (struct sockaddr *) & (struct sockaddr_in) {
            .sin_family = AF_INET,
            .sin_addr.s_addr = INADDR_ANY,
            .sin_port = htons (8090) // TODO: port !!!!!                    
          },
          .addrlen = sizeof (struct sockaddr_in)
        },
        .conn_handler = (ucp_listener_conn_handler_t) {
          .cb = handle_connection,
          .arg = &server_thread->handle_connection_args
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

  server_thread->ucp_listener = ucp_listener;

  return 0;
}

void
ucx_dev_oob_server_destroy (struct ucx_dev_oob_server_thread * server_thread)
{
  server_thread->request_stop = 1;

  if (server_thread->ep) ucp_ep_close_nb (server_thread->ep, UCP_EP_CLOSE_MODE_FLUSH);

  if (server_thread->ucp_listener) ucp_listener_destroy (server_thread->ucp_listener);
}

