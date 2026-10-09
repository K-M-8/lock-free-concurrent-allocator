
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

using namespace std;
using Clock = chrono::steady_clock;

// ============================================================
// CONFIGURATION
// ============================================================

constexpr array<size_t, 6> SIZE_CLASSES = {
    16, 32, 64, 128, 256, 512
};

constexpr uint32_t BLOCK_COUNT = 10000;
constexpr uint32_t NIL = 0xFFFFFFFFu;
constexpr size_t MAX_REQUEST = 512;

struct Allocation {
    void* ptr;
    size_t requested;
};

// ============================================================
// 1. MUTEX-BASED BASELINE
// ============================================================

class MutexAllocator {
    mutex mtx;

public:
    void* allocate(size_t bytes) {
        lock_guard<mutex> lock(mtx);
        return malloc(bytes);
    }

    void deallocate(void* ptr) {
        if (!ptr) return;

        lock_guard<mutex> lock(mtx);
        free(ptr);
    }
};

// ============================================================
// 2. FIXED-SIZE MEMORY BLOCK
// ============================================================

struct alignas(64) Block {
    atomic<uint32_t> next{NIL};
    atomic<bool> inUse{false};
    atomic<size_t> requested{0};

    // Each block has room for the largest size class.
    alignas(64) byte data[MAX_REQUEST];
};

// ============================================================
// 3. CAS-BASED SIZE-CLASS POOL
// ============================================================

class Pool {
    const size_t blockSize;
    const uint32_t capacity;

    unique_ptr<Block[]> blocks;

    // Lower 32 bits: head block index
    // Upper 32 bits: version tag
    atomic<uint64_t> head{0};

    atomic<uint64_t> casRetries{0};
    atomic<uint64_t> allocationCount{0};
    atomic<uint64_t> freeCount{0};

    // Current and peak internal fragmentation.
    atomic<size_t> currentWaste{0};
    atomic<size_t> peakWaste{0};

    static uint64_t pack(uint32_t index, uint32_t tag) {
        return (uint64_t(tag) << 32) | uint64_t(index);
    }

    static uint32_t indexOf(uint64_t value) {
        return uint32_t(value & 0xFFFFFFFFULL);
    }

    static uint32_t tagOf(uint64_t value) {
        return uint32_t(value >> 32);
    }

    void updatePeak(size_t value) {
        size_t old = peakWaste.load(memory_order_relaxed);

        while (old < value &&
               !peakWaste.compare_exchange_weak(
                   old, value,
                   memory_order_relaxed,
                   memory_order_relaxed)) {
            // Retry if another thread updated the peak.
        }
    }

    // Remove one block from the shared free-list.
    bool pop(uint32_t& result) {
        uint64_t oldHead =
            head.load(memory_order_acquire);

        while (indexOf(oldHead) != NIL) {
            uint32_t index = indexOf(oldHead);

            uint32_t next =
                blocks[index].next.load(memory_order_relaxed);

            uint64_t newHead =
                pack(next, tagOf(oldHead) + 1);

            if (head.compare_exchange_weak(
                    oldHead, newHead,
                    memory_order_acq_rel,
                    memory_order_acquire)) {
                result = index;
                return true;
            }

            casRetries.fetch_add(1, memory_order_relaxed);
        }

        return false;
    }

    // Return a block to the shared free-list.
    void push(uint32_t index) {
        uint64_t oldHead =
            head.load(memory_order_acquire);

        while (true) {
            blocks[index].next.store(
                indexOf(oldHead),
                memory_order_relaxed);

            uint64_t newHead =
                pack(index, tagOf(oldHead) + 1);

            if (head.compare_exchange_weak(
                    oldHead, newHead,
                    memory_order_acq_rel,
                    memory_order_acquire)) {
                return;
            }

            casRetries.fetch_add(1, memory_order_relaxed);
        }
    }

public:
    Pool(size_t size, uint32_t count)
        : blockSize(size),
          capacity(count),
          blocks(make_unique<Block[]>(count)) {

        for (uint32_t i = 0; i < capacity; ++i) {
            blocks[i].next.store(
                i + 1 < capacity ? i + 1 : NIL,
                memory_order_relaxed);
        }

        head.store(pack(0, 0), memory_order_release);
    }

    // Allocate a block from this size class.
    void* allocate(size_t requestedBytes) {
        uint32_t index;

        if (!pop(index)) {
            return nullptr;
        }

        // A block removed from the free-list must be unused.
        bool wasInUse = blocks[index].inUse.exchange(
            true, memory_order_relaxed);

        if (wasInUse) {
            // Indicates allocator corruption; never reuse it.
            return nullptr;
        }

        blocks[index].requested.store(
            requestedBytes, memory_order_relaxed);

        size_t waste = blockSize - requestedBytes;

        size_t current =
            currentWaste.fetch_add(
                waste, memory_order_relaxed) + waste;

        updatePeak(current);

        allocationCount.fetch_add(1, memory_order_relaxed);

        return blocks[index].data;
    }

