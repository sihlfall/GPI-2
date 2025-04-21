#ifndef MPMC_QUEUE_H_
#define MPMC_QUEUE_H_

#include <stdint.h>

typedef uint16_t alf_tag_type;
typedef uint64_t alf_payload_type;

struct alf_tag_payload_pair {
    alf_tag_type tag;
    alf_payload_type payload;
};

struct mpmc_queue;

int alf_enqueue (struct mpmc_queue * q, struct alf_tag_payload_pair d);
int alf_dequeue (struct mpmc_queue * q, struct alf_tag_payload_pair * d);
int alf_peek (struct mpmc_queue * q, struct alf_tag_payload_pair * d);
int alf_is_empty (struct mpmc_queue * q);

#endif