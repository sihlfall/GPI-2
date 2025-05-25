
#include "GASPI.h"
#include "GPI2_CommCtx.h"
#include "GPI2_SEG.h"
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


enum {
  GPI2_SN_TIMEOUT = -3,
  GPI2_SN_EMFILE = -2,
  GPI2_SN_ERROR = -1
};

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
  struct gaspi_cd_header_connect * cdh = (struct gaspi_cd_header_connect *) &header_buf[0];

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
    &ucx_ctx->sn_device, target_rank, &cdh, total_header_size, recv_buf, recv_size
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
 * TOPOLOGY
 * ************************************************************************************
 */

struct gaspi_cd_header_topology {
  struct gaspi_cd_header_base general;
  int tnc;
  unsigned char header_data [];
};

static
int
gaspi_sn_recv_topology (gaspi_context_t * gctx, gaspi_timeout_t timeout_ms)
{
  const int port_to_wait =
    gctx->config->sn_port + GASPI_MAX_PPN + gctx->local_rank;
  int nsock = _gaspi_sn_wait_connection (port_to_wait, timeout_ms);

  if (nsock < 0)
  {
    return nsock;
  }

  struct gaspi_cd_header cdh;

  memset (&cdh, 0, sizeof (struct gaspi_cd_header));

  /* Read the header */
  if (gaspi_sn_readn (nsock, &cdh, sizeof (cdh)) != sizeof (cdh))
  {
    GASPI_DEBUG_PRINT_ERROR ("Failed to read topology header.");
    close (nsock);
    return GPI2_SN_ERROR;
  }

  gctx->rank = cdh.rank;
  gctx->tnc = cdh.tnc;
  if (cdh.op != GASPI_SN_TOPOLOGY)
  {
    GASPI_DEBUG_PRINT_ERROR ("Received unexpected topology data.");
  }

  gctx->hn_poff = (char *) calloc (gctx->tnc, 65);
  if (gctx->hn_poff == NULL)
  {
    GASPI_DEBUG_PRINT_ERROR ("Failed to allocate memory.");
    close (nsock);
    return GPI2_SN_ERROR;
  }

  gctx->poff = gctx->hn_poff + gctx->tnc * 64;

  /* Read the topology */
  if (gaspi_sn_readn (nsock, gctx->hn_poff, gctx->tnc * 65) != gctx->tnc * 65)
  {
    GASPI_DEBUG_PRINT_ERROR ("Failed to read topology data.");
    close (nsock);
    return GPI2_SN_ERROR;
  }

  if (gaspi_sn_close (nsock) != 0)
  {
    GASPI_DEBUG_PRINT_ERROR ("Failed to close connection.");
    return GPI2_SN_ERROR;
  }

  return 0;
}

#define container_of(ptr, type, member) \
  ((type *)((unsigned char *)(ptr) - offsetof(type, member)))

struct send_topologies_tracker {
  unsigned char indices[8 * sizeof (unsigned int)];
  unsigned int completed_mask;
  unsigned int error_mask;
};

static
struct send_topologies_tracker
make_send_topologies_tracker (void)
{
  struct send_topologies_tracker tracker = {0};
  for (int i = 0; i < 8 * sizeof (unsigned int); ++i) tracker.indices [i] = i;
  return tracker;
};

static
void
cb_mark_send (void * user_data, _Bool was_error)
{
  unsigned char * p = (unsigned char *) user_data;
  int idx = *p;
  struct send_topologies_tracker * tracker = 
    container_of(p - idx, struct send_topologies_tracker, indices[0]);
  tracker->completed_mask |= 1u << idx;
  if (was_error) tracker->error_mask |= 1u << idx;
}

static
void
cpu_pause (int * pausecnt, int maxpause)
{
  int cnt = *pausecnt;
  for (int i = 0; i <= cnt; ++i) _mm_pause ();
  if (cnt < maxpause) *pausecnt = cnt * 2;
}

