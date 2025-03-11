#include <iostream>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <chrono>
#include <string>
#include <sstream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

// 日志函数
void log(const std::string &message, const char *file, int line)
{
    std::ostringstream oss;
    oss << "[" << file << ":" << line << "] " << message;
    std::cout << oss.str() << std::endl;
}

// 任务接口
class ITask
{
public:
    virtual ~ITask() = default;
    virtual void execute() = 0;
    virtual void suspend() = 0;
    virtual void cancel() = 0;
    virtual size_t getId() const = 0;
    virtual bool shouldRetry() const { return retryCount_ < maxRetries_; }

protected:
    int retryCount_ = 0;
    int maxRetries_ = 3;
    bool isSuspended_ = false;
    bool isCancelled_ = false;
};

// 发送 UDP 报文的任务
class SendTask : public ITask
{
public:
    SendTask(int socketFd, const std::string &msg, const sockaddr_in &destAddr,
             bool isPeriodic = false, std::chrono::milliseconds period = std::chrono::milliseconds(1000))
        : socketFd_(socketFd), message_(msg), destAddr_(destAddr),
          isPeriodic_(isPeriodic), period_(period)
    {
        id_ = std::hash<std::string>{}(msg + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    }

    void execute() override
    {
        if (isCancelled_ || isSuspended_)
            return;

        socklen_t addrLen = sizeof(destAddr_);
        ssize_t sent = sendto(socketFd_, message_.c_str(), message_.size(), 0,
                              (struct sockaddr *)&destAddr_, addrLen);
        if (sent < 0)
        {
            retryCount_++;
            log("Failed to send message, retry " + std::to_string(retryCount_) + "/" + std::to_string(maxRetries_),
                __FILE__, __LINE__);
        }
        else
        {
            log("Message sent: " + message_, __FILE__, __LINE__);
        }
    }

    void suspend() override { isSuspended_ = true; }
    void cancel() override { isCancelled_ = true; }
    size_t getId() const override { return id_; }
    bool isPeriodic() const { return isPeriodic_; }
    std::chrono::milliseconds getPeriod() const { return period_; }

private:
    int socketFd_;
    std::string message_;
    sockaddr_in destAddr_;
    size_t id_;
    bool isPeriodic_;
    std::chrono::milliseconds period_;
};

// 任务调度器
class TaskScheduler
{
public:
    TaskScheduler() : running_(false) {}

    void start()
    {
        running_ = true;
        thread_ = std::thread(&TaskScheduler::run, this);
    }

    void stop()
    {
        running_ = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cv_.notify_one();
        }
        if (thread_.joinable())
            thread_.join();
    }

    void schedule(std::shared_ptr<ITask> task, int priority)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push({priority, task});
        cv_.notify_one();
    }

private:
    struct TaskEntry
    {
        int priority;
        std::shared_ptr<ITask> task;
        bool operator<(const TaskEntry &other) const { return priority < other.priority; }
    };

    std::priority_queue<TaskEntry> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
    bool running_;

    void run()
    {
        while (running_)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]
                     { return !tasks_.empty() || !running_; });
            if (!running_)
                break;

            TaskEntry entry = tasks_.top();
            tasks_.pop();
            lock.unlock();

            std::shared_ptr<SendTask> task = std::dynamic_pointer_cast<SendTask>(entry.task);
            task->execute();

            if (task->isPeriodic() && !task->shouldRetry())
            {
                std::this_thread::sleep_for(task->getPeriod());
                schedule(task, entry.priority);
            }
            else if (task->shouldRetry())
            {
                schedule(task, entry.priority);
            }
        }
    }
};

// UDP 服务器
class UDPServer
{
public:
    UDPServer(int port, TaskScheduler *scheduler)
        : port_(port), scheduler_(scheduler), running_(false)
    {
        initSocket();
    }

    ~UDPServer()
    {
        if (socketFd_ >= 0)
            close(socketFd_);
    }

    void start()
    {
        running_ = true;
        thread_ = std::thread(&UDPServer::run, this);
    }

