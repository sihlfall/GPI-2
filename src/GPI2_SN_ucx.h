#ifndef GPI2_SN_UCX_H_
#define GPI2_SN_UCX_H_

#include "GASPI_types.h"

gaspi_return_t gaspiu_sn_connect_to_rank (
  gaspi_rank_t rank, gaspi_timeout_t timeout_ms
);
gaspi_return_t gaspiu_sn_command (
  enum gaspi_sn_ops op, gaspi_rank_t rank,
  gaspi_timeout_t timeout_ms, const void * arg
);

#endif