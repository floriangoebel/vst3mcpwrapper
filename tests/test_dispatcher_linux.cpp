#include <gtest/gtest.h>

#include "dispatcher.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace VST3MCPWrapper;

class DispatcherTest : public ::testing::Test {
protected:
    void SetUp() override {
        dispatcher_ = std::make_unique<MainThreadDispatcher>();
    }

    void TearDown() override {
        dispatcher_.reset();
    }

    std::unique_ptr<MainThreadDispatcher> dispatcher_;
};

// Test dispatch() posts a task and returns a future with the correct value
TEST_F(DispatcherTest, DispatchReturnsCorrectValue) {
    auto future = dispatcher_->dispatch<int>([]() { return 42; }, -1);
    ASSERT_EQ(future.wait_for(std::chrono::seconds(5)),
              std::future_status::ready);
    EXPECT_EQ(future.get(), 42);
}

// Test dispatch with string return type
TEST_F(DispatcherTest, DispatchReturnsStringValue) {
    auto future = dispatcher_->dispatch<std::string>(
        []() { return std::string("hello"); }, std::string("shutdown"));
    ASSERT_EQ(future.wait_for(std::chrono::seconds(5)),
              std::future_status::ready);
    EXPECT_EQ(future.get(), "hello");
}

// Test void dispatch
TEST_F(DispatcherTest, DispatchVoidCompletes) {
    std::atomic<bool> executed{false};
    auto future = dispatcher_->dispatch([&executed]() { executed = true; });
    ASSERT_EQ(future.wait_for(std::chrono::seconds(5)),
              std::future_status::ready);
    future.get(); // should not throw
    EXPECT_TRUE(executed.load());
}

// Test multiple sequential dispatches execute in order
TEST_F(DispatcherTest, SequentialDispatchesExecuteInOrder) {
    std::vector<int> order;
    std::mutex orderMutex;

    std::vector<std::future<void>> futures;
    for (int i = 0; i < 10; ++i) {
        futures.push_back(dispatcher_->dispatch([&order, &orderMutex, i]() {
            std::lock_guard<std::mutex> lock(orderMutex);
            order.push_back(i);
        }));
    }

    for (auto& f : futures) {
        ASSERT_EQ(f.wait_for(std::chrono::seconds(5)),
                  std::future_status::ready);
    }

    ASSERT_EQ(order.size(), 10u);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(order[i], i);
    }
}

// Test concurrent dispatches from multiple threads all complete
TEST_F(DispatcherTest, ConcurrentDispatchesAllComplete) {
    constexpr int kThreadCount = 8;
    constexpr int kDispatchesPerThread = 50;
    std::atomic<int> counter{0};

    std::vector<std::thread> threads;
    std::vector<std::future<void>> allFutures;
    std::mutex futuresMutex;

    for (int t = 0; t < kThreadCount; ++t) {
        threads.emplace_back([&]() {
            for (int d = 0; d < kDispatchesPerThread; ++d) {
                auto future =
                    dispatcher_->dispatch([&counter]() { counter++; });
                std::lock_guard<std::mutex> lock(futuresMutex);
                allFutures.push_back(std::move(future));
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    for (auto& f : allFutures) {
        ASSERT_EQ(f.wait_for(std::chrono::seconds(10)),
                  std::future_status::ready);
    }

    EXPECT_EQ(counter.load(), kThreadCount * kDispatchesPerThread);
}

// Test shutdown() causes isAlive() to return false
TEST_F(DispatcherTest, ShutdownSetsAliveToFalse) {
    EXPECT_TRUE(dispatcher_->isAlive());
    dispatcher_->shutdown();
    EXPECT_FALSE(dispatcher_->isAlive());
}

// Test dispatch() after shutdown returns the shutdown value without hanging
TEST_F(DispatcherTest, DispatchAfterShutdownReturnsShutdownValue) {
    dispatcher_->shutdown();

    auto future =
        dispatcher_->dispatch<int>([]() { return 42; }, -1);
    ASSERT_EQ(future.wait_for(std::chrono::seconds(5)),
              std::future_status::ready);
    EXPECT_EQ(future.get(), -1);
}

// Test void dispatch after shutdown still completes the future
TEST_F(DispatcherTest, VoidDispatchAfterShutdownCompletes) {
    dispatcher_->shutdown();

    std::atomic<bool> executed{false};
    auto future = dispatcher_->dispatch([&executed]() { executed = true; });
    ASSERT_EQ(future.wait_for(std::chrono::seconds(5)),
              std::future_status::ready);
    future.get(); // should not throw
    EXPECT_FALSE(executed.load()); // func should NOT have been called
}

// Test destructor joins the worker thread cleanly (no thread leak)
TEST_F(DispatcherTest, DestructorJoinsCleanly) {
    // Dispatch some tasks then destroy — should not hang or crash
    for (int i = 0; i < 5; ++i) {
        dispatcher_->dispatch<int>([i]() { return i; }, -1);
    }
    dispatcher_.reset(); // triggers destructor

    // If we reach this point, the destructor completed without hanging
    SUCCEED();
}

// Test that creating and destroying many dispatchers doesn't leak threads
TEST_F(DispatcherTest, RepeatedCreateDestroyNoLeak) {
    dispatcher_.reset(); // destroy the one from SetUp

    for (int i = 0; i < 20; ++i) {
        auto d = std::make_unique<MainThreadDispatcher>();
        auto future = d->dispatch<int>([]() { return 1; }, 0);
        ASSERT_EQ(future.wait_for(std::chrono::seconds(5)),
                  std::future_status::ready);
        EXPECT_EQ(future.get(), 1);
    }
    // If we reach here without hanging, threads are being joined properly
    SUCCEED();
}
