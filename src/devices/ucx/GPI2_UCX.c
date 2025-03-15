#include "GASPI.h"
#include "GPI2.h"
#include "GPI2_Dev.h"
#include "GPI2_SN.h"
#include "GPI2_UCX.h"
#include "GPI2_Utility.h"
#include <stdio.h>

#define NOTIMPLEMENTED() \
  do {                                                                \
    fprintf(stderr, "Not implemented [%s:%i]\n", __FILE__, __LINE__); \    
    exit(1);                                                          \
  } while (0);

int
pgaspi_dev_create_endpoint (gaspi_context_t const *const GASPI_UNUSED (gctx),
                            const int GASPI_UNUSED (i),
                            void **info,
                            void **remote_info,
                            size_t * info_size)
{
  NOTIMPLEMENTED()
  return 0;
}

//TODO:
int
pgaspi_dev_disconnect_context (gaspi_context_t * const GASPI_UNUSED (gctx),
                               const int GASPI_UNUSED (i))
{
  NOTIMPLEMENTED()
  return 0;
}

int
pgaspi_dev_connect_context (gaspi_context_t const *const gctx,
                            const int i)
{
  NOTIMPLEMENTED()
}

int
pgaspi_dev_comm_queue_connect (gaspi_context_t const *const GASPI_UNUSED (gctx),
                               const unsigned short GASPI_UNUSED (q),
                               const int GASPI_UNUSED (i))
{
  NOTIMPLEMENTED()
}

int
pgaspi_dev_comm_queue_delete (gaspi_context_t const *const gctx,
                              const unsigned int id)
{
  NOTIMPLEMENTED()
}

int
pgaspi_dev_comm_queue_create (gaspi_context_t const *const gctx,
                              const unsigned int id,
                              const unsigned short GASPI_UNUSED (remote_node))
{
  NOTIMPLEMENTED()
}

int
pgaspi_dev_comm_queue_is_valid (gaspi_context_t const *const gctx,
                                const unsigned int id)
{
  NOTIMPLEMENTED()
}

int
pgaspi_dev_init_core (gaspi_context_t * const gctx)
{
  NOTIMPLEMENTED()
}

int
pgaspi_dev_cleanup_core (gaspi_context_t * const gctx)
{
  NOTIMPLEMENTED()
}
