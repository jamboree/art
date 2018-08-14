/*//////////////////////////////////////////////////////////////////////////////
    Copyright (c) 2018 Jamboree

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//////////////////////////////////////////////////////////////////////////////*/
#ifndef ART_DETACHED_TASK_HPP_INCLUDED
#define ART_DETACHED_TASK_HPP_INCLUDED

#include <art/core.hpp>

namespace art
{
    struct detached_task
    {
        struct promise_type
        {
            detached_task get_return_object() noexcept { return {}; }

            coro_ts::suspend_never initial_suspend() noexcept
            {
                return {};
            }

            coro_ts::suspend_never final_suspend() noexcept
            {
                return {};
            }

            void return_void() noexcept {}

            void unhandled_exception() noexcept { std::terminate(); }
        };
    };
}

#endif