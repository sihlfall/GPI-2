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

static
void *
server_run (void * args)
{
  __attribute_maybe_unused__ int reterr = 0;
  struct ucx_dev_oob_server_thread * myself = (struct ucx_dev_oob_server_thread *) args;

  myself->server_fd = socket (AF_INET, SOCK_STREAM, 0);
  CHECK_GOTO_ERROR(myself->server_fd != 0, "Socket failed", err_create_socket)

  {
    int optval = 1;
    setsockopt (myself->server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
  }
  {
    struct sockaddr_in address = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = INADDR_ANY,
      .sin_port = htons (myself->port)
    };
    CHECK_GOTO_ERROR(
      bind (myself->server_fd, (struct sockaddr *) &address, sizeof (struct sockaddr_in)) >= 0,
      "Bind failed", err_bind
    )
  }

  CHECK_GOTO_ERROR(
    listen (myself->server_fd, myself->max_connections) >= 0,
    "Listen failed", err_listen
  )

  printf("Server is listening on port %d\n", myself->port);

  while (!myself->request_stop) {
    ucp_worker_progress(myself->ucp_worker);
/*
    int new_socket = accept (myself->server_fd, NULL, NULL);
    if (new_socket < 0) {
      if (myself->request_stop) break;
      printf ("Accept failed");
      perror ("Accept failed");
      continue;
    }

    char cmd;
    int n_bytes_read = read (new_socket, &cmd, 1);
    printf ("Received: %c\n", cmd);

    if (n_bytes_read && cmd == 'a') {
      send (new_socket, myself->response_buffer, myself->response_buffer_length, 0);
    }

    close (new_socket);*/
  }

err_listen:
err_bind:
  close (myself->server_fd); myself->server_fd = 0;
  printf ("Server is shutting down...\n");
err_create_socket:
  pthread_exit (NULL);
}

static inline
void
set_response (
  struct ucx_dev_oob_server_thread * server_thread,
  struct ucx_dev_oob_response const * response
)
{
  size_t response_buffer_length = sizeof (size_t) + response->length;
  unsigned char * response_buffer = (unsigned char *) malloc (response_buffer_length);
  /* TODO: Check for error? */

  fprintf(stderr, "Response length is %lu\n", response->length);

  to_big_endian_64 (response_buffer, response->length);
  memcpy (response_buffer + 8, response->data, response->length);

  server_thread->response_buffer_length = response_buffer_length;
  server_thread->response_buffer = response_buffer;
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

static int server_received = 0;
static size_t server_received_chars = 0;

static void
stream_recv_cb (void *request, ucs_status_t status, size_t length, void *user_data)
{
  fprintf(stderr, "Server receive cb called\n");
  server_received = 1;
  server_received_chars = length;
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
  fprintf(stdout, "Server endpoint created\n");

  {
    char msg = 0;
    size_t chars_received = 0;
    ucs_status_ptr_t request = ucp_stream_recv_nbx(
      server_ep, &msg, 1, &chars_received,
      & (ucp_request_param_t) {
        .op_attr_mask = UCP_OP_ATTR_FIELD_FLAGS | UCP_OP_ATTR_FIELD_CALLBACK,
        .flags = UCP_STREAM_RECV_FLAG_WAITALL,
        .cb = { .recv_stream = stream_recv_cb }
      }
    );
    if (request != NULL)
    {
      while (!server_received) ucp_worker_progress (connection_args->worker);
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
        request = ucp_stream_send_nbx (server_ep, &cmd, 1, & (ucp_request_param_t) {0});
      }
      ucp_ep_flush (server_ep);
  
 //     while (!send_complete) { ucp_worker_progress (connection_args->worker); }
      fprintf(stdout, "Client send complete\n");
  
      ucp_request_free (request);
    }  
  }
}