    void stop()
    {
        running_ = false;
        if (thread_.joinable())
            thread_.join();
    }

    void send(const std::string &message, const std::string &destAddr, int destPort,
              bool isPeriodic = false, std::chrono::milliseconds period = std::chrono::milliseconds(1000))
    {
        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(destPort);
        inet_pton(AF_INET, destAddr.c_str(), &addr.sin_addr);

        std::shared_ptr<SendTask> task = std::make_shared<SendTask>(socketFd_, message, addr, isPeriodic, period);
        scheduler_->schedule(task, isPeriodic ? 1 : 0);
    }

private:
    int port_;
    TaskScheduler *scheduler_;
    std::thread thread_;
    bool running_;
    int socketFd_ = -1;
    int epollFd_ = -1;

    void initSocket()
    {
        socketFd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (socketFd_ < 0)
        {
            log("Failed to create socket", __FILE__, __LINE__);
            return;
        }

        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);
        if (bind(socketFd_, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        {
            log("Failed to bind socket", __FILE__, __LINE__);
            close(socketFd_);
            socketFd_ = -1;
            return;
        }

        epollFd_ = epoll_create1(0);
        epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = socketFd_;
        epoll_ctl(epollFd_, EPOLL_CTL_ADD, socketFd_, &ev);
    }

    void run()
    {
        epoll_event events[10];
        char buffer[1024];

        while (running_)
        {
            int nfds = epoll_wait(epollFd_, events, 10, 1000);
            for (int i = 0; i < nfds; ++i)
            {
                if (events[i].data.fd == socketFd_)
                {
                    sockaddr_in srcAddr;
                    socklen_t addrLen = sizeof(srcAddr);
                    ssize_t len = recvfrom(socketFd_, buffer, sizeof(buffer) - 1, 0,
                                           (struct sockaddr *)&srcAddr, &addrLen);
                    if (len > 0)
                    {
                        buffer[len] = '\0';
                        std::string msg(buffer);
                        onReceive(msg, srcAddr);
                    }
                }
            }
        }
    }

    void onReceive(const std::string &message, const sockaddr_in &srcAddr)
    {
        log("Received: " + message, __FILE__, __LINE__);
        // std::string ack = "ACK: " + message;
        // sendto(socketFd_, ack.c_str(), ack.size(), 0, (struct sockaddr *)&srcAddr, sizeof(srcAddr));
    }
};

// 线程管理器
class ThreadManager
{
public:
    ThreadManager(TaskScheduler *scheduler) : scheduler_(scheduler) {}

    void createServerThread(int port)
    {
        std::unique_ptr<UDPServer> server(new UDPServer(port, scheduler_));
        server->start();
        servers.push_back(std::move(server));
    }

    void stopAll()
    {
        for (std::vector<std::unique_ptr<UDPServer>>::iterator it = servers.begin(); it != servers.end(); ++it)
        {
            (*it)->stop();
        }
    }

    // 新增公共方法：通过服务器索引发送消息
    void sendMessage(size_t serverIndex, const std::string &message, const std::string &destAddr, int destPort,
                     bool isPeriodic = false, std::chrono::milliseconds period = std::chrono::milliseconds(1000))
    {
        if (serverIndex < servers.size())
        {
            servers[serverIndex]->send(message, destAddr, destPort, isPeriodic, period);
        }
        else
        {
            log("Invalid server index", __FILE__, __LINE__);
        }
    }

private:
    TaskScheduler *scheduler_;
    std::vector<std::unique_ptr<UDPServer>> servers;
};

// 主函数
int main()
{
    TaskScheduler scheduler;
    scheduler.start();

    ThreadManager manager(&scheduler);
    manager.createServerThread(8080);
    manager.createServerThread(8081);

    // 使用公共接口发送消息
    manager.sendMessage(0, "Hello from 8080", "127.0.0.1", 8081, true, std::chrono::milliseconds(2000));
    manager.sendMessage(1, "Hello from 8081", "127.0.0.1", 8080);

    std::this_thread::sleep_for(std::chrono::seconds(10));
    manager.stopAll();
    scheduler.stop();

    return 0;
}