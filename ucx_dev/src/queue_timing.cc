#include <thread>
#include "mpmc_queue_timing.h"

extern "C" {
#include "queue_timing_cpp_interop.h"
}

struct Queue {
  using value_type = int64_t;

  Queue () : q{nullptr} {
    this->q = queue_create();
  }
  ~Queue () {
    if (this->q) queue_destroy (this->q);
  }
  bool push (int64_t v) {
    return !!queue_push (this->q, v);
  }
  bool pop (int64_t & v) {
    return !!queue_pop (this->q, &v);
  }
  bool empty () const {
    return !!queue_is_empty (this->q);
  }

  struct mpmc_queue * q;
};

inline std::ostream& operator<<(std::ostream& os, Queue & q) noexcept
{
    return os; //q.dump_state(os);
}

int main () {
  auto q = Queue {};
  auto bw = es::lockfree::tests::QBandwidth<Queue> {q, 2, 2, 5000};
  bw.run ();
}

