#include "GPI2.h"

char *
pgaspi_gethostname (const unsigned int id)
{
  //TODO: ctx as arg
  gaspi_context_t const *const gctx = &glb_gaspi_ctx;

  return gctx->hn_poff + id * 64;
}
