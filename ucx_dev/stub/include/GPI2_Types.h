#ifndef GPI2_TYPES_H_
#define GPI2_TYPES_H_

#include "GASPI_types.h"
#include <stddef.h>


#define GASPI_MAX_QP (16)
#define GASPI_COLL_QP     (GASPI_MAX_QP)

typedef struct
{
  void *ctx;
} gaspi_device_t;

struct gaspi_rc_mseg_mr {
  void * mem_h;
  void * addr;
  void * rkey_buffer;
  size_t rkey_buffer_size;
};

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

  struct gaspi_rc_mseg_mr mr[2];

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

typedef struct
{
  gaspi_rc_mseg_t *rrcd;
} gaspi_group_ctx_t;

typedef struct
{
  int local_rank;
  int rank;
  int tnc;
  float cycles_to_msecs;
  char *hn_poff;
  gaspi_group_ctx_t *groups;
  gaspi_state_t *state_vec[GASPI_MAX_QP + 3];

  /* GASPI configuration */
  gaspi_config_t *config;

  /* Device */
  gaspi_device_t *device;

  gaspi_rc_mseg_t nsrc;
  gaspi_rc_mseg_t **rrmd;

  gaspi_uint ne_count_grp;
  gaspi_uint ne_count_c[GASPI_MAX_QP];

} gaspi_context_t;

#ifdef GPI_DEVICE_UCX
struct gaspi_rc_mseg_rkey {
  void * buffer; size_t buffer_size;
};
#endif


#endif