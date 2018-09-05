/*//////////////////////////////////////////////////////////////////////////////
    Copyright (c) 2018 Jamboree

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//////////////////////////////////////////////////////////////////////////////*/
#ifndef ART_SYNC_WORK_GROUP_HPP_INCLUDED
#define ART_SYNC_WORK_GROUP_HPP_INCLUDED

#include <atomic>
#include <memory>
#include <cassert>
#include <art/core.hpp>

namespace art
{
    class work_group
    {
        std::atomic<void*> _then{nullptr};
        std::atomic<unsigned> _work_count{0};

        void push_work() noexcept
        {
            _work_count.fetch_add(1u, std::memory_order_release);
        }

        void pop_work() noexcept
        {
            if (_work_count.fetch_sub(1u, std::memory_order_relaxed) == 1u)
            {
                if (auto then = _then.exchange(this, std::memory_order_acquire))
                    coroutine_handle<>::from_address(then)();
            }
        }

        struct work_deleter
        {
            void operator()(work_group* group) const noexcept
            {
                group->pop_work();
            }
        };

    public:
        class work
        {
            std::unique_ptr<work_group, work_deleter> _group;

        public:
            work() = default;

            explicit work(work_group& group) noexcept : _group(&group)
            {
                group.push_work();
            }
        };

        ~work_group()
        {
            assert(await_ready() && "pending work in work_group");
        }

        work create()
        {
            return work(*this);
        }

        bool await_ready() noexcept
        {
            return !_work_count.load(std::memory_order_relaxed);
        }

        bool await_suspend(coroutine_handle<> cb) noexcept
        {
            if (_then.exchange(cb.address(), std::memory_order_release))
                return false;
            return true;
        }

        void await_resume() noexcept
        {
            _then.store(nullptr, std::memory_order_relaxed);
        }
    };
}

#endif