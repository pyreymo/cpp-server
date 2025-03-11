#ifndef TASK_SCHEDULER_H
#define TASK_SCHEDULER_H

#include "itask.h"
#include "ievent_loop.h"
#include "itimer.h"
#include "thread_pool.h"
#include "log.h"
#include <memory>
#include <uv.h>

class TaskScheduler
{
public:
    TaskScheduler(IEventLoop *eventLoop, ThreadPool *threadPool)
        : eventLoop_(eventLoop), threadPool_(threadPool)
    {
        uv_async_init(eventLoop->getRawLoop(), &asyncHandle_, onAsync);
        asyncHandle_.data = this;
    }

    ~TaskScheduler()
    {
        uv_close((uv_handle_t *)&asyncHandle_, nullptr);
    }

    void schedule(std::shared_ptr<ITask> task, INetwork *network, int priority)
    {
        ITimer *timer = eventLoop_->createTimer();
        auto entry = std::make_shared<TaskEntry>(TaskEntry{priority, task, network, timer});
        timer->data = entry.get();
        uint64_t timeout = 0;
        uint64_t repeat = task->isPeriodic() ? task->getPeriod().count() : 0;
        timer->start(timeout, repeat, [this, entry]()
                     { onTimer(entry); });
    }

private:
    struct TaskEntry
    {
        int priority;
        std::shared_ptr<ITask> task;
        INetwork *network;
        ITimer *timer;
    };

    void onTimer(std::shared_ptr<TaskEntry> entry)
    {
        // 将任务执行提交到线程池
        threadPool_->enqueue([this, entry]()
                             {
            if (!entry->task->isCancelled() && !entry->task->isSuspended()) {
                // 在线程池中准备任务，但不直接调用 network->send
                auto asyncTask = std::make_shared<AsyncTask>(entry->task, entry->network);
                {
                    std::lock_guard<std::mutex> lock(asyncMutex_);
                    asyncTasks_.push_back(asyncTask);
                }
                uv_async_send(&asyncHandle_); // 通知主线程执行网络操作
            } });

        if (!entry->task->isPeriodic() && entry->task->shouldRetry())
        {
            entry->timer->start(1000, 0, [this, entry]()
                                { onTimer(entry); });
        }
        else if (!entry->task->isPeriodic() && !entry->task->shouldRetry())
        {
            entry->timer->stop();
        }
    }

    static void onAsync(uv_async_t *handle)
    {
        TaskScheduler *scheduler = static_cast<TaskScheduler *>(handle->data);
        std::vector<std::shared_ptr<AsyncTask>> tasks;
        {
            std::lock_guard<std::mutex> lock(scheduler->asyncMutex_);
            tasks.swap(scheduler->asyncTasks_);
        }
        for (auto &task : tasks)
        {
            task->task->execute(task->network); // 在主线程执行网络操作
        }
    }

    struct AsyncTask
    {
        std::shared_ptr<ITask> task;
        INetwork *network;
        AsyncTask(std::shared_ptr<ITask> t, INetwork *n) : task(t), network(n) {}
    };

    IEventLoop *eventLoop_;
    ThreadPool *threadPool_;
    uv_async_t asyncHandle_;
    std::vector<std::shared_ptr<AsyncTask>> asyncTasks_;
    std::mutex asyncMutex_;
};

#endif // TASK_SCHEDULER_H