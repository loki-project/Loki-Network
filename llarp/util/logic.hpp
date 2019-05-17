#ifndef LLARP_LOGIC_HPP
#define LLARP_LOGIC_HPP

#include <util/mem.h>
#include <util/threadpool.h>
#include <util/timer.hpp>

namespace llarp
{
  class Logic
  {
   public:
    struct llarp_threadpool* thread;
    struct llarp_timer_context* timer;

    Logic()
        : thread(llarp_init_same_process_threadpool())
        , timer(llarp_init_timer())
    {
    }

    Logic(struct llarp_threadpool* tp) : thread(tp), timer(llarp_init_timer())
    {
    }

    /// single threaded tick
    void
    tick(llarp_time_t now);

    /// isolated tick
    void
    tick_async(llarp_time_t now);

    void
    stop_timer();

    void
    stop();

    void
    mainloop();

    void
    queue_job(struct llarp_thread_job job);

    bool
    queue_func(std::function< void(void) > func);

    uint32_t
    call_later(const llarp_timeout_job& job);

    void
    cancel_call(uint32_t id);

    void
    remove_call(uint32_t id);
  };
}  // namespace llarp

#endif
