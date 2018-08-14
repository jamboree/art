/*//////////////////////////////////////////////////////////////////////////////
    Copyright (c) 2018 Jamboree

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//////////////////////////////////////////////////////////////////////////////*/
#ifndef ART_DETAIL_TASK_HPP_INCLUDED
#define ART_DETAIL_TASK_HPP_INCLUDED

#include <atomic>
#include <type_traits>
#include <art/core.hpp>
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

        coro_ts::suspend_never final_suspend() noexcept
        {
            return {};
        }
    };

    template<class T, class Base>
    struct promise_data : promise_base
    {
        using val_t = wrap_reference_t<T>;

        template<class U = T>
        void return_value(U&& u)
        {
            //_data->return_value(std::forward<U>(u));
            new(&_state->_data.value) val_t(std::forward<U>(u));
            _state->_tag = tag::value;
        }

        void unhandled_exception() noexcept
        {
            new(&_state->_data.exception) std::exception_ptr(std::current_exception());
            _state->_tag = tag::exception;
        }

        struct state : Base
        {
            T&& get()
            {
                if (Base::_tag == tag::exception)
                    std::rethrow_exception(_data.exception);
                return static_cast<T&&>(_data.value);
            }

            ~state()
            {
                _data.destroy(Base::_tag);
            }

            storage<val_t> _data;
        };

        state* _state;
    };

    template<class Base>
    struct promise_data<void, Base> : promise_base
    {
        void return_void() noexcept
        {
            _state->_tag = tag::value;
        }

        void unhandled_exception() noexcept
        {
            _state->_e = std::current_exception();
            _state->_tag = tag::exception;
        }

        struct state : Base
        {
            void get()
            {
                if (Base::_tag == tag::exception)
                    std::rethrow_exception(_e);
            }

            std::exception_ptr _e;
        };

        state* _state;
    };

    template<class Derived, class Promise>
    struct impl;

    template<template<class> class Task, class T, class Promise>
    struct impl<Task<T>, Promise>
    {
        struct promise_type : promise_data<T, Promise>
        {
            promise_type()
            {
                this->_state = new state;
            }

            ~promise_type()
            {
                if (!this->_state->finalize())
                    delete this->_state;
            }

            Task<T> get_return_object()
            {
                return Task<T>(this->_state);
            }
        };

        impl() noexcept : _state() {}

        impl(impl&& other) noexcept : _state(other._state)
        {
            other._state = nullptr;
        }

        impl& operator=(impl&& other) noexcept
        {
            if (_state)
                release();
            _state = other._state;
            other._state = nullptr;
            return *this;
        }

        ~impl()
        {
            if (_state)
                release();
        }

        explicit operator bool() const noexcept
        {
            return !!_state;
        }

        bool valid() const noexcept
        {
            return !!_state;
        }

        void swap(Task<T>& other) noexcept
        {
            std::swap(_state, other._state);
        }

        void reset() noexcept
        {
            if (_state)
            {
                release();
                _state = nullptr;
            }
        }

    protected:
        using state = typename promise_data<T, Promise>::state;

        explicit impl(state* s) noexcept : _state(s) {}

        void release() noexcept
        {
            if (_state->test_last())
                delete _state;
        }

        state* _state;
    };

    template<class ToTask, class FromTask>
    inline ToTask convert(FromTask t)
    {
        co_return co_await t;
    }
}

#endif