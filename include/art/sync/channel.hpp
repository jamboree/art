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
#include <art/detail/spinlock.hpp>
#include <art/detail/unlock_guard.hpp>

namespace art::detail
{
    struct buffer_head
    {
        spinlock _lock;
        std::size_t _used;
        std::size_t _head;
        std::size_t _size;

        explicit buffer_head(std::size_t size) : _used(), _head(), _size(size) {}

        std::size_t try_push() noexcept
        {
            if (_used == _size)
                return 0;
            return (_head + _used) % _size + 1;
        }

        std::size_t try_pop() noexcept
        {
            if (!_used)
                return 0;
            auto curr = _head + 1;
            _head = curr % _size;
            return curr;
        }
    };

    template<class T>
    struct alignas(std::max(alignof(T), alignof(buffer_head))) channel_buffer : buffer_head
    {
        using buffer_head::buffer_head;

        T* data() noexcept
        {
            auto p = reinterpret_cast<char*>(this) + sizeof(channel_buffer);
            return reinterpret_cast<T*>(p);
        }

        static channel_buffer* create(std::size_t size)
        {
            if (!size)
                return nullptr;
            auto p = ::operator new(sizeof(channel_buffer) + size * sizeof(T), std::align_val_t(alignof(channel_buffer)));
            return new(p) channel_buffer(size);
        }

        void destroy() noexcept
        {
            while (_used)
            {
                auto curr = _head;
                data()[curr].~T();
                --_used;
                _head = (curr + 1) % _size;
            }
            ::operator delete(this, std::align_val_t(alignof(channel_buffer)));
        }
    };

    template<class T>
    struct data_extactor
    {
        T& data;

        ~data_extactor()
        {
            data.~T();
        }
    };
}

namespace art
{
    template<class T>
    struct channel
    {
        explicit channel(executor& exe = default_executor()) noexcept : _buf(), _exe(exe) {}

        explicit channel(std::size_t buf_size, executor& exe = default_executor())
          : _buf(buffer::create(buf_size)), _exe(exe)
        {}

        ~channel()
        {
            if (_buf)
                _buf->destroy();
        }

        void close() noexcept
        {
            if (auto p = _side.exchange(this, std::memory_order_acquire))
            {
                if (p != this)
                    _exe(static_cast<awaiter_base*>(p)->_coro);
            }
        }

        [[nodiscard]] auto push(T val)
        {
            struct awaiter : awaiter_base
            {
                bool await_suspend(coroutine_handle<> coro) noexcept
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
            if (_buf)
            {
                _buf->_lock.lock();
                if (auto side = _side.load(std::memory_order_relaxed))
                {
                    if (side == this || !_side.compare_exchange_strong(side, nullptr, std::memory_order_relaxed))
                    {
                        _buf->_lock.unlock();
                        return awaiter{{nullptr, {}, std::move(val)}};
                    }
                    _buf->_lock.unlock();
                    auto other = static_cast<awaiter_base*>(side);
                    other->_data = std::move(val);
                    _exe(other->_coro);
                    return awaiter{};
                }
                unlock_guard unlock(_buf->_lock);
                if (auto idx = _buf->try_push())
                {
                    new(_buf->data() + (idx - 1)) T(std::move(val));
                    ++_buf->_used;
                    return awaiter{};
                }
            }
            return awaiter{{this, {}, std::move(val)}};
        }

        [[nodiscard]] auto pop()
        {
            struct awaiter : awaiter_base
            {
                bool await_suspend(coroutine_handle<> coro) noexcept
                {
                    return this->do_suspend(coro, [](std::optional<T>& here, std::optional<T>& there)
                    {
                        here = std::move(there);
                    });
                }

                std::optional<T> await_resume() noexcept
                {
                    return std::move(this->_data);
                }
            };
            if (_buf)
            {
                _buf->_lock.lock();
                if (auto idx = _buf->try_pop())
                {
                    if (auto side = _side.load(std::memory_order_relaxed))
                    {
                        if (side != this && _side.compare_exchange_strong(side, nullptr, std::memory_order_relaxed))
                        {
                            _buf->_lock.unlock();
                            auto other = static_cast<awaiter_base*>(side);
                            T tmp = std::exchange(_buf->data()[idx - 1], *other->_data);
                            other->_data = std::nullopt;
                            _exe(other->_coro);
                            return awaiter{{nullptr, {}, std::move(tmp)}};
                        }
                    }
                    --_buf->_used;
                    unlock_guard unlock(_buf->_lock);
                    detail::data_extactor<T> ex{_buf->data()[idx - 1]};
                    return awaiter{{nullptr, {}, std::move(ex.data)}};
                }
                _buf->_lock.unlock();
            }
            return awaiter{{this, {}, {}}};
        }

    private:
        using buffer = detail::channel_buffer<T>;

        struct awaiter_base
        {
            channel* _self;
            coroutine_handle<> _coro;
            std::optional<T> _data;

            bool await_ready() const noexcept
            {
                return !_self;
            }

            template<class F>
            bool do_suspend(coroutine_handle<> coro, F f) noexcept
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
                        _self->_exe(other->_coro);
                    }
                }
                return false;
            }
        };

        buffer* _buf;
        executor& _exe;
        std::atomic<void*> _side{nullptr};
    };
}

#endif