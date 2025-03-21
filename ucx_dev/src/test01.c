#include "GPI2_UCX_Interface.h"
#include "GPI2.h"

#include <stdio.h>
#include <stdlib.h>

#define ASSERT_ZERO(x)                                        \
  do {                                                        \
    if ((x)) {                                                \
      fprintf(                                                \
        stderr,                                               \
        "Assert failed at line %s:%d. Exiting\n",             \
        __FILE__, __LINE__                                    \
      );                                                      \
      exit(1);                                                \
    }                                                         \
  } while (0);
  
int main()
{
  gaspi_context_t gctx = {
    .local_rank = 0,
    .rank = 0,
    .tnc = 1,
    .hn_poff = "19000",
    .config = & (gaspi_config_t) {
      .dev_config = (gaspi_dev_config_t) {
        .params = { .tcp = { .port = 19000 }}
      }
    },
    .device = & (gaspi_device_t) { .ctx = NULL }
  };

  glb_gaspi_ctx = gctx;

  ASSERT_ZERO (pgaspi_dev_init_core (&gctx))

  ASSERT_ZERO (pgaspi_dev_cleanup_core (&gctx))

  return 0;
}