/*
Copyright (c) Fraunhofer ITWM - Carsten Lojewski <lojewski@itwm.fhg.de>, 2013-2024

This file is part of GPI-2.

GPI-2 is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License
version 3 as published by the Free Software Foundation.

GPI-2 is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GPI-2. If not, see <http://www.gnu.org/licenses/>.
*/
#include "GASPI.h"
#include "GPI2_UCX.h"
#include "GPI2_Types.h"
#include <stdio.h>
#include <stdlib.h>

#define NOTIMPLEMENTED() \
  do {                                                                \
    fprintf(stderr, "Not implemented [%s:%i]\n", __FILE__, __LINE__); \
    exit(1);                                                          \
  } while (0);

int
pgaspi_dev_post_group_write (gaspi_context_t * const gctx,
                             void *local_addr, int length, int dst,
                             void *remote_addr,
                             unsigned char group)
{
  fprintf (stderr, "[Rank %d] Post group write called\n", gctx->rank);
  gaspi_ucx_ctx * ucx_device_ctx = (gaspi_ucx_ctx *) gctx->device->ctx;

  if (ucx_device_rdma_write (
    &ucx_device_ctx->ucx_device, local_addr, length, dst,
    gctx->groups[group].rrcd[gctx->rank].mr[0].rkey_buffer, remote_addr,
    &ucx_device_ctx->ucx_device.scqGroups, dst
  ) != UCX_DEVICE_OK)
  {
    fprintf (stderr, "ucx_device_rdma_write failed\n");
    goto err;
  };

  __atomic_add_fetch (&gctx->ne_count_grp, 1, __ATOMIC_RELAXED);
  
  return 0;

err:
  return -1;
}

/*
static
int
ucx_poll_cq (
  struct mpmc_queue * cq, uint32_t num_entries, struct ucx_wc wc[num_entries]
)
{
  int i = 0;
  while (1)
  {
    // TODO: Check for errors.
    if (i >= num_entries) break;
    struct alf_tag_payload_pair d;
    if (!alf_dequeue (cq, &d)) break;
    wc[i++] = * (struct ucx_wc *) d.payload;
    free ((void *) d.payload);
  }
  return i;
}
*/

/* TODO: number of elems to poll as arg */
int
pgaspi_dev_poll_groups (gaspi_context_t * const gctx)
{
  gaspi_ucx_ctx * ucx_dev_ctx = (gaspi_ucx_ctx *) gctx->device->ctx;
  
  int ret = ucx_poll_cq (&ucx_dev_ctx->ucx_device.scqGroups, gctx->ne_count_grp,
    ucx_dev_ctx->wc_grp_send);
  
  if (ret < 0)
  {
    gaspi_uint i;
    for (i = 0; i < gctx->ne_count_grp; i++)
    {
      /* TODO: wc_grp_send is a [64] so we're basically assuming
          that ne_count_grp will never exceed that */
      if (ucx_dev_ctx->wc_grp_send[i].status != UCX_WC_SUCCESS)
      {
        //TODO: for now here because we need to identify the erroneous rank
        // but has to go out of device
        gctx->state_vec[GASPI_COLL_QP][ucx_dev_ctx->wc_grp_send[i].wr_id] =
          GASPI_STATE_CORRUPT;
      }
  
      GASPI_DEBUG_PRINT_ERROR(
        "Failed request to %lu. Collectives queue might be broken",
        ucx_dev_ctx->wc_grp_send[i].wr_id
      );
    }

    return -1;
  }
  
  __atomic_sub_fetch (&gctx->ne_count_grp, ret, __ATOMIC_RELAXED);

  return ret;
}