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
      perror ((msg));                         \
      printf ((msg));                         \
      goto errlabel;                          \
    }                                         \
  } while (0);                 


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

typedef struct
{
  int dummy;
} gpi2_common_ucx_request_t;


int
ucx_dev_init_device (struct ucx_dev_args * args, struct ucx_dev_oob_server_thread * wpool)
{
  // code taken from open mpi and modified
  int ret = 0;

  ucs_status_t status;
  ucp_config_t *config = NULL;
  ucp_params_t context_params;

  status = ucp_config_read("GPI2", NULL, &config);
  if (UCS_OK != status) {
    GASPI_DEBUG_PRINT_ERROR("ucp_config_read failed: %d", status);
    return -1;
  }

  /* initialize UCP context */
  memset(&context_params, 0, sizeof(context_params));
  context_params.field_mask = UCP_PARAM_FIELD_FEATURES | UCP_PARAM_FIELD_MT_WORKERS_SHARED
                              | UCP_PARAM_FIELD_ESTIMATED_NUM_EPS | UCP_PARAM_FIELD_REQUEST_INIT
                              | UCP_PARAM_FIELD_REQUEST_SIZE;
  context_params.features = UCP_FEATURE_RMA | UCP_FEATURE_AMO32 | UCP_FEATURE_AMO64;
  context_params.mt_workers_shared = 0; // TODO: (enable_mt ? 1 : 0);
  context_params.estimated_num_eps = args->peers_num;
  context_params.request_init = NULL; // TODO: opal_common_ucx_req_init;
  context_params.request_size = sizeof(gpi2_common_ucx_request_t);

/* # if HAVE_DECL_UCP_PARAM_FIELD_ESTIMATED_NUM_PPN
  context_params.estimated_num_ppn = opal_process_info.num_local_peers + 1;
  context_params.field_mask |= UCP_PARAM_FIELD_ESTIMATED_NUM_PPN;
  # endif */

  status = ucp_init(&context_params, config, &wpool->ucp_ctx);
  if (UCS_OK != status) {
    GASPI_DEBUG_PRINT_ERROR("ucp_init failed: %d", status);
    ucp_config_release(config);
    goto err_exit;
  }
  ucp_config_release(config);

  ucp_worker_params_t worker_params;
  ucp_worker_h worker;

  memset(&worker_params, 0, sizeof(worker_params));
  worker_params.field_mask = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
  worker_params.thread_mode = UCS_THREAD_MODE_SINGLE;
  status = ucp_worker_create(wpool->ucp_ctx, &worker_params, &worker);
  if (UCS_OK != status) {
    GASPI_DEBUG_PRINT_ERROR("ucp_worker_create failed: %d", status);
    goto err_create_worker;
  }
  wpool->default_worker = worker;

  return ret;

err_create_worker:
  ucp_cleanup(wpool->ucp_ctx);

err_exit:
  return -1;
}

void
ucx_dev_stop_device(struct ucx_dev_oob_server_thread * wpool)
{
  ucp_worker_destroy(wpool->default_worker);
  ucp_cleanup(wpool->ucp_ctx);
}
