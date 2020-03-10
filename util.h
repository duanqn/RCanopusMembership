#ifndef UTIL_H_
#define UTIL_H_

#include <type_traits>
#include <functional>
#include <chrono>
#include <thread>
#include <future>

namespace AlgoLib{
namespace Util{
    class TCleanup{
        private:
        std::function<void ()> m_func;

        // Do not dynamically allocate an object with this class
        static void * operator new(size_t) = delete;
        static void * operator new[](size_t) = delete;
        static void operator delete(void *) = delete;
        static void operator delete[](void *) = delete;

        public:
        TCleanup(std::function<void ()> const& func): m_func(func){}
        TCleanup(std::function<void ()> && func): m_func(func){}

        ~TCleanup(){
            m_func();
        }
    };

    template<class Function, class... Args>
    class DelayedAsyncExecution{
        private:
        using return_type=std::invoke_result_t<Function, Args...>;
        std::future<return_type> m_result;
        std::thread *pStdThread;

        template <class T>
        std::decay_t<T> decay_copy(T&& v) { return std::forward<T>(v); }

        public:
        explicit DelayedAsyncExecution(std::chrono::milliseconds const& sleep_ms, Function&& func, Args&&... args){
            std::packaged_task<return_type(Args...)> task([this, &func, &sleep_ms](Args... args){
                std::this_thread::sleep_for(sleep_ms);
                return func(args...);
            });
            m_result = task.get_future();

            pStdThread = new std::thread(std::move(task), args...);
        }

        return_type waitForResult(){
            if(pStdThread != nullptr){
                if(pStdThread->joinable()){
                    pStdThread->join();
                }
            }
            return m_result.get();
        }

        ~DelayedAsyncExecution(){
            if(pStdThread != nullptr){
                if(pStdThread->joinable()){
                    pStdThread->join();
                }
                delete pStdThread;
            }
        }
    };

    template<class Function, class... Args>
    class AsyncExecution{
        private:
        using return_type=std::invoke_result_t<Function, Args...>;
        std::future<return_type> m_result;
        std::thread *pStdThread;

        template <class T>
        std::decay_t<T> decay_copy(T&& v) { return std::forward<T>(v); }

        public:
        explicit AsyncExecution(Function&& func, Args&&... args){
            std::packaged_task<return_type(Args...)> task(func);
            m_result = task.get_future();

            pStdThread = new std::thread(std::move(task), args...);
        }

        return_type waitForResult(){
            if(pStdThread != nullptr){
                if(pStdThread->joinable()){
                    pStdThread->join();
                }
            }
            return m_result.get();
        }

        ~AsyncExecution(){
            if(pStdThread != nullptr){
                if(pStdThread->joinable()){
                    pStdThread->join();
                }
                delete pStdThread;
            }
        }
    };
}
}

#endif
