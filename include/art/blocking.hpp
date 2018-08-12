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
#include <art/coroutine.hpp>

namespace art::blocking_detail
{
    struct promise_base
    {
        coro_ts::suspend_never initial_suspend() noexcept
        {
            return {};
        }

        void return_void() noexcept
        {
            std::unique_lock<std::mutex> lock(mtx);
            ready = true;
        }

        std::mutex mtx;
        std::condition_variable cond;
        bool ready = false;
    };

    struct task
    {
        struct promise_type : promise_base
        {
            coro_ts::suspend_always final_suspend() noexcept
            {
                cond.notify_one();
                return {};
            }

            task get_return_object()
            {
                return task(coroutine_handle<promise_type>::from_promise(*this));
            }

            void wait()
            {
                std::unique_lock<std::mutex> lock(mtx);
                while (!ready)
                    cond.wait(lock);
            }
        };

        explicit task(coroutine_handle<promise_type> coro) : coro(coro) {}

        struct finalizer
        {
            coroutine_handle<promise_type> coro;

            ~finalizer()
            {
                coro.destroy();
            }
        };

        template<class Awaitable>
        static task run(Awaitable& a)
        {
            co_await suspend([&](auto c) { return a.await_suspend(c); });
        }

        coroutine_handle<promise_type> coro;
    };

    struct timed_task
    {
        struct promise_type : promise_base
        {
            detail::suspend_if final_suspend() noexcept
            {
                cond.notify_one();
                return !last_owner.test_and_set(std::memory_order_relaxed);
            }

            timed_task get_return_object()
            {
                return timed_task(coroutine_handle<promise_type>::from_promise(*this));
            }

            template<class Clock, class Duration>
            bool wait_until(std::chrono::time_point<Clock, Duration> const& timeout_time)
            {
                std::unique_lock<std::mutex> lock(mtx);
                while (!ready)
                {
                    if (cond.wait_until(lock, timeout_time) == std::cv_status::timeout)
                        break;
                }
                return !!ready;
            }

            std::atomic_flag last_owner = ATOMIC_FLAG_INIT;
        };

        explicit timed_task(coroutine_handle<promise_type> coro) : coro(coro) {}

        struct finalizer
        {
            coroutine_handle<promise_type> coro;

            ~finalizer()
            {
                if (coro.promise().last_owner.test_and_set(std::memory_order_relaxed))
                    coro.destroy();
            }
        };

        template<class Awaitable>
        static timed_task run(Awaitable a)
        {
            co_await suspend([&](auto c) { return a.await_suspend(c); });
        }

        coroutine_handle<promise_type> coro;
    };

    template<class A>
    inline A&& wait(A&& a)
    {
        if (!a.await_ready())
            task::finalizer{task::run(a).coro}.coro.promise().wait();
        return std::forward<A>(a);
    }

    template<class A, class Clock, class Duration>
    inline bool wait_until(A&& a, std::chrono::time_point<Clock, Duration> const& timeout_time)
    {
        if (a.await_ready())
            return true;

        return timed_task::finalizer{timed_task::run<A>(std::forward<A>(a)).coro}.
            coro.promise().wait_until(timeout_time);
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