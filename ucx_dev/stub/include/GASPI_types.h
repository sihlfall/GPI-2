#ifndef GASPI_TYPES_H_
#define GASPI_TYPES_H_

/* Types */
typedef char gaspi_char;
typedef unsigned char gaspi_uchar;
typedef short gaspi_short;
typedef unsigned short gaspi_ushort;
typedef int gaspi_int;
typedef unsigned int gaspi_uint;
typedef long gaspi_long;
typedef unsigned long gaspi_ulong;
typedef float gaspi_float;
typedef double gaspi_double;

typedef unsigned long gaspi_timeout_t;
typedef unsigned short gaspi_rank_t;
typedef unsigned char gaspi_group_t;
typedef unsigned int gaspi_number_t;
typedef void *gaspi_pointer_t;
typedef void* gaspi_reduce_state_t;
typedef unsigned char gaspi_queue_id_t;
typedef unsigned long gaspi_size_t;
typedef unsigned char gaspi_segment_id_t;
typedef unsigned long gaspi_offset_t;
typedef unsigned long gaspi_atomic_value_t;
typedef float gaspi_time_t;
typedef unsigned long gaspi_cycles_t;
typedef unsigned int gaspi_notification_id_t;
typedef unsigned int gaspi_notification_t;
typedef unsigned int gaspi_statistic_counter_t;
typedef char * gaspi_string_t;

typedef int gaspi_memory_description_t;

typedef enum
{
  GASPI_ERROR = -1,
  GASPI_SUCCESS = 0,
  GASPI_TIMEOUT = 1,
  GASPI_ERR_EMFILE = 2,
  GASPI_ERR_ENV = 3,
  GASPI_ERR_SN_PORT = 4,
  GASPI_ERR_CONFIG = 5,
  GASPI_ERR_NOINIT = 6,
  GASPI_ERR_INITED = 7,
  GASPI_ERR_NULLPTR = 8,
  GASPI_ERR_INV_SEGSIZE = 9,
  GASPI_ERR_INV_SEG = 10,
  GASPI_ERR_INV_GROUP = 11,
  GASPI_ERR_INV_RANK = 12,
  GASPI_ERR_INV_QUEUE = 13,
  GASPI_ERR_INV_LOC_OFF = 14,
  GASPI_ERR_INV_REM_OFF = 15,
  GASPI_ERR_INV_COMMSIZE = 16,
  GASPI_ERR_INV_NOTIF_VAL = 17,
  GASPI_ERR_INV_NOTIF_ID = 18,
  GASPI_ERR_INV_NUM = 19,
  GASPI_ERR_INV_SIZE = 20,
  GASPI_ERR_MANY_SEG = 21,
  GASPI_ERR_MANY_GRP = 22,
  GASPI_QUEUE_FULL = 23,
  GASPI_ERR_UNALIGN_OFF = 24,
  GASPI_ERR_ACTIVE_COLL = 25,
  GASPI_ERR_DEVICE = 26,
  GASPI_ERR_SN = 27,
  GASPI_ERR_MEMALLOC = 28
} gaspi_return_t;

typedef struct
{
  struct {
    struct
    {
      /* The first port to use (default 19000).  */
      /*NOTE: if more than one instance per node is used, the
        consecutive ports will be used:
        - inst 0: port
        - inst 1: port + 1
        - inst 2: port + 2
        - ....
      */
      gaspi_uint port;
    } tcp;
  } params;
} gaspi_dev_config_t;

typedef struct
{
  gaspi_dev_config_t dev_config;            /* Specific, device-dependent params */
} gaspi_config_t;

#endif