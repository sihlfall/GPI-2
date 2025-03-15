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

#define NOTIMPLEMENTED() do { exit(1); } while (0);

gaspi_return_t
pgaspi_dev_passive_send (gaspi_context_t * const gctx,
                         const gaspi_segment_id_t segment_id,
                         const gaspi_offset_t offset_local,
                         const gaspi_rank_t rank,
                         const gaspi_size_t size,
                         const gaspi_timeout_t timeout_ms)
{
  NOTIMPLEMENTED()
}

gaspi_return_t
pgaspi_dev_passive_receive (gaspi_context_t * const gctx,
                            const gaspi_segment_id_t segment_id_local,
                            const gaspi_offset_t offset_local,
                            gaspi_rank_t * const rem_rank,
                            const gaspi_size_t size,
                            const gaspi_timeout_t timeout_ms)
{
  NOTIMPLEMENTED()
}
