#pragma once
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <queue>
#include "Task.h"
#include "SharedQueues.h"

namespace T_Threads {
    inline thread_local Task* current_task = nullptr;

    class T_Thread {
    public:
        T_Thread();
        T_Thread(const T_Thread& other) = delete;
        T_Thread& operator=(const T_Thread& other) = delete;
        ~T_Thread();
        void StartWorker(size_t cpu_affinity);
        std::thread::id GetID();
        bool SetImmediateTask(Task* task_);
        int GetQueueLoad();
        void SetQueueIndex(size_t index);
        void Join();
        void NotifyWorker();
        bool AllQueuesEmpty();
        bool Ready();
    private:
        void Worker();

        std::atomic<int> queue_load_{ 0 };
        std::atomic<bool> immediate_{ false };
        std::atomic<bool> running_{ false };
        std::atomic<bool> ready_{ false };
        std::atomic<bool> joining_{ false };
        int queue_index_ = 0;
        std::mutex worker_mutex_;
        std::mutex join_mutex_;
        std::condition_variable cv_worker_done_;
        std::condition_variable cv_;
        std::condition_variable cv_affinity_;
        Task* task_ = nullptr;
        Task* immediate_task_ = nullptr;
        std::thread thread_;
        std::thread::native_handle_type native_handle_;
        std::vector<Task*> local_queue_;
        std::vector<Task*> overflow_;
#ifdef _WIN32
        uintptr_t mask_; 
#else
        // Implement POSIX sched_setaffinity if cross-platform later
#endif

    };
};