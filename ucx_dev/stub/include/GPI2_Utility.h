#ifndef GPI2_UTILITY_H_
#define GPI2_UTILITY_H_

#ifdef DEBUG
#include <errno.h>
#endif

#ifdef __GNUC__
#  define GASPI_UNUSED(x) UNUSED_ ## x __attribute__((__unused__))
#else
#  define GASPI_UNUSED(x) UNUSED_ ## (void)(x)
#endif

#ifdef DEBUG
#include "GPI2.h"
extern gaspi_config_t glb_gaspi_cfg;

#define GASPI_DEBUG_PRINT_ERROR(...)                                   \
  {                                                                     \
    int gaspi_debug_errsv = errno;                                      \
    if (gaspi_debug_errsv != 0)                                         \
    {                                                                   \
      fprintf(stderr,"[Rank %4u]: Error %d (%s) at (%s:%d):" FIRST(__VA_ARGS__) "\n",  \
              glb_gaspi_ctx.rank, gaspi_debug_errsv, (char *) strerror(gaspi_debug_errsv), \
              __FILE__, __LINE__ REST(__VA_ARGS__) );                       \
    }                                                                   \
    else                                                                \
    {                                                                   \
      fprintf(stderr,"[Rank %4u]: Error at (%s:%d):" FIRST(__VA_ARGS__) "\n",          \
              glb_gaspi_ctx.rank, __FILE__, __LINE__ REST(__VA_ARGS__));   \
    }                                                                   \
    fflush(stderr);                                                     \
  }

/* Source of the following: https://stackoverflow.com/a/11172679 */
/* by Richard Hansen */
/* expands to the first argument */
#define FIRST(...) FIRST_HELPER(__VA_ARGS__, throwaway)
#define FIRST_HELPER(first, ...) first

/*
 * if there's only one argument, expands to nothing.  if there is more
 * than one argument, expands to a comma followed by everything but
 * the first argument.  only supports up to 9 arguments but can be
 * trivially expanded.
 */
#define REST(...) REST_HELPER(NUM(__VA_ARGS__), __VA_ARGS__)
#define REST_HELPER(qty, ...) REST_HELPER2(qty, __VA_ARGS__)
#define REST_HELPER2(qty, ...) REST_HELPER_##qty(__VA_ARGS__)
#define REST_HELPER_ONE(first)
#define REST_HELPER_TWOORMORE(first, ...) , __VA_ARGS__
#define NUM(...) \
    SELECT_10TH(__VA_ARGS__, TWOORMORE, TWOORMORE, TWOORMORE, TWOORMORE,\
                TWOORMORE, TWOORMORE, TWOORMORE, TWOORMORE, ONE, throwaway)
#define SELECT_10TH(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, ...) a10

#else

#define GASPI_DEBUG_PRINT_ERROR(...)

#endif

char * pgaspi_gethostname (const unsigned int id);


#endif