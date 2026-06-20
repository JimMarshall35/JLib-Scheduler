#include "../include/T_Thread.h"
#include "../include/platform.h"
#include <iostream>
using namespace T_Threads;
T_Thread::T_Thread() {
}
T_Thread::~T_Thread() {
}
void T_Thread::StartWorker(size_t cpu_affinity)
{
	auto ready = std::make_shared<std::atomic<bool>>(false);
	thread_ = std::thread([this, ready]() {
		while (!ready->load(std::memory_order_acquire)) std::this_thread::yield();
		this->Worker();
		});
	native_handle_ = thread_.native_handle();
#ifdef _WIN32
	DWORD_PTR mask_ = 1ULL << cpu_affinity;
	SetThreadAffinityMask(native_handle_, mask_);
#endif
	ready->store(true, std::memory_order_release);
};
std::thread::id T_Thread::GetID() {
	return thread_.get_id();
}
bool T_Thread::SetImmediateTask(Task* new_task) {
	if (!new_task) return false;
	{
		immediate_task_ = new_task;
		immediate_.store(true, std::memory_order_release);
	}
	cv_.notify_one();
	return true;
}

void T_Thread::SetQueueIndex(size_t index)
{
	queue_index_ = index;
};
void T_Thread::Join() {
	bool expected = false;
	if (!joining_.compare_exchange_strong(expected, true)) return;

	running_.store(false, std::memory_order_release);
	NotifyWorker();

	std::unique_lock<std::mutex> lock(join_mutex_);
	cv_worker_done_.wait(lock, [this] {
		return !running_.load(std::memory_order_acquire);
		});

	if (thread_.joinable())
		thread_.join();

	joining_.store(false, std::memory_order_release);
}
void T_Thread::NotifyWorker()
{
	cv_.notify_one();
}

bool T_Thread::AllQueuesEmpty() {
	if (!local_queue_.empty())
		return false;
	if (!overflow_.empty())
		return false;
	if (!SharedQueues::inboxes_[queue_index_]->empty())
		return false;
	for (const auto& q : SharedQueues::thread_queues_) {
		if (!q->empty()) {
			return false;
		}
	}
	if (SharedQueues::priority_queue_[0].size_approx() > 0)
		return false;
	if (SharedQueues::priority_queue_[1].size_approx() > 0)
		return false;
	if (SharedQueues::priority_queue_[2].size_approx() > 0)
		return false;
	if (SharedQueues::priority_queue_[3].size_approx() > 0)
		return false;
	if (SharedQueues::priority_queue_[4].size_approx() > 0)
		return false;

	return true;
}
bool T_Thread::Ready()
{
	return ready_.load(std::memory_order_acquire);
}
void T_Thread::Worker() {
	running_.store(true, std::memory_order_release);
	while (running_.load(std::memory_order_acquire)) {
		Task* task_to_run = nullptr;
		{
			ready_.store(true, std::memory_order_release);
			std::unique_lock<std::mutex> lock(worker_mutex_);
			cv_.wait_for(lock, std::chrono::microseconds(5), [this]() {
				return !running_.load(std::memory_order_acquire)
					|| immediate_.load(std::memory_order_acquire)
					|| (!SharedQueues::paused_.load(std::memory_order_acquire) && !AllQueuesEmpty());
				});

			if (!running_.load(std::memory_order_acquire)) break;
		}
		auto& inbox = SharedQueues::inboxes_[queue_index_];
		auto& local_deque = SharedQueues::thread_queues_[queue_index_];
		Task* t = nullptr;

		if (!overflow_.empty()) {
			while (local_deque->size() < local_deque->capacity() && !overflow_.empty()) {
				if (!local_deque->push_bottom(overflow_.back())) {
					break;  
				}
				overflow_.pop_back();
			}
		}

		int inbox_drain_count = 0;
		while (inbox->pop(t)) {
			if (++inbox_drain_count > 100000) {
				std::cerr << "[worker " << queue_index_ << "] ERROR: inbox drain looping >100k times! Aborting.\n";
				break;  
			}

			if (!t) {
				std::cerr << "[worker " << queue_index_ << "] ERROR: popped null from inboxes_\n";
				continue;
			}
			queue_load_.fetch_add(1, std::memory_order_relaxed);

			if (inbox_drain_count % 100 == 0) {
				std::cerr << "[worker " << queue_index_ << "] inbox drain #" << inbox_drain_count << " task " << (void*)t << "\n";
			}

			if (local_deque->push_bottom(t)) {
				continue; 
			}

			std::cerr << "[worker " << queue_index_ << "] FAILED push_bottom for task " << (void*)t << "\n";

			if (!SharedQueues::priority_queue_[0].try_enqueue(t)) {
				std::cerr << "[worker " << queue_index_ << "] WARNING: task " << (void*)t << " overflow, stashing locally\n";
				overflow_.push_back(t);
			}
		}
		
		
		// --- 1. Immediate task execution ---
		bool is_handling_fork = false; 
		{
			if (immediate_task_ != nullptr) {
				if (!local_queue_.empty()) {
					while (!local_queue_.empty()) {
						Task* t = local_queue_.back();
						local_queue_.pop_back();
						SharedQueues::thread_queues_[queue_index_]->push_bottom(t);
					}
				}
				task_to_run = immediate_task_;
				current_task = immediate_task_;
				immediate_task_ = nullptr;

				immediate_.store(false, std::memory_order_release);
				is_handling_fork = true;
			}
		}
		{
			// --- 2. Local Worker queue  of set affinity---
			if (!local_queue_.empty()) {
				task_to_run = local_queue_.back();
				local_queue_.pop_back();
			}

			//--- 3 local work stealing queue of no affinity
			if (!task_to_run) {
				auto opt = SharedQueues::thread_queues_[queue_index_]->pop_bottom();
				if (opt) {
					Task* task = *opt;
					if (!task) { 
						std::cerr << "[worker " << queue_index_ << "] Null task from pop_bottom!" << std::endl; 
					}
					else { 
						task_to_run = task; 
						current_task = task; 
					}
				} 
			}

			// --- 4. Work stealing ---
			if (!task_to_run) {
				for (size_t i = 0; i < SharedQueues::thread_queues_.size(); ++i) {
					if (i == queue_index_) continue;
					auto opt = SharedQueues::thread_queues_[i]->steal();
					if (opt) {
						task_to_run = *opt;
						current_task = task_to_run;
						break;
					}
				}
			}
		}
		
		
		if(!task_to_run){
			// --- 5. Priority Queue ---
			bool success = false;
			Task* task;

			for (int i = 0; i < 5; i++) {
				success = SharedQueues::priority_queue_[i].try_dequeue(task);
				if (success) {
					task_to_run = task;
					break;
				}
			}
		}


		// --- 6. Execute task if found ---
		if (task_to_run) {
			task_to_run->execute();
			SharedQueues::graveyard.push(task_to_run);

			if (is_handling_fork)
			{
				if (queue_index_ < SharedQueues::immediate_cores_in_use.size()) {
					SharedQueues::immediate_cores_in_use[queue_index_]->store(false, std::memory_order_release);
				}
				is_handling_fork = false;
			}
			task_ = nullptr;
			current_task = nullptr;
			task_to_run = nullptr;
		}
	}

	running_.store(false, std::memory_order_release);
	cv_worker_done_.notify_all();
}
