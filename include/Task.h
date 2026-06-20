#pragma once
#include <functional>
#include <atomic>

namespace T_Threads {
    struct Task {
        using Func = void(*)(void*);

        Func fn;
        void* data = nullptr;
        std::atomic<bool> stop_flag{ false };
        std::function<void()> onComplete;
        std::atomic<bool> complete{ false };

        Task(Func f, void* d = nullptr)
            : fn(f), data(d) {
        }

        inline void execute() noexcept {
             fn(data);
            if (onComplete) onComplete();
            complete.store(true, std::memory_order_release);
        }


        inline void stop() {
            stop_flag.store(true, std::memory_order_release);
        }
    };

    template<typename F>
    class LambdaTask : public Task {
    public:
        LambdaTask(F&& f)
            : Task(nullptr, nullptr)  
        {
            struct Wrapper { F f; };
            Wrapper* w = new Wrapper{ std::forward<F>(f) };

            this->fn = [](void* ptr) {
                Wrapper* w = static_cast<Wrapper*>(ptr);
                w->f();
                delete w;
                };
            this->data = w;
        }
    };
 
};