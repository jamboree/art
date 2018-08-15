/*//////////////////////////////////////////////////////////////////////////////
    Copyright (c) 2018 Jamboree

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
        auto initial_suspend() noexcept
        {
            struct awaiter
            {
                chained_coro*& _then;
                detail::chained_coro _chained;

                explicit awaiter(chained_coro*& t) noexcept : _then(t) {}

                bool await_ready() const noexcept
                {
                    return false;
                }

                void await_suspend(coroutine_handle<> coro) noexcept
                {
                    _chained.coro = coro;
                    _then = &_chained;
                }

                void await_resume() const noexcept {}
            };
            return awaiter{_then};
        }

        suspend_if final_suspend() noexcept
        {
            if (_ref)
            {
                coroutine_final_run(_then);
                _ref = nullptr;
                return true;
            }
            return false;
        }

        void finalize() noexcept
        {
            if (_ref)
            {
                *_ref = nullptr;
                coroutine_final_cancel(_then);
            }
        }

        void** _ref = nullptr;
        chained_coro* _then = nullptr;
        tag _tag = tag::pending;
    };

    template<class T>
    struct lazy_promise : lazy_promise_base
    {
        using val_t = wrap_reference_t<T>;

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

        ~lazy_promise()
        {
            _data.destroy(_tag);
        }

        storage<val_t> _data;
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
    };
}

namespace art
{
    template<class T = void>
    struct lazy_task
    {
        struct promise_type : detail::lazy_promise<T>
        {
            ~promise_type()
            {
                this->finalize();
            }

            lazy_task get_return_object()
            {
                return lazy_task(this);
            }
        };

        lazy_task() noexcept : _promise() {}

        lazy_task(lazy_task&& other) noexcept : _promise(other._promise)
        {
            other._promise = nullptr;
        }

        explicit lazy_task(promise_type* promise) noexcept : _promise(promise) {}

        lazy_task& operator=(lazy_task&& other) noexcept
        {
            if (_promise)
                release();
            _promise = other._promise;
            other._promise = nullptr;
            return *this;
        }

        auto operator co_await() noexcept
        {
            struct awaiter
            {
                promise_type*& _promise;
                detail::chained_coro _chained;

                explicit awaiter(promise_type*& p) noexcept : _promise(p) {}

                bool await_ready() const noexcept
                {
                    return _promise->_tag != detail::tag::pending;
                }

                void await_suspend(coroutine_handle<> coro) noexcept
                {
                    auto then = _promise->_then;
                    _chained.coro = coro;
                    _promise->_then = &_chained;
                    _promise->_ref = reinterpret_cast<void**>(&_promise);
                    coroutine_final_run(then);
                }

                T await_resume() const
                {
                    struct extract
                    {
                        promise_type*& p;

                        ~extract()
                        {
                            p = nullptr;
                        }
                    };
                    return extract{_promise}.p->get();
                }
            };
            return awaiter{_promise};
        }

        explicit operator bool() const noexcept
        {
            return !!_promise;
        }

        bool valid() const noexcept
        {
            return !!_promise;
        }

        void swap(lazy_task& other) noexcept
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

        ~lazy_task()
        {
            if (_promise)
                release();
        }

    private:
        void release() noexcept
        {
            coroutine_final_run(_promise->_then);
        }

        promise_type* _promise;
    };

    template<class T>
    inline void swap(lazy_task<T>& a, lazy_task<T>& b) noexcept
    {
        a.swap(b);
    }
}

#endif