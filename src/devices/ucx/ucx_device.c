#include "ucx_device.h"

#include "ucx_qp.h"

#include "GPI2_UCX.h"
#include "ucp/api/ucp.h"

#include "GPI2.h"
#include "GPI2_Utility.h"

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

/* container_of macro that is used in the Linux kernel */
#define container_of(ptr, type, member) \
  ((type *)((char *)(ptr) - offsetof(type, member)))


struct ucx_device_msg_rdma_write_data {
  void * local_addr;
  int length;
  int dst;
  void * rkey_buffer;
  void * remote_addr;
  struct mpmc_queue * cq;
  uint64_t wr_id;
};  

struct ucx_device_msg_qp_rdma_write_data {
  struct ucx_qp * qp;
};

/* user_data associated with an endpoint;
 * since it contains the ucp_ep_h, it can be used for
 * identifying the endpoint */
struct ep_instance {
  struct ucx_device * ucx_device;
  int have_rank;
  gaspi_rank_t rank;
  ucp_ep_h ucp_ep;
};

/*
 * Function naming scheme:
 * Running on user thread:
 *   ucx_device_...: interface functions (= functions declared in .h file)
 *   no prefix: static functions
 * Running on worker thread:
 *   on_am_...: active message handlers (callbacks)
 *   cb_...: other callbacks
 *   do_...: other static functions
 */

/* 
 * ************************************************************************************
 * Endpoints
 * ************************************************************************************
 */

static
struct ep_instance *
do_ep_instance_create (struct ucx_device * ucx_device, ucp_ep_params_t ucp_ep_params)
{
  struct ep_instance * ep_instance = calloc (1, sizeof (struct ep_instance));
  ucp_ep_params.field_mask |= UCP_EP_PARAM_FIELD_USER_DATA;
  ucp_ep_params.err_handler.arg = (void *) ep_instance;
  ucp_ep_params.user_data = (void *) ep_instance;
  ucp_ep_h ep;
  {
    ucs_status_t status = ucp_ep_create (ucx_device->ucp_worker, &ucp_ep_params, &ep); 
    if (status != UCS_OK)
    {
      fprintf(
        stderr, "Failed to create an endpoint: (%s)\n", ucs_status_string(status)
      );
      goto err_ep_create;
    }
  }
  *ep_instance = (struct ep_instance) { .ucx_device = ucx_device, .ucp_ep = ep };
  return ep_instance;

err_ep_create:
  free (ep_instance);
  return NULL;
}

static
void
do_ep_instance_close (struct ep_instance * ep_instance, enum ucp_ep_close_mode mode)
{
  ucs_status_ptr_t request = ucp_ep_close_nb (ep_instance->ucp_ep, mode);
  if (UCS_PTR_IS_PTR(request)) ucp_request_release (request);
  /* TODO: We should implement some kind of garbage collection. */
  /* Currently, we leak the memory of ep_instance. */
}

static
struct ucx_device_endpoints *
ep_registry_create (struct ucx_device * ucx_device, int tnc)
{
  /* We must not assume that *ucx_device has been fully initialized. */
  struct ucx_device_endpoints * reg = calloc (
    1,
    sizeof (struct ucx_device_endpoints) + tnc * sizeof (struct ucx_device_ep_entry)
  );
  if (!reg) return NULL;
  reg->ucx_device = ucx_device;
  reg->tnc = tnc;
  return reg;
}

static
void
ep_registry_destroy (struct ucx_device_endpoints * reg)
{
  /* TODO: Close all endpoints !? */
  free (reg);
}

static
void
do_unregister_ep_on_error (struct ep_instance * ep_instance)
{
  if (!ep_instance->have_rank) return;
  struct ucx_device * ucx_device = ep_instance->ucx_device;
  gaspi_rank_t rank = ep_instance->rank;
  struct ucx_device_ep_entry* endpoint_entry = &ucx_device->endpoints->ary[rank];
  if (endpoint_entry->ep_instance != ep_instance) return;
  *endpoint_entry = (struct ucx_device_ep_entry) {0};
  ep_instance->have_rank = 0;
}

