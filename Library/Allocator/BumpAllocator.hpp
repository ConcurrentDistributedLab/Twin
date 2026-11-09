#ifndef BUMP_ALLOCATOR_HPP
#define BUMP_ALLOCATOR_HPP

#include <cstdlib>
#include <cassert>
#include <vector>
#include <cstddef>
#include <new>
#include <cstdint>

#include "../../src/core/globals.h"

/* Per-thread bump allocator: cacheline-aligned slabs, no individual frees. */
template<typename T>
class BumpAllocator {
private:
    static constexpr size_t CACHELINE_SIZE = 64;
    static constexpr size_t SLAB_SIZE = 4 * 1024 * 1024; // 4MB

    struct ThreadData {
        T* slab = nullptr;
        T* current = nullptr;
        size_t bytesRemaining = 0;
        std::vector<void*> allocations;
    };

    const int numThreads;
    ThreadData* threadData;

    size_t alignSize(size_t size) const {
        assert(size > 0);
        return ((size + CACHELINE_SIZE - 1) / CACHELINE_SIZE) * CACHELINE_SIZE;
    }

    void allocateNewSlab(int tid) {
        assert(tid >= 0 && tid < numThreads);

        void* raw = nullptr;
        if (posix_memalign(&raw, CACHELINE_SIZE, SLAB_SIZE) != 0) {
            raw = nullptr;
        }

        if (!raw) throw std::bad_alloc();

        threadData[tid].slab = static_cast<T*>(raw);
        threadData[tid].current = static_cast<T*>(raw);
        threadData[tid].bytesRemaining = SLAB_SIZE;
        threadData[tid].allocations.push_back(raw);

        assert(reinterpret_cast<std::uintptr_t>(raw) % CACHELINE_SIZE == 0);

#ifdef COMPUTE_STATS
        numAlloc += 1;
        sizeAlloc += SLAB_SIZE;
#endif
    }

public:
    BumpAllocator(int _numThreads)
        : numThreads(_numThreads) {
        assert(numThreads > 0);
        threadData = new ThreadData[numThreads];
    }

    ~BumpAllocator() {
        for (int tid = 0; tid < numThreads; ++tid) {
            for (void* ptr : threadData[tid].allocations) {
                assert(ptr != nullptr);
                std::free(ptr);
            }
        }
        delete[] threadData;
    }

    T* allocate(int tid) {
        assert(tid >= 0 && tid < numThreads);

        size_t size = alignSize(sizeof(T));

        if (threadData[tid].bytesRemaining < size || threadData[tid].slab == nullptr) {
            allocateNewSlab(tid);
        }

        assert(threadData[tid].current != nullptr);
        assert(threadData[tid].bytesRemaining >= size);

        T* result = reinterpret_cast<T*>(threadData[tid].current);
        threadData[tid].current = reinterpret_cast<T*>(
            reinterpret_cast<char*>(threadData[tid].current) + size
        );
        threadData[tid].bytesRemaining -= size;

        assert(result != nullptr);
        return result;
    }

    void resetThread(int tid) {
        assert(tid >= 0 && tid < numThreads);

        for (void* ptr : threadData[tid].allocations) {
            assert(ptr != nullptr);
            std::free(ptr);
        }
        threadData[tid].allocations.clear();
        threadData[tid].slab = nullptr;
        threadData[tid].current = nullptr;
        threadData[tid].bytesRemaining = 0;
    }

    void resetAll() {
        for (int tid = 0; tid < numThreads; ++tid) {
            resetThread(tid);
        }
    }
};

#endif // BUMP_ALLOCATOR_HPP
