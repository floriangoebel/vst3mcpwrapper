#include "dispatcher.h"

namespace VST3MCPWrapper {

MainThreadDispatcher::MainThreadDispatcher()
    : alive_(std::make_shared<std::atomic<bool>>(true))
{
    workerThread_ = std::thread([this]() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                queueCV_.wait(lock, [this]() {
                    return stopped_ || !taskQueue_.empty();
                });
                if (stopped_ && taskQueue_.empty())
                    return;
                task = std::move(taskQueue_.front());
                taskQueue_.pop();
            }
            task();
        }
    });
}

MainThreadDispatcher::~MainThreadDispatcher() {
    shutdown();
    if (workerThread_.joinable())
        workerThread_.join();
}

void MainThreadDispatcher::shutdown() {
    *alive_ = false;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        stopped_ = true;
    }
    queueCV_.notify_one();
}

void MainThreadDispatcher::postImpl(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        taskQueue_.push(std::move(task));
    }
    queueCV_.notify_one();
}

} // namespace VST3MCPWrapper
