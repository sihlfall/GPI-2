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
#include <stdio.h>
#include <stdlib.h>

#define NOTIMPLEMENTED() \
  do {                                                                \
    fprintf(stderr, "Not implemented [%s:%i]\n", __FILE__, __LINE__); \
    exit(1);                                                          \
  } while (0);

/* Communication functions */
gaspi_return_t
pgaspi_dev_write (gaspi_context_t * const gctx,
                  const gaspi_segment_id_t segment_id_local,
                  const gaspi_offset_t offset_local,
                  const gaspi_rank_t rank,
                  const gaspi_segment_id_t segment_id_remote,
                  const gaspi_offset_t offset_remote,
                  const gaspi_size_t size, const gaspi_queue_id_t queue)
{
  NOTIMPLEMENTED()
}

gaspi_return_t
pgaspi_dev_read (gaspi_context_t * const gctx,
                 const gaspi_segment_id_t segment_id_local,
                 const gaspi_offset_t offset_local,
                 const gaspi_rank_t rank,
                 const gaspi_segment_id_t segment_id_remote,
                 const gaspi_offset_t offset_remote,
                 const gaspi_size_t size, const gaspi_queue_id_t queue)
{
  NOTIMPLEMENTED()
}

gaspi_return_t
pgaspi_dev_purge (gaspi_context_t * const gctx,
                  const gaspi_queue_id_t queue,
                  const gaspi_timeout_t timeout_ms)
{
  NOTIMPLEMENTED()
}

gaspi_return_t
pgaspi_dev_wait (gaspi_context_t * const gctx,
                 const gaspi_queue_id_t queue,
                 const gaspi_timeout_t timeout_ms)
{
  NOTIMPLEMENTED()
}

gaspi_return_t
pgaspi_dev_notify (gaspi_context_t * const gctx,
                   const gaspi_segment_id_t segment_id_remote,
                   const gaspi_rank_t rank,
                   const gaspi_notification_id_t notification_id,
                   const gaspi_notification_t notification_value,
                   const gaspi_queue_id_t queue)
{
  NOTIMPLEMENTED()
}

gaspi_return_t
pgaspi_dev_write_list (gaspi_context_t * const gctx,
                       const gaspi_number_t num,
                       gaspi_segment_id_t * const segment_id_local,
                       gaspi_offset_t * const offset_local,
                       const gaspi_rank_t rank,
                       gaspi_segment_id_t * const segment_id_remote,
                       gaspi_offset_t * const offset_remote,
                       gaspi_size_t * const size, const gaspi_queue_id_t queue)
{
  NOTIMPLEMENTED()
}

gaspi_return_t
pgaspi_dev_read_list (gaspi_context_t * const gctx,
                      const gaspi_number_t num,
                      gaspi_segment_id_t * const segment_id_local,
                      gaspi_offset_t * const offset_local,
                      const gaspi_rank_t rank,
                      gaspi_segment_id_t * const segment_id_remote,
                      gaspi_offset_t * const offset_remote,
                      gaspi_size_t * const size, const gaspi_queue_id_t queue)
{
  NOTIMPLEMENTED()
}

