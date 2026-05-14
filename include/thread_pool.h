#ifndef THREAD_POOL_H_
#define THREAD_POOL_H_

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
public:
    explicit ThreadPool(size_t thread_num) : running_(true) {
        for (size_t i = 0; i < thread_num; i++) {
            workers_.emplace_back([this] {
                for (;;) {
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        cv_.wait(lock,
                                 [this] { return !running_ || !tasks_.empty(); });

                        if (!running_ && tasks_.empty()) {
                            return;
                        }

                        task = std::move(this->tasks_.front());
                        tasks_.pop();
                    }

                    task();
                }
            });
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template<typename T>
    void EnQueue(T&& f) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            tasks_.emplace(std::forward<T>(f));
        }

        cv_.notify_one();
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            running_ = false;
        }

        cv_.notify_all();
        for (auto& worker : workers_) {
            worker.join();
        }
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()> > tasks_;

    std::mutex mutex_;
    std::condition_variable cv_;

    bool running_;
};

#endif // !THREAD_POOL_H_
