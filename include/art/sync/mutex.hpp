/*//////////////////////////////////////////////////////////////////////////////
    Copyright (c) 2018 Jamboree

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//////////////////////////////////////////////////////////////////////////////*/
#ifndef ART_SYNC_MUTEX_HPP_INCLUDED
#define ART_SYNC_MUTEX_HPP_INCLUDED

#include <atomic>
#include <cassert>
#include <art/core.hpp>

namespace art
{
    class mutex
    {
        std::atomic<void*> _then;

    public:
        constexpr mutex() : _then{nullptr} {}

        // Non-copyable.
        mutex(mutex const&) = delete;
        mutex& operator=(mutex const&) = delete;

        ~mutex() { assert(!_then.load() && "mutex is not released"); }

        bool try_lock() noexcept
        {
            void* curr = nullptr;
            return _then.compare_exchange_strong(curr, this, std::memory_order_acquire, std::memory_order_relaxed);
        }

        bool lock_suspend(detail::chained_coro* chain) noexcept
        {
            auto& next = chain->next;
            next = nullptr;
            for (;;)
            {
                // If not locked, try to lock it.
                if (!next && _then.compare_exchange_strong(next, this, std::memory_order_acquire, std::memory_order_relaxed))
                    return false;
                assert(next);
                // Already locked, try to enqueue the coroutine.
                if (_then.compare_exchange_weak(next, chain, std::memory_order_acq_rel, std::memory_order_relaxed))
                    break;
            }
            return true;
        }

        void unlock() noexcept
        {
            void* curr = this;
            void* next = nullptr;
            // No others waiting, we're done.
            if (_then.compare_exchange_strong(curr, next, std::memory_order_release, std::memory_order_acquire))
                return;
            // Wake up next waiting coroutine.
            do
            {
                next = static_cast<detail::chained_coro*>(curr)->next;
            } while (!_then.compare_exchange_weak(curr, next, std::memory_order_acq_rel));
            static_cast<detail::chained_coro*>(curr)->coro();
        }
    };

    template<class Lock>
    struct unlock_guard
    {
        Lock& lock;

        explicit unlock_guard(Lock& lock) : lock(lock) {}
        unlock_guard(unlock_guard const&) = delete;
        unlock_guard& operator=(unlock_guard const&) = delete;

        ~unlock_guard()
        {
            lock.unlock();
        }
    };

    template<class Lock>
    class lock_guard
    {
        Lock& _lock;
        detail::chained_coro _chained;

    public:
        explicit lock_guard(Lock& lock) noexcept : _lock(lock) {}

        bool await_ready() const noexcept
        {
            return false;
        }

        bool await_suspend(art::coroutine_handle<> coro) noexcept
        {
            _chained.coro = coro;
            return _lock.lock_suspend(&_chained);
        }

        unlock_guard<Lock> await_resume() const noexcept
        {
            return unlock_guard<Lock>{_lock};
        }
    };
}

#endif