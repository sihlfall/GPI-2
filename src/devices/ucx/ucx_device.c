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
  ucx_device_t * ucx_device = (ucx_device_t *) args;

  fprintf(stdout, "Connection handler called\n");

  ucp_ep_h server_ep;
  {
    ucs_status_t status = ucp_ep_create(
      ucx_device->ucp_worker,
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
  ucx_device->ep = server_ep;
  fprintf(stderr, "Server endpoint created\n");
}

int
ucx_dev_create_listener (
  ucx_device_t * ucx_device, uint16_t port
)
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
          .cb = handle_connection,
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
  /* TODO: remove from here! */
  ucp_device->request_stop = 1;
  if (ucp_device->ep) ucp_ep_close_nb (ucp_device->ep, UCP_EP_CLOSE_MODE_FLUSH);

  if (ucp_device->ucp_listener) ucp_listener_destroy (ucp_device->ucp_listener);
  ucp_device->ucp_listener = 0;
}


int
ucx_dev_init_device (struct ucx_dev_args * args, ucx_device_t * ucx_device)
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
        .features = UCP_FEATURE_TAG | UCP_FEATURE_STREAM
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
  
  * ucx_device = (ucx_device_t) {
    .ucp_ctx = ucp_context,
    .ucp_worker = ucp_worker
  };

  return 0;

err_worker_create:
  ucp_cleanup(ucp_context);

err_init:
err_config_read:
  return -1;
}

void
ucx_dev_stop_device(struct ucx_device * ucx_device)
{
//  ucp_worker_destroy(wpool->default_worker);
  ucp_cleanup(ucx_device->ucp_ctx);
}
