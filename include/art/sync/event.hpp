/*//////////////////////////////////////////////////////////////////////////////
    Copyright (c) 2018 Jamboree

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//////////////////////////////////////////////////////////////////////////////*/
#ifndef ART_SYNC_EVENT_HPP_INCLUDED
#define ART_SYNC_EVENT_HPP_INCLUDED

#include <atomic>
#include <art/core.hpp>

namespace art
{
    class event
    {
        struct awaiter
        {
            std::atomic<void*>& _then;
            detail::chained_coro _chained;

            bool await_ready() noexcept
            {
                return !_then.load(std::memory_order_relaxed);
            }

            bool await_suspend(coroutine_handle<> coro) noexcept
            {
                _chained.coro = coro;
                auto prev = _then.load(std::memory_order_relaxed);
                auto curr = &_chained;
                auto& next = curr->next;
                while (prev)
                {
                    next = prev;
                    if (_then.compare_exchange_weak(prev, curr, std::memory_order_release))
                        return true;
                }
                return false;
            }

            void await_resume() noexcept {}
        };

        template<class F>
        void flush(F f)
        {
            if (auto next = _then.exchange(nullptr, std::memory_order_acquire))
            {
                while (next != this)
                {
                    auto then = static_cast<detail::chained_coro*>(next);
                    next = then->next;
                    f(then);
                }
            }
        }

    public:
        explicit event(executor& exe = default_executor())
          : _then{this}, _exe(exe)
        {}

        ~event()
        {
            // Destroy the pending coroutines in case that set() is not called.
            flush([](detail::chained_coro* chain) { chain->coro.destroy(); });
        }

        void set() noexcept
        {
            // Executor is not allowed to throw here.
            flush([this](detail::chained_coro* chain) { _exe(chain); });
        }

        void reset() noexcept
        {
            void* token = nullptr;
            _then.compare_exchange_strong(token, this, std::memory_order_relaxed);
        }

        awaiter operator co_await() noexcept
        {
            return {_then};
        }

    private:
        std::atomic<void*> _then;
        executor& _exe;
    };
}

#endif