#pragma once
#include "Task.h"
#include "TaskDeque.h"
#include "MPSCQueue.h"
#include "../include/blockingconcurrentqueue.h"
#include <array>
namespace T_Threads {
    struct QTraits : moodycamel::ConcurrentQueueDefaultTraits {
        static constexpr size_t BLOCK_SIZE = 32768;
        static constexpr size_t IMPLICIT_INITIAL_INDEX_SIZE = 1024;
    };

    namespace SharedQueues {
        inline std::vector<std::unique_ptr<std::atomic<bool>>> immediate_cores_in_use;
        inline std::atomic<bool> paused_{ false };
        inline MPSCQueue<Task*> graveyard;
        inline std::vector<std::unique_ptr<TaskDeque>> thread_queues_;
        inline std::vector<std::unique_ptr<MPSCQueue<Task*>>> inboxes_;
        inline std::array<moodycamel::ConcurrentQueue<Task*, QTraits>, 5> priority_queue_;
    }
}