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
    struct unique_promise_base
    {
        std::atomic<void*> _then{this};
        tag _tag{tag::pending};

        bool test_last() noexcept
        {
            return !_then.exchange(nullptr, std::memory_order_acquire);
        }

        bool finalize() noexcept
        {
            auto then = _then.exchange(nullptr, std::memory_order_acq_rel);
            if (then != this)
            {
                if (!then) // Task is destroyed, we're the last owner.
                    return false;
                coroutine_final_call(static_cast<chained_coro*>(then), _tag == tag::pending);
            }
            return true; // We're done. Let the task do the cleanup.
        }

        bool is_ready() const
        {
            return !_then.load(std::memory_order_acquire) && _tag != tag::pending;
        }

        bool follow(chained_coro* cb)
        {
            void* last = this;
            if (_then.compare_exchange_strong(last, cb, std::memory_order_release, std::memory_order_acquire))
                return true;
            // If there's a previous waiter, just cancel it.
            if (last)
            {
                if (_then.compare_exchange_strong(last, cb, std::memory_order_acq_rel))
                {
                    coroutine_final_cancel(static_cast<chained_coro*>(last));
                    return true;
                }
                assert(!last && "multiple coroutines await on same task");
            }
            if (_tag != tag::pending)
                return false;
            coroutine_final_cancel(cb);
            return true;
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

        auto operator co_await() noexcept
        {
            struct awaiter
            {
                typename base_type::state*& _state;
                detail::chained_coro _chained;

                bool await_ready() const noexcept
                {
                    return _state->is_ready();
                }

                bool await_suspend(coroutine_handle<> cb) noexcept
                {
                    _chained.coro = cb;
                    return _state->follow(&_chained);
                }

                T await_resume() const
                {
                    return detail::extract_state{_state}->get();
                }
            };
            return awaiter{this->_state};
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