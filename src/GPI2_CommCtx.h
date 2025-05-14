#ifndef GPI2_COMMCTX_H_
#define GPI2_COMMCTX_H_

#include "GPI2_Types.h"
#include "GPI2_Utility.h"

int gaspiu_initialize_comm_ctx (gaspi_context_t * ctx);
int gaspiu_cleanup_comm_ctx (gaspi_context_t * ctx);
int gaspiu_start_sn (gaspi_context_t * ctx);
int gaspiu_stop_sn (gaspi_context_t * ctx);

#endif