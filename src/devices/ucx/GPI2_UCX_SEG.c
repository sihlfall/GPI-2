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

#include "GPI2_UCX.h"

#include "GASPI.h"
#include "GPI2_Types.h"
//#include "GPI2_SN.h"

#include "ucp/api/ucp.h"

#include <stdlib.h>
#include <stdio.h>


#define NOTIMPLEMENTED() \
  do {                                                                \
    fprintf(stderr, "Not implemented [%s:%i]\n", __FILE__, __LINE__); \
    exit(1);                                                          \
  } while (0);

int
pgaspi_dev_register_mem (gaspi_context_t const *const gctx,
                         gaspi_rc_mseg_t * seg)
{
  gaspi_ucx_ctx * ucx_device_ctx = (gaspi_ucx_ctx *) gctx->device->ctx;
  struct ucx_device * ucx_device = &ucx_device_ctx->ucx_device;

  seg->mr[0] = (struct gaspi_rc_mseg_mr) {0};
  seg->mr[1] = (struct gaspi_rc_mseg_mr) {0};

  fprintf (stderr, "address: %p, length: %lu\n", seg->data.buf, seg->size);
  ucp_mem_h data_memh;
  {
    if (ucp_mem_map (
      ucx_device_ctx->ucp_ctx,
      & (ucp_mem_map_params_t) {
        .field_mask = UCP_MEM_MAP_PARAM_FIELD_ADDRESS |
        UCP_MEM_MAP_PARAM_FIELD_LENGTH | UCP_MEM_MAP_PARAM_FIELD_PROT |
        UCP_MEM_MAP_PARAM_FIELD_MEMORY_TYPE,
        .address = seg->data.buf,
        .length = seg->size,
        .prot = UCP_MEM_MAP_PROT_LOCAL_READ | UCP_MEM_MAP_PROT_LOCAL_WRITE |
          UCP_MEM_MAP_PROT_REMOTE_READ | UCP_MEM_MAP_PROT_REMOTE_WRITE,
        .memory_type = UCS_MEMORY_TYPE_HOST
      },
      &data_memh
    ) != UCS_OK)
    {
      fprintf (stderr, "Could not register memory\n");
      goto err_mem_map_data;
    }
  }

  void * data_rkey_buffer; size_t data_rkey_buffer_size;
  {
    if (ucp_rkey_pack (
      ucx_device_ctx->ucp_ctx, data_memh, &data_rkey_buffer, &data_rkey_buffer_size
    ) != UCS_OK)
    {
      fprintf (stderr, "Could not obtain remote handle\n");
      goto err_memh_pack_memh;
    }
  }

  if (!seg->notif_spc.buf) goto out;

  ucp_mem_h notif_spc_memh;
  {
    if (ucp_mem_map (
      ucx_device_ctx->ucp_ctx,
      & (ucp_mem_map_params_t) {
        .field_mask = UCP_MEM_MAP_PARAM_FIELD_ADDRESS |
          UCP_MEM_MAP_PARAM_FIELD_LENGTH | UCP_MEM_MAP_PARAM_FIELD_PROT |
          UCP_MEM_MAP_PARAM_FIELD_MEMORY_TYPE,
        .address = seg->notif_spc.buf,
        .length = seg->notif_spc_size,
        .prot = UCP_MEM_MAP_PROT_LOCAL_READ | UCP_MEM_MAP_PROT_LOCAL_WRITE |
          UCP_MEM_MAP_PROT_REMOTE_READ | UCP_MEM_MAP_PROT_REMOTE_WRITE,
        .memory_type = UCS_MEMORY_TYPE_HOST
      },
      &notif_spc_memh
    ) != UCS_OK)
    {
      fprintf (stderr, "Could not register memory\n");
      goto err_mem_map_notif_spc_memh;
    }
  }

  void * notif_spc_rkey_buffer; size_t notif_spc_rkey_buffer_size;
  {
    if (ucp_rkey_pack (
      ucx_device_ctx->ucp_ctx, notif_spc_memh, &notif_spc_rkey_buffer,
      &notif_spc_rkey_buffer_size
    ) != UCS_OK)
    {
      fprintf (stderr, "Could not obtain remote handle\n");
      goto err_memh_pack_notif_spc_memh;
    }
  }
  seg->mr[1] = (struct gaspi_rc_mseg_mr) {
    .addr = seg->notif_spc.buf,
    .mem_h = notif_spc_memh,
    .rkey_buffer = notif_spc_rkey_buffer,
    .rkey_buffer_size = notif_spc_rkey_buffer_size
  };

out:
  seg->mr[0] = (struct gaspi_rc_mseg_mr)  {
    .addr = seg->data.buf,
    .mem_h = data_memh,
    .rkey_buffer = data_rkey_buffer,
    .rkey_buffer_size = data_rkey_buffer_size
  };

  fprintf (stderr, "[Rank %d] Registered rkey buffer: %p\n", gctx->rank, data_rkey_buffer);
  return 0;

err_memh_pack_notif_spc_memh:
  ucp_mem_unmap (ucx_device_ctx->ucp_ctx, notif_spc_memh);
err_mem_map_notif_spc_memh:
  ucp_rkey_buffer_release(data_rkey_buffer);
err_memh_pack_memh:
  ucp_mem_unmap (ucx_device_ctx->ucp_ctx, data_memh);
err_mem_map_data:
  return -1;
}

int
pgaspi_dev_unregister_mem (gaspi_context_t const *const gctx,
                           gaspi_rc_mseg_t * seg)
{
  gaspi_ucx_ctx * ucx_device_ctx = (gaspi_ucx_ctx *) gctx->device->ctx;
  struct ucx_device * ucx_device = &ucx_device_ctx->ucx_device;

  if (seg->mr[0].addr)
  {
    ucp_rkey_buffer_release (seg->mr[0].rkey_buffer);
    ucp_mem_unmap (ucx_device_ctx->ucp_ctx, seg->mr[0].mem_h);
    seg->mr[0] = (struct gaspi_rc_mseg_mr) {0};
  }
  if (seg->mr[1].addr)
  {
    ucp_rkey_buffer_release (seg->mr[1].rkey_buffer);
    ucp_mem_unmap (ucx_device_ctx->ucp_ctx, seg->mr[1].mem_h);
    seg->mr[0] = (struct gaspi_rc_mseg_mr) {0};
  }
  return 0;
}
