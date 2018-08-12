/*//////////////////////////////////////////////////////////////////////////////
    Copyright (c) 2018 Jamboree

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//////////////////////////////////////////////////////////////////////////////*/
#ifndef ART_CORE_HPP_INCLUDED
#define ART_CORE_HPP_INCLUDED

#include <experimental/coroutine>

namespace art
{
    namespace coro_ts = std::experimental;

    using coro_ts::coroutine_handle;
}

namespace art::detail
{
    struct trivial_promise_base
    {
        coro_ts::suspend_never initial_suspend() noexcept
        {
            return {};
        }

        coro_ts::suspend_never final_suspend() noexcept
        {
            return {};
        }

        void return_void() noexcept {}
    };

    struct suspend_if
    {
        bool _ready;

        suspend_if(bool cond) noexcept : _ready(!cond) {}

        bool await_ready() const noexcept
        {
            return _ready;
        }

        void await_suspend(coroutine_handle<>) const noexcept {}

        void await_resume() const noexcept {}
    };

    template<class T>
    struct extract_promise
    {
        using P = typename T::promise_type;

        P*& p;

        P* operator->() const noexcept { return p; }

        ~extract_promise()
        {
            coroutine_handle<P>::from_promise(*p).destroy();
            p = nullptr;
        }
    };

    struct chained_coro
    {
        coroutine_handle<> coro;
        void* next;
    };

    template<unsigned N>
    struct priority : priority<N - 1> {};

    template<>
    struct priority<0> {};

    template<class A>
    inline auto get_awaiter_(A&& a, priority<2>) -> decltype(std::forward<A>(a).operator co_await())
    {
        return std::forward<A>(a).operator co_await();
    }

    template<class A>
    inline auto get_awaiter_(A&& a, priority<1>) -> decltype(operator co_await(std::forward<A>(a)))
    {
        return operator co_await(std::forward<A>(a));
    }

    template<class A>
    inline A&& get_awaiter_(A&& a, priority<0>)
    {
        return std::forward<A>(a);
    }

    template<class Task>
    struct ready_awaiter
    {
        Task task;

        bool await_ready()
        {
            return task.await_ready();
        }

        template<class P>
        auto await_suspend(coroutine_handle<P> c) -> decltype(task.await_suspend(c))
        {
            return task.await_suspend(c);
        }

        void await_resume() noexcept {}
    };
}

namespace art
{
    template<class A>
    inline auto get_awaiter(A&& a) -> decltype(detail::get_awaiter_(std::forward<A>(a), detail::priority<2>{}))
    {
        return detail::get_awaiter_(std::forward<A>(a), detail::priority<2>{});
    }

    template<class Task>
    inline detail::ready_awaiter<Task> ready(Task&& task)
    {
        return {std::forward<Task>(task)};
    }

    template<class F>
    struct suspend
    {
        F _f;

        suspend(F f) noexcept : _f(std::move(f)) {}

        bool await_ready() const noexcept
        {
            return false;
        }

        template<class P>
        auto await_suspend(coroutine_handle<P> c) -> decltype(_f(c))
        {
            return _f(c);
        }

        void await_resume() const noexcept {}
    };
}

#endif