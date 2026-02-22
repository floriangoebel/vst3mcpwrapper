#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <vector>

// Tests for the shutdown safety pattern used by the MCP server:
//   - shared_ptr<atomic<bool>> alive flag
//   - promise/future with wait_for timeout
// These tests verify the mechanics independently from the full server.

class ShutdownTest : public ::testing::Test {
protected:
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
};

// --- Alive flag tests ---

TEST_F(ShutdownTest, AliveStartsTrue) {
    EXPECT_TRUE(alive->load());
}

TEST_F(ShutdownTest, SetFalseCausesCheckToReturnFalse) {
    EXPECT_TRUE(alive->load());
    alive->store(false);
    EXPECT_FALSE(alive->load());
}

TEST_F(ShutdownTest, AliveIsAtomicAndThreadSafe) {
    // Multiple reader threads observe the flag while a writer sets it to false
    constexpr int kNumReaders = 8;
    std::atomic<bool> startBarrier{false};
    std::atomic<int> sawTrue{0};
    std::atomic<int> sawFalse{0};

    std::vector<std::thread> readers;
    readers.reserve(kNumReaders);
    for (int i = 0; i < kNumReaders; ++i) {
        readers.emplace_back([&]() {
            while (!startBarrier.load()) {} // spin until barrier
            for (int j = 0; j < 10000; ++j) {
                if (alive->load()) {
                    sawTrue.fetch_add(1);
                } else {
                    sawFalse.fetch_add(1);
                }
            }
        });
    }

    // Start all readers, then flip the flag partway through
    startBarrier.store(true);
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    alive->store(false);

    for (auto& t : readers) {
        t.join();
    }

    // Both true and false should have been observed (flag flipped mid-read)
    EXPECT_GT(sawTrue.load(), 0);
    EXPECT_GT(sawFalse.load(), 0);
    // Total reads should equal kNumReaders * 10000
    EXPECT_EQ(sawTrue.load() + sawFalse.load(), kNumReaders * 10000);
}

TEST_F(ShutdownTest, SharedPtrCopiesShareSameFlag) {
    // Simulates the pattern: lambda captures a copy of alive shared_ptr
    auto flagCopy = alive;
    EXPECT_TRUE(flagCopy->load());

    alive->store(false);
    // The copy sees the same atomic bool
    EXPECT_FALSE(flagCopy->load());
}

// --- Promise/future timeout tests ---

TEST_F(ShutdownTest, FutureTimesOutWhenPromiseNeverFulfilled) {
    std::promise<std::string> promise;
    auto future = promise.get_future();

    auto start = std::chrono::steady_clock::now();
    auto status = future.wait_for(std::chrono::milliseconds(100));
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(status, std::future_status::timeout);
    // Should have waited roughly 100ms (allow 50ms-500ms range)
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    EXPECT_GE(elapsedMs, 50);
    EXPECT_LE(elapsedMs, 500);

    // Fulfill the promise to avoid std::future_error on destruction
    promise.set_value("late");
}

TEST_F(ShutdownTest, FutureCompletesBeforeTimeout) {
    auto promise = std::make_shared<std::promise<std::string>>();
    auto future = promise->get_future();

    // Fulfill from another thread after a short delay
    std::thread worker([promise]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        promise->set_value("done");
    });

    auto status = future.wait_for(std::chrono::seconds(5));
    EXPECT_EQ(status, std::future_status::ready);
    EXPECT_EQ(future.get(), "done");

    worker.join();
}

TEST_F(ShutdownTest, AliveCheckPreventsWorkAfterShutdown) {
    // Simulates the dispatch pattern: check alive before doing work
    auto flag = alive;
    std::promise<std::string> promise;
    auto future = promise.get_future();

    // "Shutdown" before the worker runs
    alive->store(false);

    // Worker checks alive flag, returns early with shutdown message
    std::thread worker([flag, &promise]() {
        if (!flag->load()) {
            promise.set_value("Plugin is shutting down");
            return;
        }
        promise.set_value("did work");
    });

    auto status = future.wait_for(std::chrono::seconds(5));
    EXPECT_EQ(status, std::future_status::ready);
    EXPECT_EQ(future.get(), "Plugin is shutting down");

    worker.join();
}

TEST_F(ShutdownTest, ShutdownSequenceCompletesWithinBoundedTime) {
    // Simulates the full shutdown pattern:
    // 1. Set alive = false
    // 2. Outstanding dispatched work detects flag and completes early
    // 3. Future resolves within timeout
    auto flag = alive;
    auto promise = std::make_shared<std::promise<std::string>>();
    auto future = promise->get_future();

    // Simulate a dispatched block that checks alive periodically
    std::thread dispatched([flag, promise]() {
        // Simulate some setup delay
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (!flag->load()) {
            promise->set_value("shutting down");
            return;
        }
        promise->set_value("completed");
    });

    auto start = std::chrono::steady_clock::now();

    // Trigger shutdown
    alive->store(false);

    // Wait with the production timeout (5s) — should complete much faster
    auto status = future.wait_for(std::chrono::seconds(5));
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(status, std::future_status::ready);
    EXPECT_EQ(future.get(), "shutting down");

    // The whole sequence should complete well within the 10-second bound
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    EXPECT_LT(elapsedMs, 10000);
    // In practice, should be under 200ms
    EXPECT_LT(elapsedMs, 1000);

    dispatched.join();
}