    // Return a block only if the pointer belongs to this pool
    // and currently represents an allocated block.
    bool deallocate(void* ptr) {
        if (!ptr) return false;

        uintptr_t base =
            reinterpret_cast<uintptr_t>(blocks.get());

        uintptr_t address =
            reinterpret_cast<uintptr_t>(ptr);

        uintptr_t end =
            base + sizeof(Block) * capacity;

        if (address < base || address >= end) {
            return false;
        }

        size_t index =
            (address - base) / sizeof(Block);

        if (index >= capacity ||
            address != reinterpret_cast<uintptr_t>(
                blocks[index].data)) {
            return false;
        }

        bool expected = true;

        // Reject a double free.
        if (!blocks[index].inUse.compare_exchange_strong(
                expected, false,
                memory_order_relaxed,
                memory_order_relaxed)) {
            return false;
        }

        size_t requested =
            blocks[index].requested.exchange(
                0, memory_order_relaxed);

        currentWaste.fetch_sub(
            blockSize - requested,
            memory_order_relaxed);

        freeCount.fetch_add(1, memory_order_relaxed);

        push(static_cast<uint32_t>(index));

        return true;
    }

    size_t reservedPayloadBytes() const {
        return blockSize * capacity;
    }

    size_t currentInternalWaste() const {
        return currentWaste.load(memory_order_relaxed);
    }

    size_t peakInternalWaste() const {
        return peakWaste.load(memory_order_relaxed);
    }

    void resetPeak() {
        peakWaste.store(
            currentWaste.load(memory_order_relaxed),
            memory_order_relaxed);
    }

    uint64_t retries() const {
        return casRetries.load(memory_order_relaxed);
    }

    uint64_t allocations() const {
        return allocationCount.load(memory_order_relaxed);
    }

    uint64_t frees() const {
        return freeCount.load(memory_order_relaxed);
    }

    bool casIsLockFree() const {
        return head.is_lock_free();
    }
};

// ============================================================
// 4. MULTI-SIZE CONCURRENT ALLOCATOR
// ============================================================

class ConcurrentAllocator {
    array<unique_ptr<Pool>, SIZE_CLASSES.size()> pools;

public:
    ConcurrentAllocator() {
        for (size_t i = 0; i < SIZE_CLASSES.size(); ++i) {
            pools[i] = make_unique<Pool>(
                SIZE_CLASSES[i], BLOCK_COUNT);
        }
    }

    // Choose the smallest size class that can fit the request.
    void* allocate(size_t bytes) {
        if (bytes == 0) bytes = 1;

        if (bytes > MAX_REQUEST) {
            return nullptr;
        }

        for (size_t i = 0; i < SIZE_CLASSES.size(); ++i) {
            if (bytes <= SIZE_CLASSES[i]) {
                return pools[i]->allocate(bytes);
            }
        }

        return nullptr;
    }

    // A block can be freed by a different thread.
    void deallocate(void* ptr) {
        if (!ptr) return;

        for (auto& pool : pools) {
            if (pool->deallocate(ptr)) {
                return;
            }
        }

        cerr << "Warning: invalid pointer or double free rejected.\n";
    }

    uint64_t totalRetries() const {
        uint64_t result = 0;
        for (const auto& pool : pools) {
            result += pool->retries();
        }
        return result;
    }

    size_t reservedBytes() const {
        size_t result = 0;
        for (const auto& pool : pools) {
            result += pool->reservedPayloadBytes();
        }
        return result;
    }

    size_t peakInternalWaste() const {
        size_t result = 0;
        for (const auto& pool : pools) {
            result += pool->peakInternalWaste();
        }
        return result;
    }

    size_t currentInternalWaste() const {
        size_t result = 0;
        for (const auto& pool : pools) {
            result += pool->currentInternalWaste();
        }
        return result;
    }

    void resetPeakWaste() {
        for (auto& pool : pools) {
            pool->resetPeak();
        }
    }

    bool casIsLockFree() const {
        for (const auto& pool : pools) {
            if (!pool->casIsLockFree()) return false;
        }
        return true;
    }
};

// ============================================================
// 5. BENCHMARK
// ============================================================

struct Result {
    double milliseconds = 0;
    double opsPerSecond = 0;
    double averageLatencyNs = 0;
};

