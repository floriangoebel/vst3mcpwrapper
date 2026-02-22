#include "dispatcher.h"

#include <dispatch/dispatch.h>

namespace VST3MCPWrapper {

MainThreadDispatcher::MainThreadDispatcher()
    : alive_(std::make_shared<std::atomic<bool>>(true)) {}

MainThreadDispatcher::~MainThreadDispatcher() {
    shutdown();
}

void MainThreadDispatcher::shutdown() {
    *alive_ = false;
}

void MainThreadDispatcher::postImpl(std::function<void()> task) {
    auto sharedTask = std::make_shared<std::function<void()>>(std::move(task));
    dispatch_async(dispatch_get_main_queue(), ^{
        (*sharedTask)();
    });
}

} // namespace VST3MCPWrapper
