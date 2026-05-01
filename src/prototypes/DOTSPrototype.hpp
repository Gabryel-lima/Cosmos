#pragma once
// src/prototypes/DOTSPrototype.hpp
// Protótipo minimalista: SoA + ThreadPool + parallel_for

#include <vector>
#include <cstddef>
#include <functional>
#include <thread>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <future>
#include <type_traits>
#include <atomic>
#include <algorithm>
#include <stdexcept>

namespace dots_prototype {

struct ParticleSoA {
    std::vector<float> x, y, z;
    std::vector<float> vx, vy, vz;
    std::vector<float> mass;

    void resize(std::size_t n) {
        x.resize(n); y.resize(n); z.resize(n);
        vx.resize(n); vy.resize(n); vz.resize(n);
        mass.resize(n);
    }

    std::size_t size() const { return x.size(); }
};

class ThreadPool {
public:
    explicit ThreadPool(std::size_t threads = std::thread::hardware_concurrency()) {
        stop_ = false;
        std::size_t n = std::max<std::size_t>(1, threads);
        for (std::size_t i = 0; i < n; ++i) {
            workers_.emplace_back([this]() {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex_);
                        this->cond_.wait(lock, [this] { return this->stop_ || !this->tasks_.empty(); });
                        if (this->stop_ && this->tasks_.empty()) return;
                        task = std::move(this->tasks_.front());
                        this->tasks_.pop_front();
                    }
                    task();
                }
            });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        cond_.notify_all();
        for (auto &t : workers_) if (t.joinable()) t.join();
    }

    template<class F, class... Args>
    auto submit(F&& f, Args&&... args) -> std::future<typename std::invoke_result_t<F, Args...>> {
        using return_type = typename std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<return_type()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (stop_) throw std::runtime_error("submit on stopped ThreadPool");
            tasks_.emplace_back([task]() { (*task)(); });
        }
        cond_.notify_one();
        return res;
    }

    // Simple parallel_for that chunks range and waits for completion
    void parallel_for(std::size_t begin, std::size_t end, std::function<void(std::size_t)> func, std::size_t grain = 64) {
        if (begin >= end) return;
        std::size_t total = end - begin;
        std::size_t nworkers = std::max<std::size_t>(1, workers_.size());
        std::size_t chunk = std::max<std::size_t>(grain, (total + nworkers - 1) / nworkers);
        std::vector<std::future<void>> futures;
        for (std::size_t off = begin; off < end; off += chunk) {
            std::size_t b = off;
            std::size_t e = std::min(end, off + chunk);
            futures.push_back(submit([b, e, &func]() {
                for (std::size_t i = b; i < e; ++i) func(i);
            }));
        }
        for (auto &f : futures) f.get();
    }

private:
    std::vector<std::thread> workers_;
    std::deque<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable cond_;
    bool stop_ = false;
};

void run_prototype(int argc, char** argv);

} // namespace dots_prototype
