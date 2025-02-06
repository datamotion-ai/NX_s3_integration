#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <queue>
#include <vector>
#include <thread>
#include <mutex>
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
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    std::atomic<bool> stop;
    std::thread managerThread;

    size_t minThreads;
    size_t maxThreads;
    std::atomic<size_t> activeThreads;

    void addWorker();

    void adjustWorkerThreads();
};

#endif //THREADPOOL_H