static
void
cb_ep_error (void * args, ucp_ep_h ep, ucs_status_t status)
{
  struct ep_instance * ep_instance = (struct ep_instance *) args;

  switch (status)
  {
  case UCS_ERR_CONNECTION_RESET:
    fprintf (stderr, "Server: Closing endpoint ...\n");
    do_unregister_ep_on_error (ep_instance);
    do_ep_instance_close (ep_instance, UCP_EP_CLOSE_MODE_FORCE);
    fprintf (stderr, "Endpoint closed.\n");
    break;
  default:
    fprintf (
      stderr, "error handling callback was invoked with status %d (%s)\n",
      status, ucs_status_string (status)
    );
    break;
  }
}

/* 
 * ************************************************************************************
 * Connection listener
 * ************************************************************************************
 */

static
void
cb_listener_handle_connection (ucp_conn_request_h conn_request, void * arg)
{
  struct ucx_device * ucx_device = (struct ucx_device *) arg;

  fprintf (stderr, "Connection handler called\n");

  (void) do_ep_instance_create (ucx_device, (ucp_ep_params_t) {
    .field_mask = UCP_EP_PARAM_FIELD_CONN_REQUEST | UCP_EP_PARAM_FIELD_ERR_HANDLER,
    .conn_request = conn_request,
    .err_handler = { .cb = cb_ep_error }
  });
}

static
int
do_create_listener (struct ucx_device * ucx_device)
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
          .cb = cb_listener_handle_connection,
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
do_cleanup_listener (struct ucx_device * ucp_device)
{
  if (ucp_device->ucp_listener)
  {
    ucp_listener_destroy (ucp_device->ucp_listener);
    ucp_device->ucp_listener = 0;
  }
}

/* 
 * ************************************************************************************
 * Universal callback functions
 * ************************************************************************************
 */

static
void
cb_just_free_request (void * request, ucs_status_t status, void * user_data)
{
  ucp_request_free (request);
}

/* 
 * ************************************************************************************
 * Connection handshake (huhu/rehu)
 * ************************************************************************************
 */

static
void
do_send_hu (ucp_ep_h ep, enum ucx_dev_am msg_type, gaspi_rank_t * our_rank)
{
  enum ucp_send_am_flags flags = UCP_AM_SEND_FLAG_EAGER;
  if (msg_type == UCX_DEV_HUHU) flags |= UCP_AM_SEND_FLAG_REPLY;
  ucs_status_ptr_t request = ucp_am_send_nbx (
    ep, msg_type, our_rank, sizeof(gaspi_rank_t), NULL, 0,
    & (ucp_request_param_t) {
      .op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_FLAGS,
      .cb = { .send = cb_just_free_request },
      .flags = flags
    }
  );
  if (UCS_PTR_IS_ERR (request))
  {
    fprintf (stderr, "Client: Error sending AM (type: %d).\n", msg_type);
    return;
  }
}

