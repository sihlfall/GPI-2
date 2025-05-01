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

#ifndef _GPI2_SN_H_
#define _GPI2_SN_H_ 1

#include "GPI2.h"
#include "GASPI.h"
#include "GPI2_Types.h"

enum gaspi_sn_ops
{
  GASPI_SN_RESET = 0,
  GASPI_SN_HEADER = 1,
  GASPI_SN_PROC_KILL = 4,
  GASPI_SN_TOPOLOGY = 12,
  GASPI_SN_CONNECT = 14,
  GASPI_SN_DISCONNECT = 16,
  GASPI_SN_GRP_CHECK = 18,
  GASPI_SN_GRP_CONNECT = 20,
  GASPI_SN_SEG_REGISTER = 22,
  GASPI_SN_QUEUE_CREATE = 23,
  GASPI_SN_PROC_PING = 24
};

enum gaspi_sn_status
{
  GASPI_SN_STATE_INIT = 0,
  GASPI_SN_STATE_OK,
  GASPI_SN_STATE_ERROR
};

extern volatile enum gaspi_sn_status gaspi_sn_status;
extern volatile gaspi_return_t gaspi_sn_err;

gaspi_return_t
gaspi_sn_broadcast_topology (gaspi_context_t * const ctx,
                             const gaspi_timeout_t timeout_ms);


#ifdef GPI2_DEVICE_UCX
int
gaspi_sn_allgather_dynamic (
  gaspi_context_t const *const gctx,
  void * src,
  void * recv, size_t size,
  gaspi_group_t group, gaspi_timeout_t timeout_ms,
  size_t (*get_dynamic_data_size) (void * rec),
  void (*pack_dynamic_data) (unsigned char * buf, void * rec),
  void (*unpack_dynamic_data) (void * rec, unsigned char * buf)
);
#endif

int
gaspi_sn_allgather (gaspi_context_t const *const gctx,
                    void const *const src,
                    void *const recv,
                    size_t size,
                    gaspi_group_t group,
                    gaspi_timeout_t timeout_ms);
gaspi_return_t
gaspi_sn_command (const enum gaspi_sn_ops op,
                  const gaspi_rank_t rank,
                  const gaspi_timeout_t timeout_ms,
                  const void *const arg);

enum gaspi_sn_status gaspi_sn_status_get (void);

void gaspi_sn_cleanup (const int sig);

void *gaspi_sn_backend (void*);

int gaspi_sn_set_non_blocking (const int sock);

int gaspi_sn_set_blocking (const int sock);

int
gaspi_sn_connect2port (const char *hn,
                       const unsigned short port,
                       const unsigned long timeout_ms);

#endif
