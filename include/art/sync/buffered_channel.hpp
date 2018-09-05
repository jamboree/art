/*//////////////////////////////////////////////////////////////////////////////
    Copyright (c) 2018 Jamboree

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//////////////////////////////////////////////////////////////////////////////*/
#ifndef ART_SYNC_BUFFERED_CHANNEL_HPP_INCLUDED
#define ART_SYNC_BUFFERED_CHANNEL_HPP_INCLUDED

#include <new>
#include <atomic>
#include <optional>
#include <algorithm>
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

        T&& get() noexcept
        {
            return std::move(data);
        }

        ~data_extactor()
        {
            data.~T();
        }
    };

    template<class T>
    struct opt_extactor
    {
        std::optional<T>& data;

        T&& get() noexcept
        {
            return std::move(*data);
        }

        ~opt_extactor()
        {
            data = std::nullopt;
        }
    };
}

namespace art
{
    template<class T>
    struct buffered_channel
    {
        explicit buffered_channel(std::size_t buf_size, executor& exe = default_executor())
          : _buf(buffer::create(buf_size)), _exe(exe)
        {}

        ~buffered_channel()
        {
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
                bool await_suspend(coroutine_handle<> coro)
                {
                    if (this->_self->do_push(this->_data))
                        return this->do_suspend(coro);
                    return false;
                }

                bool await_resume() const noexcept
                {
                    return !this->_data;
                }
            };
            return awaiter{{this, {}, std::move(val)}};
        }

        [[nodiscard]] auto pop()
        {
            struct awaiter : awaiter_base
            {
                bool await_suspend(coroutine_handle<> coro)
                {
                    if (this->_self->do_pop(this->_data))
                        return this->do_suspend(coro);
                    return false;
                }

                std::optional<T> await_resume()
                {
                    return std::move(this->_data);
                }
            };
            return awaiter{{this, {}, {}}};
        }

    private:
        using buffer = detail::channel_buffer<T>;

        bool do_push(std::optional<T>& data)
        {
            _buf->_lock.lock();
            if (auto side = _side.load(std::memory_order_relaxed))
            {
                if (side == this || !_side.compare_exchange_strong(side, nullptr, std::memory_order_relaxed))
                {
                    _buf->_lock.unlock();
                    return false;
                }
                _buf->_lock.unlock();
                auto other = static_cast<awaiter_base*>(side);
                other->_data = detail::opt_extactor<T>{data}.get();
                _exe(other->_coro);
                return false;
            }
            if (auto idx = _buf->try_push())
            {
                unlock_guard unlock(_buf->_lock);
                new(_buf->data() + (idx - 1)) T(detail::opt_extactor<T>{data}.get());
                ++_buf->_used;
                return false;
            }
            return true;
        }

        bool do_pop(std::optional<T>& data)
        {
            _buf->_lock.lock();
            if (auto idx = _buf->try_pop())
            {
                if (auto other = try_notify_push())
                {
                    data = std::exchange(_buf->data()[idx - 1], detail::opt_extactor<T>{other->_data}.get());
                    _exe(other->_coro);
                    return false;
                }
                --_buf->_used;
                unlock_guard unlock(_buf->_lock);
                data = detail::data_extactor<T>{_buf->data()[idx - 1]}.get();
                return false;
            }
            // Degenerated case where buffer size is 0.
            if (auto other = try_notify_push())
            {
                data = std::move(other->_data);
                _exe(other->_coro);
                return false;
            }
            return true;
        }

        struct awaiter_base
        {
            buffered_channel* _self;
            coroutine_handle<> _coro;
            std::optional<T> _data;

            bool await_ready() const noexcept
            {
                return false;
            }

            bool do_suspend(coroutine_handle<> coro) noexcept
            {
                _coro = coro;
                void* p = nullptr;
                bool ret = _self->_side.compare_exchange_strong(p, this, std::memory_order_release, std::memory_order_acquire);
                _self->_buf->_lock.unlock();
                return ret;
            }
        };

        awaiter_base* try_notify_push() noexcept
        {
            if (auto side = _side.load(std::memory_order_relaxed))
            {
                if (side != this && _side.compare_exchange_strong(side, nullptr, std::memory_order_relaxed))
                {
                    _buf->_lock.unlock();
                    return static_cast<awaiter_base*>(side);
                }
            }
            return nullptr;
        }

        buffer* _buf;
        executor& _exe;
        std::atomic<void*> _side{nullptr};
    };
}

#endif