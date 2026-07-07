#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std;

namespace btos {

class ThreadPool {
  public:

    explicit ThreadPool(unsigned n_threads, std::size_t capacity = 1024)
        : capacity_(capacity == 0 ? 1 : capacity) {
        if (n_threads == 0) throw std::invalid_argument("ThreadPool: n_threads must be > 0");
        workers_.reserve(n_threads);
        for (unsigned i = 0; i < n_threads; ++i)
            workers_.emplace_back([this](std::stop_token st) { worker(st); });
    }

    ~ThreadPool() {
        {
            std::lock_guard lk(mu_);
            stopping_ = true;
        }
        cv_not_empty_.notify_all();
        cv_not_full_.notify_all();
        workers_.clear();
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void submit(std::function<void()> task) {
        std::unique_lock lk(mu_);
        cv_not_full_.wait(lk, [this] { return queue_.size() < capacity_ || stopping_; });
        if (stopping_) throw std::runtime_error("ThreadPool: submit after shutdown");
        queue_.push_back(std::move(task));
        ++pending_;
        cv_not_empty_.notify_one();
    }

    void wait_idle() {
        std::unique_lock lk(mu_);
        cv_idle_.wait(lk, [this] { return pending_ == 0; });
    }

    [[nodiscard]] unsigned size() const { return static_cast<unsigned>(workers_.size()); }

  private:
    void worker(std::stop_token st) {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock lk(mu_);
                cv_not_empty_.wait(lk, [this, &st] {
                    return !queue_.empty() || stopping_ || st.stop_requested();
                });
                if (queue_.empty()) return;
                task = std::move(queue_.front());
                queue_.pop_front();
                cv_not_full_.notify_one();
            }
            task();
            {
                std::lock_guard lk(mu_);
                --pending_;
                if (pending_ == 0) cv_idle_.notify_all();
            }
        }
    }

    std::mutex mu_;
    std::condition_variable cv_not_empty_, cv_not_full_, cv_idle_;
    std::deque<std::function<void()>> queue_;
    std::size_t capacity_;
    std::size_t pending_{0};
    bool stopping_{false};
    std::vector<std::jthread> workers_;
};

}
