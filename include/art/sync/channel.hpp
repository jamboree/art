/*//////////////////////////////////////////////////////////////////////////////
    Copyright (c) 2018 Jamboree

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//////////////////////////////////////////////////////////////////////////////*/
#ifndef ART_SYNC_CHANNEL_HPP_INCLUDED
#define ART_SYNC_CHANNEL_HPP_INCLUDED

#include <new>
#include <atomic>
#include <optional>
#include <art/core.hpp>

namespace art
{
    template<class T>
    struct channel
    {
        explicit channel(executor& exe = default_executor()) noexcept : _exe(exe) {}

        ~channel() noexcept
        {
            if (auto p = _side.exchange(this, std::memory_order_acquire))
            {
                if (p != this)
                    static_cast<awaiter_base*>(p)->_coro.destroy();
            }
        }

        void close() noexcept
        {
            if (auto p = _side.exchange(this, std::memory_order_acquire))
            {
                if (p != this)
                    static_cast<awaiter_base*>(p)->resume();
            }
        }

        [[nodiscard]] auto push(T val)
        {
            return push(std::move(val), _exe);
        }

        [[nodiscard]] auto push(T val, executor& exe)
        {
            struct awaiter : awaiter_base
            {
                bool await_suspend(coroutine_handle<> coro)
                {
                    return this->do_suspend(coro, [](std::optional<T>& here, std::optional<T>& there)
                    {
                        there = std::move(here);
                    });
                }

                bool await_resume() const noexcept
                {
                    return !this->_data;
                }
            };
            return awaiter{{this, exe, {}, std::move(val)}};
        }

        [[nodiscard]] auto pop()
        {
            return pop(_exe);
        }

        [[nodiscard]] auto pop(executor& exe)
        {
            struct awaiter : awaiter_base
            {
                bool await_suspend(coroutine_handle<> coro)
                {
                    return this->do_suspend(coro, [](std::optional<T>& here, std::optional<T>& there)
                    {
                        here = std::move(there);
                    });
                }

                std::optional<T> await_resume()
                {
                    return std::move(this->_data);
                }
            };
            return awaiter{{this, exe, {}, {}}};
        }

    private:

        struct awaiter_base
        {
            channel* _self;
            executor& _exe;
            coroutine_handle<> _coro;
            std::optional<T> _data;

            bool await_ready() const noexcept
            {
                return false;
            }

            template<class F>
            bool do_suspend(coroutine_handle<> coro, F f)
            {
                _coro = coro;
                void* p = nullptr;
                if (_self->_side.compare_exchange_strong(p, this, std::memory_order_release, std::memory_order_acquire))
                    return true;
                if (p != _self)
                {
                    auto other = static_cast<awaiter_base*>(p);
                    if (_self->_side.compare_exchange_strong(p, nullptr, std::memory_order_relaxed))
                    {
                        f(_data, other->_data);
                        other->resume();
                    }
                }
                return false;
            }

            void resume()
            {
                _exe(_coro);
            }
        };

        executor& _exe;
        std::atomic<void*> _side{nullptr};
    };
}

#endif