template <typename Allocator>
Result benchmark(
    Allocator& allocator,
    int threadCount,
    int operationsPerThread) {

    atomic<int> ready{0};
    atomic<bool> start{false};

    vector<thread> workers;
    workers.reserve(threadCount);

    atomic<uint64_t> measuredLatencyNs{0};

    const array<size_t, 6> requestSizes = {
        8, 24, 48, 96, 192, 400
    };

    for (int t = 0; t < threadCount; ++t) {
        workers.emplace_back([&, t]() {
            mt19937 rng(12345 + t);
            vector<Allocation> held;
            held.reserve(operationsPerThread / 4 + 16);

            ready.fetch_add(1, memory_order_release);

            while (!start.load(memory_order_acquire)) {
                this_thread::yield();
            }

            uint64_t localLatency = 0;

            for (int i = 0; i < operationsPerThread; ++i) {
                auto opStart = Clock::now();

                if (!held.empty() && rng() % 100 < 45) {
                    size_t position = rng() % held.size();

                    allocator.deallocate(held[position].ptr);

                    held[position] = held.back();
                    held.pop_back();
                } else {
                    size_t requested =
                        requestSizes[rng() % requestSizes.size()];

                    void* ptr = allocator.allocate(requested);

                    if (ptr) {
                        held.push_back({ptr, requested});
                    }
                }

                auto opEnd = Clock::now();

                localLatency += static_cast<uint64_t>(
                    chrono::duration_cast<chrono::nanoseconds>(
                        opEnd - opStart).count());
            }

            // Clean up allocations after measurement.
            for (const auto& allocation : held) {
                allocator.deallocate(allocation.ptr);
            }

            measuredLatencyNs.fetch_add(
                localLatency, memory_order_relaxed);
        });
    }

    while (ready.load(memory_order_acquire) != threadCount) {
        this_thread::yield();
    }

    auto begin = Clock::now();
    start.store(true, memory_order_release);

    for (auto& worker : workers) {
        worker.join();
    }

    auto end = Clock::now();

    double milliseconds =
        chrono::duration<double, milli>(end - begin).count();

    uint64_t operations =
        uint64_t(threadCount) * operationsPerThread;

    return {
        milliseconds,
        operations / (milliseconds / 1000.0),
        double(measuredLatencyNs.load()) / operations
    };
}

// ============================================================
// 6. MAIN
// ============================================================

int main() {
    cout << "\n";
    cout << "============================================\n";
    cout << " PHASE 2: CONCURRENT MEMORY ALLOCATOR\n";
    cout << "============================================\n";

    cout << "Size classes: ";
    for (size_t size : SIZE_CLASSES) {
        cout << size << " ";
    }
    cout << "bytes\n";

    cout << "Blocks per class: " << BLOCK_COUNT << "\n";
    cout << "Maximum request: " << MAX_REQUEST << " bytes\n\n";

    MutexAllocator mutexAllocator;
    ConcurrentAllocator concurrentAllocator;

    cout << "64-bit atomic CAS is lock-free: "
         << (concurrentAllocator.casIsLockFree() ? "Yes" : "No")
         << "\n\n";

    constexpr int OPERATIONS = 30000;

    ofstream csv("results.csv");

    if (!csv) {
        cerr << "Error: cannot create results.csv\n";
        return 1;
    }

    csv << "threads,mutex_ms,mutex_ops_sec,mutex_avg_latency_ns,"
           "cas_ms,cas_ops_sec,cas_avg_latency_ns,"
           "cas_retries,peak_internal_waste_bytes,"
           "reserved_payload_bytes\n";

    for (int threads : {1, 2, 4, 8}) {
        cout << "Testing " << threads << " thread(s)...\n";

        concurrentAllocator.resetPeakWaste();

        Result mutexResult =
            benchmark(mutexAllocator, threads, OPERATIONS);

        uint64_t retriesBefore =
            concurrentAllocator.totalRetries();

        Result casResult =
            benchmark(concurrentAllocator, threads, OPERATIONS);

        uint64_t retriesAfter =
            concurrentAllocator.totalRetries();

        uint64_t retries = retriesAfter - retriesBefore;

        size_t peakWaste =
            concurrentAllocator.peakInternalWaste();

        cout << fixed << setprecision(2);

        cout << "  Mutex throughput : "
             << mutexResult.opsPerSecond << " ops/sec\n";

        cout << "  CAS throughput   : "
             << casResult.opsPerSecond << " ops/sec\n";

        cout << "  Mutex avg latency: "
             << mutexResult.averageLatencyNs << " ns/op\n";

        cout << "  CAS avg latency  : "
             << casResult.averageLatencyNs << " ns/op\n";

        cout << "  CAS retries      : " << retries << "\n";

        cout << "  Peak internal waste (estimate): "
             << peakWaste << " bytes\n\n";

        csv << threads << ","
            << mutexResult.milliseconds << ","
            << mutexResult.opsPerSecond << ","
            << mutexResult.averageLatencyNs << ","
            << casResult.milliseconds << ","
            << casResult.opsPerSecond << ","
            << casResult.averageLatencyNs << ","
            << retries << ","
            << peakWaste << ","
            << concurrentAllocator.reservedBytes() << "\n";
    }

    csv.close();

    cout << "Final live internal waste: "
         << concurrentAllocator.currentInternalWaste()
         << " bytes\n";

    cout << "Reserved size-class payload capacity: "
         << concurrentAllocator.reservedBytes()
         << " bytes\n";

    cout << "\nBenchmark finished. See results.csv\n";

    return 0;
}