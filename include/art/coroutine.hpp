/*//////////////////////////////////////////////////////////////////////////////
    Copyright (c) 2018 Jamboree

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//////////////////////////////////////////////////////////////////////////////*/
#ifndef ART_COROUTINE_HPP_INCLUDED
#define ART_COROUTINE_HPP_INCLUDED

#include <art/detail/core.hpp>

namespace art
{
    struct coroutine : private coroutine_handle<>
    {
        using handle_type = coroutine_handle<>;

        using handle_type::operator bool;
        using handle_type::operator();
        using handle_type::resume;
        using handle_type::done;

        struct promise_type : detail::trivial_promise_base
        {
            coroutine get_return_object()
            {
                return coroutine(coroutine_handle<promise_type>::from_promise(*this));
            }
        };

        coroutine() noexcept : _ptr() {}

        explicit coroutine(handle_type handle) noexcept : _ptr(handle) {}

        coroutine(coroutine&& other) noexcept : _ptr(other._ptr)
        {
            other._ptr = nullptr;
        }

        coroutine& operator=(coroutine&& other) noexcept
        {
            reset(other._ptr);
            other._ptr = nullptr;
            return *this;
        }

        ~coroutine()
        {
            if (_ptr)
                _ptr.destroy();
        }

        void reset() noexcept
        {
            if (_ptr)
            {
                _ptr.destroy();
                _ptr = nullptr;
            }
        }

        void reset(handle_type handle) noexcept
        {
            if (_ptr)
                _ptr.destroy();
            _ptr = handle;
        }

        void swap(coroutine& other) noexcept
        {
            std::swap(_ptr, other._ptr);
        }

        handle_type handle() const noexcept
        {
            return _ptr;
        }

        handle_type detach() noexcept
        {
            auto handle = _ptr;
            _ptr = nullptr;
            return handle;
        }

    private:
        handle_type _ptr;
    };

    inline bool operator==(coroutine const& lhs, coroutine const& rhs)
    {
        return lhs.handle() == rhs.handle();
    }

    inline bool operator!=(coroutine const& lhs, coroutine const& rhs)
    {
        return lhs.handle() != rhs.handle();
    }
}

#endif