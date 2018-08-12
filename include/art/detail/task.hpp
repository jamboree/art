/*//////////////////////////////////////////////////////////////////////////////
    Copyright (c) 2018 Jamboree

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//////////////////////////////////////////////////////////////////////////////*/
#ifndef ART_DETAIL_TASK_HPP_INCLUDED
#define ART_DETAIL_TASK_HPP_INCLUDED

#include <atomic>
#include <type_traits>
#include <art/detail/core.hpp>
#include <art/detail/storage.hpp>

namespace art
{
    template<class T = void>
    struct task;

    template<class T = void>
    struct shared_task;
}

namespace art::detail
{
    struct promise_base
    {
        coro_ts::suspend_never initial_suspend() noexcept
        {
            return {};
        }
    };

    template<class T, class Base>
    struct promise_data : Base
    {
        using val_t = wrap_reference_t<T>;

        template<class U = T>
        void return_value(U&& u)
        {
            new(&_data.value) val_t(std::forward<U>(u));
            Base::_tag = tag::value;
        }

        void unhandled_exception() noexcept
        {
            new(&_data.exception) std::exception_ptr(std::current_exception());
            Base::_tag = tag::exception;
        }

        T&& get()
        {
            if (Base::_tag == tag::exception)
                std::rethrow_exception(_data.exception);
            return static_cast<T&&>(_data.value);
        }

        ~promise_data()
        {
            _data.destroy(Base::_tag);
        }

        storage<val_t> _data;
    };

    template<class Base>
    struct promise_data<void, Base> : Base
    {
        void return_void() noexcept
        {
            Base::_tag = tag::value;
        }

        void unhandled_exception() noexcept
        {
            _e = std::current_exception();
            Base::_tag = tag::exception;
        }

        void get()
        {
            if (Base::_tag == tag::exception)
                std::rethrow_exception(_e);
        }

        std::exception_ptr _e;
    };

    template<class Derived, class Promise>
    struct impl;

    template<template<class> class Task, class T, class Promise>
    struct impl<Task<T>, Promise>
    {
        struct promise_type : promise_data<T, Promise>
        {
            Task<T> get_return_object()
            {
                return Task<T>(this);
            }
        };

        impl() noexcept : _promise() {}

        impl(impl&& other) noexcept : _promise(other._promise)
        {
            other._promise = nullptr;
        }

        impl& operator=(impl&& other) noexcept
        {
            if (_promise)
                release();
            _promise = other._promise;
            other._promise = nullptr;
            return *this;
        }

        explicit impl(promise_type* promise) noexcept : _promise(promise) {}

        ~impl()
        {
            if (_promise)
                release();
        }

        explicit operator bool() const noexcept
        {
            return !!_promise;
        }

        bool valid() const noexcept
        {
            return !!_promise;
        }

        void swap(Task<T>& other) noexcept
        {
            std::swap(_promise, other._promise);
        }

        void reset() noexcept
        {
            if (_promise)
            {
                release();
                _promise = nullptr;
            }
        }

    protected:
        void release() noexcept
        {
            if (_promise->test_last())
                coroutine_handle<promise_type>::from_promise(*_promise).destroy();
        }

        promise_type* _promise;
    };

    template<class ToTask, class FromTask>
    inline ToTask convert(FromTask t)
    {
        co_return co_await t;
    }
}

#endif