static
ucs_status_t
on_am_huhu (
  void * arg, const void * header, size_t header_length, void * data, size_t length,
  const ucp_am_recv_param_t * param
)
{
  struct ucx_device * ucx_device = (struct ucx_device *) arg;
  gaspi_rank_t peer_rank = * (gaspi_rank_t *) header;
  gaspi_rank_t * our_rank = &ucx_device->rank;

  fprintf (stderr, "Received a HUHU.\n");
  fprintf (stderr, "Received header length: %llu\n", header_length);
  fprintf (stderr, "Received rank: %u\n", (unsigned int) peer_rank);

  if (!(param->recv_attr & UCP_AM_RECV_ATTR_FIELD_REPLY_EP))
  {
    fprintf (stderr, "Endpoint missing, send with UCP_AM_SEND_FLAG_REPLY");
    goto err;
  }
  if (peer_rank >= ucx_device->endpoints->tnc)
  {
    fprintf (stderr, "Invalid rank value received\n");
    /* TODO: Send error */
    goto err;
  }

  struct ucx_device_ep_entry* endpoint_entry = 
    &ucx_device->endpoints->ary[peer_rank];

  /* If there is an open connection attempt from our side and the incoming attempt
   * has priority, we cancel our own connection attempt and continue with
   * the incoming attempt. */
  if (
    endpoint_entry->status == ucx_device_endpoint_connecting && *our_rank > peer_rank
  )
  {
    struct ep_instance * ep_instance = endpoint_entry->ep_instance;
    ep_instance->have_rank = 0;
    *endpoint_entry = (struct ucx_device_ep_entry) {
      .status = ucx_device_endpoint_not_connected
    };
    do_ep_instance_close (ep_instance, UCP_EP_CLOSE_MODE_FORCE);
  }

  /* Query reply ep for associated ep_instance. */
  struct ucp_ep_attr attr = { .field_mask = UCP_EP_ATTR_FIELD_USER_DATA };
  ucs_status_t status = ucp_ep_query (param->reply_ep, &attr);
  if (status != UCS_OK || !(attr.field_mask & UCP_EP_ATTR_FIELD_USER_DATA))
  {
    fprintf (stderr, "Could not query reply ep\n");
    goto err;
  }
  struct ep_instance * reply_ep_instance = (struct ep_instance *) attr.user_data;

  /* If we already have an ep for this rank, cancel incoming connection attempt. */
  if (
    endpoint_entry->status != ucx_device_endpoint_not_connected && *our_rank != peer_rank
  )
  {
    if (reply_ep_instance != endpoint_entry->ep_instance)
    {
      do_ep_instance_close (reply_ep_instance, UCP_EP_CLOSE_MODE_FORCE);
    }
    goto out;
  }

  /* Register reply_ep as endpoint for peer_rank. */
  reply_ep_instance->rank = peer_rank;
  reply_ep_instance->have_rank = 1;
  *endpoint_entry = (struct ucx_device_ep_entry) {
    .status = ucx_device_endpoint_ok,
    .ep = reply_ep_instance->ucp_ep,
    .ep_instance = reply_ep_instance
  };

  /* Send REHU to peer for confirmation. */
  if (*our_rank == peer_rank) goto out;
  do_send_hu (reply_ep_instance->ucp_ep, UCX_DEV_REHU, our_rank);

err:
out:
  return UCS_OK;
}

static
ucs_status_t
on_am_rehu (
  void * arg, const void * header, size_t header_length, void * data, size_t length,
  const ucp_am_recv_param_t * param
)
{
  struct ucx_device * ucx_device = (struct ucx_device *) arg;
  gaspi_rank_t peer_rank = * (gaspi_rank_t *) header;

  fprintf (stderr, "[Rank %d] Received a REHU.\n", ucx_device->rank);
  fprintf (stderr, "Received rank: %u\n", (unsigned int) peer_rank);

  if (peer_rank >= ucx_device->endpoints->tnc)
  {
    fprintf (stderr, "Invalid rank value received.\n");
    /* TODO: Send error? */
    goto err;
  }
  
  struct ucx_device_ep_entry* endpoint_entry = &ucx_device->endpoints->ary[peer_rank];
  if (endpoint_entry->status != ucx_device_endpoint_connecting)
  {
    fprintf (stderr, "[Rank %d] Unexpected REHU -- ignoring, my status is %d.\n", ucx_device->rank, endpoint_entry->status);
    goto err;
  }
  endpoint_entry->status = ucx_device_endpoint_ok;

  /* TODO: Should we verify that the REHU was received on the correct endpoint? */

err:
  return UCS_OK;
}

