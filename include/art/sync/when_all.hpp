/*//////////////////////////////////////////////////////////////////////////////
    Copyright (c) 2018 Jamboree

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//////////////////////////////////////////////////////////////////////////////*/
#ifndef ART_SYNC_WHEN_ALL_HPP_INCLUDED
#define ART_SYNC_WHEN_ALL_HPP_INCLUDED

#include <tuple>
#include <vector>
#include <art/task.hpp>
#include <art/detail/copy_or_move.hpp>

namespace art::detail
{
    template<class T>
    task<std::vector<T>> when_all_impl(std::vector<T> seq)
    {
        for (auto& elem : seq)
            co_await when_ready(get_awaiter(elem));
        co_return std::move(seq);
    }

    template<class... T, std::size_t... I>
    task<std::tuple<T...>> when_all_impl(std::tuple<T...> seq, std::index_sequence<I...>)
    {
        (co_await when_ready(get_awaiter(std::get<I>(seq))), ...);
        co_return std::move(seq);
    }
}

namespace art
{
    template<class InputIt>
    inline auto when_all(InputIt first, InputIt last) ->
        task<std::vector<typename std::iterator_traits<InputIt>::value_type>>
    {
        using task_t = typename std::iterator_traits<InputIt>::value_type;
        using seq_t = std::vector<task_t>;
        using iter = detail::copy_or_move_iter<InputIt, std::is_copy_constructible<task_t>::value>;
        return detail::when_all_impl(seq_t(iter::wrap(first), iter::wrap(last)));
    }

    template<class... Futures>
    inline auto when_all(Futures&&... futures) ->
        task<std::tuple<std::decay_t<Futures>...>>
    {
        using seq_t = std::tuple<std::decay_t<Futures>...>;
        std::make_index_sequence<sizeof...(Futures)> indices;
        return detail::when_all_impl(seq_t(detail::copy_or_move<Futures>(futures)...), indices);
    }
}

#endif