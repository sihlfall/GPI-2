#include "ucx_device.h"
#include "GPI2_UCX.h"
#include "GASPI.h"
#include "ucp/api/ucp.h"
#include "arpa/inet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Server
 */

static char input[256];

static
int get_command_line_input (char ip [64], uint16_t * rank) {
  if (fgets(input, sizeof(input) - 1, stdin)) {
    input[sizeof(input) - 1] = '0';
    input[strcspn(input, "\n")] = 0;

    if (strcmp(input, "q") == 0) {
      return 0;
    } else {
      int r;
      int matched = sscanf(input, "c %63s %d", ip, &r);
      if (matched != 2) return 0;
      *rank = (uint16_t) r;
      return 1;
    }
  }
  return 0;
}

static
int
run (uint16_t base_port, gaspi_rank_t my_rank)
{
  int ret = 0;

  ucx_device_t ucx_device;
  if (ucx_dev_init_device (&ucx_device, my_rank, base_port + my_rank)) {
    fprintf (stderr, "Could not create ucx_device\n");
    ret = 1;
    goto err_init_device;
  }

  if (ucx_dev_start_device (&ucx_device)) {
    fprintf (stderr, "Could not start thread.\n");
    ret = 1;
    goto err_start_device;
  }

  fprintf(stderr, "UCX device running. Enter c host rank to connect or q to quit.\n");

  enum { max_ips = 10 };
  char ipbuffer [max_ips][64] = {0};
  int iip = 0;

  gaspi_rank_t other_rank;

  while (iip < max_ips && get_command_line_input (ipbuffer [iip], &other_rank)) {
    ucx_dev_connect_to (&ucx_device, ipbuffer[iip], base_port + other_rank);
  }

  fprintf (stderr, "Stopping ucx device thread ...\n");
  ucx_dev_stop_device (&ucx_device);

err_start_device:
  fprintf (stderr, "Cleanup ...\n");
  ucx_dev_cleanup_device (&ucx_device);
  fprintf (stderr, "Server stopped.\n");

err_init_device:
  return ret;
}

static
void
abort_with_usage_message (void)
{
  fprintf(stderr, "Usage:\ntest04 base_port my_rank");
  exit (1);
}

struct config {
  uint16_t base_port;
  gaspi_rank_t my_rank;
};

int
main (int argc, char ** argv)
{
  struct config config = {0};

  int ret = 0;

  if (argc != 3) abort_with_usage_message ();

  {
    int tmp = atoi (argv[1]);
    if (tmp > 0xffff || tmp < 0) abort_with_usage_message ();
    config.base_port = (uint16_t) tmp;
  }

  {
    int tmp = atoi (argv[2]);
    if (tmp > 0xffff || tmp < 0) abort_with_usage_message ();
    config.my_rank = (uint16_t) tmp;
  }

  ret = run (config.base_port, config.my_rank);

  return ret;
}