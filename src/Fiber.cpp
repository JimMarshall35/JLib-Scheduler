#include "../include/Fiber.h"
#include "../include/T_Thread.h"
#include "../include/TaskScheduler.h"
using namespace T_Threads;
void Fiber::Init(void(*entryPoint)())
{
	// 16-byte-align the very top of this fiber's stack.
	uintptr_t top = ((uintptr_t)((char*)stackBase + stackSize)) & ~(uintptr_t)0xF;
	uintptr_t* sp = (uintptr_t*)top;

	// Windows x64 ABI: the CALLER must leave 32 bytes of shadow space ABOVE the
	// return address for the callee to spill its register params. When
	// ContextSwitch 'ret's into entryPoint, that shadow space is whatever sits
	// above the entry RSP. Reserve it HERE, inside this fiber's own stack --
	// otherwise the entry function writes past stackTop, which is either the
	// next fiber's base (silent corruption) or, for the last fiber, unmapped
	// memory (0xC0000005 write AV at the stack-region boundary).
	sp -= 4;                              // 32 bytes shadow space (owned by this fiber)
	*(--sp) = 0;                          // landing slot: entry RSP points here (unused)
	*(--sp) = (uintptr_t)entryPoint;      // return address consumed by ContextSwitch 'ret'

	// 8 callee-saved registers consumed by ContextSwitch's pops (r15 is lowest).
	*(--sp) = 0; // rbx
	*(--sp) = 0; // rbp
	*(--sp) = 0; // rdi
	*(--sp) = 0; // rsi
	*(--sp) = 0; // r12
	*(--sp) = 0; // r13
	*(--sp) = 0; // r14
	*(--sp) = 0; // r15  <-- ctx.rsp; ContextSwitch starts popping here

	ctx.rsp = (void*)sp;
}

void T_Threads::Fiber::CoYield() {
	this->status.store(FiberStatus::READY, std::memory_order_relaxed);

	// 1. Access the current thread
	auto* thread = T_Thread::self;

	// 2. Put the task back in the queue
	TaskScheduler::Instance().Push(currentRunningTask);

	// 3. Jump back to the thread's "Home Base" (schedulerCtx)
	ContextSwitch(&this->ctx, &thread->schedulerCtx);
}

void Fiber::Suspend() {
	// The fiber knows its own task
	Task* myTask = currentRunningTask;
	auto thread = T_Thread::GetCurrent();

	// Now perform the context switch out...
	ContextSwitch(&this->ctx, &thread->schedulerCtx);
}
void Fiber::Resume() {
	// 1. Atomically ensure we only resume if we are truly suspended
	FiberStatus expected = FiberStatus::SUSPENDED;
	if (this->status.compare_exchange_strong(expected, FiberStatus::READY, std::memory_order_release)) {

		// 2. Re-inject into the scheduler
		TaskScheduler::Instance().Push(currentRunningTask);
	}
}