gaspi_return_t
pgaspi_dev_write_notify (
  gaspi_context_t * gctx,
  gaspi_segment_id_t segment_id_local,
  gaspi_offset_t offset_local,
  gaspi_rank_t rank,
  gaspi_segment_id_t segment_id_remote,
  gaspi_offset_t offset_remote,
  gaspi_size_t size,
  gaspi_notification_id_t notification_id,
  gaspi_notification_t notification_value,
  gaspi_queue_id_t queue
)
{
  if (gctx->ne_count_c[queue] + 2 > gctx->config->queue_size_max)
  {
    return GASPI_QUEUE_FULL;
  }

  struct ucx_send_wr swr, swrN;

  swr = (struct ucx_send_wr) {
    .wr_id = rank,
    .next = &swrN,
    .sg_list = & (struct ucx_sge) {
      .addr = (uintptr_t) (
        gctx->rrmd[segment_id_local][gctx->rank].data.addr + offset_local
      ),
      .length = size,
      .mem_h = gctx->rrmd[segment_id_local][gctx->rank].mr[0].mem_h
    },
    .num_sge = 1,
    .wr = {
      .rdma = {
        .remote_addr = gctx->rrmd[segment_id_remote][rank].data.addr + offset_remote,
        .rkey_buffer_size = gctx->rrmd[segment_id_remote][rank].mr[0].rkey_buffer_size,
        .rkey_buffer = gctx->rrmd[segment_id_remote][rank].mr[0].rkey_buffer
      }
    }
    /*
    .opcode = IBV_WR_RDMA_WRITE;
    .send_flags = IBV_SEND_SIGNALED;
    */
  };

  fprintf (stderr, "Rkey buffer ptr: %p\n", gctx->rrmd[segment_id_remote][rank].mr[0].rkey_buffer);
  gaspi_notification_t * notification_ptr = (gaspi_notification_t *) (
    gctx->nsrc.notif_spc.buf + notification_id * sizeof (gaspi_notification_t)
  );
  *notification_ptr = notification_value;

  swrN = (struct ucx_send_wr) {
    .wr_id = rank,
    .next = NULL,
    .sg_list = & (struct ucx_sge) {
      .addr = (uintptr_t) notification_ptr,
      .length = sizeof (gaspi_notification_t),
      .mem_h = gctx->nsrc.mr[1].mem_h
    },
    .num_sge = 1,
    .wr = {
      .rdma = {
        .remote_addr = gctx->rrmd[segment_id_remote][rank].notif_spc.addr +
          notification_id * sizeof (gaspi_notification_t),
        .rkey_buffer_size = gctx->rrmd[segment_id_remote][rank].mr[1].rkey_buffer_size,
        .rkey_buffer = gctx->rrmd[segment_id_remote][rank].mr[1].rkey_buffer
      }
    }
    /*
    .opcode = IBV_WR_RDMA_WRITE;
    .send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE;;
    */
  };

  gaspi_ucx_ctx * ucx_dev_ctx = (gaspi_ucx_ctx *) gctx->device->ctx;
  struct ucx_send_wr * bad_wr;

  fprintf (stderr, "Write request (rkey buffer size): %d\n", swr.wr.rdma.rkey_buffer_size);
  // TODO: function should return status code, and then we should have an if here
  ucx_qp_post_send (
    &ucx_dev_ctx->ucx_device,
    ucx_dev_ctx->qpC[queue][rank], &swr, &bad_wr);
  /*
  {
    return GASPI_ERROR;
  }
  */

  gctx->ne_count_c[queue] += 2;

  return GASPI_SUCCESS;
}

gaspi_return_t
pgaspi_dev_write_list_notify (gaspi_context_t * const gctx,
                              const gaspi_number_t num,
                              gaspi_segment_id_t * const segment_id_local,
                              gaspi_offset_t * const offset_local,
                              const gaspi_rank_t rank,
                              gaspi_segment_id_t * const segment_id_remote,
                              gaspi_offset_t * const offset_remote,
                              gaspi_size_t * const size,
                              const gaspi_segment_id_t segment_id_notification,
                              const gaspi_notification_id_t notification_id,
                              const gaspi_notification_t notification_value,
                              const gaspi_queue_id_t queue)
{
  NOTIMPLEMENTED()
}

gaspi_return_t
pgaspi_dev_read_notify (gaspi_context_t * const gctx,
                        const gaspi_segment_id_t segment_id_local,
                        const gaspi_offset_t offset_local,
                        const gaspi_rank_t rank,
                        const gaspi_segment_id_t segment_id_remote,
                        const gaspi_offset_t offset_remote,
                        const gaspi_size_t size,
                        const gaspi_notification_id_t notification_id,
                        const gaspi_queue_id_t queue)
{
  NOTIMPLEMENTED()
}

gaspi_return_t
pgaspi_dev_read_list_notify (gaspi_context_t * const gctx,
                             const gaspi_number_t num,
                             gaspi_segment_id_t * const segment_id_local,
                             gaspi_offset_t * const offset_local,
                             const gaspi_rank_t rank,
                             gaspi_segment_id_t * const segment_id_remote,
                             gaspi_offset_t * const offset_remote,
                             gaspi_size_t * const size,
                             const gaspi_segment_id_t segment_id_notification,
                             const gaspi_notification_id_t notification_id,
                             const gaspi_queue_id_t queue)
{
  NOTIMPLEMENTED()
}
