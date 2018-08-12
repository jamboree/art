/*//////////////////////////////////////////////////////////////////////////////
    Copyright (c) 2018 Jamboree

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//////////////////////////////////////////////////////////////////////////////*/
#ifndef ART_TASK_HPP_INCLUDED
#define ART_TASK_HPP_INCLUDED

#include <atomic>
#include <cassert>
#include <art/detail/task.hpp>

namespace art::detail
{
    struct unique_promise_base : promise_base
    {
        std::atomic<void*> _then{this};
        tag _tag{tag::pending};

        bool test_last() noexcept
        {
            return !_then.exchange(nullptr, std::memory_order_acquire);
        }

        suspend_if final_suspend() noexcept
        {
            auto then = _then.exchange(nullptr, std::memory_order_acq_rel);
            if (then != this)
            {
                if (!then) // Task is destroyed, we're the last owner.
                    return false;
                coroutine_handle<>::from_address(then)();
            }
            return true; // We're done. Let the task do the cleanup.
        }

        bool follow(coroutine_handle<> cb)
        {
            void* last = this;
            if (_then.compare_exchange_strong(last, cb.address(), std::memory_order_release, std::memory_order_acquire))
                return true;
            // If there's a previous waiter, just cancel it because it's only
            // allowed for when_any.
            if (last)
            {
                if (_then.compare_exchange_strong(last, cb.address(), std::memory_order_acq_rel))
                {
                    coroutine_handle<>::from_address(last).destroy();
                    return true;
                }
                assert(!last && "multiple coroutines await on same task");
            }
            return false;
        }
    };
}

namespace art
{
    template<class T>
    struct task
      : detail::impl<task<T>, detail::unique_promise_base>
    {
        using base_type = detail::impl<task<T>, detail::unique_promise_base>;

        using base_type::base_type;

        task() = default;

        task(task&&) = default;

        task& operator=(task&& other) = default;

        bool await_ready() const noexcept
        {
            return !this->_promise->_then.load(std::memory_order_acquire);
        }

        bool await_suspend(coroutine_handle<> cb) const noexcept
        {
            return this->_promise->follow(cb);
        }

        T await_resume()
        {
            return detail::extract_promise<task>{this->_promise}->get();
        }

        shared_task<T> share()
        {
            return detail::convert<shared_task<T>>(std::move(*this));
        }
    };

    template<class T>
    inline void swap(task<T>& a, task<T>& b) noexcept
    {
        a.swap(b);
    }
}

#endif