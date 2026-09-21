#include "ThreadPool.h"
#include "common.hpp"

ThreadPool::ThreadPool(size_t minThreads, size_t maxThreads)
    :minThreads(minThreads < 1 ? 1 : minThreads),
     maxThreads(maxThreads < 1 ? 1 : maxThreads),
     busyWorkers(0)
{
    DEBUGLOG("ThreadPool::ThreadPool");
    stop.store(false);
    if (this->maxThreads.load() < this->minThreads)
        this->maxThreads.store(this->minThreads);
    for (size_t i = 0; i < this->minThreads; ++i) {
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
    // Do not rely solely on the 1 s manager tick: if every worker is busy (or
    // none exists), spawn one now so the task is picked up immediately.
    ensureWorkers();
}

void ThreadPool::shutdown()
{
    DEBUGLOG("ThreadPool::shutdown");
    {
        std::lock_guard<std::mutex> qlock(queueMutex);
        std::lock_guard<std::mutex> wlock(workersMutex);
        stop.store(true);
        for (Worker& worker : workers)
            worker.stopFlag->store(true);
        for (Worker& worker : retiring)
            worker.stopFlag->store(true);
    }
    DEBUGLOG("ThreadPool::shutdown start");

    condition.notify_all();

    if (managerThread.joinable()) 
        managerThread.join();

    {
        std::lock_guard<std::mutex> lock(workersMutex);
        for (Worker& worker : workers) {
            if (worker.thread.joinable())
                worker.thread.join();
        }
        workers.clear();
        for (Worker& worker : retiring) {
            if (worker.thread.joinable())
                worker.thread.join();
        }
        retiring.clear();
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
    if (value < minThreads)
        value = minThreads;
    maxThreads.store(value);
}

int ThreadPool::getWorkingTaskCount()
{
    DEBUGLOG("ThreadPool::getWorkingTaskCount");
    std::lock_guard<std::mutex> lock(queueMutex);
    return static_cast<int>(tasks.size());
}

size_t ThreadPool::workerCount()
{
    std::lock_guard<std::mutex> lock(workersMutex);
    return workers.size();
}

size_t ThreadPool::busyWorkerCount() const
{
    return busyWorkers.load();
}

void ThreadPool::ensureWorkers()
{
    if (stop.load())
        return;
    size_t pending = 0;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        pending = tasks.size();
    }
    if (pending == 0)
        return;
    try
    {
        std::lock_guard<std::mutex> lock(workersMutex);
        const size_t live = workers.size();
        // Every live worker is busy (or there are none) and we still have headroom.
        if (live < maxThreads.load() && busyWorkers.load() >= live)
        {
            INFOLOG("ThreadPool::ensureWorkers spawning worker", live, pending);
            addWorkerLocked();
        }
    }
    catch (const std::exception& e)
    {
        ERRORLOG("ThreadPool::ensureWorkers failed:", e.what());
    }
}

ThreadPool::~ThreadPool()
{
    DEBUGLOG("ThreadPool::~ThreadPool");
    shutdown();
}

void ThreadPool::addWorker()
{
    DEBUGLOG("ThreadPool::addWorker");
    std::lock_guard<std::mutex> lock(workersMutex);
    addWorkerLocked();
}

void ThreadPool::addWorkerLocked()
{
    Worker worker;
    worker.stopFlag = std::make_shared<std::atomic<bool>>(false);
    worker.finished = std::make_shared<std::atomic<bool>>(false);
    auto stopFlag = worker.stopFlag;
    auto finished = worker.finished;
    try
    {
        worker.thread = std::thread([this, stopFlag, finished] {
            try
            {
                while (true)
                {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queueMutex);
                        condition.wait(lock, [this, stopFlag] {
                            return !tasks.empty() || stop.load() || stopFlag->load();
                        });

                        if (stop.load())
                        {
                            INFOLOG("Thread closed!!");
                            break;
                        }

                        // Drain before retiring: a task enqueued while this worker is
                        // being retired must not be left in the queue with no runner.
                        if (!tasks.empty())
                        {
                            task = std::move(tasks.front());
                            tasks.pop();
                        }
                        else if (stopFlag->load())
                        {
                            INFOLOG("Thread closed!!");
                            break;
                        }
                    }

                    if (task)
                    {
                        ++busyWorkers;
                        try
                        {
                            task();
                        }
                        catch (const std::exception& e)
                        {
                            ERRORLOG("ThreadPool task exception:", e.what());
                        }
                        catch (...)
                        {
                            ERRORLOG("ThreadPool task exception: Unknown");
                        }
                        --busyWorkers;
                    }
                }
            }
            catch (const std::exception& e)
            {
                ERRORLOG("ThreadPool worker exception:", e.what());
            }
            catch (...)
            {
                ERRORLOG("ThreadPool worker exception: Unknown");
            }
            finished->store(true);
        });
    }
    catch (const std::exception& e)
    {
        ERRORLOG("ThreadPool::addWorker failed to start thread:", e.what());
        return;
    }
    workers.push_back(std::move(worker));
    DEBUGLOG("adding worker threads!!", workers.size());
}

void ThreadPool::joinFinishedRetiring()
{
    std::vector<std::thread> toJoin;
    {
        std::lock_guard<std::mutex> lock(workersMutex);
        for (auto it = retiring.begin(); it != retiring.end();)
        {
            if (it->finished->load())
            {
                toJoin.push_back(std::move(it->thread));
                it = retiring.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    for (std::thread& t : toJoin)
    {
        if (t.joinable())
            t.join();
    }
}

void ThreadPool::adjustWorkerThreads()
{
    DEBUGLOG("ThreadPool::adjustWorkerThreads");
    while (!stop.load()) 
    {
        try
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (stop.load())
                break;

            joinFinishedRetiring();

            size_t queueSize, threadCount;
            queueSize = getWorkingTaskCount();
            {
                std::lock_guard<std::mutex> lock(workersMutex);
                threadCount = workers.size();
            }
            const size_t busy = busyWorkers.load();
            DEBUGLOG("queueSize:",queueSize, " threadCount:",threadCount, " busy:", busy,
                     " maxThreads:",maxThreads.load(), " minThreads:", minThreads);

            if (threadCount < minThreads)
            {
                // Never run below the floor (e.g. a worker start failed earlier).
                addWorker();
            }
            else if (queueSize > 0 && busy >= threadCount && threadCount < maxThreads.load())
            {
                addWorker();
            }
            else if ((queueSize == 0) && (busy == 0) && (threadCount > minThreads))
            {
                // Retire one idle worker; never block on it — it is joined once finished.
                std::lock_guard<std::mutex> lock(workersMutex);
                if (workers.size() > minThreads)
                {
                    INFOLOG("detaching worker threads!!", workers.size());
                    Worker worker = std::move(workers.back());
                    workers.pop_back();
                    worker.stopFlag->store(true);
                    retiring.push_back(std::move(worker));
                    condition.notify_all();
                    INFOLOG("detached worker threads!!", workers.size());
                }
            }
        }
        catch (const std::exception& e)
        {
            ERRORLOG("ThreadPool manager exception:", e.what());
        }
        catch (...)
        {
            ERRORLOG("ThreadPool manager exception: Unknown");
        }
    }
}
