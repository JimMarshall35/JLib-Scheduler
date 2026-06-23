#pragma once
#include "Stack.h"
#include "Fiber.h"
#include "Epochs.h"
#include "FiberStackArena.h"
#include <vector>
#include <mutex>
#include "platform.h"
namespace T_Threads {
    class Scheduler; // Just tell the compiler this exists

    class FiberPool {
        FiberStackArena arena;
        std::vector<Fiber> allFibers;   // owns the fibers (stable addresses)
        std::vector<Fiber*> freeList;   // available fibers
        std::mutex freeMutex;           // guards freeList

    public:
        FiberPool(int count);

        Fiber* Acquire();

        void Release(Fiber* f);
        static void SwitchBackToScheduler();
        static void FiberEntryWrapper();
    };
};