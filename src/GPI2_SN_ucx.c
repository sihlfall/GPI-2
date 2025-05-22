
#include "GASPI.h"
#include "GPI2_CommCtx.h"
#include "GPI2_Types.h"
#include "GPI2_SN.h"
#include "devices/ucx/GPI2_UCX.h"
#include "devices/ucx/ucx_device.h"
#include "devices/ucx/ucx_device_sn_backend.h"

#include <arpa/inet.h>
#include <pthread.h>

#include <stdalign.h>

// TODO: Delete when we do not have SN and SN_ucx in parallel anymore

#define TEMP_PORT_OFFSET (30)

struct gaspi_cd_header_base {
  size_t op_len;
  enum gaspi_sn_ops op;
  int rank;
};

struct gaspi_cd_header
{
  size_t op_len;
  enum gaspi_sn_ops op;

  int rank, tnc;
  int ret, seg_id;
  unsigned long addr, size, notif_addr;
#ifdef GPI2_DEVICE_UCX
  uint64_t data_rkey_buffer_size;
  uint64_t notif_rkey_buffer_size;
#endif
#ifdef GPI2_DEVICE_IB
  int rkey[2];
#endif

};

int gaspiu_init_and_start_sn (gaspi_context_t * gctx)
{
  gaspi_ucx_ctx * ucx_ctx = (gaspi_ucx_ctx *) gctx->device->ctx;
  
  uint16_t port = gctx->config->sn_port + gctx->local_rank + TEMP_PORT_OFFSET;  
  if (
    ucx_device_sn_init (
      gctx, &ucx_ctx->sn_device, ucx_ctx->ucp_ctx, gctx->tnc, port
    ) != UCX_DEVICE_SN_OK
  ) {
    fprintf (stderr, "Error initializing SN device\n");
    goto err_sn_init;
  };
  if (
    ucx_device_sn_start (&ucx_ctx->sn_device) != UCX_DEVICE_SN_OK
  ) {
    fprintf (stderr, "Error initializing SN device\n");
    goto err_sn_start;
  }
  return 0;

err_sn_start:
  ucx_device_sn_cleanup (&ucx_ctx->sn_device);
err_sn_init:
  return -1;
}

int
gaspiu_stop_and_cleanup_sn (gaspi_context_t * gctx)
{
  gaspi_ucx_ctx * ucx_ctx = (gaspi_ucx_ctx *) gctx->device->ctx;
  ucx_device_sn_stop (&ucx_ctx->sn_device);
  ucx_device_sn_cleanup (&ucx_ctx->sn_device);
  return 0;
}

gaspi_return_t
gaspiu_sn_connect_to_rank (gaspi_rank_t rank, gaspi_timeout_t timeout_ms)
{
  gaspi_context_t const *const gctx = &glb_gaspi_ctx;
  gaspi_ucx_ctx * ucx_ctx = (gaspi_ucx_ctx *) gctx->device->ctx;

  if (
    ucx_device_sn_connect_to_rank (
      &ucx_ctx->sn_device, pgaspi_gethostname (rank),
      gctx->config->sn_port + TEMP_PORT_OFFSET + gctx->poff[rank],
      rank, timeout_ms
    ) != UCX_DEVICE_SN_OK
  ) {
    fprintf (stderr, "SN: Could not connect to rank %d\n", (int) rank);
    return GASPI_ERROR;
  }
  return GASPI_SUCCESS;
}

struct gaspi_cd_header_connect {
  struct gaspi_cd_header_base general;
  unsigned char header_data [];
};

static
gaspi_return_t
gaspiu_sn_send_recv_cmd (
  gaspi_rank_t target_rank, enum gaspi_sn_ops op,
  void * send_buf, size_t send_size,
  void * recv_buf, size_t recv_size
)
{
  fprintf (stderr, "gaspiu_sn_send_recv_cmd called with op %d\n", op);

  gaspi_context_t * gctx = &glb_gaspi_ctx;

  alignas (struct gaspi_cd_header_connect)
    unsigned char header_buf [UCX_DEVICE_SN_MAX_HEADER_LENGTH];
  size_t max_header_data_length =
    UCX_DEVICE_SN_MAX_HEADER_LENGTH - sizeof (struct gaspi_cd_header_connect);
  struct gaspi_cd_header_connect * cdh = &header_buf[0];

  size_t total_header_size;
  *cdh = (struct gaspi_cd_header_connect) {
    .general = {
      .op_len = send_size,
      .op = op,
      .rank = gctx->rank
    }
  };
  if (send_size <= max_header_data_length) {
    memcpy (cdh->header_data, send_buf, send_size);
    total_header_size = sizeof (struct gaspi_cd_header_connect) + send_size;
  } else {
    fprintf (stderr, "Too much data to send\n");
    goto err_size_too_large;
  }

  gaspi_ucx_ctx * ucx_ctx = (gaspi_ucx_ctx *) gctx->device->ctx;
  if (ucx_device_sn_send_recv_cmd (
    &ucx_ctx->sn_device, target_rank, &cdh, total_header_size,
    recv_buf, recv_size
  ) != UCX_DEVICE_SN_OK) {
    fprintf (stderr, "Error send/recv\n");
    goto err_send_recv;
  };

  fprintf (stderr, "SN: Successfully sent AM\n");

  return GASPI_SUCCESS;

err_send_recv:
err_size_too_large:
  return GASPI_ERROR;
}

