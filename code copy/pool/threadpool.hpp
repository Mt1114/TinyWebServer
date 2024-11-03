#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <thread>
#include <assert.h>

class ThreadPool
{
public:
    ThreadPool() = default;
    ThreadPool(ThreadPool &&) = default;
    // 尽量用make_shared代替new，如果通过new再传递给shared_ptr，内存是不连续的，会造成内存碎片化
    explicit ThreadPool(int threadCount = 8) : pool_(std::make_shared<Pool>())
    { // make_shared:传递右值，功能是在动态内存中分配一个对象并初始化它，返回指向此对象的shared_ptr
        assert(threadCount > 0);
        for (int i = 0; i < threadCount; i++)
        {
            std::thread([this]()
                        {
                    unique_lock<mutex> locker(pool_->mtx_);
                    while (1){
                        if(!pool_->tasks.empty()){
                            auto task = std::move(pool_->tasks.front());
                            pool_->tasks.pop();
                            locker.unlock();
                            task();
                            locker.lock();
                        }
                        else if(pool_->isClosed){
                            break;
                        } else{
                            pool_->cond_.wait(locker);
                        }
                    } })
                .detach();
        }
    }

    ~ThreadPool()
    {
        if (pool_)
        {
            pool_->isClosed = false;
            unique_lock<mutex> locker(pool_->mtx_);
        }
        pool_->cond_.notify_all();
    }

    template <typename T>
    void AddTask(T &&task)
    {
        unique_lock<mutex> locker(pool_->mtx_);
        pool_->tasks.emplace(std::forward<T>(task));
        pool_->cond_.notify_one();
    }

private:
    // 用一个结构体封装起来，方便调用
    struct Pool
    {
        std::mutex mtx_;
        std::condition_variable cond_;
        bool isClosed;
        std::queue<std::function<void()>> tasks; // 任务队列，函数类型为void()
    };
    std::shared_ptr<Pool> pool_;
};

#endif
