/*//////////////////////////////////////////////////////////////////////////////
    Copyright (c) 2018 Jamboree

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//////////////////////////////////////////////////////////////////////////////*/
#ifndef ART_SHARED_TASK_HPP_INCLUDED
#define ART_SHARED_TASK_HPP_INCLUDED

#include <atomic>
#include <art/detail/task.hpp>

namespace art::detail
{
    struct shared_promise_base
    {
        std::atomic<void*> _then{this};
        std::atomic<unsigned> _use_count{2u};
        tag _tag{tag::pending};

        bool test_last() noexcept
        {
            return _use_count.fetch_sub(1u, std::memory_order_acquire) == 1u;
        }

        bool finalize() noexcept
        {
            auto next = _then.exchange(nullptr, std::memory_order_acq_rel);
            auto const call = _tag == tag::pending ? coroutine_final_cancel : coroutine_final_run;
            while (next != this)
            {
                auto then = static_cast<chained_coro*>(next);
                next = then->next;
                call(then);
            }
            return _use_count.fetch_sub(1u, std::memory_order_release) != 1u;
        }

        bool is_ready() const
        {
            return !_then.load(std::memory_order_acquire) && _tag != tag::pending;
        }

        bool follow(chained_coro* curr)
        {
            auto& next = curr->next;
            next = _then.load(std::memory_order_acquire);
            while (next)
            {
                if (_then.compare_exchange_weak(next, curr, std::memory_order_release, std::memory_order_acquire))
                    return true;
            }
            if (_tag != tag::pending)
                return false;
            coroutine_final_cancel(curr);
            return true;
        }
    };
}

namespace art
{
    template<class T>
    struct shared_task
      : detail::impl<shared_task<T>, detail::shared_promise_base>
    {
        using base_type = detail::impl<shared_task<T>, detail::shared_promise_base>;

        using base_type::base_type;

        shared_task() = default;

        shared_task(shared_task&&) = default;

        shared_task(shared_task const& other) noexcept : base_type(other._state)
        {
            if (auto state = this->_state)
                state->_use_count.fetch_add(1u, std::memory_order_relaxed);
        }

        shared_task(task<T>&& other)
          : base_type(detail::convert<shared_task>(std::move(other)))
        {}

        shared_task& operator=(shared_task other) noexcept
        {
            this->~shared_task();
            return *new(this) shared_task(std::move(other));
        }

        auto operator co_await() const noexcept
        {
            struct awaiter
            {
                using state = typename base_type::state;
                state* _state;
                detail::chained_coro _chained;

                explicit awaiter(state* s) noexcept : _state(s) {}

                bool await_ready() const noexcept
                {
                    return _state->is_ready();
                }

                bool await_suspend(coroutine_handle<> cb) noexcept
                {
                    _chained.coro = cb;
                    return _state->follow(&_chained);
                }

                detail::cref_t<T> await_resume() const
                {
                    return _state->get();
                }
            };
            return awaiter{this->_state};
        }
    };

    template<class T>
    inline void swap(shared_task<T>& a, shared_task<T>& b) noexcept
    {
        a.swap(b);
    }
}

#endif