/* ************************************************************************************
 * CONNECT
 * ************************************************************************************
 */

static
gaspi_return_t
gaspiu_sn_send_recv_connect (
  gaspi_rank_t target_rank, gaspi_dev_exch_info_t * dev_info
)
{
  size_t rc_size = dev_info->info_size;
  if (rc_size == 0) { fprintf (stderr, "rc_size == 0\n"); return GASPI_SUCCESS; }
  return gaspiu_sn_send_recv_cmd (
    target_rank, GASPI_SN_CONNECT,
    dev_info->local_info, rc_size,
    dev_info->remote_info, rc_size
  );
}

static
void
gaspiu_sn_handle_connect (
  gaspi_context_t * gctx, struct ucx_device_sn * udsn, void * recv_param,
  struct gaspi_cd_header_connect * header
)
{
  int peer_rank = header->general.rank;
  gaspi_dev_exch_info_t * exch_info = &(gctx->ep_conn[peer_rank].exch_info);
  gaspi_timeout_t sn_config_timeout = gctx->config->sn_timeout;

  gaspi_return_t eret = pgaspi_create_endpoint_to (
    peer_rank, exch_info, sn_config_timeout
  );
  if (eret != GASPI_SUCCESS) {
    GASPI_DEBUG_PRINT_ERROR("Failed to create endpoint with %u\n", peer_rank);
    goto err;
  }

  size_t info_size = exch_info->info_size;
  if (header->general.op_len < info_size) {
    GASPI_DEBUG_PRINT_ERROR ("Failed to read with %u\n", header->general.rank);
    goto err;
  }
  memcpy (exch_info->remote_info, header->header_data, info_size);

  eret = pgaspi_connect_endpoint_to (peer_rank, sn_config_timeout);
  if (eret != GASPI_SUCCESS) {
    /* We set io_err, connection is closed and remote peer reads EOF */
    /* TODO: ????? */
    GASPI_DEBUG_PRINT_ERROR("Failed to connect endpoint with %u\n", peer_rank);
    goto err;
  }

  if (!exch_info->local_info) {
    GASPI_DEBUG_PRINT_ERROR(
      "Unexpected error: no exch information to %u\n", peer_rank
    );
    goto err;
  }

  ucx_device_sn_send_cmd_response (
    udsn, recv_param, exch_info->local_info, exch_info->info_size
  );

err:
  ;
}


/* ************************************************************************************
 * GRP_CONNECT
 * ************************************************************************************
 */

struct gaspiu_cd_header_group_connect {
  struct gaspi_cd_header_base general;
  int group;
};

struct gaspiu_mseg_exch_info {
  void * data_ptr;
  void * notif_spc_ptr;
  unsigned long size;
  size_t notif_spc_size;
  int trans;
  int user_provided;
  gaspi_memory_description_t desc;
  size_t data_rkey_buffer_size;
  size_t notif_spc_rkey_buffer_size;
  unsigned char rkeys_buffer[];
};

struct mseg_exch_info_size_pair {
  struct gaspiu_mseg_exch_info * info;
  size_t size;
};