static
gaspi_return_t
gaspi_sn_send_topologies (
  gaspi_context_t * gctx, unsigned int our_rank, unsigned int start_mask,
  gaspi_timeout_t timeout_ms
)
{
  if (!start_mask) return GASPI_SUCCESS;

  fprintf (stderr, "gaspiu_sn_send_topology called\n");

  alignas (struct gaspi_cd_header_topology)
    unsigned char header_buf [UCX_DEVICE_SN_MAX_HEADER_LENGTH];
  size_t max_header_data_length =
    UCX_DEVICE_SN_MAX_HEADER_LENGTH - sizeof (struct gaspi_cd_header_topology);
  struct gaspi_cd_header_topology * cdh = (struct gaspi_cd_header_topology *) &header_buf[0];

  gaspi_context_t * gctx = &glb_gaspi_ctx;

  size_t send_size = gctx->tnc * 65;

  size_t total_header_size;
  *cdh = (struct gaspi_cd_header_topology) {
    .general = {
      .op_len = send_size,
      .op = GASPI_SN_TOPOLOGY,
      .rank = our_rank
    }
  };
  if (send_size <= max_header_data_length) {
    memcpy (cdh->header_data, gctx->hn_poff, send_size);
    total_header_size = sizeof (struct gaspi_cd_header_topology) + send_size;
  } else {
    fprintf (stderr, "Too much data to send\n");
    goto err_size_too_large;
  }  

  struct send_topologies_tracker tracker = make_send_topologies_tracker ();
  unsigned int full_mask = start_mask * 2u - 1u;

  gaspi_ucx_ctx * ucx_ctx = (gaspi_ucx_ctx *) gctx->device->ctx;

  for (
    unsigned int mask = start_mask, tracker_index = 0;
    mask;
    mask >>= 1, ++tracker_index
  ) {
    ucx_device_sn_send_nb (
      &ucx_ctx->sn_device, our_rank | mask,
      &cdh, total_header_size,
      cb_mark_send, &tracker.indices[tracker_index]
    );
  }

  {
    int pausecnt = 1, maxpause = 512;
    while (tracker.completed_mask != full_mask) cpu_pause (&pausecnt, maxpause);
  }

  if (tracker.error_mask) {
    fprintf (stderr, "Error broadcasting topology\n");
    goto err_send;
  }

  /* TODO: Timeout! */
  fprintf (stderr, "SN: Successfully broadcast topology\n");

  return GASPI_SUCCESS;

err_size_too_large:
err_send:
  return GASPI_ERROR;
}

static
unsigned int
blsmsk (unsigned int x) {
  /* compiler recognizes this pattern as blsmsk */
  return x ^ (x - 1u);
}

