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

#define NOTIMPLEMENTED() \
  do {                                                                \
    fprintf(stderr, "Not implemented [%s:%i]\n", __FILE__, __LINE__); \    
    exit(1);                                                          \
  } while (0);

gaspi_return_t
pgaspi_dev_atomic_fetch_add (gaspi_context_t * const gctx,
                             const gaspi_segment_id_t segment_id,
                             const gaspi_offset_t offset,
                             const gaspi_rank_t rank,
                             const gaspi_atomic_value_t val_add)
{
  NOTIMPLEMENTED()
}

gaspi_return_t
pgaspi_dev_atomic_compare_swap (gaspi_context_t * const gctx,
                                const gaspi_segment_id_t segment_id,
                                const gaspi_offset_t offset,
                                const gaspi_rank_t rank,
                                const gaspi_atomic_value_t comparator,
                                const gaspi_atomic_value_t val_new)
{
  NOTIMPLEMENTED()
}
                                