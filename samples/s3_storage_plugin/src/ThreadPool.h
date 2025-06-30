#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <queue>
#include <vector>
#include <thread>
#include <mutex>
#include <vector>
#include <condition_variable>
#include <atomic>
#include <functional>

class ThreadPool {
public:
    explicit ThreadPool(size_t minThreads, size_t maxThreads);

    void enqueueTask(std::function<void()> task);

    void shutdown();

    void setMaxThreads(size_t value);

    int getWorkingTaskCount();

    ~ThreadPool();

private:
    std::mutex workersMutex;
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::vector<std::shared_ptr<std::atomic<bool>>> stopFlags;
    std::mutex queueMutex;
    std::condition_variable condition;
    std::thread managerThread;

    size_t minThreads;
    size_t maxThreads;

    std::atomic<bool> stop;

    void addWorker();

    void adjustWorkerThreads();
};

#endif //THREADPOOL_H