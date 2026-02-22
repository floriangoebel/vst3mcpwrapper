#pragma once

#include <atomic>
#include <functional>
#include <future>
#include <memory>

#ifndef __APPLE__
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#endif

namespace VST3MCPWrapper {

// Platform-independent thread dispatch abstraction.
// On macOS: uses dispatch_async(dispatch_get_main_queue())
// On Linux: uses a dedicated worker thread with a task queue
//
// Encapsulates the alive flag + promise/future pattern used by MCP tool
// handlers for load_plugin and unload_plugin.
class MainThreadDispatcher {
public:
    MainThreadDispatcher();
    ~MainThreadDispatcher();

    MainThreadDispatcher(const MainThreadDispatcher&) = delete;
    MainThreadDispatcher& operator=(const MainThreadDispatcher&) = delete;

    // Dispatch a callable that returns R to the dispatch thread.
    // If the dispatcher has been shut down, the future is immediately
    // fulfilled with shutdownValue instead of invoking func.
    template<typename R>
    std::future<R> dispatch(std::function<R()> func, R shutdownValue) {
        auto promise = std::make_shared<std::promise<R>>();
        auto future = promise->get_future();
        auto flag = alive_;
        postImpl([promise, func = std::move(func), flag,
                  shutdownValue = std::move(shutdownValue)]() {
            if (!*flag) {
                promise->set_value(std::move(shutdownValue));
                return;
            }
            try {
                promise->set_value(func());
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        });
        return future;
    }

    // Dispatch a void callable to the dispatch thread.
    // If the dispatcher has been shut down, the callable is skipped
    // but the future is still fulfilled.
    std::future<void> dispatch(std::function<void()> func) {
        auto promise = std::make_shared<std::promise<void>>();
        auto future = promise->get_future();
        auto flag = alive_;
        postImpl([promise, func = std::move(func), flag]() {
            if (*flag) {
                try {
                    func();
                } catch (...) {
                    promise->set_exception(std::current_exception());
                    return;
                }
            }
            promise->set_value();
        });
        return future;
    }

    // Signal shutdown. Prevents subsequent dispatched tasks from executing.
    void shutdown();

    // Check if the dispatcher is still alive (not shut down).
    bool isAlive() const { return *alive_; }

private:
    // Platform-specific: post a task to the dispatch thread.
    // On macOS: dispatch_async(dispatch_get_main_queue())
    // On Linux: enqueue to the worker thread
    void postImpl(std::function<void()> task);

    std::shared_ptr<std::atomic<bool>> alive_;

#ifndef __APPLE__
    std::thread workerThread_;
    std::mutex queueMutex_;
    std::condition_variable queueCV_;
    std::queue<std::function<void()>> taskQueue_;
    bool stopped_ = false;
#endif
};

} // namespace VST3MCPWrapper
