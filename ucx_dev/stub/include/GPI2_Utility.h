#ifndef GPI2_UTILITY_H_
#define GPI2_UTILITY_H_

#ifdef __GNUC__
#  define GASPI_UNUSED(x) UNUSED_ ## x __attribute__((__unused__))
#else
#  define GASPI_UNUSED(x) UNUSED_ ## (void)(x)
#endif

#ifdef DEBUG
#include "GPI2.h"
extern gaspi_config_t glb_gaspi_cfg;
#define GASPI_DEBUG_PRINT_ERROR(msg,...)                                \
  {                                                                     \
    int gaspi_debug_errsv = errno;                                      \
    if (gaspi_debug_errsv != 0)                                         \
    {                                                                   \
      fprintf(stderr,"[Rank %4u]: Error %d (%s) at (%s:%d):" msg "\n",  \
              glb_gaspi_ctx.rank, gaspi_debug_errsv, (char *) strerror(gaspi_debug_errsv), \
              __FILE__, __LINE__, ##__VA_ARGS__);                       \
    }                                                                   \
    else                                                                \
    {                                                                   \
      fprintf(stderr,"[Rank %4u]: Error at (%s:%d):" msg "\n",          \
              glb_gaspi_ctx.rank, __FILE__, __LINE__, ##__VA_ARGS__);   \
    }                                                                   \
    fflush(stderr);                                                     \
  }
#else

#define GASPI_DEBUG_PRINT_ERROR(msg,...)
#endif

char * pgaspi_gethostname (const unsigned int id);


#endif