static
void
do_connect_to (
  struct ucx_device * ucx_device, char const * hostip4, uint16_t port,
  gaspi_rank_t peer_rank
)
{
  struct ucx_device_ep_entry* endpoint_entry = &ucx_device->endpoints->ary[peer_rank];
  if (endpoint_entry->status != ucx_device_endpoint_not_connected)
  {
    fprintf (stderr, "Connection under way or established for rank %d\n", (int) peer_rank);
    goto out_existing_connection;
  }

  fprintf (stderr, "Creating endpoint\n");

  struct sockaddr_in serv_addr = {
    .sin_family = AF_INET,
    .sin_port = htons (port)
  };
  if (inet_pton (AF_INET, hostip4, &serv_addr.sin_addr) < 0)
  {
    fprintf (stderr, "Invalid address/Address not supported");
    goto err;
  };
  
  struct ep_instance * ep_instance = do_ep_instance_create (
    ucx_device,
    (ucp_ep_params_t) {
      .field_mask = UCP_EP_PARAM_FIELD_FLAGS |
        UCP_EP_PARAM_FIELD_SOCK_ADDR   |
        UCP_EP_PARAM_FIELD_ERR_HANDLER |
        UCP_EP_PARAM_FIELD_ERR_HANDLING_MODE,
      .err_mode = UCP_ERR_HANDLING_MODE_PEER,
      .err_handler = { .cb = cb_ep_error },
      .flags = UCP_EP_PARAMS_FLAGS_CLIENT_SERVER,
      .sockaddr = {
        .addr = (struct sockaddr *) &serv_addr,
        .addrlen = sizeof (struct sockaddr_in)
      }
    }
  );
  if (!ep_instance)
  {
    fprintf(stderr, "Creating client EP failed\n");
    goto err;
  }
  ep_instance->rank = peer_rank;
  ep_instance->have_rank = 1;

  *endpoint_entry = (struct ucx_device_ep_entry) {
    .status = ucx_device_endpoint_connecting,
    .ep = ep_instance->ucp_ep,
    .ep_instance = ep_instance
  };

  fprintf(stderr, "Client endpoint created\n");
  fprintf(stderr, "Sending HUHU with rank %d\n", ucx_device->rank);
  do_send_hu (ep_instance->ucp_ep, UCX_DEV_HUHU, &ucx_device->rank);

err:
out_existing_connection:
  return;
}

ucx_device_status_t
ucx_device_connect_to (
  struct ucx_device * ucx_device, char const * hostip4, uint16_t port,
  gaspi_rank_t peer_rank
)
{
  struct ucx_device_msg_connect_data * d =
    calloc (1, sizeof (struct ucx_device_msg_connect_data));
  *d = (struct ucx_device_msg_connect_data) {
    .host = hostip4,
    .port = port,
    .peer_rank = peer_rank
  };
  return alf_enqueue (&ucx_device->queue, (struct alf_tag_payload_pair) {
    .tag = UCX_DEV_MSG_CONNECT,
    .payload = (alf_payload_type) d
  }) ? UCX_DEVICE_OK : UCX_DEVICE_ERR_UNSPECIFIED;
}

/* 
 * ************************************************************************************
 * RDMA write
 * ************************************************************************************
 */

struct cb_rdma_write_complete_user_data {
  struct mpmc_queue * cq;
  uint64_t wr_id;
};

static
void
enqueue_send_completion (struct mpmc_queue * cq, uint64_t wr_id)
{
  struct ucx_wc * wc = malloc (sizeof (struct ucx_wc));
  *wc = (struct ucx_wc) {
    .status = UCX_WC_SUCCESS, /* TODO: Might also be an error! */
    .wr_id = wr_id
  };

  (void) alf_enqueue (cq, (struct alf_tag_payload_pair) { .payload = (uintptr_t) wc });
}

static
void
cb_rdma_write_complete (void * request, ucs_status_t status, void * user_data)
{
  /* TODO: not good, rkey handle must be destroyed as well!!!! */

  if (status != UCS_OK) goto err;
  struct cb_rdma_write_complete_user_data * ud =
    (struct cb_rdma_write_complete_user_data *) user_data;

  enqueue_send_completion (ud->cq, ud->wr_id);
  free (user_data);
  ucp_request_free (request);
  return;

err:
  fprintf (stderr, "rdma write resulted in an error\n");
}

 

