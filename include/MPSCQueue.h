#pragma once
#include <atomic>
#include <memory>
#include <iostream>
#include "Task.h"

namespace T_Threads {
	// T is Task*
	template <typename T>
	class MPSCQueue {
	private:
		// REMOVED struct Node entirely

		std::atomic<T> tail_;
		T head_; // Dummy/Sentinel Task*

	public:
		MPSCQueue() {
			head_ = new Task(); // A dummy task
			head_->next.store(nullptr, std::memory_order_relaxed);
			tail_.store(head_, std::memory_order_relaxed);
		}

		~MPSCQueue() {
			clear();
			delete head_;
		}

		// Push accepts Task* directly
		void push(T task) {
			task->next.store(nullptr, std::memory_order_relaxed);
			T prev = tail_.exchange(task, std::memory_order_acq_rel);
			prev->next.store(task, std::memory_order_release);
		}

		// Batch push accepts Task* pointers
		void push_batch(T head_batch, T tail_batch, size_t count) {
			tail_batch->next.store(nullptr, std::memory_order_relaxed);
			T prev = tail_.exchange(tail_batch, std::memory_order_acq_rel);
			prev->next.store(head_batch, std::memory_order_release);
		}

		bool pop(T& out_result) {
			T head = head_;
			T next = head->next.load(std::memory_order_acquire);

			if (!next) {
				if (tail_.load(std::memory_order_acquire) == head) return false;
				while (!(next = head->next.load(std::memory_order_acquire))) {
					std::this_thread::yield();
				}
			}

			out_result = next;
			head_ = next;
			// Note: Do NOT delete head_ here if it's a Task*
			return true;
		}

		void clear() {
			T dummy;
			while (pop(dummy)) {}
		}
		bool empty() const {
			// 1. Get the current head
			// 2. The queue is empty if head->next_ is null
			//    AND tail_ points to the sentinel head_
			return head_->next.load(std::memory_order_acquire) == nullptr;
		}
	};
};
