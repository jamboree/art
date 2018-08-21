/*//////////////////////////////////////////////////////////////////////////////
    Copyright (c) 2018 Jamboree

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//////////////////////////////////////////////////////////////////////////////*/
#ifndef ART_DETAIL_UNLOCK_GUARD_HPP_INCLUDED
#define ART_DETAIL_UNLOCK_GUARD_HPP_INCLUDED

namespace art
{
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
}

#endif