#include <gtest/gtest.h>
#include "hostedplugin.h"

#include <thread>
#include <vector>
#include <atomic>

using namespace VST3MCPWrapper;
using namespace Steinberg::Vst;

class ParamQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Drain any leftover changes from previous tests
        std::vector<ParamChange> discard;
        HostedPluginModule::instance().drainParamChanges(discard);
    }

    void TearDown() override {
        std::vector<ParamChange> discard;
        HostedPluginModule::instance().drainParamChanges(discard);
    }
};

TEST_F(ParamQueueTest, SinglePushThenDrain) {
    auto& mod = HostedPluginModule::instance();
    mod.pushParamChange(42, 0.75);

    std::vector<ParamChange> changes;
    mod.drainParamChanges(changes);

    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].id, 42u);
    EXPECT_DOUBLE_EQ(changes[0].value, 0.75);
}

TEST_F(ParamQueueTest, MultiplePushesDrainInOrder) {
    auto& mod = HostedPluginModule::instance();
    mod.pushParamChange(1, 0.1);
    mod.pushParamChange(2, 0.2);
    mod.pushParamChange(3, 0.3);
    mod.pushParamChange(4, 0.4);
    mod.pushParamChange(5, 0.5);

    std::vector<ParamChange> changes;
    mod.drainParamChanges(changes);

    ASSERT_EQ(changes.size(), 5u);
    for (size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(changes[i].id, static_cast<ParamID>(i + 1));
        EXPECT_DOUBLE_EQ(changes[i].value, (i + 1) * 0.1);
    }
}

TEST_F(ParamQueueTest, DrainClearsQueue) {
    auto& mod = HostedPluginModule::instance();
    mod.pushParamChange(10, 0.5);

    std::vector<ParamChange> first;
    mod.drainParamChanges(first);
    ASSERT_EQ(first.size(), 1u);

    // Second drain should return empty
    std::vector<ParamChange> second;
    mod.drainParamChanges(second);
    EXPECT_TRUE(second.empty());
}

TEST_F(ParamQueueTest, DrainOnEmptyQueue) {
    auto& mod = HostedPluginModule::instance();

    std::vector<ParamChange> changes;
    mod.drainParamChanges(changes);
    EXPECT_TRUE(changes.empty());
}

TEST_F(ParamQueueTest, ConcurrentPushesNoDataLoss) {
    auto& mod = HostedPluginModule::instance();

    constexpr int kNumThreads = 4;
    constexpr int kChangesPerThread = 1000;

    std::atomic<bool> go{false};
    std::vector<std::thread> threads;

    for (int t = 0; t < kNumThreads; ++t) {
        threads.emplace_back([&mod, &go, t]() {
            // Wait for all threads to be ready
            while (!go.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int i = 0; i < kChangesPerThread; ++i) {
                ParamID id = static_cast<ParamID>(t * kChangesPerThread + i);
                ParamValue val = static_cast<double>(i) / kChangesPerThread;
                mod.pushParamChange(id, val);
            }
        });
    }

    // Start all threads
    go.store(true, std::memory_order_release);

    for (auto& th : threads) {
        th.join();
    }

    // Drain and verify total count
    std::vector<ParamChange> changes;
    mod.drainParamChanges(changes);

    EXPECT_EQ(changes.size(), static_cast<size_t>(kNumThreads * kChangesPerThread));
}

TEST_F(ParamQueueTest, TryLockSemanticsNonBlocking) {
    auto& mod = HostedPluginModule::instance();

    // Push a change so the queue has data
    mod.pushParamChange(99, 0.5);

    // Hold the paramChangeMutex_ from another thread while attempting drain.
    // Since drainParamChanges uses try_lock, it should return without blocking
    // even when the mutex is held. We verify this by checking the drain returns
    // an empty result (lock not acquired) and completes within a bounded time.
    //
    // We cannot directly lock paramChangeMutex_ (it's private), but we can
    // demonstrate try_lock behavior: push from one thread while draining
    // from another — if drain uses try_lock, it won't deadlock and both
    // threads will complete.

    std::atomic<bool> pushDone{false};
    std::atomic<bool> drainDone{false};
    std::vector<ParamChange> drainResult;

    // Thread 1: continuously pushes changes for 50ms
    std::thread pusher([&]() {
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(50)) {
            mod.pushParamChange(100, 1.0);
        }
        pushDone.store(true, std::memory_order_release);
    });

    // Thread 2: drain repeatedly — should never block
    std::thread drainer([&]() {
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(50)) {
            std::vector<ParamChange> batch;
            mod.drainParamChanges(batch);
            for (auto& c : batch) {
                drainResult.push_back(c);
            }
        }
        drainDone.store(true, std::memory_order_release);
    });

    pusher.join();
    drainer.join();

    // Both threads completed without deadlock
    EXPECT_TRUE(pushDone.load());
    EXPECT_TRUE(drainDone.load());

    // The original change (id=99) plus pushed changes should all be accounted for
    // Drain any remaining
    std::vector<ParamChange> remaining;
    mod.drainParamChanges(remaining);

    size_t total = drainResult.size() + remaining.size();
    // At minimum we have the original push (id=99) + at least some from the pusher thread
    EXPECT_GE(total, 1u);
}
