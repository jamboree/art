/*//////////////////////////////////////////////////////////////////////////////
    Copyright (c) 2019 Jamboree

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//////////////////////////////////////////////////////////////////////////////*/
#ifndef ART_LAZY_TASK_HPP_INCLUDED
#define ART_LAZY_TASK_HPP_INCLUDED

#include <art/core.hpp>
#include <art/detail/storage.hpp>

namespace art::detail
{
    struct lazy_promise_base
    {
        coro_ts::suspend_always initial_suspend() noexcept { return {}; }

        struct final_awaiter
        {
            bool await_ready() { return false; }
            void await_suspend(coroutine_handle<>) { _coro(); }
            void await_resume() {}

            coroutine_handle<> _coro;
        };

        final_awaiter final_suspend() noexcept
        {
            return {_coro};
        }

        coroutine_handle<> _coro;
    };

    template<class T>
    struct lazy_promise : lazy_promise_base
    {
        using val_t = wrap_reference_t<T>;

        ~lazy_promise()
        {
            _data.destroy(_tag);
        }

        template<class U = T>
        void return_value(U&& u)
        {
            new(&_data.value) val_t(std::forward<U>(u));
            _tag = tag::value;
        }

        void unhandled_exception() noexcept
        {
            new(&_data.exception) std::exception_ptr(std::current_exception());
            _tag = tag::exception;
        }

        T&& get()
        {
            if (_tag == tag::exception)
                std::rethrow_exception(_data.exception);
            return static_cast<T&&>(_data.value);
        }

        storage<val_t> _data;
        tag _tag{tag::pending};
    };

    template<>
    struct lazy_promise<void> : lazy_promise_base
    {
        void return_void() noexcept
        {
            _tag = tag::value;
        }

        void unhandled_exception() noexcept
        {
            _e = std::current_exception();
            _tag = tag::exception;
        }

        void get()
        {
            if (_tag == tag::exception)
                std::rethrow_exception(_e);
        }

        std::exception_ptr _e;
        tag _tag{tag::pending};
    };
}

namespace art
{
    template<class T>
    struct [[nodiscard]] lazy_task
    {
        struct promise_type : detail::lazy_promise<T>
        {
            lazy_task get_return_object() { return lazy_task{this}; }
        };

        lazy_task(lazy_task&& other) noexcept : _coro(other._coro)
        {
            other._coro = nullptr;
        }

        lazy_task& operator=(lazy_task&& other) noexcept
        {
            if (_coro)
                _coro.destroy();
            _coro = other._coro;
            other._coro = nullptr;
            return *this;
        }

        ~lazy_task()
        {
            if (_coro)
                _coro.destroy();
        }

        explicit operator bool() const noexcept
        {
            return !!_coro;
        }

        void swap(lazy_task other) noexcept
        {
            std::swap(_coro, other._coro);
        }

        void reset() noexcept
        {
            if (_coro)
            {
                _coro.destroy();
                _coro = nullptr;
            }
        }

        bool await_ready() { return false; }

        void await_suspend(coroutine_handle<> coro)
        {
            _coro.promise()._coro = coro;
            _coro();
        }

        T await_resume()
        {
            struct finalizer
            {
                coroutine_handle<promise_type> coro;
                ~finalizer() { coro.destroy(); }
            } fin{_coro};
            _coro = nullptr;
            return fin.coro.promise().get();
        }

    private:
        explicit lazy_task(promise_type* p) noexcept
          : _coro(coroutine_handle<promise_type>::from_promise(*p))
        {}

        coroutine_handle<promise_type> _coro;
    };
}

#endif