int
ucx_dev_oob_server_initialize (
  struct ucx_dev_oob_server_thread * server_thread,
  ucp_worker_h ucp_server_worker, ucp_worker_h ucp_data_worker,
  uint16_t port, int max_connections,
  struct ucx_dev_oob_response const * response
)
{
  memset (server_thread, 0, sizeof (struct ucx_dev_oob_server_thread));
  set_response (server_thread, response);
  server_thread->port = port;
  server_thread->max_connections = max_connections;
  server_thread->ucp_worker = ucp_server_worker;

  server_thread->handle_connection_args = (struct handle_connection_args) {
    .worker = ucp_data_worker,
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

  if (pthread_create (&server_thread->server_tid, NULL, server_run, server_thread)) {
    perror ("Failed to create server thread");
    free (server_thread->response_buffer);
    return 1;
  }

  return 0;
}

void
ucx_dev_oob_server_destroy (struct ucx_dev_oob_server_thread * server_thread)
{
  server_thread->request_stop = 1;
  shutdown (server_thread->server_fd, SHUT_RD); /* interrupt accept () */
  pthread_join (server_thread->server_tid, NULL);

  if (server_thread->ep) ucp_ep_close_nb (server_thread->ep, UCP_EP_CLOSE_MODE_FLUSH);

  if (server_thread->ucp_listener) ucp_listener_destroy (server_thread->ucp_listener);
  free (server_thread->response_buffer);
}

static int send_complete = 0;
static int recv_complete = 1;

static void send_cb (void *request, ucs_status_t status, void *user_data)
{
  send_complete = 1;
}

static void client_recv_ack_cb (void *request, ucs_status_t status, size_t length, void *user_data)
{
  fprintf(stdout, "Client recv handler called\n");
  recv_complete = 1;
}

int
ucx_dev_oob_client_make_request(
  ucp_worker_h ucp_worker,
  char const * hostip4, uint16_t port,
  struct ucx_dev_oob_response * response
)
{
  * response = (struct ucx_dev_oob_response) {0};

  fprintf(stderr, "Creating endpoint");

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


  /* To do: Ask for status and only close if not yet closed */
  ucp_ep_close_nb (client_ep, UCP_EP_CLOSE_MODE_FORCE);
  ucp_ep_destroy (client_ep);

  fprintf(stdout, "Finished");
  return 0;

  /*
  fprintf(stderr, "Making OOB request to %s:%d", hostip4, port);

  int reterr = 0;
  response->data = NULL; response->length = 0;

  int sock = socket (AF_INET, SOCK_STREAM, 0);
  CHECK_GOTO_ERROR (sock >= 0, "Socket creation error", err_create_socket)

  struct sockaddr_in serv_addr = {
    .sin_family = AF_INET,
    .sin_port = htons (port)
  };
  CHECK_GOTO_ERROR (
    inet_pton(AF_INET, hostip4, &serv_addr.sin_addr) >= 0,
    "Invalid address/Address not supported", err_convert_address
  )

  CHECK_GOTO_ERROR (
    connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) >= 0,
    "Connection failed", err_connect
  )

  char message = 'a';
  send(sock, &message, 1, 0);
  printf("Message sent: %c\n", message);

  ssize_t n_bytes_read;

  // Read server response
  unsigned char lbuffer[8];
  n_bytes_read = read (sock, lbuffer, 8);
  CHECK_GOTO_ERROR(n_bytes_read == 8, "Invalid response", err_receiving_length)
  fprintf(stderr, "Lbuffer: "); print_hex(&lbuffer[0]);
  size_t n_bytes_to_expect = from_big_endian_64 (lbuffer);

  fprintf(stderr, "Server response (bytes to expect): %lu\n", n_bytes_to_expect);
  response->data = (unsigned char *) malloc (n_bytes_to_expect);

  n_bytes_read = read(sock, response->data, n_bytes_to_expect);
  if (n_bytes_read != n_bytes_to_expect) {
    free (response->data); response->data = NULL;
    CHECK_GOTO_ERROR(0, "Invalid response -- too few bytes received", err_inconsistent_response)
  }
  response->length = n_bytes_read;

err_inconsistent_response:
err_receiving_length:
  close(sock);

err_connect:
err_convert_address:
err_create_socket:
  return reterr;
  */
}

void
ucx_dev_oob_client_cleanup_response (struct ucx_dev_oob_response * response)
{
  if (response->data) free (response->data);
  * response = (struct ucx_dev_oob_response) {0};
}
