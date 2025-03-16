#include "ucx_device.h"
#include "ucp/api/ucp.h"

#include "GPI2.h"
#include "GPI2_Utility.h"


typedef struct
{
} gpi2_common_ucx_request_t;


int
ucx_dev_init_device (struct ucx_dev_args * args, ucx_wpool_t * wpool)
{
  // code taken from open mpi and modified
  int ret = 0;

  ucs_status_t status;
  ucp_config_t *config = NULL;
  ucp_params_t context_params;

  status = ucp_config_read("GPI2", NULL, &config);
  if (UCS_OK != status) {
    GASPI_DEBUG_PRINT_ERROR("ucp_config_read failed: %d", status);
    return -1;
  }

  /* initialize UCP context */
  memset(&context_params, 0, sizeof(context_params));
  context_params.field_mask = UCP_PARAM_FIELD_FEATURES | UCP_PARAM_FIELD_MT_WORKERS_SHARED
                              | UCP_PARAM_FIELD_ESTIMATED_NUM_EPS | UCP_PARAM_FIELD_REQUEST_INIT
                              | UCP_PARAM_FIELD_REQUEST_SIZE;
  context_params.features = UCP_FEATURE_RMA | UCP_FEATURE_AMO32 | UCP_FEATURE_AMO64;
  context_params.mt_workers_shared = 0; // TODO: (enable_mt ? 1 : 0);
  context_params.estimated_num_eps = args->peers_num;
  context_params.request_init = NULL; // TODO: opal_common_ucx_req_init;
  context_params.request_size = sizeof(gpi2_common_ucx_request_t);

/* # if HAVE_DECL_UCP_PARAM_FIELD_ESTIMATED_NUM_PPN
  context_params.estimated_num_ppn = opal_process_info.num_local_peers + 1;
  context_params.field_mask |= UCP_PARAM_FIELD_ESTIMATED_NUM_PPN;
  # endif */

  status = ucp_init(&context_params, config, &wpool->ucp_ctx);
  if (UCS_OK != status) {
    GASPI_DEBUG_PRINT_ERROR("ucp_init failed: %d", status);
    ucp_config_release(config);
    goto err_exit;
  }
  ucp_config_release(config);

  ucp_worker_params_t worker_params;
  ucp_worker_h worker;

  memset(&worker_params, 0, sizeof(worker_params));
  worker_params.field_mask = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
  worker_params.thread_mode = UCS_THREAD_MODE_SINGLE;
  status = ucp_worker_create(wpool->ucp_ctx, &worker_params, &worker);
  if (UCS_OK != status) {
    GASPI_DEBUG_PRINT_ERROR("ucp_worker_create failed: %d", status);
    goto err_create_worker;
  }
  wpool->default_worker = worker;

  return ret;

err_create_worker:
  ucp_cleanup(wpool->ucp_ctx);

err_exit:
  return -1;
}

void
ucx_dev_stop_device(ucx_wpool_t * wpool)
{
  ucp_worker_destroy(wpool->default_worker);
  ucp_cleanup(wpool->ucp_ctx);
}
