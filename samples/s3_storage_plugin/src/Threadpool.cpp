#include "ThreadPool.h"
#include "common.hpp"


ThreadPool::ThreadPool(size_t minThreads, size_t maxThreads)
    : stop(false), minThreads(minThreads), maxThreads(maxThreads), activeThreads(0) 
{
    DEBUGLOG("ThreadPool::ThreadPool");
    for (size_t i = 0; i < minThreads; ++i) {
        addWorker();
    }
    
    managerThread = std::thread(&ThreadPool::adjustWorkerThreads, this);
}

void ThreadPool::enqueueTask(std::function<void()> task) 
{
    DEBUGLOG("ThreadPool::enqueueTask");
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        tasks.push(std::move(task));
    }
    condition.notify_one();
}

void ThreadPool::shutdown() 
{
    DEBUGLOG("ThreadPool::shutdown");
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        stop = true;
    }
    condition.notify_all();

    for (std::thread& worker : workers) {
        if (worker.joinable()) worker.join();
    }

    if (managerThread.joinable()) managerThread.join();
}

void ThreadPool::setMaxThreads(size_t value)
{
    DEBUGLOG("ThreadPool::setMaxThreads");
    maxThreads = value;
}

int ThreadPool::getWorkingTaskCount()
{
    DEBUGLOG("ThreadPool::getWorkingTaskCount");
    std::unique_lock<std::mutex> lock(queueMutex);
    return tasks.size();
}

ThreadPool::~ThreadPool() 
{ 
    DEBUGLOG("ThreadPool::~ThreadPool");
    shutdown(); 
}

void ThreadPool::addWorker()
{
    DEBUGLOG("ThreadPool::addWorker");
    workers.emplace_back([this] {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                condition.wait(lock, [this] { return !tasks.empty() || stop; });

                if (stop && tasks.empty()) return;

                task = std::move(tasks.front());
                tasks.pop();
                ++activeThreads;
            }

            task();

            {
                std::lock_guard<std::mutex> lock(queueMutex);
                --activeThreads;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
}

void ThreadPool::adjustWorkerThreads() 
{
    DEBUGLOG("ThreadPool::adjustWorkerThreads");
    while (!stop) {
        std::this_thread::sleep_for(std::chrono::seconds(2));

        std::lock_guard<std::mutex> lock(queueMutex);

        size_t queueSize = tasks.size();
        size_t currentThreads = workers.size();

        if (queueSize > activeThreads && currentThreads < maxThreads) {
            addWorker();
        } else if (queueSize == 0 && currentThreads > minThreads) {
            workers.back().detach();
            workers.pop_back();
        }
    }
}