static
void
do_rdma_write (
  struct ucx_device * ucx_device, void * local_addr, int length, int dst,
  void * rkey_buffer, uint64_t remote_addr,
  struct mpmc_queue * cq, uint64_t wr_id
)
{
  fprintf (
    stderr, "[Rank %d] do_rdma_write called for destination %d\n",
    (int) ucx_device->rank, dst
  );

  if (dst < 0 || dst >= ucx_device->endpoints->tnc)
  {
    fprintf (stderr, "Invalid destination: %d\n", dst);
    goto err;
  }
  struct ucx_device_ep_entry * ep_entry = &ucx_device->endpoints->ary[dst];
  if (ep_entry->status != ucx_device_endpoint_ok)
  {
    fprintf (stderr, "Endpoint not connected (%d)\n", dst);
    goto err;
  }
  ucp_ep_h ep = ep_entry->ep;

  ucp_rkey_h rkey_handle;
  {
    if (ucp_ep_rkey_unpack (ep, rkey_buffer, &rkey_handle) != UCS_OK)
    {
      fprintf (stderr, "Could not unpack rkey handle\n");
      goto err;
    }
  }

  struct cb_rdma_write_complete_user_data * user_data = 
    malloc (sizeof (struct cb_rdma_write_complete_user_data));
  /* TODO: Check for NULL */
  *user_data = (struct cb_rdma_write_complete_user_data) {
    .cq = cq,
    .wr_id = dst
  };
  ucs_status_ptr_t request = ucp_put_nbx (
    ep, local_addr, length, remote_addr, rkey_handle,
    & (ucp_request_param_t) {
      .op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_USER_DATA,
      .cb = { .send = cb_rdma_write_complete },
      .user_data = user_data
    }
  );
  if (request == UCS_OK)
  {
    enqueue_send_completion (cq, wr_id);
    free (user_data);
  }
  else if (UCS_PTR_IS_ERR(request))
  {
    fprintf (stderr, "ucp_put_nbx resulted in an error\n");
    goto err_put_nbx;
  }

  ucp_rkey_destroy (rkey_handle); 
  return;

err_put_nbx:
  free (user_data);
  ucp_rkey_destroy (rkey_handle);
err:
}

ucx_device_status_t
ucx_device_rdma_write (
  struct ucx_device * ucx_device, void * local_addr, int length, int dst,
  void * rkey_buffer, void * remote_addr, struct mpmc_queue * cq, uint64_t wr_id
)
{
  struct ucx_device_msg_rdma_write_data * d =
    calloc (1, sizeof (struct ucx_device_msg_rdma_write_data));
  *d = (struct ucx_device_msg_rdma_write_data) {
    .local_addr = local_addr,
    .length = length,
    .dst = dst,
    .rkey_buffer = rkey_buffer,
    .remote_addr = remote_addr,
    .cq = cq,
    .wr_id = wr_id
  };
  fprintf (stderr, "[Rank %d] Enqueuing RDMA WRITE\n", ucx_device->rank);
  return alf_enqueue (&ucx_device->queue, (struct alf_tag_payload_pair) {
    .tag = UCX_DEV_MSG_RDMA_WRITE,
    .payload = (alf_payload_type) d
  }) ? UCX_DEVICE_OK : UCX_DEVICE_ERR_UNSPECIFIED;
}

struct cb_rdma_qp_flush_complete_user_data {
  struct ucx_device * ucx_device;
  struct ucx_qp * qp;
};

static
void
cb_reschedule_qp_rdma_write (void * request, ucs_status_t status, void * user_data)
{
  /* TODO: not good, rkey handle must be destroyed as well!!!! */

  if (status != UCS_OK) goto err;
  struct cb_rdma_qp_flush_complete_user_data * ud =
    (struct cb_rdma_qp_flush_complete_user_data *) user_data;

  ucx_device_qp_rdma_write (ud->ucx_device, ud->qp);
  free (user_data);
  ucp_request_free (request);
  return;

err:
  fprintf (stderr, "qp rdma write resulted in an error\n");
}

static
void
enqueue_wc (struct mpmc_queue * cq, struct ucx_wc * wc)
{
  struct ucx_wc * p = malloc (sizeof (struct ucx_wc));
  *p = *wc;
  (void) alf_enqueue (cq, (struct alf_tag_payload_pair) { .payload = (uintptr_t) p });
}

struct cb_enqueue_wc_and_free_request_user_data {
  struct mpmc_queue * cq;
  uint64_t wr_id;
};

static
void
cb_enqueue_wc_and_free_request (void * request, ucs_status_t status, void * user_data)
{
  struct cb_enqueue_wc_and_free_request_user_data * ud =
   (struct cb_enqueue_wc_and_free_request_user_data *) user_data;
  enqueue_wc (ud->cq, & (struct ucx_wc) {
    .status = status == UCS_OK ? UCX_WC_SUCCESS : UCX_WC_ERR,
    .wr_id = ud->wr_id
  });
  ucp_request_free (request);
  free (user_data);
}