gaspi_return_t
gaspiu_sn_broadcast_topology (gaspi_context_t * gctx, gaspi_timeout_t timeout_ms)
{
  _Static_assert (sizeof (gctx->tnc) <= sizeof (unsigned int));
  _Static_assert (sizeof (gctx->rank) <= sizeof (unsigned int));
  unsigned int tnc = gctx->tnc;
  unsigned int rank = gctx->rank;

  if (rank) {
    int rres = gaspi_sn_recv_topology (gctx, timeout_ms);
    if (rres) return rres == GPI2_SN_TIMEOUT ? GASPI_TIMEOUT : GASPI_ERROR;
  }

  unsigned int delta = tnc - 1u - rank;
  if (!delta || rank & 1u) return GASPI_SUCCESS;

  unsigned int uint_most_significant = 1u << (8 * sizeof (unsigned int) - 1);
  unsigned int delta_most_significant = uint_most_significant >> __builtin_clz (delta);
  unsigned int start_mask = blsmsk (rank / 2u | delta_most_significant) / 2u + 1u;
  return gaspi_sn_send_topologies (gctx, rank, start_mask, timeout_ms);
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
 * PROC_KILL
 * ************************************************************************************
 */

static
gaspi_return_t
gaspiu_sn_send_proc_kill (gaspi_rank_t target_rank)
{
  fprintf (stderr, "gaspiu_sn_send_proc_kill called\n");

  gaspi_context_t * gctx = &glb_gaspi_ctx;

  struct gaspi_cd_header_connect cdh = {
    .general = {
      .op_len = 0,
      .op = GASPI_SN_PROC_KILL,
      .rank = gctx->rank
    }
  };

  gaspi_ucx_ctx * ucx_ctx = (gaspi_ucx_ctx *) gctx->device->ctx;
  if (ucx_device_sn_send_cmd (
    &ucx_ctx->sn_device, target_rank, &cdh, sizeof (struct gaspi_cd_header_connect)
  ) != UCX_DEVICE_SN_OK) {
    fprintf (stderr, "Error send\n");
    goto err_send;
  };

  fprintf (stderr, "SN: Successfully sent AM\n");

  return GASPI_SUCCESS;

err_send:
  return GASPI_ERROR;
}

static
void
gaspiu_sn_handle_proc_kill (
  gaspi_context_t * gctx, struct ucx_device_sn * udsn, void * recv_param,
  struct gaspi_cd_header_connect * header
)
{
  _exit (-1);
}

/* ************************************************************************************
 * DISCONNECT
 * ************************************************************************************
 */

static
gaspi_return_t
gaspiu_sn_send_disconnect (gaspi_rank_t target_rank)
{
  fprintf (stderr, "gaspiu_sn_send_proc_kill called\n");

  gaspi_context_t * gctx = &glb_gaspi_ctx;

  struct gaspi_cd_header_connect cdh = {
    .general = {
      .op_len = 0,
      .op = GASPI_SN_DISCONNECT,
      .rank = gctx->rank
    }
  };

  gaspi_ucx_ctx * ucx_ctx = (gaspi_ucx_ctx *) gctx->device->ctx;
  if (ucx_device_sn_send_cmd (
    &ucx_ctx->sn_device, target_rank, &cdh, sizeof (struct gaspi_cd_header_connect)
  ) != UCX_DEVICE_SN_OK) {
    fprintf (stderr, "Error send\n");
    goto err_send;
  };

  fprintf (stderr, "SN: Successfully sent AM\n");

  return GASPI_SUCCESS;

err_send:
  return GASPI_ERROR;
}

static
void
gaspiu_sn_handle_disconnect (
  gaspi_context_t * gctx, struct ucx_device_sn * udsn, void * recv_param,
  struct gaspi_cd_header_connect * header
)
{
  int peer_rank = header->general.rank;
  if (gctx->ep_conn[peer_rank].cstat != GASPI_ENDPOINT_CONNECTED) return;
  gaspi_timeout_t sn_config_timeout = gctx->config->sn_timeout;
  if (pgaspi_local_disconnect (peer_rank, sn_config_timeout) != GASPI_SUCCESS) {
    GASPI_DEBUG_PRINT_ERROR("Failed to disconnect with %u.", peer_rank);
  }
}


/* ************************************************************************************
 * GRP_CHECK
 * ************************************************************************************
 */

struct gaspiu_cd_header_group_check {
  struct gaspi_cd_header_base general;
  int tnc;
  int group;
  int ret;
};

static
gaspi_return_t
gaspiu_sn_send_recv_group_check (
  gaspi_rank_t target_rank, gaspi_timeout_t timeout_ms, gaspi_group_exch_info_t * gb
)
{
  fprintf (stderr, "gaspiu_sn_send_recv_group_check called\n");

  gaspi_context_t * gctx = &glb_gaspi_ctx;

  /* TODO: Handle timeout. */
  //struct timeb t0, t1;

  //#pragma GCC diagnostic push
  //#pragma GCC diagnostic ignored "-Wdeprecated-declarations"      
  //ftime (&t0);
  //#pragma GCC diagnostic pop

  struct gaspiu_cd_header_group_check cdh = {
    .general = {
      .op_len = sizeof (*gb), /* TODO: ?????? */
      .op = GASPI_SN_GRP_CHECK,
      .rank = gctx->rank
    },
    .group = gb->group,
    .tnc = gb->tnc,
    .ret = gb->cs
  };

  while (1) {
    gaspi_group_exch_info_t rem_gb = {0};
    gaspi_ucx_ctx * ucx_ctx = (gaspi_ucx_ctx *) gctx->device->ctx;
    if (ucx_device_sn_send_recv_cmd (
      &ucx_ctx->sn_device, target_rank, &cdh, sizeof (cdh), &rem_gb, sizeof (rem_gb)
    ) != UCX_DEVICE_SN_OK) {
      fprintf (stderr, "Error send/recv\n");
      goto err_send_recv;
    };
  
    if (rem_gb.ret >= 0 && gb->cs == rem_gb.cs) break;

    /* TODO: Check for timeout! */
    //#pragma GCC diagnostic push
    //#pragma GCC diagnostic ignored "-Wdeprecated-declarations"    
    //ftime (&t1);
    //#pragma GCC diagnostic pop
    //unsigned int delta_ms =
    //  (t1.time - t0.time) * 1000 + (t1.millitm - t0.millitm);
    //if (delta_ms > timeout_ms)
    //{
    //  return 1;
    //}

    if (gaspi_thread_sleep (250) < 0) {
      gaspi_printf ("gaspi_thread_sleep error");
    }

    //check if groups match
    /* if(gb.cs != rem_gb.cs) */
    /* { */
    /* GASPI_DEBUG_PRINT_ERROR("Mismatch with rank %d: ranks in group dont match\n", */
    /* group_to_commit>rank_grp[i]); */
    /* eret = GASPI_ERROR; */
    /* goto errL; */
    /* } */
    //usleep(250000);
    //GASPI_DELAY();
  }

  return GASPI_SUCCESS;

err_send_recv:
  return GASPI_ERROR;
}

static
void
gaspiu_sn_handle_group_check (
  gaspi_context_t * gctx, struct ucx_device_sn * udsn, void * recv_param,
  struct gaspiu_cd_header_group_check * header
)
{
  gaspi_group_exch_info_t * gb = pgaspi_group_create_exch_info (
    header->group, header->tnc
  );
  ucx_device_sn_send_cmd_response (udsn, recv_param, gb, sizeof (*gb));
  free (gb);
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
 * SEG_REGISTER
 * ************************************************************************************
 */

struct gaspi_segment_register_cd_header
{
  struct gaspi_cd_header_base general;
  int seg_id;
  unsigned long addr, size, notif_addr;
  uint64_t data_rkey_buffer_size;
  uint64_t notif_rkey_buffer_size;
  unsigned char rkeys_buffer [];
};

struct gaspi_segment_register_cd_header_size_pair {
  struct gaspi_segment_register_cd_header * h;
  size_t size;
};

static
struct gaspi_segment_register_cd_header_size_pair
create_segment_register_cd_header (gaspi_rc_mseg_t * segment, int segment_id, int rank)
{
  size_t data_rkey_buffer_size = segment->mr[0].rkey_buffer_size;
  size_t notif_rkey_buffer_size = segment->mr[1].rkey_buffer_size;
  size_t struct_size = sizeof (struct gaspi_segment_register_cd_header) +
    data_rkey_buffer_size + notif_rkey_buffer_size;
  struct gaspi_segment_register_cd_header * h = calloc (1, struct_size);
  *h = (struct gaspi_segment_register_cd_header) {
    .general = {
      .op_len = 0,
      .op = GASPI_SN_SEG_REGISTER,
      .rank = rank
    },
    .seg_id = segment_id,
    .addr = segment->data.addr,
    .notif_addr = segment->notif_spc.addr,
    .size = segment->size,
    .data_rkey_buffer_size = data_rkey_buffer_size,
    .notif_rkey_buffer_size = notif_rkey_buffer_size  
  };
  fprintf (stderr,
    "Creating seg reg cd header with rkey_buffer %p\n", segment->mr[0].rkey_buffer
  );
  memcpy (&h->rkeys_buffer[0], segment->mr[0].rkey_buffer, data_rkey_buffer_size);
  memcpy (&h->rkeys_buffer[data_rkey_buffer_size],
    segment->mr[1].rkey_buffer, notif_rkey_buffer_size
  );
  return (struct gaspi_segment_register_cd_header_size_pair) {
    .h = h, .size = struct_size
  };
}

static
gaspi_return_t
gaspiu_sn_send_recv_segment_register (
  gaspi_rank_t target_rank, gaspi_segment_id_t segment_id
)
{
  fprintf (stderr, "gaspiu_sn_send_recv_seg_register called\n");

  gaspi_context_t * gctx = &glb_gaspi_ctx;

  gaspi_rc_mseg_t * segment = &gctx->rrmd[segment_id][gctx->rank];
  struct gaspi_segment_register_cd_header_size_pair p =
    create_segment_register_cd_header (segment, segment_id, gctx->rank);
  struct gaspi_segment_register_cd_header * h = p.h;
  uint64_t hsz = p.size;

  int result = 1;

  gaspi_ucx_ctx * ucx_ctx = (gaspi_ucx_ctx *) gctx->device->ctx;
  if (ucx_device_sn_send_recv_cmd (
    &ucx_ctx->sn_device, target_rank, &h, hsz,
    &result, sizeof(result)
  ) != UCX_DEVICE_SN_OK) {
    fprintf (stderr, "Error send/recv segment header to rank %u\n", target_rank);
    goto err_send_recv;
  };
 
  /* Registration failed on the remote side */
  if (result) goto err_registration;
 
  return GASPI_SUCCESS;

err_registration:
err_send_recv:
   free (h);
   return GASPI_ERROR;
}

static
void
gaspiu_sn_handle_segment_register (
  gaspi_context_t * gctx, struct ucx_device_sn * udsn, void * recv_param,
  struct gaspi_segment_register_cd_header * header
)
{
  size_t data_rkey_buffer_size = header->data_rkey_buffer_size;
  size_t notif_rkey_buffer_size = header->notif_rkey_buffer_size;
  size_t total_size = data_rkey_buffer_size + notif_rkey_buffer_size;
  unsigned char * data_rkey_buffer = NULL, * notif_rkey_buffer = NULL;
  if (data_rkey_buffer_size) {
    data_rkey_buffer = malloc (data_rkey_buffer_size);
    fprintf (stderr, "Allocated data_rkey_buffer: %p\n", data_rkey_buffer);
    memcpy (data_rkey_buffer, &header->rkeys_buffer[0], data_rkey_buffer_size);
  }
  if (notif_rkey_buffer_size) {
    notif_rkey_buffer = malloc (notif_rkey_buffer_size);
    fprintf (stderr, "Allocated notif_rkey_buffer: %p\n", notif_rkey_buffer);
    memcpy (
      notif_rkey_buffer, &header->rkeys_buffer[data_rkey_buffer_size],
      notif_rkey_buffer_size
    );
  }

  int ack = gaspi_segment_set ((gaspi_segment_descriptor_t) {
    .rank = header->general.rank,
    .seg_id = header->seg_id,
    .addr = header->addr,
    .size = header->size,
    .notif_addr = header ->notif_addr,
    .data_rkey_buffer = data_rkey_buffer,
    .notif_rkey_buffer = notif_rkey_buffer
  });

  ucx_device_sn_send_cmd_response (udsn, recv_param, &ack, sizeof (ack));
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

/* ************************************************************************************
 * PROC_PING
 * ************************************************************************************
 */

static
gaspi_return_t
gaspiu_sn_send_recv_proc_ping (gaspi_rank_t target_rank)
{
  fprintf (stderr, "gaspiu_sn_send_proc_ping called\n");

  gaspi_context_t * gctx = &glb_gaspi_ctx;

  return gaspiu_sn_send_recv_cmd (
    target_rank, GASPI_SN_PROC_PING, & (int) {0}, 0, & (int) {0}, 0
  );
}

static
void
gaspiu_sn_handle_proc_ping (
  gaspi_context_t * gctx, struct ucx_device_sn * udsn, void * recv_param,
  struct gaspi_cd_header_connect * header
)
{
  ucx_device_sn_send_cmd_response (udsn, recv_param, & (int) {0}, sizeof (int));
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
  case GASPI_SN_PROC_KILL:
    gaspiu_sn_handle_proc_kill (
      (gaspi_context_t *) gctx, udsn, recv_param,
      (struct gaspi_cd_header_connect *) header
    );
    break;
  case GASPI_SN_DISCONNECT:
    gaspiu_sn_handle_disconnect (
      (gaspi_context_t *) gctx, udsn, recv_param,
      (struct gaspi_cd_header_connect *) header
    );
    break;
  case GASPI_SN_GRP_CHECK:
    gaspiu_sn_handle_group_check (
      (gaspi_context_t *) gctx, udsn, recv_param,
      (struct gaspiu_cd_header_group_check *) header
    );
    break;
  case GASPI_SN_GRP_CONNECT:
    gaspiu_sn_handle_group_connect (
      (gaspi_context_t *) gctx, udsn, recv_param,
      (struct gaspiu_cd_header_group_connect *) header
    );
    break;
  case GASPI_SN_SEG_REGISTER:
    gaspiu_sn_handle_segment_register (
      (gaspi_context_t *) gctx, udsn, recv_param,
      (struct gaspiu_segment_regester_cd_header *) header
    );
    break;
  case GASPI_SN_QUEUE_CREATE:
    gaspiu_sn_handle_queue_create (
      (gaspi_context_t *) gctx, udsn, recv_param,
      (struct gaspi_cd_header_connect *) header
    );
    break;
  case GASPI_SN_PROC_PING:
    gaspiu_sn_handle_proc_ping (
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
  case GASPI_SN_PROC_KILL:
    eret = gaspiu_sn_send_proc_kill (rank);
    if (eret != GASPI_SUCCESS) goto err_command;
    break;
  case GASPI_SN_DISCONNECT:
    eret = gaspiu_sn_send_disconnect (rank);
    if (eret != GASPI_SUCCESS) goto err_command;
    break;
  case GASPI_SN_GRP_CHECK:
    eret = gaspiu_sn_send_recv_group_check (
      rank, timeout_ms, (gaspi_group_exch_info_t *) arg
    );
    if (eret != GASPI_SUCCESS) goto err_command;
    break;
  case GASPI_SN_GRP_CONNECT:
    eret = gaspiu_sn_send_recv_group_connect (rank, (gaspi_group_t *) arg);
    if (eret != GASPI_SUCCESS) goto err_command;
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