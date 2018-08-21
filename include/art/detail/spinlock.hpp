/*//////////////////////////////////////////////////////////////////////////////
    Copyright (c) 2018 Jamboree

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//////////////////////////////////////////////////////////////////////////////*/
#ifndef ART_DETAIL_SPINLOCK_HPP_INCLUDED
#define ART_DETAIL_SPINLOCK_HPP_INCLUDED

#include <atomic>

namespace art::detail
{
    class spinlock
    {
        std::atomic_flag _flag = ATOMIC_FLAG_INIT;

    public:
        spinlock() = default;
        spinlock(spinlock const&) = delete;
        spinlock& operator=(spinlock const&) = delete;

        void lock() noexcept
        {
            while (_flag.test_and_set(std::memory_order_acquire));
        }

        bool try_lock() noexcept
        {
            return !_flag.test_and_set(std::memory_order_acquire);
        }

        void unlock() noexcept
        {
            _flag.clear(std::memory_order_release);
        }
    };
}

#endif