static
void
do_qp_rdma_write (
  struct ucx_device * ucx_device, struct ucx_qp * qp
)
{
  fprintf (stderr, "do_qp_rdma_write called\n");
  struct alf_tag_payload_pair d;
  if (!alf_dequeue (&qp->sq, &d))
  {
    fprintf (stderr, "Info: Send queue empty\n");
    return;
  }
  struct qp_queue_element * el = (struct qp_queue_element *) d.payload;
  int dst = qp->dst;
  struct ucx_device_ep_entry * ep_entry = &ucx_device->endpoints->ary[dst];
  if (ep_entry->status != ucx_device_endpoint_ok)
  {
    fprintf (stderr, "Endpoint not connected (%d)\n", dst);
    goto err;
  }
  ucp_ep_h ep = ep_entry->ep;

  if (el->num_sge != 1)
  {
    fprintf (stderr, "Multiple sges not supported yet\n");
    goto err;
  }

  fprintf (stderr, "Here! Rkey buffer: %p\n", el->wr.rdma.rkey_buffer);

  ucp_rkey_h rkey_handle;
  {
    if (ucp_ep_rkey_unpack (
      ep, el->wr.rdma.rkey_buffer, &rkey_handle
    ) != UCS_OK)
    {
      fprintf (stderr, "Could not unpack rkey handle\n");
      goto err;
    }
  }

  fprintf (stderr, "And here!\n");

  struct cb_enqueue_wc_and_free_request_user_data * ud = 
    malloc (sizeof (struct cb_enqueue_wc_and_free_request_user_data));
  *ud = (struct cb_enqueue_wc_and_free_request_user_data) {
    .cq = qp->cq,
    .wr_id = el->wr_id
  };
  ucs_status_ptr_t request_put = ucp_put_nbx (
    ep, el->sg_list[0].addr, el->sg_list[0].length,
    el->wr.rdma.remote_addr, rkey_handle,
    & (ucp_request_param_t) {
      .op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_USER_DATA,
      .cb = { .send = cb_enqueue_wc_and_free_request },
      .user_data = ud
    }
  );
  if (request_put == UCS_OK)
  {
    enqueue_wc (qp->cq, & (struct ucx_wc) { .status = UCX_WC_SUCCESS, .wr_id = el->wr_id });
    free (ud);
  }
  else if (UCS_PTR_IS_ERR(request_put))
  {
    fprintf (stderr, "ucp_put_nbx (qp) resulted in an error\n");
    free (ud);
    ucp_rkey_destroy (rkey_handle);
    goto err_put_nbx;
  }
  ucp_rkey_destroy (rkey_handle);

  struct cb_rdma_qp_flush_complete_user_data * flush_user_data = calloc (1,
    sizeof (struct cb_rdma_qp_flush_complete_user_data));
  /* TODO: Check for NULL? */
  *flush_user_data = (struct cb_rdma_qp_flush_complete_user_data) {
    .ucx_device = ucx_device,
    .qp = qp
  };

  ucs_status_ptr_t request_flush = ucp_ep_flush_nbx (
    ep, &(ucp_request_param_t) {
      .op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_USER_DATA,
      .cb = { .send = cb_reschedule_qp_rdma_write },
      .user_data = flush_user_data
    }
  );
  if (request_flush == UCS_OK)
  {
    ucx_device_qp_rdma_write (ucx_device, qp);
  }
  else if (UCS_PTR_IS_ERR (request_flush))
  {
    fprintf (stderr, "ucp_ep_flush (qp) resulted in an error\n");
    goto err_flush_nbx;
  }

  return;

err_flush_nbx:
  free (flush_user_data);
err_put_nbx:
err:
}

ucx_device_status_t
ucx_device_qp_rdma_write (
  struct ucx_device * ucx_device, struct ucx_qp * qp
)
{
  struct ucx_device_msg_qp_rdma_write_data * d =
    calloc (1, sizeof (struct ucx_device_msg_qp_rdma_write_data));
  *d = (struct ucx_device_msg_qp_rdma_write_data) {
    .qp = qp
  };
  fprintf (stderr, "[Rank %d] Enqueuing QP RDMA WRITE\n", ucx_device->rank);
  return alf_enqueue (&ucx_device->queue, (struct alf_tag_payload_pair) {
    .tag = UCX_DEV_MSG_QP_RDMA_WRITE,
    .payload = (alf_payload_type) d
  }) ? UCX_DEVICE_OK : UCX_DEVICE_ERR_UNSPECIFIED;
}

