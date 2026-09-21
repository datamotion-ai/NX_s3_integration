#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <queue>
#include <vector>
#include <thread>
#include <mutex>
#include <memory>
#include <condition_variable>
#include <atomic>
#include <functional>

/**
 * Elastic worker pool used by s3Client::fileUploadThread.
 *
 * Liveness guarantees (see docs/logs8-findings-report.md Finding 2):
 *  - at least one worker is always kept alive (minThreads >= 1);
 *  - a worker drains queued tasks before honouring its retire flag, so a task
 *    enqueued while the manager retires a worker is never orphaned;
 *  - the manager never blocks on a busy worker; retired threads are joined
 *    only after they have signalled completion;
 *  - enqueueTask()/ensureWorkers() spawn a worker synchronously when every
 *    existing worker is busy, so pickup does not depend on the 1 s manager tick;
 *  - manager and worker bodies never let an exception escape (which would
 *    std::terminate the Media Server).
 */
class ThreadPool {
public:
    explicit ThreadPool(size_t minThreads, size_t maxThreads);

    void enqueueTask(std::function<void()> task);

    void shutdown();

    void setMaxThreads(size_t value);

    /** Number of tasks waiting in the queue (not counting tasks being executed). */
    int getWorkingTaskCount();

    /** Number of live (non-retired) worker threads. */
    size_t workerCount();

    /** Number of workers currently executing a task. */
    size_t busyWorkerCount() const;

    /** Spawn a worker if tasks are pending and no worker is free to take them. */
    void ensureWorkers();

    ~ThreadPool();

private:
    struct Worker
    {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> stopFlag;
        std::shared_ptr<std::atomic<bool>> finished;
    };

    std::mutex workersMutex;
    std::vector<Worker> workers;
    std::vector<Worker> retiring;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    std::thread managerThread;

    size_t minThreads;
    std::atomic<size_t> maxThreads;

    std::atomic<bool> stop;
    std::atomic<size_t> busyWorkers;

    void addWorker();
    void addWorkerLocked();
    void joinFinishedRetiring();

    void adjustWorkerThreads();
};

#endif //THREADPOOL_H
