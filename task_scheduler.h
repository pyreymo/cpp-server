#ifndef TASK_SCHEDULER_H
#define TASK_SCHEDULER_H

#include "itask.h"
#include "ievent_loop.h"
#include "itimer.h"
#include "log.h"
#include <memory>

class TaskScheduler
{
public:
    TaskScheduler(IEventLoop *eventLoop) : eventLoop_(eventLoop) {}

    void schedule(std::shared_ptr<ITask> task, INetwork *network, int priority)
    {
        ITimer *timer = eventLoop_->createTimer();
        TaskEntry *entry = new TaskEntry{priority, task, network, timer};
        timer->data = entry;
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

    void onTimer(TaskEntry *entry)
    {
        entry->task->execute(entry->network);
        if (!entry->task->isPeriodic() && entry->task->shouldRetry())
        {
            entry->timer->start(1000, 0, [this, entry]()
                                { onTimer(entry); }); // 1秒后重试
        }
        else if (!entry->task->isPeriodic() && !entry->task->shouldRetry())
        {
            entry->timer->stop();
            delete entry;
        }
    }

    IEventLoop *eventLoop_;
};

#endif // TASK_SCHEDULER_H