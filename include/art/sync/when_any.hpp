/*//////////////////////////////////////////////////////////////////////////////
    Copyright (c) 2018 Jamboree

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//////////////////////////////////////////////////////////////////////////////*/
#ifndef ART_SYNC_WHEN_ANY_HPP_INCLUDED
#define ART_SYNC_WHEN_ANY_HPP_INCLUDED

#include <tuple>
#include <atomic>
#include <vector>
#include <memory>
#include <art/task.hpp>
#include <art/detail/copy_or_move.hpp>

namespace art
{ 
    template<class Sequence>
    struct when_any_result
    {
        std::size_t index;
        Sequence futures;
    };

    namespace detail
    {
        struct detached
        {
            struct promise_type
            {
                detached get_return_object() noexcept { return{}; }

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
        };

        template<class A, class State>
        detached wait_any_at(std::size_t i, A a, std::shared_ptr<State> state)
        {
            co_await suspend([&](auto c) { return a.await_suspend(c); });
            state->set_ready(i);
        }
            
        template<class A, std::size_t I, class State>
        detached wait_any_at(std::integral_constant<std::size_t, I>, A a, std::shared_ptr<State> state)
        {
            co_await suspend([&](auto c) { return a.await_suspend(c); });
            state->set_ready(I);
        }

        template<class I, class A, class State>
        bool try_any_at(I i, A&& a, std::shared_ptr<State> state)
        {
            if (a.await_ready())
            {
                state->set_ready(i);
                return true;
            }
            wait_any_at<A>(i, std::forward<A>(a), std::move(state));
            return false;
        }

        template<class Sequence>
        struct when_any_state
        {
            std::atomic<void*> coro;
            when_any_result<Sequence> result;

            template<class... T>
            when_any_state(bool/*dummy*/, T&&... t)
              : coro{nullptr}, result{0, {std::forward<T>(t)...}}
            {}

            ~when_any_state()
            {
                coroutine_handle<>::from_address(coro.load(std::memory_order_relaxed)).destroy();
            }

            void set_ready(std::size_t i)
            {
                if (auto then = coro.exchange(nullptr, std::memory_order_relaxed))
                {
                    result.index = i;
                    coroutine_handle<>::from_address(then)();
                }
            }

            static task<when_any_result<Sequence>> make_task(when_any_state& state)
            {
                co_await suspend([&](coroutine_handle<> c)
                {
                    state.coro.store(c.address(), std::memory_order_relaxed);
                });
                co_return std::move(state.result);
            }
        };

        template<class State>
        inline void wait_any_each(State const& state, std::integral_constant<std::size_t, 0>, std::integral_constant<std::size_t, 0>)
        {
            state->set_ready(std::size_t(-1));
        }

        template<class State, std::size_t I, std::size_t E>
        inline void wait_any_each(State const& state, std::integral_constant<std::size_t, I> i, std::integral_constant<std::size_t, E> e)
        {
            if (try_any_at(i, get_awaiter(std::get<I>(state->result.futures)), state))
                return;
            if (state->coro.load(std::memory_order_relaxed))
                wait_any_each(state, std::integral_constant<std::size_t, I + 1>{}, e);
        }

        template<class State, std::size_t I>
        inline void wait_any_each(State const&, std::integral_constant<std::size_t, I>, std::integral_constant<std::size_t, I>) {}
    }

    template<class InputIt>
    auto when_any(InputIt first, InputIt last) ->
        task<when_any_result<std::vector<typename std::iterator_traits<InputIt>::value_type>>>
    {
        using task_t = typename std::iterator_traits<InputIt>::value_type;
        using seq_t = std::vector<task_t>;
        using iter = detail::copy_or_move_iter<InputIt, std::is_copy_constructible<task_t>::value>;
        auto state = std::make_shared<detail::when_any_state<seq_t>>(true, iter::wrap(first), iter::wrap(last));
        auto ret(detail::when_any_state<seq_t>::make_task(*state));
        if (const std::size_t n = state->result.futures.size())
        {
            for (std::size_t i = 0; i != n; ++i)
            {
                if (try_any_at(i, get_awaiter(state->result.futures[i]), state))
                    break;
                if (!state->coro.load(std::memory_order_relaxed))
                    break;
            }
        }
        else
            state->set_ready(std::size_t(-1));
        return ret;
    }

    template<class... Futures>
    auto when_any(Futures&&... futures) ->
        task<when_any_result<std::tuple<std::decay_t<Futures>...>>>
    {
        using seq_t = std::tuple<std::decay_t<Futures>...>;
        auto state = std::make_shared<detail::when_any_state<seq_t>>(true, detail::copy_or_move<Futures>(futures)...);
        auto ret(detail::when_any_state<seq_t>::make_task(*state));
        detail::wait_any_each(state, std::integral_constant<std::size_t, 0>{}, std::tuple_size<seq_t>{});
        return ret;
    }
}

#endif