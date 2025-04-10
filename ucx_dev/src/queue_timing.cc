
#include "mpmc_queue_timing.h"
#include <thread>
#include <tuple>

extern "C" {
#include "mpmc_queue.h"
#include "queue_timing_cpp_interop.h"
}

struct Queue {
  using value_type = uint64_t;

  Queue () : q{nullptr} {
    this->q = queue_create();
  }
  ~Queue () {
    if (this->q) queue_destroy (this->q);
  }
  bool push (uint64_t v) {
    using Pair = struct alf_tag_payload_pair;
    return !!alf_enqueue (this->q, Pair { .payload = v });
  }
  bool pop (uint64_t & v) {
    struct alf_tag_payload_pair p = {0};
    int ok = !!alf_dequeue (this->q, &p);
    if (ok) v = p.payload;
    return !!ok;
  }
  bool empty () const {
    return !!alf_is_empty (this->q);
  }

  struct mpmc_queue * q;
};

static inline std::ostream& operator<<(std::ostream& os, Queue & q) noexcept
{
    return os; //q.dump_state(os);
}

static void abort_with_usage_message () {
  std::cerr <<
    "Usage:\n"
    "queue_timing [n_producers] [n_consumers] [time_in_ms]\n";
  exit (1);
}

static auto parse_cmd_line_arguments (int argc, char ** argv) {
  if (argc > 4) abort_with_usage_message ();
  int n_producers = argc > 1 ? atoi (argv[1]) : 1;
  int n_consumers = argc > 2 ? atoi (argv[2]) : 1;
  int time_in_ms = argc > 3 ? atoi (argv[3]) : 400;
  if (n_producers <= 0 || n_consumers <= 0 || time_in_ms <= 0) abort_with_usage_message ();
  return std::make_tuple (
    static_cast<unsigned int> (n_producers),
    static_cast<unsigned int> (n_consumers),
    static_cast<unsigned int> (time_in_ms)
  );
}

int main (int argc, char ** argv) {
  auto q = Queue {};
  auto [n_producers, n_consumers, time_in_ms] = parse_cmd_line_arguments (argc, argv);
  auto bw = es::lockfree::tests::QBandwidth<Queue> {q, n_producers, n_consumers, time_in_ms};
  bw.run ();
}

