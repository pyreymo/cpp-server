#include "libuv_event_loop.h"
#include "task_scheduler.h"
#include "thread_pool.h"
#include "thread_manager.h"

int main()
{
    std::unique_ptr<IEventLoop> eventLoop(new LibuvEventLoop());
    ThreadPool threadPool(4);
    TaskScheduler scheduler(eventLoop.get(), &threadPool);
    ThreadManager manager(eventLoop.get(), &scheduler, &threadPool);

    manager.createServer(8080);
    manager.createServer(8081);

    manager.sendMessage(0, "Hello from 8080", "127.0.0.1", 8081, true, std::chrono::milliseconds(2000));
    manager.sendMessage(1, "Hello from 8081", "127.0.0.1", 8080);

    eventLoop->run();
    return 0;
}