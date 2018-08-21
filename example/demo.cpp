#include <iostream>
#include <art/task.hpp>
#include <art/shared_task.hpp>
#include <art/blocking.hpp>
#include <art/sync/when_any.hpp>
#include <art/sync/when_all.hpp>
#include <art/sync/channel.hpp>
#include <art/sync/mutex.hpp>

art::task<int> stall(art::coroutine_handle<>& ret)
{
    co_await art::suspend([&](art::coroutine_handle<> c) { ret = c; });
    co_return 0;
}

art::task<int> inc(art::task<int> t)
{
    co_return (co_await t) + 1;
}

struct Resource
{
    ~Resource()
    {
        std::cout << "Resource is released";
    }
};

art::task<> subtask(art::task<int> t)
{
    Resource res;
    co_await t;
}

art::task<> writer(art::channel<int>& ch)
{
    for (int i = 0; i != 5; ++i)
    {
        co_await ch.push(i);
        std::cout << "pushed: " << i << "\n";
    }
    ch.close();
}

art::task<> reader(art::channel<int>& ch)
{
    while (auto v = co_await ch.pop())
    {
        std::cout << "poped: " << *v << "\n";
    }
}

int main()
{
    {
        // This is a long chain of tasks, for testing if the implementation
        // avoids stack-overflow caused by recursion.
        std::cout << "[task-chaining]\n";
        art::coroutine_handle<> c;
        auto t = stall(c);
        for (int i = 0; i != 65536; ++i)
            t = inc(std::move(t));
        c();
        std::cout << "ans: " << get(t);
        std::cout << "\n------------\n";
    }
    {
        // Destroy the coroutine to test if the implementation
        // can safely cancel the task and its dependants.
        // Resource should be released.
        std::cout << "[task-cancelling]\n";
        art::coroutine_handle<> c;
        subtask(stall(c));
        c.destroy();
        std::cout << "\n------------\n";
    }
    {
        // Waiting a cancelled task throws an exception.
        std::cout << "[wait-cancelled]\n";
        try
        {
            art::coroutine_handle<> c;
            auto t = stall(c);
            c.destroy();
            art::wait(t);
        }
        catch (const std::exception& e)
        {
            std::cout << e.what();
        }
        std::cout << "\n------------\n";
    }
    {
        std::cout << "[timed-wait]\n";
        art::coroutine_handle<> c;
        auto t = stall(c);
        // Should timeout.
        if (!wait_for(t, std::chrono::milliseconds(10)))
            std::cout << "timeout\n";
        c();
        std::cout << "ans: " << get(t);
        std::cout << "\n------------\n";
    }
    {
        // By default, channel is unbuffered. To use buffering,
        // simply specify the number in the ctor.
        std::cout << "[channel]\n";
        art::channel<int> ch;
        writer(ch);
        reader(ch);
        std::cout << "\n------------\n";
    }
}