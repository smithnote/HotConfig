// Copyright (c) 2020 smithemail@163.com. All rights reserved.
// Author：smithemail@163.com
// Time：2020-08-22

#ifndef HOT_CONFIG_THREAD_POOL_H_
#define HOT_CONFIG_THREAD_POOL_H_

#include <vector>
#include <queue>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

namespace HotConfig {

class ThreadPool {
  public:
    ThreadPool(size_t threads_size = 4) : threads_size_(threads_size), running_(false) {}
    ~ThreadPool() {
        stop();
    }

    bool start() {
        if (running_) {
            return true;
        }
        running_ = true;
        for (size_t i = 0; i < threads_size_; ++i) {
            workers_.emplace_back(&ThreadPool::worker, this);
        }
        return true;
    }

    bool stop() {
        if (!running_) {
            return true;
        }
        running_ = false;
        condition_.notify_all();
        for (std::thread &worker: workers_) {
            worker.join();
        }
        return true;
    }
    
    template<class F, class... Args>
    bool push(F&& f, Args&&... args) {
        if (!running_) {
            return false;
        }
        auto task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        std::unique_lock<std::mutex> lock(queue_mutex_);
        tasks_.push(std::move(task));
        condition_.notify_one();
        return true;
    }

  private:
    bool worker() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                this->condition_.wait(lock, [&](){
                    return !running_ || !tasks_.empty();
                });
                if (!running_) {
                    return true;
                }
                task = std::move(tasks_.front());
                this->tasks_.pop();
            }
            task();
        }
        return true;
    }

  private:
    size_t threads_size_;
    std::atomic<bool> running_;
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
};

}

#endif 
