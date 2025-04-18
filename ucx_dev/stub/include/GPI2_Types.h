#ifndef GPI2_TYPES_H_
#define GPI2_TYPES_H_

#include "GASPI_types.h"
#include <stddef.h>

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

#ifdef GPI_DEVICE_UCX
struct gaspi_rc_mseg_rkey {
  void * buffer; size_t buffer_size;
};
#endif

typedef struct
{
  union
  {
    unsigned char *buf;
    void *ptr;
    unsigned long addr;
  } data;

  union
  {
    unsigned char *buf;
    void *ptr;
    unsigned long addr;
  } notif_spc;

  void *mr[2];

#ifdef GPI2_DEVICE_IB
  unsigned int rkey[2];
#endif
#ifdef GPI_DEVICE_UCX
  struct gaspi_rc_mseg_rkey rkey[2];
#endif

  unsigned long size;
  size_t notif_spc_size;
  int trans;                  /* info transmitted */

  int user_provided;
  gaspi_memory_description_t desc;

} gaspi_rc_mseg_t;

#endif