static
gaspi_return_t
gaspiu_sn_send_recv_group_connect (
  gaspi_rank_t target_rank, gaspi_group_t group
)
{
  fprintf (stderr, "gaspiu_sn_send_recv_group_connect called\n");

  gaspi_context_t * gctx = &glb_gaspi_ctx;

  struct gaspiu_cd_header_group_connect cdh = {
    .general = {
      .op_len = sizeof (gaspi_rc_mseg_t),
      .op = GASPI_SN_GRP_CONNECT,
      .rank = gctx->rank
    },
    .group = group
  };
  size_t total_header_size = sizeof (struct gaspiu_cd_header_group_connect);
  
  enum { max_recv_size = 1024 };
  alignas (struct gaspiu_mseg_exch_info) unsigned char recv_buf [max_recv_size];
  struct gaspiu_mseg_exch_info * info = (struct gaspiu_mseg_exch_info *) &recv_buf[0];

  gaspi_ucx_ctx * ucx_ctx = (gaspi_ucx_ctx *) gctx->device->ctx;
  if (ucx_device_sn_send_recv_cmd (
    &ucx_ctx->sn_device, target_rank, &cdh, total_header_size,
    recv_buf, max_recv_size
  ) != UCX_DEVICE_SN_OK) {
    fprintf (stderr, "Error send/recv group connect\n");
    goto err_send_recv;
  };

  /* Now: handle received data */
  unsigned char * data_rkey_buffer = NULL;
  if (info->data_rkey_buffer_size) {
    fprintf(stderr, "Size: %lu\n", info->data_rkey_buffer_size);
    data_rkey_buffer = malloc (info->data_rkey_buffer_size);
    memcpy(
      data_rkey_buffer,
      &recv_buf[sizeof(struct gaspiu_mseg_exch_info)],
      info->data_rkey_buffer_size
    );
  }
  unsigned char * notif_spc_rkey_buffer = NULL;
  if (info->notif_spc_rkey_buffer_size) {
    notif_spc_rkey_buffer = malloc (info->notif_spc_rkey_buffer_size);
    memcpy(
      notif_spc_rkey_buffer,
      &recv_buf[sizeof(struct gaspiu_mseg_exch_info) + info->data_rkey_buffer_size],
      info->data_rkey_buffer_size
    );
  }
  gctx->groups[group].rrcd[target_rank] = (gaspi_rc_mseg_t) {
    .data.ptr = info->data_ptr,
    .notif_spc.ptr = info->notif_spc_ptr,
    .size = info->size,
    .notif_spc_size = info->notif_spc_size,
    .trans = info->trans,
    .user_provided = info->user_provided,
    .desc = info->desc,
    .mr = {
      {
        .rkey_buffer = data_rkey_buffer,
        .rkey_buffer_size = info->data_rkey_buffer_size
      },
      {
        .rkey_buffer = notif_spc_rkey_buffer,
        .rkey_buffer_size = info->notif_spc_rkey_buffer_size
      }
    }
  };

  return GASPI_SUCCESS;

err_send_recv:
  return GASPI_ERROR;
}

static
struct mseg_exch_info_size_pair
_gaspi_create_mseg_exch_info (gaspi_rc_mseg_t * mseg)
{
  size_t data_rkey_buffer_size = mseg->mr[0].rkey_buffer_size;
  size_t notif_spc_rkey_buffer_size = mseg->mr[1].rkey_buffer_size;

  size_t sz = sizeof (struct gaspiu_mseg_exch_info)
    + data_rkey_buffer_size + notif_spc_rkey_buffer_size;

  struct gaspiu_mseg_exch_info * info = malloc (sz);
  /* TO DO: Check for NULL? */
  *info = (struct gaspiu_mseg_exch_info) {
    .data_ptr = mseg->data.ptr,
    .notif_spc_ptr = mseg->notif_spc.ptr,
    .size = mseg->size,
    .notif_spc_size = mseg->notif_spc_size,
    .trans = mseg->trans,
    .user_provided = mseg->user_provided,
    .desc = mseg->desc,
    .data_rkey_buffer_size = data_rkey_buffer_size,
    .notif_spc_rkey_buffer_size = notif_spc_rkey_buffer_size
  };
  memcpy (&info->rkeys_buffer[0], mseg->mr[0].rkey_buffer, data_rkey_buffer_size);
  memcpy (&info->rkeys_buffer[data_rkey_buffer_size], mseg->mr[1].rkey_buffer,
    notif_spc_rkey_buffer_size);

  return (struct mseg_exch_info_size_pair) { .info = info, .size = sz };
}

static
void
gaspiu_sn_handle_group_connect (
  gaspi_context_t * gctx, struct ucx_device_sn * udsn, void * recv_param,
  struct gaspiu_cd_header_group_connect * header
)
{
  const gaspi_group_ctx_t *grp_to_connect = &(gctx->groups[header->group]);

  //TODO: to remove?
  while ((grp_to_connect->id == -1))
  {
    GASPI_DELAY();
  }

  struct mseg_exch_info_size_pair p = _gaspi_create_mseg_exch_info (
    &grp_to_connect->rrcd[gctx->rank]
  );

  ucx_device_sn_send_cmd_response (udsn, recv_param, p.info, p.size);

  free (p.info);
}

