#pragma once
#include <vector>
#include "Fiber.h"
#include "GlobalFiberPool.h"

namespace T_Threads {
    template<size_t MaxCapacity = 256>
    struct alignas(64) ThreadLocalCache {
        Fiber* localFibers[MaxCapacity]; // Fixed array, stack-allocated or embedded in struct
        size_t activeCapacity;            // Your calculated size
        size_t count = 0;             // Track the number of items
        size_t head = 0;
        size_t tail = 0;
        GlobalFiberPool* globalPool = nullptr;
        // Set the global pool this cache refills from
        void Initialize(GlobalFiberPool* pool,size_t runtimeCapacity) {
            activeCapacity = (runtimeCapacity <= MaxCapacity) ? runtimeCapacity : MaxCapacity;
            globalPool = pool;
        }
        void Push(Fiber* f) {
            if (count < activeCapacity) {
                localFibers[tail] = f;
                tail = (tail + 1) % activeCapacity;
                count++;
            }
            else {
                // Just return half via a pointer, NO SHIFT, NO VECTOR!
                size_t half = count / 2;
                globalPool->ReturnBatch(&localFibers[head], half);
                head = (head + half) % activeCapacity;
                count -= half;
                // add new fiber...
            }
        }

        Fiber* Pop() {
            // 1. Refill if empty
            if (count == 0 && globalPool) {
                Fiber* temp[64];
                size_t stolenCount = globalPool->StealInto(temp, 64);

                for (size_t i = 0; i < stolenCount; ++i) {
                    localFibers[tail] = temp[i]; // Push to tail
                    tail = (tail + 1) % activeCapacity;
                    count++;
                }
            }

            // 2. Return nullptr if still empty
            if (count == 0) return nullptr;

            // 3. Pop from the SAME end (LIFO)
            // Decrement tail to get the most recently added fiber
            tail = (tail == 0) ? (activeCapacity - 1) : (tail - 1);
            Fiber* f = localFibers[tail];
            count--;
            return f;
        }
    };
};