/* 
 * ************************************************************************************
 * Run (worker thread main function, incl. message loop)
 * ************************************************************************************
 */

static
void *
do_run (void * args)
{
  struct ucx_device * myself = (struct ucx_device *) args;

  if (do_create_listener (myself))
  {
    fprintf (stderr, "Creating listener failed\n");
    goto err;
  }
  int count = 0;
  while (!myself->should_stop)
  {
    struct alf_tag_payload_pair msg;
    if (alf_peek (&myself->queue, &msg)) {
      //fprintf (stderr, "[Rank %d]: Processing %d\n", myself->rank, msg.tag);
      switch (msg.tag)
      {
      case UCX_DEV_MSG_CONNECT:
        {
          if (!alf_dequeue (&myself->queue, &msg) || msg.tag != UCX_DEV_MSG_CONNECT) {
            fprintf (stderr, "Inconsistent queue, exiting\n");
            goto err_inconsistent;
          }
          struct ucx_device_msg_connect_data * p =
            (struct ucx_device_msg_connect_data *) msg.payload;
          do_connect_to (myself, p->host, p->port, p->peer_rank);
          free (p); /* TO DO: This is pretty bad. */
        }
        break;
      case UCX_DEV_MSG_RDMA_WRITE:
        {
          struct ucx_device_msg_rdma_write_data * p =
            (struct ucx_device_msg_rdma_write_data *) msg.payload;
          int dst = p->dst;
          if (dst < 0 || dst >= myself->endpoints->tnc)
          {
            fprintf (stderr, "Invalid destination: %d\n", dst);
            break; /* To do: dequeue and free payload */
          }
          struct ucx_device_ep_entry * ep_entry = &myself->endpoints->ary[dst];
          if (ep_entry->status != ucx_device_endpoint_ok) {
            if (count % 100 == 0)
              fprintf (stderr, "[Rank %d] Endpoint %d not ok, status %d\n", myself->rank, dst, ep_entry->status);
            ++count;
            goto progress;
          }
          if (!alf_dequeue (&myself->queue, &msg) || msg.tag != UCX_DEV_MSG_RDMA_WRITE) {
            fprintf (stderr, "Inconsistent queue, exiting\n");
            goto err_inconsistent;
          }
          
          fprintf (stderr, "Calling RDMA write\n");
          do_rdma_write (myself, p->local_addr, p->length, p->dst,
            p->rkey_buffer, (uintptr_t) p->remote_addr, p->cq, p->wr_id);
          free (p); /* TO DO: This is pretty bad. */
        }
        break;
      case UCX_DEV_MSG_QP_RDMA_WRITE:
        {
          struct ucx_device_msg_qp_rdma_write_data * p =
            (struct ucx_device_msg_qp_rdma_write_data *) msg.payload;
          if (!alf_dequeue (&myself->queue, &msg) || msg.tag != UCX_DEV_MSG_QP_RDMA_WRITE) {
            fprintf (stderr, "Inconsistent queue, exiting\n");
            goto err_inconsistent;
          }
          
          fprintf (stderr, "Calling QP RDMA write\n");
          do_qp_rdma_write (myself, p->qp);
          free (p); /* TO DO: This is pretty bad. */
        }
        break;
      default:
        fprintf (stderr, "Unknown message\n");
        break;
      }
    }

progress:
    ucp_worker_progress(myself->ucp_worker);
  }

err_inconsistent:
  do_cleanup_listener (myself);

err:
  /* TODO: Different return code in case of error. */
  pthread_exit (NULL);
}
 
/* 
 * ************************************************************************************
 * Start device, stop device
 * ************************************************************************************
 */

ucx_device_status_t
ucx_device_start (struct ucx_device * ucx_device)
{
  ucx_device->should_stop = 0;
  if (pthread_create (&ucx_device->server_tid, NULL, do_run, ucx_device))
  {
    perror ("Failed to create server thread");
    return 1;
  }

  return 0;
}

void
ucx_device_stop (struct ucx_device * ucx_device)
{
  ucx_device->should_stop = 1;
  (void) pthread_join (ucx_device->server_tid, NULL);
} 

