#include "ThreadPool.h"
#include "common.hpp"

ThreadPool::ThreadPool(size_t minThreads, size_t maxThreads)
    :minThreads(minThreads), maxThreads(maxThreads)
{
    DEBUGLOG("ThreadPool::ThreadPool");
    stop.store(false);
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
        if (stop.load()) {
            DEBUGLOG("ThreadPool is stopping—rejecting new task");
            return;
        }
        tasks.push(std::move(task));
    }
    condition.notify_one();
}

void ThreadPool::shutdown()
{
    DEBUGLOG("ThreadPool::shutdown");
    {
        std::lock_guard<std::mutex> qlock(queueMutex);
        std::lock_guard<std::mutex> wlock(workersMutex);
        stop.store(true);
        for (std::shared_ptr<std::atomic<bool>> stopWorker : stopFlags) 
        {
            stopWorker->store(true);
        }
    }
    DEBUGLOG("ThreadPool::shutdown start");

    condition.notify_all();

    if (managerThread.joinable()) 
        managerThread.join();

    {
        std::lock_guard<std::mutex> lock(workersMutex);
        for (std::thread& worker : workers) {
            if (worker.joinable())
                worker.join();
        }
        workers.clear();
        stopFlags.clear();
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        while (!tasks.empty()) {
            tasks.pop();
        }
    }
    DEBUGLOG("ThreadPool::shutdown done");
}

void ThreadPool::setMaxThreads(size_t value)
{
    DEBUGLOG("ThreadPool::setMaxThreads");
    maxThreads = value;
}

int ThreadPool::getWorkingTaskCount()
{
    DEBUGLOG("ThreadPool::getWorkingTaskCount");
    std::lock_guard<std::mutex> lock(queueMutex);
    return static_cast<int>(tasks.size());
}

ThreadPool::~ThreadPool()
{
    DEBUGLOG("ThreadPool::~ThreadPool");
    shutdown();
}

void ThreadPool::addWorker()
{
    DEBUGLOG("ThreadPool::addWorker");
    auto stopFlag = std::make_shared<std::atomic<bool>>(false);
    {
        std::lock_guard<std::mutex> lock(workersMutex);
        stopFlags.push_back(stopFlag);
        workers.emplace_back([this, stopFlag] {
            while (true)
            {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queueMutex);
                    condition.wait(lock, [this, stopFlag] { return !tasks.empty() || stop.load() || stopFlag->load(); });

                    if (stop.load() || stopFlag->load())
                    {
                        INFOLOG("Thread closed!!");
                        return;
                    }

                    if (!tasks.empty())
                    {
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                }

                if (task)
                    task();
            }
            });
        DEBUGLOG("adding worker threads!!", workers.size());
    }
}

void ThreadPool::adjustWorkerThreads()
{
    DEBUGLOG("ThreadPool::adjustWorkerThreads");
    while (!stop.load()) 
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        size_t queueSize, threadCount;
        queueSize = getWorkingTaskCount();
        {
            std::lock_guard<std::mutex> lock(workersMutex);
            threadCount = workers.size();
        }
        DEBUGLOG("queueSize:",queueSize, " threadCount:",threadCount, " maxThreads:",maxThreads, " minThreads:", minThreads);
        if (queueSize > threadCount && threadCount < maxThreads ) 
        {
            addWorker();
        }
        else if ((queueSize == 0) && (threadCount > minThreads) ) 
        {
            std::thread threadToJoin;
            {
                std::lock_guard<std::mutex> lock(workersMutex);
                INFOLOG("detaching worker threads!!", workers.size());
                stopFlags.back()->store(true);
                condition.notify_all();

                threadToJoin = std::move(workers.back());
                workers.pop_back();
                stopFlags.pop_back();
                INFOLOG("detached worker threads!!", workers.size());
            }
            if (threadToJoin.joinable())
                threadToJoin.join();
        }
    }
}