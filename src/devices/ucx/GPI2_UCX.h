#ifndef _GPI2_UCX_H_
#define _GPI2_UCX_H_

#include "GPI2.h"
#include "GPI2_Dev.h"

#include "ucx_device.h"


typedef struct
{
  struct ucx_device ucx_device;
} gaspi_ucx_ctx;

#endif //_GPI2_UCX_H_
