#ifndef LLARP_TIMER_HPP
#define LLARP_TIMER_HPP

#include <util/threadpool.h>
#include <util/time.hpp>

#include <functional>

/** called with userptr, original timeout, left */
using llarp_timer_handler_func =
    std::function< void(void *, uint64_t, uint64_t) >;

struct llarp_timeout_job
{
  uint64_t timeout{};
  void *user{};
  llarp_timer_handler_func handler;
};

struct llarp_timer_context;

struct llarp_timer_context *
llarp_init_timer();

uint32_t
llarp_timer_call_later(struct llarp_timer_context *t,
                       const struct llarp_timeout_job &job);

void
llarp_timer_cancel_job(struct llarp_timer_context *t, uint32_t id);

void
llarp_timer_remove_job(struct llarp_timer_context *t, uint32_t id);

// cancel all
void
llarp_timer_stop(struct llarp_timer_context *t);

/// set timer's timestamp, if now is 0 use the current time from system clock,
/// llarp_time_t now
void
llarp_timer_set_time(struct llarp_timer_context *t, llarp_time_t now);

// blocking run timer and send events to thread pool
void
llarp_timer_run(struct llarp_timer_context *t, struct llarp_threadpool *pool);

/// single threaded run timer, tick all timers
void
llarp_timer_tick_all(struct llarp_timer_context *t);

/// tick all timers into a threadpool asynchronously
void
llarp_timer_tick_all_async(struct llarp_timer_context *t,
                           struct llarp_threadpool *pool, llarp_time_t now);

void
llarp_free_timer(struct llarp_timer_context **t);

#endif