/* ************************************************************************************
 * QUEUE_CREATE
 * ************************************************************************************
 */

static
gaspi_return_t
gaspiu_sn_send_recv_queue_create (
  gaspi_rank_t target_rank, gaspi_dev_exch_info_t * dev_info
)
{
  fprintf (stderr, "gaspiu_sn_send_recv_queue_create called\n");

  size_t rc_size = dev_info->info_size;
  if (rc_size > 0) {
    return gaspiu_sn_send_recv_cmd (
      target_rank, GASPI_SN_QUEUE_CREATE, dev_info->local_info, rc_size,
      & (int) {0}, sizeof (int)
    );    
  }

  return GASPI_SUCCESS;
}

static
void
gaspiu_sn_handle_queue_create (
  gaspi_context_t * gctx, struct ucx_device_sn * udsn, void * recv_param,
  struct gaspi_cd_header_connect * header
)
{
  gaspi_dev_exch_info_t * exch_info = &(gctx->ep_conn[header->general.rank].exch_info);
  if (!exch_info->remote_info) {
    GASPI_DEBUG_PRINT_ERROR("Unexpected error: no connection to %u\n", header->general.rank);
    goto err;
  }

  /* read remote info */
  size_t info_size = exch_info->info_size;
  if (header->general.op_len < info_size) {
    GASPI_DEBUG_PRINT_ERROR ("Failed to read with %u\n", header->general.rank);
    goto err;
  }
  memcpy (exch_info->remote_info, header->header_data, info_size);

  ucx_device_sn_send_cmd_response (udsn, recv_param, & (int) {0}, sizeof (int));

err:
  ;
}

void
gaspiu_sn_handle_cmd (
  void * gctx, struct ucx_device_sn * udsn, void * recv_param, void * header
)
{
  struct gaspi_cd_header_base * general = (struct gaspi_cd_header_base *) header;
  fprintf (stderr, "Handle called with op %d\n", general->op);
  switch (general->op) {
  case GASPI_SN_CONNECT:
    gaspiu_sn_handle_connect (
      (gaspi_context_t *) gctx, udsn, recv_param,
      (struct gaspi_cd_header_connect *) header
    );
    break;
  case GASPI_SN_GRP_CONNECT:
    gaspiu_sn_handle_group_connect (
      (gaspi_context_t *) gctx, udsn, recv_param,
      (struct gaspiu_cd_header_group_connect *) header
    );
    break;
  case GASPI_SN_QUEUE_CREATE:
    gaspiu_sn_handle_queue_create (
      (gaspi_context_t *) gctx, udsn, recv_param,
      (struct gaspi_cd_header_connect *) header
    );
    break;
  default:
    break;
  }
}

gaspi_return_t
gaspiu_sn_command (
  enum gaspi_sn_ops op, gaspi_rank_t rank,
  gaspi_timeout_t timeout_ms, const void * arg
)
{
  gaspi_return_t eret = GASPI_ERROR;

  eret = gaspiu_sn_connect_to_rank (rank, timeout_ms);
  if (eret != GASPI_SUCCESS) goto err_connect;

  fprintf (stderr, "SN: Handling command %d\n", op);

  switch (op) {
  case GASPI_SN_CONNECT:
    eret = gaspiu_sn_send_recv_connect (rank, (gaspi_dev_exch_info_t *) arg);
    if (eret != GASPI_SUCCESS) goto err_command;
    break;
  case GASPI_SN_GRP_CONNECT:
    {
      gaspi_group_t group = *(gaspi_group_t *) arg;
      eret = gaspiu_sn_send_recv_group_connect (rank, group);
      if (eret != GASPI_SUCCESS) goto err_command;
    
    }
    break;
  case GASPI_SN_QUEUE_CREATE:
    eret = gaspiu_sn_send_recv_queue_create (rank, (gaspi_dev_exch_info_t *) arg);
    if (eret != GASPI_SUCCESS) goto err_command;
    break;
  default:
    eret = GASPI_ERROR; /* unhandled */
    goto err_command;
    break;
  }

  /* TODO: Close connection if sn_persistent not set. */

  return GASPI_SUCCESS;

err_command:
err_connect:
  return eret;
}