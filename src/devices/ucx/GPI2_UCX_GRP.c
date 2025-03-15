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

#define NOTIMPLEMENTED() do { exit(1); } while (0);

int
pgaspi_dev_post_group_write (gaspi_context_t * const gctx,
                             void *local_addr, int length, int dst,
                             void *remote_addr,
                             unsigned char GASPI_UNUSED (g))
{
  NOTIMPLEMENTED()
}

/* TODO: number of elems to poll as arg */
int
pgaspi_dev_poll_groups (gaspi_context_t * const gctx)
{
  NOTIMPLEMENTED()
}
