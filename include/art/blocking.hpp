/*//////////////////////////////////////////////////////////////////////////////
    Copyright (c) 2018 Jamboree

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//////////////////////////////////////////////////////////////////////////////*/
#ifndef ART_BLOCKING_HPP_INCLUDED
#define ART_BLOCKING_HPP_INCLUDED

#include <mutex>
#include <thread>
#include <atomic>
#include <cstddef>
#include <system_error>
#include <type_traits>
#include <condition_variable>
#include <art/core.hpp>

namespace art::blocking_detail
{
    struct state
    {
        void report_error()
        {
            if (!returned)
                throw std::system_error(std::make_error_code(std::errc::operation_canceled));
        }

        void notify()
        {
            {
                std::unique_lock<std::mutex> lock(mtx);
                ready = true;
            }
            cond.notify_one();
        }

        void wait()
        {
            std::unique_lock<std::mutex> lock(mtx);
            while (!ready)
                cond.wait(lock);
            report_error();
        }

        std::mutex mtx;
        std::condition_variable cond;
        bool returned = false;
        bool ready = false;
    };

    struct timed_state : state
    {
        template<class Clock, class Duration>
        bool wait_until(std::chrono::time_point<Clock, Duration> const& timeout_time)
        {
            std::unique_lock<std::mutex> lock(mtx);
            while (!ready)
            {
                if (cond.wait_until(lock, timeout_time) == std::cv_status::timeout)
                    return false;
            }
            report_error();
            return true;
        }

        std::atomic_flag last_owner = ATOMIC_FLAG_INIT;
    };

    struct promise_base
    {
        coro_ts::suspend_never final_suspend() noexcept
        {
            return {};
        }
        void return_void() noexcept
        {
            _state->returned = true;
        }

        void unhandled_exception() noexcept { std::terminate(); }

        state* _state;
    };

    struct task
    {
        struct promise_type : promise_base
        {
            ~promise_type()
            {
                _state->notify();
            }

            coro_ts::suspend_always initial_suspend() noexcept
            {
                return {};
            }

            task get_return_object()
            {
                return task(coroutine_handle<promise_type>::from_promise(*this));
            }
        };

        explicit task(coroutine_handle<promise_type> coro) : coro(coro) {}

        template<class Awaitable>
        static task run(Awaitable& a)
        {
            co_await suspend([&](auto c) { return a.await_suspend(c); });
        }

        void wait(state& s)
        {
            coro.promise()._state = &s;
            coro.resume();
            s.wait();
        }

        coroutine_handle<promise_type> coro;
    };

    struct timed_task
    {
        struct promise_type : promise_base
        {
            promise_type()
            {
                _state = new timed_state;
            }

            ~promise_type()
            {
                auto state = static_cast<timed_state*>(_state);
                state->notify();
                if (state->last_owner.test_and_set(std::memory_order_relaxed))
                    delete state;
            }

            coro_ts::suspend_never initial_suspend() noexcept
            {
                return {};
            }

            timed_task get_return_object()
            {
                return timed_task(static_cast<timed_state*>(_state));
            }
        };

        explicit timed_task(timed_state* state) : state(state) {}

        struct finalizer
        {
            timed_state* state;

            ~finalizer()
            {
                if (state->last_owner.test_and_set(std::memory_order_relaxed))
                    delete state;
            }
        };

        template<class Awaitable>
        static timed_task run(Awaitable a)
        {
            co_await suspend([&](auto c) { return a.await_suspend(c); });
        }

        timed_state* state;
    };

    template<class A>
    inline A&& wait(A&& a)
    {
        if (!a.await_ready())
        {
            state s;
            task::run(a).wait(s);
        }
        return std::forward<A>(a);
    }

    template<class A, class Clock, class Duration>
    inline bool wait_until(A&& a, std::chrono::time_point<Clock, Duration> const& timeout_time)
    {
        if (a.await_ready())
            return true;

        return timed_task::finalizer{timed_task::run<A>(std::forward<A>(a)).state}.
            state->wait_until(timeout_time);
    }
}

namespace art
{
    template<class Awaitable>
    inline void wait(Awaitable&& a)
    {
        blocking_detail::wait(get_awaiter(a));
    }

    template<class Awaitable, class Clock, class Duration>
    bool wait_until(Awaitable&& a, std::chrono::time_point<Clock, Duration> const& timeout_time)
    {
        return blocking_detail::wait_until(get_awaiter(std::forward<Awaitable>(a)), timeout_time);
    }

    template<class Awaitable, class Rep, class Period>
    inline bool wait_for(Awaitable&& a, std::chrono::duration<Rep, Period> const& rel_time)
    {
        return wait_until(std::forward<Awaitable>(a), std::chrono::steady_clock::now() + rel_time);
    }

    template<class Awaitable>
    inline decltype(auto) get(Awaitable&& a)
    {
        return blocking_detail::wait(get_awaiter(a)).await_resume();
    }
}

#endif