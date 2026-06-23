#include "../include/FiberPool.h"
#include "../include/TaskScheduler.h"
#include "../include/T_Thread.h"
using namespace T_Threads;
FiberPool::FiberPool(int count) : arena(count * 1024 * 1024) {

	allFibers.resize(count);
	freeList.reserve(count);
	for (int i = 0; i < count; ++i) {
		// Allocate the stack from the arena
		void* stackMem = arena.AllocateStack(1024 * 1024);

		// Init the Fiber
		allFibers[i].stackBase = stackMem;
		allFibers[i].stackSize = 1024 * 1024;

		// Add to the free list
		freeList.push_back(&allFibers[i]);
	}
}
Fiber* FiberPool::Acquire()
{
	// Mutex-guarded free list. A lock-free stack here needs ABA protection AND a
	// correctly-fenced epoch publish (store-release on localEpoch followed by a
	// load-acquire of head is a StoreLoad pair that x86 may reorder, so the
	// reclaimer can free a node we're mid-read on). The pool is touched once per
	// task execution -- far cheaper than the context switch + task it guards --
	// so a plain mutex is both correct and negligible.
	std::lock_guard<std::mutex> lock(freeMutex);
	if (freeList.empty()) return nullptr;
	Fiber* f = freeList.back();
	freeList.pop_back();
	return f;
}

void T_Threads::FiberPool::Release(Fiber* f)
{
	std::lock_guard<std::mutex> lock(freeMutex);
	freeList.push_back(f);
}

void T_Threads::FiberPool::SwitchBackToScheduler()
{         // Find the current thread's scheduler context
			// (You might need a thread_local pointer to the current T_Thread)
	auto* self = T_Thread::GetCurrent();

	// Switch from the Fiber's context back to the worker loop
	ContextSwitch(&self->currentFiber->ctx, &self->schedulerCtx);
}

void T_Threads::FiberPool::FiberEntryWrapper()
{
	// 1. Get the worker thread that spawned this fiber
	auto* thread = T_Thread::GetCurrent();

	if (thread->currentFiber && thread->currentRunningTask) {
		thread->currentRunningTask->Execute();
	}
	// Mark DEAD before returning so Worker knows the task completed (not suspended)
	if (thread->currentFiber)
		thread->currentFiber->status.store(FiberStatus::DEAD, std::memory_order_release);
	ContextSwitch(&thread->currentFiber->ctx, &thread->schedulerCtx);
}
