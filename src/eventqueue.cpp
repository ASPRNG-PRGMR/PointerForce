#include "eventqueue.hpp"

// ─────────────────────────────────────────────
//  eventqueue.cpp
// ─────────────────────────────────────────────

namespace pf {

void EventQueue::push(InputEvent ev)
{
    {
        std::lock_guard<std::mutex> lock(mu_);
        q_.push(std::move(ev));
    }
    cv_.notify_one();
}

std::optional<InputEvent> EventQueue::pop(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait_for(lock, timeout, [this]{ return !q_.empty() || shutdown_; });

    if (q_.empty()) return std::nullopt;

    InputEvent ev = std::move(q_.front());
    q_.pop();
    return ev;
}

void EventQueue::shutdown()
{
    {
        std::lock_guard<std::mutex> lock(mu_);
        shutdown_ = true;
    }
    cv_.notify_all();
}

} // namespace pf
