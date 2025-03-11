#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

#include "inetwork.h"
#include "ievent_loop.h"
#include "task_scheduler.h"
#include "thread_pool.h"
#include "udp_server.h"
#include "send_task.h"
#include <vector>
#include <memory>

class ThreadManager
{
public:
    ThreadManager(IEventLoop *eventLoop, TaskScheduler *scheduler, ThreadPool *threadPool)
        : eventLoop_(eventLoop), scheduler_(scheduler), threadPool_(threadPool) {}

    void createServer(int port)
    {
        std::unique_ptr<INetwork> server(new UDPServer(eventLoop_, port));
        server->startReceiving([port](const std::string &msg)
                               {
                                   std::cout << "Port " << port << " received: " << msg << std::endl; // 回显消息
                               });
        servers.push_back(std::move(server));
    }

    void sendMessage(size_t serverIndex, const std::string &message, const std::string &destAddr, int destPort,
                     bool isPeriodic = false, std::chrono::milliseconds period = std::chrono::milliseconds(1000))
    {
        if (serverIndex < servers.size())
        {
            std::shared_ptr<ITask> task = std::make_shared<SendTask>(message, destAddr, destPort, isPeriodic, period);
            scheduler_->schedule(task, servers[serverIndex].get(), 1);
        }
    }

private:
    IEventLoop *eventLoop_;
    TaskScheduler *scheduler_;
    ThreadPool *threadPool_;
    std::vector<std::unique_ptr<INetwork>> servers;
};

#endif // THREAD_MANAGER_H