/* 
 * ************************************************************************************
 * Init device, cleanup device
 * ************************************************************************************
 */

ucx_device_status_t
ucx_device_init (struct ucx_device * ucx_device, gaspi_rank_t rank, uint16_t host_port)
{
  ucp_config_t * config = NULL;
  {
    ucs_status_t status = ucp_config_read("GPI2", NULL, &config);
    if (status != UCS_OK)
    {
      GASPI_DEBUG_PRINT_ERROR("ucp_config_read failed: %d", status);
      goto err_config_read;
    }
  }

  ucp_context_h ucp_context = 0;
  {
    ucs_status_t status = ucp_init (
      & (ucp_params_t) {
        .field_mask = UCP_PARAM_FIELD_FEATURES,
        .features = UCP_FEATURE_AM | UCP_FEATURE_RMA | UCP_FEATURE_AMO64
      },
      config,
      &ucp_context
    );
    if (status != UCS_OK)
    {
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
    if (status != UCS_OK)
    {
      GASPI_DEBUG_PRINT_ERROR("ucp_worker_create failed: %d", status);
      goto err_worker_create;
    }
  }
  
  {
    ucs_status_t status = ucp_worker_set_am_recv_handler (ucp_worker,
      & (ucp_am_handler_param_t) {
        .field_mask = UCP_AM_HANDLER_PARAM_FIELD_ID |
          UCP_AM_HANDLER_PARAM_FIELD_FLAGS |
          UCP_AM_HANDLER_PARAM_FIELD_CB | UCP_AM_HANDLER_PARAM_FIELD_ARG,
        .id = UCX_DEV_HUHU,
        .flags = UCP_AM_FLAG_WHOLE_MSG,
        .cb = on_am_huhu,
        .arg = (void *) ucx_device
      }
    );
    if (status != UCS_OK)
    {
      GASPI_DEBUG_PRINT_ERROR("setting AM HUHU callback failed: %d", status);
      goto err_set_am_recv_handler;
    }
  }
  {
    ucs_status_t status = ucp_worker_set_am_recv_handler (ucp_worker,
      & (ucp_am_handler_param_t) {
        .field_mask = UCP_AM_HANDLER_PARAM_FIELD_ID |
          UCP_AM_HANDLER_PARAM_FIELD_FLAGS |
          UCP_AM_HANDLER_PARAM_FIELD_CB | UCP_AM_HANDLER_PARAM_FIELD_ARG,
        .id = UCX_DEV_REHU,
        .flags = UCP_AM_FLAG_WHOLE_MSG,
        .cb = on_am_rehu,
        .arg = (void *) ucx_device
      }
    );
    if (status != UCS_OK)
    {
      GASPI_DEBUG_PRINT_ERROR("setting AM REHU callback failed: %d", status);
      goto err_set_am_recv_handler;
    }
  }

  struct ucx_device_endpoints * ep_registry;
  {
    /* To do: Change MAX_ENDPOINTS to tnc! */
    ep_registry = ep_registry_create (ucx_device, UCX_DEVICE_MAX_ENDPOINTS);
    if (!ep_registry)
    {
      GASPI_DEBUG_PRINT_ERROR("could not allocate memory for ep registry");
      goto err_ep_registry_create;
    }
  }

  *ucx_device = (struct ucx_device) {
    .rank = rank,
    .ucp_ctx = ucp_context,
    .ucp_worker = ucp_worker,
    .host_port = host_port,
    .queue = (struct mpmc_queue) {0},
    .endpoints = ep_registry,
    .scqGroups = (struct mpmc_queue) {0}
  };

  return UCX_DEVICE_OK;

err_ep_registry_create:
err_set_am_recv_handler:
  ucp_worker_destroy (ucp_worker);

err_worker_create:
  ucp_cleanup (ucp_context);

err_init:
err_config_read:
  return UCX_DEVICE_ERR_UNSPECIFIED;
}

void
ucx_device_cleanup (struct ucx_device * ucx_device)
{
  ep_registry_destroy (ucx_device->endpoints);
  ucp_worker_destroy (ucx_device->ucp_worker);
  ucp_cleanup (ucx_device->ucp_ctx);
}

