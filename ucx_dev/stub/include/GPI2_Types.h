#ifndef GPI2_TYPES_H_
#define GPI2_TYPES_H_

#include "GASPI_types.h"

typedef struct
{
  void *ctx;
} gaspi_device_t;

typedef struct
{
  int local_rank;
  int rank;
  int tnc;
  char *hn_poff;

  /* GASPI configuration */
  gaspi_config_t *config;

  /* Device */
  gaspi_device_t *device;

} gaspi_context_t;

typedef struct
{
  int dummy;
} gaspi_rc_mseg_t;

#endif