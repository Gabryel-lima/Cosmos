#pragma once
// src/physics/ThreadPool.hpp — Pool de threads de tamanho fixo mínimo para tarefas de física.

#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <queue>
#include <vector>
#include <future>
#include <atomic>
#include <algorithm>
#include <type_traits>

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads = 0) {
        size_t n = (num_threads == 0)
            ? std::max(1u, std::thread::hardware_concurrency())
            : num_threads;
        for (size_t i = 0; i < n; ++i)
            workers_.emplace_back([this] { worker_loop(); });
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) t.join();
    }

    /// Enviar uma tarefa, retorna um future com o resultado.
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using RetT = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<RetT()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<RetT> fut = task->get_future();
        {
            std::unique_lock<std::mutex> lock(mutex_);
            queue_.push([task]() { (*task)(); });
        }
        cv_.notify_one();
        return fut;
    }

    size_t num_threads() const { return workers_.size(); }

private:
    void worker_loop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
                if (stop_ && queue_.empty()) return;
                task = std::move(queue_.front());
                queue_.pop();
            }
            task();
        }
    }

    std::vector<std::thread>         workers_;
    std::queue<std::function<void()>> queue_;
    std::mutex                        mutex_;
    std::condition_variable           cv_;
    bool                              stop_ = false;
};
